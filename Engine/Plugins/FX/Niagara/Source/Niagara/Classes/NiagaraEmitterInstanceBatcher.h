// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*==============================================================================
NiagaraEmitterInstanceBatcher.h: Queueing and batching for Niagara simulation;
use to reduce per-simulation overhead by batching together simulations using
the same VectorVM byte code / compute shader code
==============================================================================*/
#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"
#include "RendererInterface.h"
#include "NiagaraParameters.h"
#include "NiagaraEmitter.h"
#include "Tickable.h"
#include "Modules/ModuleManager.h"
#include "RHIResources.h"
#include "FXSystem.h"
#include "NiagaraRendererProperties.h"
#include "ParticleResources.h"
#include "Runtime/Engine/Private/Particles/ParticleSortingGPU.h"
#include "NiagaraGPUSortInfo.h"
#include "NiagaraScriptExecutionContext.h"

class FNiagaraIndicesVertexBuffer : public FParticleIndicesVertexBuffer
{
public:

	FNiagaraIndicesVertexBuffer(int32 InIndexCount);

	FUnorderedAccessViewRHIRef VertexBufferUAV;

	// The allocation count.
	const int32 IndexCount;

	// Currently used count.
	int32 UsedIndexCount = 0;
};

class NiagaraEmitterInstanceBatcher : public FFXSystemInterface
{
public:
	using FNiagaraBufferArray = TArray<FUnorderedAccessViewRHIParamRef, TMemStackAllocator<>>;
	using FOverlappableTicks = TArray<FNiagaraGPUSystemTick*, TMemStackAllocator<>>;

	NiagaraEmitterInstanceBatcher(ERHIFeatureLevel::Type InFeatureLevel, EShaderPlatform InShaderPlatform)
		: FeatureLevel(InFeatureLevel)
		, ShaderPlatform(InShaderPlatform)
		, ParticleSortBuffers(true)
	{
	}

	~NiagaraEmitterInstanceBatcher();

	static const FName Name;
	virtual FFXSystemInterface* GetInterface(const FName& InName) override;

	// The batcher assumes ownership of the data here.
	void GiveSystemTick_RenderThread(FNiagaraGPUSystemTick& Tick);

	void GiveEmitterContextToDestroy_RenderThread(FNiagaraComputeExecutionContext* Context);
	void GiveDataSetToDestroy_RenderThread(FNiagaraDataSet* DataSet);

	void EnqueueDeferredDeletesForDI_RenderThread(FNiagaraDataInterfaceProxy* Proxy)
	{
		DIProxyDeferredDeletes_RT.Add(Proxy);
	}

#if WITH_EDITOR
	virtual void Suspend() override {}
	virtual void Resume() override {}
#endif // #if WITH_EDITOR

	virtual void DrawDebug(FCanvas* Canvas) override {}
	virtual void AddVectorField(UVectorFieldComponent* VectorFieldComponent) override {}
	virtual void RemoveVectorField(UVectorFieldComponent* VectorFieldComponent) override {}
	virtual void UpdateVectorField(UVectorFieldComponent* VectorFieldComponent) override {}
	virtual void PreInitViews() override;
	virtual bool UsesGlobalDistanceField() const override;
	virtual void PreRender(FRHICommandListImmediate& RHICmdList, const class FGlobalDistanceFieldParameterData* GlobalDistanceFieldParameterData) override;

	virtual void Tick(float DeltaTime) override
	{
		BuildBatches();
	}

	// TODO: process queue, build batches from context with the same script
	//  also need to figure out how to handle multiple sets of parameters across a batch
	//	for now this executes every single sim in the queue individually, which is terrible 
	//	in terms of overhead
	void BuildBatches()
	{
	}

	uint32 GetEventSpawnTotal(const FNiagaraComputeExecutionContext *InContext) const;

	virtual void PostRenderOpaque(
		FRHICommandListImmediate& RHICmdList,
		const FUniformBufferRHIParamRef ViewUniformBuffer,
		const class FShaderParametersMetadata* SceneTexturesUniformBufferStruct,
		FUniformBufferRHIParamRef SceneTexturesUniformBuffer) override;

	void ExecuteAll(FRHICommandList &RHICmdList, FUniformBufferRHIParamRef ViewUniformBuffer);
	void TickSingle(const FNiagaraGPUSystemTick& Tick, FNiagaraComputeInstanceData *Instance, FRHICommandList &RHICmdList, FUniformBufferRHIParamRef ViewUniformBuffer) const;
	void ResizeBuffersAndGatherResources(FOverlappableTicks& OverlappableTick, FRHICommandList& RHICmdList, TMap<const FNiagaraComputeExecutionContext*, uint32>& ContextToMaxInstancesInBatch,
		FNiagaraBufferArray& CurrDataBuffers, FNiagaraBufferArray& PrevDataBuffers, FNiagaraBufferArray& CurrBufferIntFloat, FNiagaraBufferArray& PrevBufferIntFloat);

