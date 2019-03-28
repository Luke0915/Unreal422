// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraRendererVolumes.h"
#include "ParticleResources.h"
#include "NiagaraMeshVertexFactory.h"
#include "NiagaraDataSet.h"
#include "NiagaraStats.h"
#include "Async/ParallelFor.h"
#include "Engine/StaticMesh.h"

DECLARE_CYCLE_STAT(TEXT("Generate Volume Vertex Data [GT]"), STAT_NiagaraGenVolumeVertexData, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Render Volumes [RT]"), STAT_NiagaraRenderVolumes, STATGROUP_Niagara);



extern int32 GbNiagaraParallelEmitterRenderers;



#define LOCTEXT_NAMESPACE "NiagaraRendererVolumes"

class FNiagaraVolumeMeshCollectorResourcesMesh : public FOneFrameResource
{
public:
	FNiagaraMeshVertexFactory VertexFactory;
	FNiagaraMeshUniformBufferRef UniformBuffer;

	virtual ~FNiagaraVolumeMeshCollectorResourcesMesh()
	{
		VertexFactory.ReleaseResource();
	}
};


NiagaraRendererVolumes::NiagaraRendererVolumes(ERHIFeatureLevel::Type FeatureLevel, UNiagaraRendererProperties *InProps) :
	NiagaraRenderer()
	, LastSyncedId(INDEX_NONE)
{
	//check(InProps);
	Properties = Cast<UNiagaraVolumeRendererProperties>(InProps);

	if (Properties && Properties->ParticleMesh)
	{
		BaseExtents = Properties->ParticleMesh->GetBounds().BoxExtent;
	}

}

void NiagaraRendererVolumes::ReleaseRenderThreadResources()
{
	WorldSpacePrimitiveUniformBuffer.ReleaseResource();
}

void NiagaraRendererVolumes::CreateRenderThreadResources()
{
}


void NiagaraRendererVolumes::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector, const FNiagaraSceneProxy *SceneProxy) const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraRender);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderVolumes);

	SimpleTimer MeshElementsTimer;

	FNiagaraDynamicDataVolumes *DynamicDataMesh = (static_cast<FNiagaraDynamicDataVolumes*>(DynamicDataRender));
	//if (!DynamicDataMesh || !DynamicDataMesh->DataSet || DynamicDataMesh->DataSet->GetNumInstances()==0)
	if (/*!DynamicDataMesh 
		||*/ 
		nullptr == Properties
		|| !GSupportsResourceView  // Current shader requires SRV to draw properly in all cases.
		|| Properties->ParticleMesh == nullptr
		)
	{
		return;
	}

	{
		// Update the primitive uniform buffer if needed.
		if (!WorldSpacePrimitiveUniformBuffer.IsInitialized())
		{
			FMatrix Mat = FMatrix::Identity;
			//Mat.ApplyScale(1000.0f);
			FPrimitiveUniformShaderParameters PrimitiveUniformShaderParameters = GetPrimitiveUniformShaderParameters(
				Mat,
				Mat,
				SceneProxy->GetActorPosition(),
				SceneProxy->GetBounds(),
				SceneProxy->GetLocalBounds(),
				SceneProxy->GetPreSkinnedLocalBounds(),
				SceneProxy->ReceivesDecals(),
				false,
				false,
				SceneProxy->UseSingleSampleShadowFromStationaryLights(),
				SceneProxy->GetScene().HasPrecomputedVolumetricLightmap_RenderThread(),
				SceneProxy->UseEditorDepthTest(),
				SceneProxy->GetLightingChannelMask(),
				SceneProxy->GetLpvBiasMultiplier(),
				INDEX_NONE,
				INDEX_NONE
			);
			WorldSpacePrimitiveUniformBuffer.SetContents(PrimitiveUniformShaderParameters);
			WorldSpacePrimitiveUniformBuffer.InitResource();
		}
		
		// Compute the per-view uniform buffers.
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				const FSceneView* View = Views[ViewIndex];
				const FStaticMeshLODResources& LODModel = Properties->ParticleMesh->RenderData->LODResources[0];
				const FStaticMeshVertexFactories& VFs = Properties->ParticleMesh->RenderData->LODVertexFactories[0];

				const ERHIFeatureLevel::Type FeatureLevel = ViewFamily.GetFeatureLevel();

				//Grab the material proxies we'll be using for each section and check them for translucency.
				TArray<FMaterialRenderProxy*, TInlineAllocator<32>> MaterialProxies;
				bool bHasTranslucentMaterials = false;
				for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); SectionIndex++)
				{
					const FStaticMeshSection& Section = LODModel.Sections[SectionIndex];
					UMaterialInterface* ParticleMeshMaterial = Properties->Material;
					FMaterialRenderProxy* MaterialProxy = nullptr;
					
					if (MaterialProxy == nullptr && ParticleMeshMaterial)
					{
						MaterialProxy = ParticleMeshMaterial->GetRenderProxy();
					}

					if (MaterialProxy == nullptr)
					{
						MaterialProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
					}

					MaterialProxies.Add(MaterialProxy);
					if (MaterialProxy)
					{
						EBlendMode BlendMode = MaterialProxy->GetMaterial(FeatureLevel)->GetBlendMode();
						bHasTranslucentMaterials |= BlendMode == BLEND_AlphaComposite || BlendMode == BLEND_Translucent;
					}
				}

				const bool bIsWireframe = AllowDebugViewmodes() && View->Family->EngineShowFlags.Wireframe;

				for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); SectionIndex++)
				{
					const FStaticMeshSection& Section = LODModel.Sections[SectionIndex];
					FMaterialRenderProxy* MaterialProxy = MaterialProxies[SectionIndex];
					if ((Section.NumTriangles == 0) || (MaterialProxy == NULL))
					{
						//@todo. This should never occur, but it does occasionally.
						continue;
					}

					const bool bUseSelectedMaterial = false;
					const bool bUseHoveredMaterial = false;

					FMeshBatch& Mesh = Collector.AllocateMesh();
					Mesh.LCI = NULL;
					Mesh.ReverseCulling = SceneProxy->IsLocalToWorldDeterminantNegative();
					Mesh.CastShadow = SceneProxy->CastsDynamicShadow();
					Mesh.DepthPriorityGroup = (ESceneDepthPriorityGroup)SceneProxy->GetDepthPriorityGroup(View);
					Mesh.VertexFactory = &VFs.VertexFactory;

					FMeshBatchElement& BatchElement = Mesh.Elements[0];
					BatchElement.PrimitiveUniformBufferResource = &WorldSpacePrimitiveUniformBuffer;
					
					if (bIsWireframe)
					{
						if (LODModel.AdditionalIndexBuffers != nullptr && LODModel.AdditionalIndexBuffers->WireframeIndexBuffer.IsInitialized())
						{
							Mesh.Type = PT_LineList;
							Mesh.MaterialRenderProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
							BatchElement.FirstIndex = 0;
							BatchElement.IndexBuffer = &LODModel.AdditionalIndexBuffers->WireframeIndexBuffer;
							BatchElement.NumPrimitives = LODModel.AdditionalIndexBuffers->WireframeIndexBuffer.GetNumIndices() / 2;

						}
						else
						{
							Mesh.Type = PT_TriangleList;
							Mesh.MaterialRenderProxy = MaterialProxy;
							Mesh.bWireframe = true;
							BatchElement.FirstIndex = 0;
							BatchElement.IndexBuffer = &LODModel.IndexBuffer;
							BatchElement.NumPrimitives = LODModel.IndexBuffer.GetNumIndices() / 3;
						}
					}
					else
					{
						Mesh.Type = PT_TriangleList;
						Mesh.MaterialRenderProxy = MaterialProxy;
						BatchElement.IndexBuffer = &LODModel.IndexBuffer;
						BatchElement.FirstIndex = Section.FirstIndex;
						BatchElement.NumPrimitives = Section.NumTriangles;
					}


					Mesh.bCanApplyViewModeOverrides = true;
					Mesh.bUseWireframeSelectionColoring = SceneProxy->IsSelected();

					check(BatchElement.NumPrimitives > 0);
					Collector.AddMesh(ViewIndex, Mesh);
				}
			}
		}
	}

	CPUTimeMS += MeshElementsTimer.GetElapsedMilliseconds();
}



