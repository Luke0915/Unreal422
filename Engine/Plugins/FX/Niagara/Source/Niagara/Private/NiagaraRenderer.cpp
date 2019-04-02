// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraRenderer.h"
#include "ParticleResources.h"
#include "ParticleBeamTrailVertexFactory.h"
#include "NiagaraDataSet.h"
#include "NiagaraStats.h"
#include "NiagaraVertexFactory.h"
#include "Engine/Engine.h"
#include "DynamicBufferAllocator.h"

DECLARE_CYCLE_STAT(TEXT("Generate Particle Lights"), STAT_NiagaraGenLights, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Sort Particles"), STAT_NiagaraSortParticles, STATGROUP_Niagara);

DECLARE_CYCLE_STAT(TEXT("Global Float Alloc - All"), STAT_NiagaraAllocateGlobalFloatAll, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Global Float Alloc - InsideLock"), STAT_NiagaraAllocateGlobalFloatInsideLock, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Global Float Alloc - Alloc New Buffer"), STAT_NiagaraAllocateGlobalFloatAllocNew, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Global Float Alloc - Map Buffer"), STAT_NiagaraAllocateGlobalFloatMapBuffer, STATGROUP_Niagara);


/** Enable/disable parallelized System renderers */
int32 GbNiagaraParallelEmitterRenderers = 1;
static FAutoConsoleVariableRef CVarParallelEmitterRenderers(
	TEXT("niagara.ParallelEmitterRenderers"),
	GbNiagaraParallelEmitterRenderers,
	TEXT("Whether to run Niagara System renderers in parallel"),
	ECVF_Default
	);


class FNiagaraDummyRWBufferFloat : public FRenderResource
{
public:
	FNiagaraDummyRWBufferFloat(const FString InDebugId) : DebugId(InDebugId) {}
	FString DebugId;
	FRWBuffer Buffer;

	virtual void InitRHI() override
	{
		UE_LOG(LogNiagara, Log, TEXT("FNiagaraDummyRWBufferFloat InitRHI %s"), *DebugId);
		Buffer.Initialize(sizeof(float), 1, EPixelFormat::PF_R32_FLOAT, BUF_Static, *DebugId);
	}

	virtual void ReleaseRHI() override
	{
		UE_LOG(LogNiagara, Log, TEXT("FNiagaraDummyRWBufferFloat ReleaseRHI %s"), *DebugId);
		Buffer.Release();
	}
};

class FNiagaraDummyRWBufferMatrix : public FRenderResource
{
public:
	FNiagaraDummyRWBufferMatrix(const FString InDebugId) : DebugId(InDebugId) {}
	FString DebugId;
	FRWBufferStructured Buffer;

	virtual void InitRHI() override
	{
		UE_LOG(LogNiagara, Log, TEXT("FNiagaraDummyRWBufferMatrix InitRHI %s"), *DebugId);
		Buffer.Initialize(64, 1, BUF_Static, *DebugId);
	}

	virtual void ReleaseRHI() override
	{
		UE_LOG(LogNiagara, Log, TEXT("FNiagaraDummyRWBufferMatrix ReleaseRHI %s"), *DebugId);
		Buffer.Release();
	}
};

class FNiagaraDummyRWBufferInt : public FRenderResource
{
public:
	FNiagaraDummyRWBufferInt(const FString InDebugId) : DebugId(InDebugId) {}
	FString DebugId;
	FRWBuffer Buffer;

	virtual void InitRHI() override
	{
		UE_LOG(LogNiagara, Log, TEXT("FNiagaraDummyRWBufferInt InitRHI %s"), *DebugId);
		Buffer.Initialize(sizeof(int32), 1, EPixelFormat::PF_R32_SINT, BUF_Static, *DebugId);
	}

	virtual void ReleaseRHI() override
	{
		UE_LOG(LogNiagara, Log, TEXT("FNiagaraDummyRWBufferInt ReleaseRHI %s"), *DebugId);
		Buffer.Release();
	}
};

class FNiagaraDummyRWBufferUInt : public FRenderResource
{
public:
	FNiagaraDummyRWBufferUInt(const FString InDebugId) : DebugId(InDebugId) {}
	FString DebugId;
	FRWBuffer Buffer;

	virtual void InitRHI() override
	{
		UE_LOG(LogNiagara, Log, TEXT("FNiagaraDummyRWBufferUInt InitRHI %s"), *DebugId);
		Buffer.Initialize(sizeof(uint32), 1, EPixelFormat::PF_R32_UINT, BUF_Static, *DebugId);
	}

	virtual void ReleaseRHI() override
	{
		UE_LOG(LogNiagara, Log, TEXT("FNiagaraDummyRWBufferUInt ReleaseRHI %s"), *DebugId);
		Buffer.Release();
	}
};

FRWBuffer& NiagaraRenderer::GetDummyFloatBuffer()
{
	check(IsInRenderingThread());
	static TGlobalResource<FNiagaraDummyRWBufferFloat> DummyFloatBuffer(TEXT("NiagaraRenderer::DummyFloat"));
	return DummyFloatBuffer.Buffer;
}

FRWBufferStructured& NiagaraRenderer::GetDummyMatrixBuffer()
{
	check(IsInRenderingThread());
	static TGlobalResource<FNiagaraDummyRWBufferMatrix> DummyMatrixBuffer(TEXT("NiagaraRenderer::DummyMatrix"));
	return DummyMatrixBuffer.Buffer;
}

FRWBuffer& NiagaraRenderer::GetDummyIntBuffer()
{
	check(IsInRenderingThread());
	static TGlobalResource<FNiagaraDummyRWBufferInt> DummyIntBuffer(TEXT("NiagaraRenderer::DummyInt"));
	return DummyIntBuffer.Buffer;
}

FRWBuffer& NiagaraRenderer::GetDummyUIntBuffer()
{
	check(IsInRenderingThread());
	static TGlobalResource<FNiagaraDummyRWBufferUInt> DummyUIntBuffer(TEXT("NiagaraRenderer::DummyUInt"));
	return DummyUIntBuffer.Buffer;
}

NiagaraRenderer::NiagaraRenderer()
	: CPUTimeMS(0.0f)
	, bLocalSpace(false)
	, bEnabled(true)
	, DynamicDataRender(nullptr)
	, BaseExtents(1.0f, 1.0f, 1.0f)
{
	Material = UMaterial::GetDefaultMaterial(MD_Surface);
}


NiagaraRenderer::~NiagaraRenderer() 
{
}

void NiagaraRenderer::Release()
{
	check(IsInGameThread());
	NiagaraRenderer* Renderer = this;
	ENQUEUE_RENDER_COMMAND(NiagaraRendererDeletion)(
		[Renderer](FRHICommandListImmediate& RHICmdList)
		{
			delete Renderer;
		}
	);
}

void NiagaraRenderer::SortIndices(ENiagaraSortMode SortMode, int32 SortAttributeOffset, const FNiagaraDataBuffer& Buffer, const FMatrix& LocalToWorld, const FSceneView* View, FGlobalDynamicReadBuffer::FAllocation& OutIndices)const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSortParticles);

	uint32 NumInstances = Buffer.GetNumInstances();
	check(OutIndices.ReadBuffer->NumBytes >= OutIndices.FirstIndex + NumInstances * sizeof(int32));
	check(SortMode != ENiagaraSortMode::None);
	check(SortAttributeOffset != INDEX_NONE);

	int32* RESTRICT IndexBuffer = (int32*)(OutIndices.Buffer);

	struct FParticleOrder
	{
		int32 Index;
		float Order;
	};
	FMemMark Mark(FMemStack::Get());
	FParticleOrder* RESTRICT ParticleOrder = (FParticleOrder*)FMemStack::Get().Alloc(sizeof(FParticleOrder) * NumInstances, alignof(FParticleOrder));

	auto SortAscending = [](const FParticleOrder& A, const FParticleOrder& B) { return A.Order < B.Order; };
	auto SortDescending = [](const FParticleOrder& A, const FParticleOrder& B) { return A.Order > B.Order; };

	if (SortMode == ENiagaraSortMode::ViewDepth || SortMode == ENiagaraSortMode::ViewDistance)
	{
		float* RESTRICT PositionX = (float*)Buffer.GetComponentPtrFloat(SortAttributeOffset);
		float* RESTRICT PositionY = (float*)Buffer.GetComponentPtrFloat(SortAttributeOffset + 1);
		float* RESTRICT PositionZ = (float*)Buffer.GetComponentPtrFloat(SortAttributeOffset + 2);
		auto GetPos = [&PositionX, &PositionY, &PositionZ](int32 Idx)
		{
			return FVector(PositionX[Idx], PositionY[Idx], PositionZ[Idx]);
		};

		//TODO Parallelize in batches? Move to GPU for large emitters?
		if (SortMode == ENiagaraSortMode::ViewDepth)
		{
			FMatrix ViewProjMatrix = View->ViewMatrices.GetViewProjectionMatrix();
			if (bLocalSpace)
			{
				for (uint32 i = 0; i < NumInstances; ++i)
				{
					ParticleOrder[i].Index = i;
					ParticleOrder[i].Order = ViewProjMatrix.TransformPosition(LocalToWorld.TransformPosition(GetPos(i))).W;
				}
			}
			else
			{
				for (uint32 i = 0; i < NumInstances; ++i)
				{
					ParticleOrder[i].Index = i;
					ParticleOrder[i].Order = ViewProjMatrix.TransformPosition(GetPos(i)).W;
				}
			}

			Sort(ParticleOrder, NumInstances, SortDescending);
		}
		else 
		{
			// check(SortMode == ENiagaraSortMode::ViewDistance); Should always be true because we are within this block. If code is moved or copied, please change.
			FVector ViewOrigin = View->ViewMatrices.GetViewOrigin();
			if (bLocalSpace)
			{
				for (uint32 i = 0; i < NumInstances; ++i)
				{
					ParticleOrder[i].Index = i;
					ParticleOrder[i].Order = (ViewOrigin - LocalToWorld.TransformPosition(GetPos(i))).SizeSquared();
				}
			}
			else
			{
				for (uint32 i = 0; i < NumInstances; ++i)
				{
					ParticleOrder[i].Index = i;
					ParticleOrder[i].Order = (ViewOrigin - GetPos(i)).SizeSquared();
				}
			}

			Sort(ParticleOrder, NumInstances, SortDescending);
		}
	}
	else
	{
		float* RESTRICT CustomSorting = (float*)Buffer.GetComponentPtrFloat(SortAttributeOffset);
		for (uint32 i = 0; i < NumInstances; ++i)
		{
			ParticleOrder[i].Index = i;
			ParticleOrder[i].Order = CustomSorting[i];
		}
		if (SortMode == ENiagaraSortMode::CustomAscending)
		{
			Sort(ParticleOrder, NumInstances, SortAscending);
		}
		else if (SortMode == ENiagaraSortMode::CustomDecending)
		{
			Sort(ParticleOrder, NumInstances, SortDescending);
		}
	}

	//Now transfer to the real index buffer.
	for (uint32 i = 0; i < NumInstances; ++i)
	{
		IndexBuffer[i] = ParticleOrder[i].Index;
	}
}

	
//////////////////////////////////////////////////////////////////////////
// Light renderer