	void DispatchAllOnCompute(FOverlappableTicks& OverlappableTick, FRHICommandList& RHICmdList, FUniformBufferRHIParamRef ViewUniformBuffer,
		FNiagaraBufferArray& CurrDataBuffers, FNiagaraBufferArray& PrevDataBuffers, FNiagaraBufferArray& CurrBufferIntFloat, FNiagaraBufferArray& PrevBufferIntFloat);

	int32 AddSortedGPUSimulation(const FNiagaraGPUSortInfo& SortInfo);
	void SortGPUParticles(FRHICommandListImmediate& RHICmdList);
	void ResolveParticleSortBuffers(FRHICommandListImmediate& RHICmdList, int32 ResultBufferIndex);

	const FParticleIndicesVertexBuffer& GetGPUSortedBuffer() const { return SortedVertexBuffers.Last(); }
	const FGlobalDistanceFieldParameterData& GetGlobalDistanceFieldParameters() const { return GlobalDistanceFieldParams; }

	void ProcessDebugInfo(FRHICommandList &RHICmdList, const FNiagaraComputeExecutionContext *Context) const;

	void SetDataInterfaceParameters(const TArray<FNiagaraDataInterfaceProxy*>& DataInterfaceProxies, FNiagaraShader* Shader, FRHICommandList &RHICmdList, const FNiagaraComputeInstanceData* Instance, const FNiagaraGPUSystemTick& Tick) const;
	void UnsetDataInterfaceParameters(const TArray<FNiagaraDataInterfaceProxy*>& DataInterfaceProxies, FNiagaraShader* Shader, FRHICommandList& RHICmdList, const FNiagaraComputeInstanceData* Instance, const FNiagaraGPUSystemTick& Tick) const;

	template<bool bDoResourceTransitions>
	void Run(	const FNiagaraGPUSystemTick& Tick,
				const FNiagaraComputeInstanceData* Instance, 
				uint32 UpdateStartInstance, 
				const uint32 TotalNumInstances, 
				FNiagaraShader* Shader,
				FRHICommandList &RHICmdList, 
				FUniformBufferRHIParamRef ViewUniformBuffer, 
				bool bCopyBeforeStart = false
			) const;

	void ClearIndexBufferCur(FRHICommandList &RHICmdList, FNiagaraComputeExecutionContext *Context) const;
	void ResolveDatasetWrites(FRHICommandList &RHICmdList, FNiagaraComputeInstanceData *Context) const;
	void ResizeCurrentBuffer(FRHICommandList &RHICmdList, FNiagaraComputeExecutionContext *Context, uint32 NewNumInstances, uint32 PrevNumInstances) const;

private:
	void SimStepClearAndSetup(const FNiagaraComputeInstanceData& Instance, FRHICommandList& RHICmdList) const;
//	void SimStepReadback(const FNiagaraComputeInstanceData& Instance, FRHICommandList& RHICmdList) const;

	inline uint32 UnpackEmitterDispatchCount(uint8* PackedData)
	{
		return *(uint32*)PackedData;
	}

	inline FNiagaraComputeInstanceData* UnpackEmitterComputeDispatchArray(uint8* PackedData)
	{
		return (FNiagaraComputeInstanceData*)(PackedData + sizeof(uint32));
	}

	void FinishDispatches();
	void ReleaseTicks();

	/** Feature level of this effects system */
	ERHIFeatureLevel::Type FeatureLevel;
	/** Shader platform that will be rendering this effects system */
	EShaderPlatform ShaderPlatform;

	// Number of particle to sort this frame.
	int32 SortedParticleCount = 0;
	int32 NumFramesRequiringShrinking = 0;
	TArray<FNiagaraGPUSortInfo> SimulationsToSort;
	FParticleSortBuffers ParticleSortBuffers;

	// The result of the GPU sort. Each next element replace the previous.
	// The last entry is used to transfer the result of the ParticleSortBuffers.
	TIndirectArray<FNiagaraIndicesVertexBuffer> SortedVertexBuffers;
	
	TArray<FNiagaraGPUSystemTick> Ticks_RT;
	FGlobalDistanceFieldParameterData GlobalDistanceFieldParams;

	// For deferred deletion of resources
	TSet<FNiagaraComputeExecutionContext*> ContextsToDestroy_RT;
	TSet<FNiagaraDataSet*> DataSetsToDestroy_RT;

	TSet<FNiagaraDataInterfaceProxy*> DIProxyDeferredDeletes_RT;
};