bool NiagaraRendererVolumes::SetMaterialUsage()
{
	return Material != nullptr;
}

void NiagaraRendererVolumes::TransformChanged()
{
	WorldSpacePrimitiveUniformBuffer.ReleaseResource();
}

/** Update render data buffer from attributes */
FNiagaraDynamicDataBase *NiagaraRendererVolumes::GenerateVertexData(const FNiagaraSceneProxy* Proxy, FNiagaraDataSet &Data, const ENiagaraSimTarget Target)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderGT);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraGenVolumeVertexData);

	if (!Properties || Properties->ParticleMesh == nullptr || !bEnabled)
	{
		return nullptr;
	}

	SimpleTimer VertexDataTimer;
//	TArray<FNiagaraMeshInstanceVertex>& RenderData = DynamicData->VertexData;
	//TArray< FNiagaraMeshInstanceVertexDynamicParameter>& RenderMaterialVertexData = DynamicData->MaterialParameterVertexData;

	if (LastSyncedId != Properties->SyncId)
	{
		LastSyncedId = Properties->SyncId;
	}

	//Bail if we don't have the required attributes to render this emitter.
	if (!bEnabled)
	{
		return nullptr;
	}

	FNiagaraDynamicDataVolumes *DynamicData = nullptr;

	if (Data.CurrData().GetNumInstances() > 0)
	{
		DynamicData = new FNiagaraDynamicDataVolumes;

		//TODO: This buffer is far fatter than needed. Just pull out the data needed for rendering.
		Data.CurrData().CopyTo(DynamicData->RTParticleData);

		//DynamicData->DataSet = &Data;
	}

	CPUTimeMS = VertexDataTimer.GetElapsedMilliseconds();
	return DynamicData;  
}



void NiagaraRendererVolumes::SetDynamicData_RenderThread(FNiagaraDynamicDataBase* NewDynamicData)
{
	check(IsInRenderingThread());

	if (DynamicDataRender)
	{
		delete static_cast<FNiagaraDynamicDataVolumes*>(DynamicDataRender);
		DynamicDataRender = NULL;
	}
	DynamicDataRender = NewDynamicData;
}

int NiagaraRendererVolumes::GetDynamicDataSize()
{
	uint32 Size = sizeof(FNiagaraDynamicDataVolumes);
	return Size;
}

bool NiagaraRendererVolumes::HasDynamicData()
{
	return DynamicDataRender != nullptr;
}

#if WITH_EDITORONLY_DATA

const TArray<FNiagaraVariable>& NiagaraRendererVolumes::GetRequiredAttributes()
{
	return Properties->GetRequiredAttributes();
}

const TArray<FNiagaraVariable>& NiagaraRendererVolumes::GetOptionalAttributes()
{
	return Properties->GetOptionalAttributes();
}

#endif


#undef LOCTEXT_NAMESPACE