NiagaraRendererLights::NiagaraRendererLights(ERHIFeatureLevel::Type FeatureLevel, UNiagaraRendererProperties *InProps) :
	NiagaraRenderer()
{
	Properties = Cast<UNiagaraLightRendererProperties>(InProps);

#if STATS
	if (UNiagaraEmitter* Emitter = InProps->GetTypedOuter<UNiagaraEmitter>())
	{
		EmitterStatID = Emitter->GetStatID(false, false);
	}
#endif
}


void NiagaraRendererLights::ReleaseRenderThreadResources()
{
}

void NiagaraRendererLights::CreateRenderThreadResources()
{
}



/** Update render data buffer from attributes */
FNiagaraDynamicDataBase *NiagaraRendererLights::GenerateVertexData(const FNiagaraSceneProxy* Proxy, FNiagaraDataSet &Data, const ENiagaraSimTarget Target)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraGenLights);

	SimpleTimer VertexDataTimer;

	//Bail if we don't have the required attributes to render this emitter.
	if (!bEnabled || !Properties)
	{
		return nullptr;
	}

	//I'm not a great fan of pulling scalar components out to a structured vert buffer like this.
	//TODO: Experiment with a new VF that reads the data directly from the scalar layout.
	FNiagaraDataSetIterator<FVector> PosItr(Data, Properties->PositionBinding.DataSetVariable);
	FNiagaraDataSetIterator<FLinearColor> ColItr(Data, Properties->ColorBinding.DataSetVariable);
	FNiagaraDataSetIterator<float> RadiusItr(Data, Properties->RadiusBinding.DataSetVariable);
	FNiagaraDataSetIterator<float> ExponentItr(Data, Properties->LightExponentBinding.DataSetVariable);
	FNiagaraDataSetIterator<float> ScatteringItr(Data, Properties->VolumetricScatteringBinding.DataSetVariable);
	FNiagaraDataSetIterator<int32> EnabledItr(Data, Properties->LightRenderingEnabledBinding.DataSetVariable);

	FNiagaraDynamicDataLights *DynamicData = new FNiagaraDynamicDataLights;
	const FMatrix& LocalToWorldMatrix = Proxy->GetLocalToWorld();
	FVector DefaultColor = FVector(Properties->ColorBinding.DefaultValueIfNonExistent.GetValue<FLinearColor>());
	FVector DefaultPos = FVector4(LocalToWorldMatrix.GetOrigin());
	float DefaultRadius = Properties->RadiusBinding.DefaultValueIfNonExistent.GetValue<float>();
	float DefaultScattering = Properties->VolumetricScatteringBinding.DefaultValueIfNonExistent.GetValue<float>();

	DynamicData->LightArray.Empty();

	for (uint32 ParticleIndex = 0; ParticleIndex < Data.GetNumInstances(); ParticleIndex++)
	{
		bool ShouldRenderParticleLight = !Properties->bOverrideRenderingEnabled || (EnabledItr.IsValid() ? (*EnabledItr) : true);
		float LightRadius = (RadiusItr.IsValid() ? (*RadiusItr) : DefaultRadius) * Properties->RadiusScale;
		if (ShouldRenderParticleLight && LightRadius > 0)
		{
			SimpleLightData LightData;
			LightData.LightEntry.Radius = LightRadius;
			LightData.LightEntry.Color = (ColItr.IsValid() ? FVector((*ColItr)) : DefaultColor) + Properties->ColorAdd;
			LightData.LightEntry.Exponent = Properties->bUseInverseSquaredFalloff ? 0 : (ExponentItr.IsValid() ? (*ExponentItr) : 1);
			LightData.LightEntry.bAffectTranslucency = Properties->bAffectsTranslucency;
			LightData.LightEntry.VolumetricScatteringIntensity = ScatteringItr.IsValid() ? (*ScatteringItr) : DefaultScattering;
			LightData.PerViewEntry.Position = PosItr.IsValid() ? (*PosItr) : DefaultPos;
			if (bLocalSpace)
			{
				LightData.PerViewEntry.Position = LocalToWorldMatrix.TransformPosition(LightData.PerViewEntry.Position);
			}

			DynamicData->LightArray.Add(LightData);
		}

		PosItr.Advance();
		ColItr.Advance();
		RadiusItr.Advance();
		ExponentItr.Advance();
		ScatteringItr.Advance();
		EnabledItr.Advance();
	}

	CPUTimeMS = VertexDataTimer.GetElapsedMilliseconds();
	return DynamicData;
}


void NiagaraRendererLights::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector, const FNiagaraSceneProxy *SceneProxy) const
{

}

void NiagaraRendererLights::SetDynamicData_RenderThread(FNiagaraDynamicDataBase* NewDynamicData)
{
	if (DynamicDataRender)
	{
		delete static_cast<FNiagaraDynamicDataLights*>(DynamicDataRender);
		DynamicDataRender = NULL;
	}
	DynamicDataRender = static_cast<FNiagaraDynamicDataLights*>(NewDynamicData);
}

int NiagaraRendererLights::GetDynamicDataSize()
{
	return 0;
}
bool NiagaraRendererLights::HasDynamicData()
{
	return false;
}

bool NiagaraRendererLights::SetMaterialUsage()
{
	return false;
}

void NiagaraRendererLights::TransformChanged()
{
}

#if WITH_EDITORONLY_DATA

const TArray<FNiagaraVariable>& NiagaraRendererLights::GetRequiredAttributes()
{
	return Properties->GetRequiredAttributes();
}

const TArray<FNiagaraVariable>& NiagaraRendererLights::GetOptionalAttributes()
{
	return Properties->GetOptionalAttributes();
}

#endif