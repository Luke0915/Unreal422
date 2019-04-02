// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraScriptExecutionContext.h"
#include "RHI.h"
#include "RHIGPUReadback.h"
#include "NiagaraStats.h"
#include "NiagaraShader.h"
#include "NiagaraSortingGPU.h"
#include "NiagaraWorldManager.h"
#include "ShaderParameterUtils.h"
#include "SceneUtils.h"
#include "ShaderParameterUtils.h"
#include "ClearQuad.h"
#include "Runtime/Engine/Private/GPUSort.h"

DECLARE_CYCLE_STAT(TEXT("Niagara Dispatch Setup"), STAT_NiagaraGPUDispatchSetup_RT, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("GPU Emitter Dispatch [RT]"), STAT_NiagaraGPUSimTick_RT, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("GPU Data Readback [RT]"), STAT_NiagaraGPUReadback_RT, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Allocate GPU Readback Data [RT]"), STAT_NiagaraAllocateGPUReadback_RT, STATGROUP_Niagara);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Niagara GPU Sim"), STAT_GPU_NiagaraSim, STATGROUP_GPU);
DECLARE_DWORD_COUNTER_STAT(TEXT("# GPU Particles"), STAT_NiagaraGPUParticles, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("# GPU Sorted Particles"), STAT_NiagaraGPUSortedParticles, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("# GPU Sorted Buffers"), STAT_NiagaraGPUSortedBuffers, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("Readback latency (frames)"), STAT_NiagaraReadbackLatency, STATGROUP_Niagara);

DECLARE_GPU_STAT_NAMED(NiagaraGPU, TEXT("Niagara"));
DECLARE_GPU_STAT_NAMED(NiagaraGPUSimulation, TEXT("Niagara GPU Simulation"));
DECLARE_GPU_STAT_NAMED(NiagaraGPUSorting, TEXT("Niagara GPU sorting"));
DECLARE_GPU_STAT_NAMED(NiagaraSimReadback, TEXT("Niagara GPU Simulation read back"));
DECLARE_GPU_STAT_NAMED(NiagaraIndexBufferClear, TEXT("Niagara index buffer clear"));

uint32 FNiagaraComputeExecutionContext::TickCounter = 0;

int32 GNiagaraOverlapCompute = -1;
static FAutoConsoleVariableRef CVarNiagaraUseAsyncCompute(
	TEXT("fx.NiagaraOverlapCompute"),
	GNiagaraOverlapCompute,
	TEXT("If 1, use compute dispatch overlap for better performance. \n"),
	ECVF_Default
);

FNiagaraIndicesVertexBuffer::FNiagaraIndicesVertexBuffer(int32 InIndexCount)
	: IndexCount(InIndexCount)
{
	FRHIResourceCreateInfo CreateInfo;
	VertexBufferRHI = RHICreateVertexBuffer((uint32)IndexCount * sizeof(int32), BUF_Static | BUF_ShaderResource | BUF_UnorderedAccess, CreateInfo);
	VertexBufferSRV = RHICreateShaderResourceView(VertexBufferRHI, sizeof(int32), PF_R32_SINT);
	VertexBufferUAV = RHICreateUnorderedAccessView(VertexBufferRHI, PF_R32_SINT);
}

const FName NiagaraEmitterInstanceBatcher::Name(TEXT("NiagaraEmitterInstanceBatcher"));

FFXSystemInterface* NiagaraEmitterInstanceBatcher::GetInterface(const FName& InName)
{
	return InName == Name ? this : nullptr;
}

NiagaraEmitterInstanceBatcher::~NiagaraEmitterInstanceBatcher()
{
	ParticleSortBuffers.ReleaseRHI();
	ReleaseTicks();
}

void NiagaraEmitterInstanceBatcher::GiveSystemTick_RenderThread(FNiagaraGPUSystemTick& Tick)
{
	// Now we consume DataInterface instance data.
	if (Tick.DIInstanceData)
	{
		uint8* BasePointer = (uint8*) Tick.DIInstanceData->PerInstanceDataForRT;

		//UE_LOG(LogNiagara, Log, TEXT("RT Give DI (dipacket) %p (baseptr) %p"), Tick.DIInstanceData, BasePointer);
		for(auto& Pair : Tick.DIInstanceData->InterfaceProxiesToOffsets)
		{
			FNiagaraDataInterfaceProxy* Proxy = Pair.Key;
			uint8* InstanceDataPtr = BasePointer + Pair.Value;

			//UE_LOG(LogNiagara, Log, TEXT("\tRT DI (proxy) %p (instancebase) %p"), Proxy, InstanceDataPtr);
			Proxy->ConsumePerInstanceDataFromGameThread(InstanceDataPtr, Tick.SystemInstanceID);
		}
	}

	// A note:
	// This is making a copy of Tick. That structure is small now and we take a copy to avoid
	// making a bunch of small allocations on the game thread. We may need to revisit this.
	Ticks_RT.Add(Tick);
}

void NiagaraEmitterInstanceBatcher::GiveEmitterContextToDestroy_RenderThread(FNiagaraComputeExecutionContext* Context)
{
	ContextsToDestroy_RT.Add(Context);
}

void NiagaraEmitterInstanceBatcher::GiveDataSetToDestroy_RenderThread(FNiagaraDataSet* DataSet)
{
	DataSetsToDestroy_RT.Add(DataSet);
}

void NiagaraEmitterInstanceBatcher::FinishDispatches()
{
	ReleaseTicks();

	for (FNiagaraComputeExecutionContext* Context : ContextsToDestroy_RT)
	{
		delete Context;
	}
	ContextsToDestroy_RT.Reset();

	for (FNiagaraDataSet* DataSet : DataSetsToDestroy_RT)
	{
		delete DataSet;
	}
	DataSetsToDestroy_RT.Reset();

	for (FNiagaraDataInterfaceProxy* Proxy : DIProxyDeferredDeletes_RT)
	{
		Proxy->DeferredDestroy();
	}

	DIProxyDeferredDeletes_RT.Empty();
}

void NiagaraEmitterInstanceBatcher::ReleaseTicks()
{
	for (FNiagaraGPUSystemTick& Tick : Ticks_RT)
	{
		Tick.Destroy();
	}

	Ticks_RT.Empty(0);
}

void NiagaraEmitterInstanceBatcher::ResizeBuffersAndGatherResources(FOverlappableTicks& OverlappableTick, FRHICommandList& RHICmdList, TMap<const FNiagaraComputeExecutionContext*, uint32>& ContextToMaxInstancesInBatch,
	FNiagaraBufferArray& CurrDataBuffers, FNiagaraBufferArray& PrevDataBuffers, FNiagaraBufferArray& CurrBufferIntFloat, FNiagaraBufferArray& PrevBufferIntFloat)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraGPUDispatchSetup_RT);

	for (FNiagaraGPUSystemTick* Tick : OverlappableTick)
	{
		uint32 DispatchCount = Tick->Count;
		FNiagaraComputeInstanceData* Instances = Tick->GetInstanceData();
		for (uint32 Index = 0; Index < DispatchCount; Index++)
		{
			FNiagaraComputeInstanceData& Instance = Instances[Index];
			FNiagaraComputeExecutionContext* Context = Instance.Context;
			if (Context)
			{
//				uint32* MaxInstancesInBatch = ContextToMaxInstancesInBatch.Find(Context);
//				check(MaxInstancesInBatch);
				check(Context);

				Context->MainDataSet->Tick();

				if (Context->GPUScript_RT->GetShader())
				{
					FNiagaraShader* Shader = Context->GPUScript_RT->GetShader();

					uint32 PrevNumInstances = Context->MainDataSet->PrevData().GetNumInstances();
					uint32 NewNumInstances = Instance.SpawnRateInstances + Instance.EventSpawnTotal + PrevNumInstances;
					{
						// allocate for additional instances spawned and set the new number in the data set, if the new number is greater (meaning if we're spawning in this run)
						// TODO: interpolated spawning
						//
						if (NewNumInstances > PrevNumInstances)
						{
							//UE_LOG(LogNiagara, Warning, TEXT("Resize up!"));
							Context->MainDataSet->CurrData().AllocateGPU(NewNumInstances + 1, RHICmdList);
							Context->MainDataSet->CurrData().SetNumInstances(NewNumInstances);
						}
						// if we're not spawning, we need to make sure that the current buffer alloc size and number of instances matches the last one
						// we may have spawned in the last tick, so the buffers may be different sizes
						//
						else if (Context->MainDataSet->CurrData().GetNumInstances() < Context->MainDataSet->PrevData().GetNumInstances())
						{
							//UE_LOG(LogNiagara, Warning, TEXT("Resize down!"));
							Context->MainDataSet->CurrData().AllocateGPU(PrevNumInstances + 1, RHICmdList);
							Context->MainDataSet->CurrData().SetNumInstances(PrevNumInstances);
						}

// Commented out as this would need a buffer allocation change (look for all MaxInstancesInBatch)
/*						// allocate for additional instances spawned and set the new number in the data set, if the new number is greater (meaning if we're spawning in this run)
						// TODO: interpolated spawning
						//
						if (*MaxInstancesInBatch > Context->MainDataSet->CurrData().GetNumInstancesAllocated())
						{
							//UE_LOG(LogNiagara, Warning, TEXT("Resize up!"));
							Context->MainDataSet->CurrData().AllocateGPU(*MaxInstancesInBatch + 1, RHICmdList);
							Context->MainDataSet->CurrData().SetNumInstances(NewNumInstances);
						}

						// Note: Woodchuck will not shrink its buffers.

						// If the PrevData buffers don't exist let's just create them now.
						if (Context->MainDataSet->PrevData().GetGPUBufferFloat()->Buffer == nullptr)
						{
							Context->MainDataSet->PrevData().AllocateGPU(*MaxInstancesInBatch + 1, RHICmdList);
						}

						//// if we're not spawning, we need to make sure that the current buffer alloc size and number of instances matches the last one
						//// we may have spawned in the last tick, so the buffers may be different sizes
						////
						//else if (Context->MainDataSet->CurrData().GetNumInstances() < Context->MainDataSet->PrevData().GetNumInstances())
						//{
						//	//UE_LOG(LogNiagara, Warning, TEXT("Resize down!"));
						//	Context->MainDataSet->CurrData().AllocateGPU(PrevNumInstances + 1, RHICmdList);
						//	Context->MainDataSet->CurrData().SetNumInstances(PrevNumInstances);
						//}*/
					}

					// set up a data set index buffer, if we don't have one yet
					if (!Context->MainDataSet->HasDatasetIndices())
					{
						Context->MainDataSet->SetupCurDatasetIndices();
					}

					// We are setting this before we dispatch. We assume all instances survive until we get a valid
					// readback for this data set. The readback will do the fixup later.
					Context->MainDataSet->CurrData().SetNumInstances(NewNumInstances);

					if (Shader->FloatInputBufferParam.IsBound())
					{
						PrevBufferIntFloat.Add(Context->MainDataSet->PrevData().GetGPUBufferFloat()->UAV);
					}
					if (Shader->IntInputBufferParam.IsBound())
					{
						PrevBufferIntFloat.Add(Context->MainDataSet->PrevData().GetGPUBufferInt()->UAV);
					}

					if (Shader->FloatOutputBufferParam.IsBound())
					{
						CurrBufferIntFloat.Add(Context->MainDataSet->CurrData().GetGPUBufferFloat()->UAV);
					}

					if (Shader->IntOutputBufferParam.IsBound())
					{
						CurrBufferIntFloat.Add(Context->MainDataSet->CurrData().GetGPUBufferInt()->UAV);
					}

					// If we don't have an argument buffer this frame there may not be a previous one either.
					if (Context->MainDataSet->GetPrevDataSetIndices().Buffer)
					{
						PrevDataBuffers.Add(Context->MainDataSet->GetPrevDataSetIndices().UAV);
					}
					// We _must_ always have a current argument buffer.
					checkSlow(Context->MainDataSet->GetCurDataSetIndices().Buffer);
					CurrDataBuffers.Add(Context->MainDataSet->GetCurDataSetIndices().UAV);
				}
			} // end if Context
		} // end for (Instances)
	} // end for (Ticks)
}

void NiagaraEmitterInstanceBatcher::DispatchAllOnCompute(FOverlappableTicks& OverlappableTick, FRHICommandList& RHICmdList, FUniformBufferRHIParamRef ViewUniformBuffer,
	FNiagaraBufferArray& CurrDataBuffers, FNiagaraBufferArray& PrevDataBuffers, FNiagaraBufferArray& CurrBufferIntFloat, FNiagaraBufferArray& PrevBufferIntFloat)
{
	FRHICommandListImmediate& RHICmdListImmediate = FRHICommandListExecutor::GetImmediateCommandList();

	// Disable automatic cache flush so that we can have our compute work overlapping. Barrier will be used as a sync mechanism.
	RHICmdList.AutomaticCacheFlushAfterComputeShader(false);

	//
	//	Transition current index buffer ready for compute and clear then all using overlapping compute work items.
	//

	RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, CurrDataBuffers.GetData(), CurrDataBuffers.Num());

	{
		SCOPED_DRAW_EVENT(RHICmdList, NiagaraIndexBufferClear);
		SCOPED_GPU_STAT(RHICmdList, NiagaraIndexBufferClear);

		for (FNiagaraGPUSystemTick* Tick : OverlappableTick)
		{
			uint32 DispatchCount = Tick->Count;
			FNiagaraComputeInstanceData* Instances = Tick->GetInstanceData();
			for (uint32 Index = 0; Index < DispatchCount; Index++)
			{
				FNiagaraComputeInstanceData& Instance = Instances[Index];
				FNiagaraComputeExecutionContext* Context = Instance.Context;
				if (Context && Context->GPUScript_RT->GetShader())
				{
#if WITH_EDITORONLY_DATA
					if (Context->DebugInfo.IsValid())
					{
						ProcessDebugInfo(RHICmdList, Context);
					}
#endif // WITH_EDITORONLY_DATA

					// clear data set index buffer for the simulation shader to write number of written instances
					ClearUAV(RHICmdList, Context->MainDataSet->GetCurDataSetIndices(), 0);
				}

			}
		}
	}

	//
	//	Add a rw barrier for the current data buffers we just cleared and mark others as read/write as needed for particles simulation.
	//
	RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, CurrDataBuffers.GetData(), CurrDataBuffers.Num());
	RHICmdList.TransitionResources(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, CurrBufferIntFloat.GetData(), CurrBufferIntFloat.Num());
	RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, PrevDataBuffers.GetData(), PrevDataBuffers.Num());
	RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, PrevBufferIntFloat.GetData(), PrevBufferIntFloat.Num());
	RHICmdList.FlushComputeShaderCache();


	{
		SCOPED_DRAW_EVENT(RHICmdList, NiagaraGPUSimulation);
		SCOPED_GPU_STAT(RHICmdList, NiagaraGPUSimulation);
		for (FNiagaraGPUSystemTick* Tick : OverlappableTick)
		{
			uint32 DispatchCount = Tick->Count;
			FNiagaraComputeInstanceData* Instances = Tick->GetInstanceData();
			for (uint32 Index = 0; Index < DispatchCount; Index++)
			{
				FNiagaraComputeInstanceData& Instance = Instances[Index];
				FNiagaraComputeExecutionContext* Context = Instance.Context;
				if (Context && Context->GPUScript_RT->GetShader())
				{
					FNiagaraComputeExecutionContext::TickCounter++;

					// run shader, sim and spawn in a single dispatch
					uint32 UpdateStartInstance = 0;
					Run<false>(*Tick, &Instance, UpdateStartInstance, Instance.Context->MainDataSet->CurrData().GetNumInstances(), Context->GPUScript_RT->GetShader(), RHICmdList, ViewUniformBuffer);
				}
			}
		}
	}


	//
	//	Now Copy to staging buffer the data we want to read back (alive particle count). And make buffer ready for that and draw commands on the graphics pipe too.
	//
	RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, CurrDataBuffers.GetData(), CurrDataBuffers.Num());
	RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, CurrBufferIntFloat.GetData(), CurrBufferIntFloat.Num());
	RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, CurrDataBuffers.GetData(), CurrDataBuffers.Num());
	RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, CurrBufferIntFloat.GetData(), CurrBufferIntFloat.Num());
	RHICmdList.FlushComputeShaderCache();

	// We have done all our overlapping compute work on this list so go back to default behavior and flush.
	RHICmdList.AutomaticCacheFlushAfterComputeShader(true);

	{
		SCOPED_DRAW_EVENT(RHICmdList, NiagaraSimReadback);
		SCOPED_GPU_STAT(RHICmdList, NiagaraSimReadback);
		for (FNiagaraGPUSystemTick* Tick : OverlappableTick)
		{
			uint32 DispatchCount = Tick->Count;
			FNiagaraComputeInstanceData* Instances = Tick->GetInstanceData();
			for (uint32 Index = 0; Index < DispatchCount; Index++)
			{
				FNiagaraComputeInstanceData& Instance = Instances[Index];
				FNiagaraComputeExecutionContext* Context = Instance.Context;
				if (Context && Context->GPUScript_RT->GetShader())
				{
					// Don't resolve if the data if there are no instances (prevents a transition issue warning).
					if (Context->MainDataSet->CurrData().GetNumInstances() > 0)
					{
						// resolve data set writes - grabs the number of instances written from the index set during the simulation run
						ResolveDatasetWrites(RHICmdList, &Instance);
					}
					check(Context->MainDataSet->HasDatasetIndices());
				}
			}
		}
	}
	// the VF grabs PrevDataRender for drawing, so need to transition
	RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, PrevBufferIntFloat.GetData(), PrevBufferIntFloat.Num());

	// We have done all our compute work
	RHICmdList.FlushComputeShaderCache();
	RHICmdList.SubmitCommandsHint();
}

void NiagaraEmitterInstanceBatcher::PostRenderOpaque(FRHICommandListImmediate& RHICmdList, const FUniformBufferRHIParamRef ViewUniformBuffer, const class FShaderParametersMetadata* SceneTexturesUniformBufferStruct, FUniformBufferRHIParamRef SceneTexturesUniformBuffer)
{
	ExecuteAll(RHICmdList, ViewUniformBuffer);
	// Sort buffer only after the tick, so that the sorting and rendering stay coherent.
	SortGPUParticles(RHICmdList);
}

void NiagaraEmitterInstanceBatcher::ExecuteAll(FRHICommandList &RHICmdList, FUniformBufferRHIParamRef ViewUniformBuffer)
{
	// This is always called by the renderer so early out if we have no work.
	if (Ticks_RT.Num() == 0)
	{
		return;
	}

	SCOPED_DRAW_EVENT(RHICmdList, NiagaraEmitterInstanceBatcher_ExecuteAll);


// Commented out as this would need a buffer allocation change (look for all MaxInstancesInBatch)
	// GROSS MEMORY ALLOCATIONS. Is there a stack allocator for sets?
	TMap<const FNiagaraComputeExecutionContext*, uint32> ContextToMaxInstancesInBatch;
/*	for (FNiagaraGPUSystemTick& Tick : Ticks_RT)
	{
		uint32 DispatchCount = Tick.Count;
		FNiagaraComputeInstanceData* Instances = Tick.GetInstanceData();

		for (uint32 i = 0; i < DispatchCount; i++)
		{
			const FNiagaraComputeInstanceData& Instance = Instances[i];
			const FNiagaraComputeExecutionContext* Context = Instance.Context;

			uint32 PrevNumInstances = Context->MainDataSet->PrevData().GetNumInstances();
			uint32 NewNumInstances = Instance.SpawnRateInstances + Instance.EventSpawnTotal + PrevNumInstances;

			uint32& MaxInstances = ContextToMaxInstancesInBatch.FindOrAdd(Context);

			MaxInstances = FMath::Max(MaxInstances, NewNumInstances);
		}
	}*/

	if (GNiagaraOverlapCompute>0)
	{

		TArray<FOverlappableTicks> SimPasses;
		{
			TMap<FNiagaraComputeExecutionContext*, FOverlappableTicks> ContextToTicks;

			// Ticks are added in order. Two tick with the same context should not overlap so should be in two different batch.
			// Those ticks should still be executed in order.
			for (FNiagaraGPUSystemTick& Tick : Ticks_RT)
			{
				FNiagaraComputeInstanceData* data = Tick.GetInstanceData();
				ContextToTicks.FindOrAdd(data->Context).Add(&Tick);
			}

			//UE_LOG(LogTemp, Log, TEXT("Context count = %d"), ContextToTicks.Num());
			//uint32 i = 0;
			//for (auto& Ticks : ContextToTicks)
			//{
			//	UE_LOG(LogTemp, Log, TEXT("Ticks in context %d = %d"), i, Ticks.Value.Num());
			//	++i;
			//}

			// Count the maximum number of tick per context to know the number of passes we need to do
			uint32 NumSimPass = 0;
			for (auto& Ticks : ContextToTicks)
			{
				NumSimPass = FMath::Max(NumSimPass, (uint32)Ticks.Value.Num());
			}

			// Transpose now only once the data to get all independent tick per pass
			SimPasses.AddDefaulted(NumSimPass);
			for (auto& Ticks : ContextToTicks)
			{
				uint32 tickPass = 0;
				for (auto& Tick : Ticks.Value)
				{
					SimPasses[tickPass].Add(Tick);
					tickPass++;
				}
			}

			//UE_LOG(LogTemp, Log, TEXT("Simulation pass count = %d"), NumSimPass);
			//i = 0;
			//for (auto& PassTicks : SimPasses)
			//{
			//	UE_LOG(LogTemp, Log, TEXT("Ticks in pass %d = %d"), i, PassTicks.Num());
			//	++i;
			//}
		}

		for (auto& SimPass : SimPasses)
		{
			FNiagaraBufferArray CurrDataBuffers;
			FNiagaraBufferArray PrevDataBuffers;
			FNiagaraBufferArray CurrBufferIntFloat;
			FNiagaraBufferArray PrevBufferIntFloat;

			// This initial pass gathers all the buffers that are read from and written to so we can do batch resource transitions.
			// It also ensures the GPU buffers are large enough to hold everything.
			ResizeBuffersAndGatherResources(SimPass, RHICmdList, ContextToMaxInstancesInBatch,
				CurrDataBuffers, PrevDataBuffers, CurrBufferIntFloat, PrevBufferIntFloat);

			DispatchAllOnCompute(SimPass, RHICmdList, ViewUniformBuffer,
				CurrDataBuffers, PrevDataBuffers, CurrBufferIntFloat, PrevBufferIntFloat);
		}


	}
	else
	{
		for (FNiagaraGPUSystemTick& Tick : Ticks_RT)
		{
			uint32 DispatchCount = Tick.Count;
			FNiagaraComputeInstanceData* Instances = Tick.GetInstanceData();

			for (uint32 i = 0; i < DispatchCount; i++)
			{
				FNiagaraComputeInstanceData& Instance = Instances[i];
				TickSingle(Tick, &Instance, RHICmdList, ViewUniformBuffer);
			}
		}
	}

	// Reset the spawn count.
	for (FNiagaraGPUSystemTick& Tick : Ticks_RT)
	{
		FNiagaraComputeInstanceData* Instances = Tick.GetInstanceData();
		for (uint32 i = 0; i < Tick.Count; i++)
		{
			Instances[i].Context->MainDataSet->NumOfSpawnedInstances_RT = 0;
		}
	}

	FinishDispatches();
}

void NiagaraEmitterInstanceBatcher::SimStepClearAndSetup(const FNiagaraComputeInstanceData& Instance, FRHICommandList& RHICmdList) const
{
	check(IsInRenderingThread());
#if 1
	FNiagaraComputeExecutionContext* Context = Instance.Context;
	check(Context);


	FNiagaraShader* ComputeShader = Context->GPUScript_RT->GetShader();
	check(ComputeShader);
	if (!ComputeShader)
	{
		return;
	}

#if WITH_EDITORONLY_DATA
	if (Context->DebugInfo.IsValid())
	{
		ProcessDebugInfo(RHICmdList, Context);
	}
#endif // WITH_EDITORONLY_DATA

	// clear data set index buffer for the simulation shader to write number of written instances to
	//
	FRWBuffer &DatasetIndexBufferWrite = Context->MainDataSet->GetCurDataSetIndices();
//	SCOPED_GPU_EVENTF(RHICmdList, NiagaraIndexBufferClear, TEXT("Niagara index buffer clear"));
	SCOPED_DRAW_EVENT(RHICmdList, NiagaraIndexBufferClear);
	SCOPED_GPU_STAT(RHICmdList, NiagaraIndexBufferClear);

	ClearUAV(RHICmdList, DatasetIndexBufferWrite, 0);
#endif
}

//void NiagaraEmitterInstanceBatcher::SimStepReadback(const FNiagaraComputeInstanceData& Instance, FRHICommandList& RHICmdList) const
//{
//#if 1
//	FNiagaraComputeExecutionContext* Context = Instance.Context;
//	check(Context);
//	FNiagaraShader* ComputeShader = Context->GPUScript_RT->GetShader();
//	if (!ComputeShader)
//	{
//		return;
//	}
//
//	// Don't resolve if the data if there are no instances (prevents a transition issue warning).
//	if (Context->MainDataSet->CurrData().GetNumInstances() > 0)
//	{
//		// resolve data set writes - grabs the number of instances written from the index set during the simulation run
//		ResolveDatasetWrites(RHICmdList, &Instance);
//	}
//
//	check(Context->MainDataSet->HasDatasetIndices());
//#endif
//}

void NiagaraEmitterInstanceBatcher::TickSingle(const FNiagaraGPUSystemTick& Tick, FNiagaraComputeInstanceData *Instance, FRHICommandList &RHICmdList, FUniformBufferRHIParamRef ViewUniformBuffer) const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraGPUSimTick_RT);

	FNiagaraComputeExecutionContext* Context = Instance->Context;
	
	check(IsInRenderingThread());
	Context->MainDataSet->Tick();

	FNiagaraComputeExecutionContext::TickCounter++;

	FNiagaraShader* ComputeShader = Context->GPUScript_RT->GetShader();
	if (!ComputeShader)
	{
		return;
	}

#if WITH_EDITORONLY_DATA
	if (Context->DebugInfo.IsValid())
	{
		ProcessDebugInfo(RHICmdList, Context);
	}
#endif // WITH_EDITORONLY_DATA

	uint32 PrevNumInstances = Context->MainDataSet->PrevData().GetNumInstances();
	uint32 NewNumInstances = Instance->SpawnRateInstances + Instance->EventSpawnTotal + PrevNumInstances;

	ResizeCurrentBuffer(RHICmdList, Context, NewNumInstances, PrevNumInstances);

	// set up a data set index buffer, if we don't have one yet
	//
	if (!Context->MainDataSet->HasDatasetIndices())
	{
		Context->MainDataSet->SetupCurDatasetIndices();
	}

	// clear data set index buffer for the simulation shader to write number of written instances to
	//
	ClearIndexBufferCur(RHICmdList, Context);

	// run shader, sim and spawn in a single dispatch
	uint32 UpdateStartInstance = 0;
	Run<true>(Tick, Instance, UpdateStartInstance, NewNumInstances, ComputeShader, RHICmdList, ViewUniformBuffer);

	// assume all instances survived; ResolveDataSetWrites will change this if the deferred readback was successful; that data may be several frames old
	Context->MainDataSet->CurrData().SetNumInstances(NewNumInstances);

	// ResolveDatasetWrites may read this, so we must transition it here.
	RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, Context->MainDataSet->GetCurDataSetIndices().UAV);		// transition to readable; we'll be using this next frame

	// Don't resolve if the data if there are no instances (prevents a transition issue warning).
	if (NewNumInstances > 0)
	{
		// resolve data set writes - grabs the number of instances written from the index set during the simulation run
		ResolveDatasetWrites(RHICmdList, Instance);
	}

	/*
	// TODO: hack - only updating event set 0 on update scripts now; need to match them to their indices and update them all
	if (Context->UpdateEventWriteDataSets.Num())
	{ 
		Context->UpdateEventWriteDataSets[0]->CurrDataRender().SetNumInstances(NumInstancesAfterSim[1]);
	}
	RunEventHandlers(Context, NumInstancesAfterSim[0], NumInstancesAfterSpawn, NumInstancesAfterNonEventSpawn, RHICmdList);
	*/

	// the VF grabs PrevDataRender for drawing, so need to transition
	RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, Context->MainDataSet->PrevData().GetGPUBufferFloat()->UAV);
	RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, Context->MainDataSet->PrevData().GetGPUBufferInt()->UAV);

	check(Context->MainDataSet->HasDatasetIndices());
}

void NiagaraEmitterInstanceBatcher::PreInitViews()
{
	SortedParticleCount = 0;
	SimulationsToSort.Reset();

	for (FNiagaraIndicesVertexBuffer& SortedVertexBuffer : SortedVertexBuffers)
	{
		SortedVertexBuffer.UsedIndexCount = 0;
	}
	
	// Accumulate the spawn count. Used in the rendering to get an upper estimate of the instance count.
	for (FNiagaraGPUSystemTick& Tick : Ticks_RT)
	{
		FNiagaraComputeInstanceData* Instances = Tick.GetInstanceData();
		for (uint32 i = 0; i < Tick.Count; i++)
		{
			Instances[i].Context->MainDataSet->NumOfSpawnedInstances_RT += Instances[i].SpawnRateInstances + Instances[i].EventSpawnTotal;
		}
	}
}

bool NiagaraEmitterInstanceBatcher::UsesGlobalDistanceField() const
{
	for (const FNiagaraGPUSystemTick& Tick : Ticks_RT)
	{
		if (Tick.bRequiredDistanceFieldData)
		{
			return true;
		}
	}

	return false;
}

void NiagaraEmitterInstanceBatcher::PreRender(FRHICommandListImmediate& RHICmdList, const class FGlobalDistanceFieldParameterData* GlobalDistanceFieldParameterData)
{
	GlobalDistanceFieldParams = GlobalDistanceFieldParameterData ? *GlobalDistanceFieldParameterData : FGlobalDistanceFieldParameterData();
}

int32 NiagaraEmitterInstanceBatcher::AddSortedGPUSimulation(const FNiagaraGPUSortInfo& SortInfo)
{
	const int32 ResultOffset = SortedParticleCount;
	SimulationsToSort.Add(SortInfo);

	SortedParticleCount += SortInfo.ParticleCount;

	if (!SortedVertexBuffers.Num())
	{
		SortedVertexBuffers.Add(new FNiagaraIndicesVertexBuffer(FMath::Max(GNiagaraGPUSortingMinBufferSize, (int32)(SortedParticleCount * GNiagaraGPUSortingBufferSlack))));
	}
	// If we don't fit anymore, reallocate to a bigger size.
	else if (SortedParticleCount > SortedVertexBuffers.Last().IndexCount)
	{
		SortedVertexBuffers.Add(new FNiagaraIndicesVertexBuffer((int32)(SortedParticleCount * GNiagaraGPUSortingBufferSlack)));
	}

	// Keep track of the last used index, which is also the first used index of next entry
	// if we need to increase the size of SortedVertexBuffers. Used in FNiagaraCopyIntBufferRegionCS
	SortedVertexBuffers.Last().UsedIndexCount = SortedParticleCount;

	return ResultOffset;
}

void NiagaraEmitterInstanceBatcher::SortGPUParticles(FRHICommandListImmediate& RHICmdList)
{
	if (SortedParticleCount > 0 && SortedVertexBuffers.Num() > 0 && SimulationsToSort.Num() && GNiagaraGPUSortingBufferSlack > 1.f)
	{
		SCOPED_GPU_STAT(RHICmdList, NiagaraGPUSorting);

		ensure(SortedVertexBuffers.Last().IndexCount >= SortedParticleCount);

		// The particle sort buffer must be able to hold all the particles.
		if (SortedVertexBuffers.Last().IndexCount != ParticleSortBuffers.GetSize())
		{
			ParticleSortBuffers.ReleaseRHI();
			ParticleSortBuffers.SetBufferSize(SortedVertexBuffers.Last().IndexCount);
			ParticleSortBuffers.InitRHI();
		}

		INC_DWORD_STAT_BY(STAT_NiagaraGPUSortedParticles, SortedParticleCount);
		INC_DWORD_STAT_BY(STAT_NiagaraGPUSortedBuffers, ParticleSortBuffers.GetSize());

		// Make sure our outputs are safe to write to.
		FUnorderedAccessViewRHIParamRef OutputUAVs[2] = { ParticleSortBuffers.GetKeyBufferUAV(), ParticleSortBuffers.GetVertexBufferUAV() };
		RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, OutputUAVs, 2);

		// EmitterKey = (EmitterIndex & EmitterKeyMask) << EmitterKeyShift.
		// SortKey = (Key32 >> SortKeyShift) & SortKeyMask.
		uint32 EmitterKeyMask = (1 << FMath::CeilLogTwo(SimulationsToSort.Num())) - 1;
		uint32 EmitterKeyShift = 16;
		uint32 SortKeyMask = 0xFFFF;
		
		{
			SCOPED_DRAW_EVENT(RHICmdList, NiagaraSortKeyGen);

			// Bind the shader
			
			FNiagaraSortKeyGenCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FNiagaraSortKeyGenCS::FSortUsingMaxPrecision>(GNiagaraGPUSortingUseMaxPrecision != 0);
			
			TShaderMapRef<FNiagaraSortKeyGenCS> KeyGenCS(GetGlobalShaderMap(FeatureLevel), PermutationVector);
			RHICmdList.SetComputeShader(KeyGenCS->GetComputeShader());
			KeyGenCS->SetOutput(RHICmdList, ParticleSortBuffers.GetKeyBufferUAV(), ParticleSortBuffers.GetVertexBufferUAV());

			// (SortKeyMask, SortKeyShift, SortKeySignBit)
			FUintVector4 SortKeyParams(SortKeyMask, 0, 0x8000, 0); 
			if (GNiagaraGPUSortingUseMaxPrecision != 0)
			{
				EmitterKeyMask = FMath::Max<uint32>(EmitterKeyMask , 1); // Need at list 1 bit for the above logic
				uint32 UnusedBits = FPlatformMath::CountLeadingZeros(EmitterKeyMask << EmitterKeyShift);
				EmitterKeyShift += UnusedBits;
				SortKeyMask = ~(EmitterKeyMask << EmitterKeyShift);

				SortKeyParams.X = SortKeyMask;
				SortKeyParams.Y = 16 - UnusedBits;
				SortKeyParams.Z <<= UnusedBits;
			}
			
			int32 OutputOffset = 0;
			for (int32 EmitterIndex = 0; EmitterIndex < SimulationsToSort.Num(); ++EmitterIndex)
			{
				const FNiagaraGPUSortInfo& SortInfo = SimulationsToSort[EmitterIndex];
				KeyGenCS->SetParameters(RHICmdList, SortInfo, (uint32)EmitterIndex << EmitterKeyShift, OutputOffset, SortKeyParams);
				DispatchComputeShader(RHICmdList, *KeyGenCS, FMath::DivideAndRoundUp(SortInfo.ParticleCount, NIAGARA_KEY_GEN_THREAD_COUNT), 1, 1);

				OutputOffset += SortInfo.ParticleCount;
			}
			KeyGenCS->UnbindBuffers(RHICmdList);
		}

		// We may be able to remove this transition if each step isn't dependent on the previous one.
		RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, OutputUAVs, 2);

		// Sort buffers and copy results to index buffers.
		{
			const uint32 KeyMask = (EmitterKeyMask << EmitterKeyShift) | SortKeyMask;
			const int32 ResultBufferIndex = SortGPUBuffers(RHICmdList, ParticleSortBuffers.GetSortBuffers(), 0, KeyMask, SortedParticleCount, FeatureLevel);
			ResolveParticleSortBuffers(RHICmdList, ResultBufferIndex);
		}

		// Only keep the last sorted index buffer, which is of the same size as ParticleSortBuffers.GetSize().
		SortedVertexBuffers.RemoveAt(0, SortedVertexBuffers.Num() - 1);

		// Resize the buffer to maximize next frame.
		// Those ratio must take into consideration the slack ratio to be stable.

		const int32 RecommandedSize = FMath::Max(GNiagaraGPUSortingMinBufferSize, (int32)(SortedParticleCount * GNiagaraGPUSortingBufferSlack));
		const float BufferUsage = (float)SortedParticleCount /	(float)ParticleSortBuffers.GetSize();

		if (RecommandedSize < ParticleSortBuffers.GetSize() / GNiagaraGPUSortingBufferSlack)
		{
			if (NumFramesRequiringShrinking >= GNiagaraGPUSortingFrameCountBeforeBufferShrinking)
			{
				NumFramesRequiringShrinking = 0;
				ParticleSortBuffers.ReleaseRHI();
				ParticleSortBuffers.SetBufferSize(0);

				// Add an entry that should fit well for next frame.
				SortedVertexBuffers.Empty();
				SortedVertexBuffers.Add(new FNiagaraIndicesVertexBuffer(RecommandedSize));
			}
			else
			{
				++NumFramesRequiringShrinking;
			}
		}
	}
	else // If the are no sort task, we don't need any of the sort buffers.
	{
		if (NumFramesRequiringShrinking >= GNiagaraGPUSortingFrameCountBeforeBufferShrinking)
		{
			NumFramesRequiringShrinking = 0;
			ParticleSortBuffers.ReleaseRHI();
			ParticleSortBuffers.SetBufferSize(0);
			SortedVertexBuffers.Empty();
		}
		else
		{
			++NumFramesRequiringShrinking;
		}
	}
}

void NiagaraEmitterInstanceBatcher::ResolveParticleSortBuffers(FRHICommandListImmediate& RHICmdList, int32 ResultBufferIndex)
{
	SCOPED_DRAW_EVENT(RHICmdList, NiagaraResolveParticleSortBuffers);

#if 0 // TODO use this once working properly!
	if (SortedVertexBuffers.Num() == 1)
	{
		RHICmdList.CopyVertexBuffer(ParticleSortBuffers.GetSortedVertexBufferRHI(ResultBufferIndex), SortedVertexBuffers.Last().VertexBufferRHI);
		return;
	}
#endif

	TShaderMapRef<FNiagaraCopyIntBufferRegionCS> CopyBufferCS(GetGlobalShaderMap(FeatureLevel));
	RHICmdList.SetComputeShader(CopyBufferCS->GetComputeShader());

	int32 StartingIndex = 0;

	for (int32 Index = 0; Index < SortedVertexBuffers.Num(); Index += NIAGARA_COPY_BUFFER_BUFFER_COUNT)
	{
		FUnorderedAccessViewRHIParamRef UAVs[NIAGARA_COPY_BUFFER_BUFFER_COUNT] = {};
		int32 UsedIndexCounts[NIAGARA_COPY_BUFFER_BUFFER_COUNT] = {};

		const int32 NumBuffers = FMath::Min<int32>(NIAGARA_COPY_BUFFER_BUFFER_COUNT, SortedVertexBuffers.Num() - Index);

		int32 LastCount = StartingIndex;
		for (int32 SubIndex = 0; SubIndex < NumBuffers; ++SubIndex)
		{
			const FNiagaraIndicesVertexBuffer& SortBuffer = SortedVertexBuffers[Index + SubIndex];
			UAVs[SubIndex] = SortBuffer.VertexBufferUAV;
			UsedIndexCounts[SubIndex] = SortBuffer.UsedIndexCount;

			LastCount = SortBuffer.UsedIndexCount;
		}

		CopyBufferCS->SetParameters(RHICmdList, ParticleSortBuffers.GetSortedVertexBufferSRV(ResultBufferIndex), UAVs, UsedIndexCounts, StartingIndex, NumBuffers);
		DispatchComputeShader(RHICmdList, *CopyBufferCS, FMath::DivideAndRoundUp(LastCount - StartingIndex, NIAGARA_COPY_BUFFER_THREAD_COUNT), 1, 1);
		RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToGfx, UAVs, NumBuffers);

		StartingIndex = LastCount;
	}
	CopyBufferCS->UnbindBuffers(RHICmdList);
}

/* Clear the data set index buffer; needs to be called before a sim run
 */
void NiagaraEmitterInstanceBatcher::ClearIndexBufferCur(FRHICommandList &RHICmdList, FNiagaraComputeExecutionContext *Context) const
{
	FRWBuffer &DatasetIndexBufferWrite = Context->MainDataSet->GetCurDataSetIndices();
	SCOPED_DRAW_EVENTF(RHICmdList, NiagaraIndexBufferClear, TEXT("Niagara index buffer clear"));

	ClearUAV(RHICmdList, DatasetIndexBufferWrite, 0);
	RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, DatasetIndexBufferWrite.UAV);
}

/* Attempt to read back simulation results (number of live instances) from the GPU via an async readback request; 
 *	If the readback isn't ready to be performed, we accumulate spawn rates and assume all instances have survived, until
 *	the GPU can tell us how many are actually alive; since that data may be several frames old, we'll always end up
 *	overallocating a bit, and the CPU might think we have more particles alive than we actually do;
 *	since we use DrawIndirect with the GPU determining draw call parameters, that's not an issue
 */
void NiagaraEmitterInstanceBatcher::ResolveDatasetWrites(FRHICommandList &RHICmdList, FNiagaraComputeInstanceData *Instance) const
{
	FNiagaraComputeExecutionContext* Context = Instance->Context;
	FRWBuffer &DatasetIndexBufferWrite = Context->MainDataSet->GetCurDataSetIndices();
	uint32 SpawnedThisFrame = Instance->SpawnRateInstances + Instance->EventSpawnTotal;
	Context->AccumulatedSpawnRate += SpawnedThisFrame;
	int32 ExistingDataCount = Context->MainDataSet->CurrData().GetNumInstances();
	if (!Context->GPUDataReadback)
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraAllocateGPUReadback_RT);

		Context->GPUDataReadback = new FRHIGPUMemoryReadback(TEXT("Niagara GPU Emitter Readback"));
		INC_DWORD_STAT(STAT_NiagaraReadbackLatency);
		Context->GPUDataReadback->EnqueueCopy(RHICmdList, DatasetIndexBufferWrite.Buffer);
		INC_DWORD_STAT_BY(STAT_NiagaraGPUParticles, ExistingDataCount);
	}
	else if (Context->GPUDataReadback->IsReady())
	{
		bool bSuccessfullyRead = false;
		{
		    SCOPE_CYCLE_COUNTER(STAT_NiagaraGPUReadback_RT);
		    int32 *NumInstancesAfterSim = static_cast<int32*>(Context->GPUDataReadback->Lock(64 * sizeof(int32)));
			if (NumInstancesAfterSim)
			{
				int32 NewExistingDataCount = NumInstancesAfterSim[1] + Context->AccumulatedSpawnRate; // index 1 is always the count
				Context->MainDataSet->CurrData().SetNumInstances(NewExistingDataCount);
				//UE_LOG(LogNiagara, Log, TEXT("GPU Syncup %s : Was(%d) Now(%d)"), *Context->DebugSimName, ExistingDataCount, NewExistingDataCount );
				INC_DWORD_STAT_BY(STAT_NiagaraGPUParticles, NewExistingDataCount);
				SET_DWORD_STAT(STAT_NiagaraReadbackLatency, 0);

				Context->AccumulatedSpawnRate = 0;
				bSuccessfullyRead = true;
			}
			else
			{
				UE_LOG(LogNiagara, Warning, TEXT("GPUDataReadback said it was ready, but returned an invalid buffer. Skipping this time.."));
				INC_DWORD_STAT_BY(STAT_NiagaraGPUParticles, ExistingDataCount);
			}
			Context->GPUDataReadback->Unlock();
		}
		if (bSuccessfullyRead)
		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraAllocateGPUReadback_RT);
			// The following code seems to take significant time on d3d12
			Context->GPUDataReadback->EnqueueCopy(RHICmdList, DatasetIndexBufferWrite.Buffer);
		}
	}
	else
	{
		INC_DWORD_STAT_BY(STAT_NiagaraGPUParticles, ExistingDataCount);
	}
}

void NiagaraEmitterInstanceBatcher::ProcessDebugInfo(FRHICommandList &RHICmdList, const FNiagaraComputeExecutionContext *Context) const
{
#if WITH_EDITORONLY_DATA
	// This method may be called from one of two places: in the tick or as part of a paused frame looking for the debug info that was submitted previously...
	// Note that PrevData is where we expect the data to be for rendering, as per NiagaraEmitterInstanceBatcher::TickSingle
	if (Context && Context->DebugInfo.IsValid())
	{
		
		// Fire off the readback if not already doing so
		if (!Context->GPUDebugDataReadbackFloat && !Context->GPUDebugDataReadbackInt && !Context->GPUDebugDataReadbackCounts)
		{
			// Do nothing.., handled in Run
		}
		// We may not have floats or ints, but we should have at least one of the two
		else if ((Context->GPUDebugDataReadbackFloat == nullptr || Context->GPUDebugDataReadbackFloat->IsReady()) 
				&& (Context->GPUDebugDataReadbackInt == nullptr || Context->GPUDebugDataReadbackInt->IsReady())
				&& Context->GPUDebugDataReadbackCounts->IsReady()
			)
		{
			//UE_LOG(LogNiagara, Warning, TEXT("Read back!"));

			int32 *NumInstancesAfterSim = static_cast<int32*>(Context->GPUDebugDataReadbackCounts->Lock(64 * sizeof(int32)));
			int32 NewExistingDataCount = NumInstancesAfterSim[1];
			{
				float* FloatDataBuffer = nullptr;
				if (Context->GPUDebugDataReadbackFloat)
				{
					FloatDataBuffer = static_cast<float*>(Context->GPUDebugDataReadbackFloat->Lock(Context->GPUDebugDataFloatSize));
				}
				int* IntDataBuffer = nullptr;
				if (Context->GPUDebugDataReadbackInt)
				{
					IntDataBuffer = static_cast<int*>(Context->GPUDebugDataReadbackInt->Lock(Context->GPUDebugDataIntSize));
				}
				Context->MainDataSet->DumpGPU(Context->DebugInfo->Frame, FloatDataBuffer, IntDataBuffer, 0, NewExistingDataCount);
				Context->DebugInfo->bWritten = true;

				if (Context->GPUDebugDataReadbackFloat)
				{
					Context->GPUDebugDataReadbackFloat->Unlock();
				}
				if (Context->GPUDebugDataReadbackInt)
				{
					Context->GPUDebugDataReadbackInt->Unlock();
				}
				Context->GPUDebugDataReadbackCounts->Unlock();
			}
			{
				// The following code seems to take significant time on d3d12
				// Clear out the readback buffers...
				if (Context->GPUDebugDataReadbackFloat)
				{
					delete Context->GPUDebugDataReadbackFloat;
					Context->GPUDebugDataReadbackFloat = nullptr;
				}
				if (Context->GPUDebugDataReadbackInt)
				{
					delete Context->GPUDebugDataReadbackInt;
					Context->GPUDebugDataReadbackInt = nullptr;
				}
				delete Context->GPUDebugDataReadbackCounts;
				Context->GPUDebugDataReadbackCounts = nullptr;	
				Context->GPUDebugDataFloatSize = 0;
				Context->GPUDebugDataIntSize = 0;
			}

			// We've updated the debug info directly, now we need to no longer keep asking and querying because this frame is done!
			Context->DebugInfo.Reset();
		}
	}
#endif // WITH_EDITORONLY_DATA
}



/* Resize data set buffers and set number of instances 
 *	Allocates one additional instance at the end, which is a scratch instance; by setting the default index from AcquireIndex in the shader
 *	to that scratch index, we can avoid branching in every single OutputData function
 */
void NiagaraEmitterInstanceBatcher::ResizeCurrentBuffer(FRHICommandList &RHICmdList, FNiagaraComputeExecutionContext *Context, uint32 NewNumInstances, uint32 PrevNumInstances) const
{

	// allocate for additional instances spawned and set the new number in the data set, if the new number is greater (meaning if we're spawning in this run)
	// TODO: interpolated spawning
	//
	if (NewNumInstances > PrevNumInstances)
	{
		//UE_LOG(LogNiagara, Warning, TEXT("Resize up!"));
		Context->MainDataSet->CurrData().AllocateGPU(NewNumInstances + 1, RHICmdList);
		Context->MainDataSet->CurrData().SetNumInstances(NewNumInstances);
	}
	// if we're not spawning, we need to make sure that the current buffer alloc size and number of instances matches the last one
	// we may have spawned in the last tick, so the buffers may be different sizes
	//
	else if (Context->MainDataSet->CurrData().GetNumInstances() < Context->MainDataSet->PrevData().GetNumInstances())
	{
		//UE_LOG(LogNiagara, Warning, TEXT("Resize down!"));
		Context->MainDataSet->CurrData().AllocateGPU(PrevNumInstances + 1, RHICmdList);
		Context->MainDataSet->CurrData().SetNumInstances(PrevNumInstances);
	}
}


/* Set shader parameters for data interfaces
 */
void NiagaraEmitterInstanceBatcher::SetDataInterfaceParameters(const TArray<FNiagaraDataInterfaceProxy*>& DataInterfaceProxies, FNiagaraShader* Shader, FRHICommandList &RHICmdList, const FNiagaraComputeInstanceData* Instance, const FNiagaraGPUSystemTick& Tick) const
{
	// set up data interface buffers, as defined by the DIs during compilation
	//

	// @todo-threadsafety This is a bit gross. Need to rethink this api.
	const FGuid& SystemInstance = Tick.SystemInstanceID;

	uint32 InterfaceIndex = 0;
	for (FNiagaraDataInterfaceProxy* Interface : DataInterfaceProxies)
	{
		FNiagaraDataInterfaceParamRef& DIParam = Shader->GetDIParameters()[InterfaceIndex];
		if (DIParam.Parameters)
		{
			FNiagaraDataInterfaceSetArgs Context;
			Context.Shader = Shader;
			Context.DataInterface = Interface;
			Context.SystemInstance = SystemInstance;
			Context.Batcher = this;
			DIParam.Parameters->Set(RHICmdList, Context);
		}

		InterfaceIndex++;
	}
}

void NiagaraEmitterInstanceBatcher::UnsetDataInterfaceParameters(const TArray<FNiagaraDataInterfaceProxy*> &DataInterfaceProxies, FNiagaraShader* Shader, FRHICommandList &RHICmdList, const FNiagaraComputeInstanceData* Instance, const FNiagaraGPUSystemTick& Tick) const
{
	// set up data interface buffers, as defined by the DIs during compilation
	//

	// @todo-threadsafety This is a bit gross. Need to rethink this api.
	const FGuid& SystemInstance = Tick.SystemInstanceID;

	uint32 InterfaceIndex = 0;
	for (FNiagaraDataInterfaceProxy* Interface : DataInterfaceProxies)
	{
		FNiagaraDataInterfaceParamRef& DIParam = Shader->GetDIParameters()[InterfaceIndex];
		if (DIParam.Parameters)
		{
			void* PerInstanceData = nullptr;
			int32* OffsetFound = nullptr;
			if (Tick.DIInstanceData && Tick.DIInstanceData->PerInstanceDataSize != 0 && Tick.DIInstanceData->InterfaceProxiesToOffsets.Num() != 0)
			{
				OffsetFound = Tick.DIInstanceData->InterfaceProxiesToOffsets.Find(Interface);
				if (OffsetFound != nullptr)
				{
					PerInstanceData = (*OffsetFound) + (uint8*)Tick.DIInstanceData->PerInstanceDataForRT;
				}
			}
			FNiagaraDataInterfaceSetArgs Context;
			Context.Shader = Shader;
			Context.DataInterface = Interface;
			Context.SystemInstance = SystemInstance;
			Context.Batcher = this;
			DIParam.Parameters->Unset(RHICmdList, Context);
		}

		InterfaceIndex++;
	}
}


/* Kick off a simulation/spawn run
 */
 template<bool bDoResourceTransitions>
void NiagaraEmitterInstanceBatcher::Run(const FNiagaraGPUSystemTick& Tick, const FNiagaraComputeInstanceData* Instance, uint32 UpdateStartInstance, const uint32 TotalNumInstances, FNiagaraShader* Shader,
	FRHICommandList &RHICmdList, FUniformBufferRHIParamRef ViewUniformBuffer, bool bCopyBeforeStart) const
{
	FNiagaraComputeExecutionContext* Context = Instance->Context;
	if (TotalNumInstances == 0 && !bDoResourceTransitions)
	{
		SCOPED_DRAW_EVENTF(RHICmdList, NiagaraGPUSimulationCS, TEXT("Niagara Gpu Sim - %s - NumInstances: %u"),
			*Context->DebugSimName,
			TotalNumInstances);
		return;
	}

	//UE_LOG(LogNiagara, Warning, TEXT("Run"));

	FNiagaraDataSet *DataSet = Context->MainDataSet;
	
	const TArray<FNiagaraDataInterfaceProxy*>& DataInterfaceProxies = Instance->DataInterfaceProxies;
	const FRHIUniformBufferLayout& CBufferLayout = Context->CBufferLayout;
	const FRWBuffer &WriteIndexBuffer = DataSet->GetCurDataSetIndices();
	FRWBuffer &ReadIndexBuffer = DataSet->GetPrevDataSetIndices();

	// if we don't have a previous index buffer, we need to prep one using the maximum number of instances; this should only happen on the first frame
	//		the data set index buffer is really the param buffer for the indirect draw call; it contains the number of live instances at index 1, and the simulation
	//		CS uses this to determine the current number of active instances in the buffer
	//
	if (ReadIndexBuffer.Buffer == nullptr)
	{
		TResourceArray<int32> InitIndexBuffer;
		InitIndexBuffer.AddUninitialized(64);
		InitIndexBuffer[1] = 0;		// number of instances 
		ReadIndexBuffer.Initialize(sizeof(int32), 64, EPixelFormat::PF_R32_UINT, BUF_DrawIndirect | BUF_Static, nullptr, &InitIndexBuffer);
	}

	RHICmdList.SetComputeShader(Shader->GetComputeShader());

	RHICmdList.SetShaderResourceViewParameter(Shader->GetComputeShader(), Shader->InputIndexBufferParam.GetBaseIndex(), ReadIndexBuffer.SRV);

	// set the view uniform buffer param
	if (Shader->ViewUniformBufferParam.IsBound() && ViewUniformBuffer)
	{
		RHICmdList.SetShaderUniformBuffer(Shader->GetComputeShader(), Shader->ViewUniformBufferParam.GetBaseIndex(), ViewUniformBuffer);
	}

	SetDataInterfaceParameters(DataInterfaceProxies, Shader, RHICmdList, Instance, Tick);

	// set the shader and data set params 
	//
	uint32 WriteBufferIdx = 0;
	uint32 ReadBufferIdx = 0;
	DataSet->SetShaderParams<bDoResourceTransitions>(Shader, RHICmdList, WriteBufferIdx, ReadBufferIdx);
	

	// set the index buffer uav
	//
	if (Shader->OutputIndexBufferParam.IsBound())
	{
		RHICmdList.SetUAVParameter(Shader->GetComputeShader(), Shader->OutputIndexBufferParam.GetUAVIndex(), WriteIndexBuffer.UAV);
	}

	// set the execution parameters
	//
	if (Shader->EmitterTickCounterParam.IsBound())
	{
		RHICmdList.SetShaderParameter(Shader->GetComputeShader(), Shader->EmitterTickCounterParam.GetBufferIndex(), Shader->EmitterTickCounterParam.GetBaseIndex(), Shader->EmitterTickCounterParam.GetNumBytes(), &FNiagaraComputeExecutionContext::TickCounter);
	}

	uint32 Copy = bCopyBeforeStart ? 1 : 0;

	RHICmdList.SetShaderParameter(Shader->GetComputeShader(), Shader->UpdateStartInstanceParam.GetBufferIndex(), Shader->UpdateStartInstanceParam.GetBaseIndex(), Shader->UpdateStartInstanceParam.GetNumBytes(), &UpdateStartInstance);					// 0, except for event handler runs
	RHICmdList.SetShaderParameter(Shader->GetComputeShader(), Shader->NumIndicesPerInstanceParam.GetBufferIndex(), Shader->NumIndicesPerInstanceParam.GetBaseIndex(), Shader->NumIndicesPerInstanceParam.GetNumBytes(), &Context->NumIndicesPerInstance);		// set from the renderer in FNiagaraEmitterInstance::Tick
	int32 InstancesToSpawnThisFrame = Instance->SpawnRateInstances + Instance->EventSpawnTotal;
	RHICmdList.SetShaderParameter(Shader->GetComputeShader(), Shader->NumSpawnedInstancesParam.GetBufferIndex(), Shader->NumSpawnedInstancesParam.GetBaseIndex(), Shader->NumSpawnedInstancesParam.GetNumBytes(), &InstancesToSpawnThisFrame);				// number of instances in the spawn run

	uint32 NumThreadGroups = 1;
	if (TotalNumInstances > NIAGARA_COMPUTE_THREADGROUP_SIZE)
	{
		NumThreadGroups = FMath::Min(NIAGARA_MAX_COMPUTE_THREADGROUPS, FMath::DivideAndRoundUp(TotalNumInstances, NIAGARA_COMPUTE_THREADGROUP_SIZE));
	}

	// setup script parameters
	if (CBufferLayout.ConstantBufferSize)
	{
		check(CBufferLayout.Resources.Num() == 0);
		const uint8* ParamData = Instance->ParamData;
		FUniformBufferRHIRef CBuffer = RHICreateUniformBuffer(ParamData, CBufferLayout, EUniformBufferUsage::UniformBuffer_SingleDraw);
		RHICmdList.SetShaderUniformBuffer(Shader->GetComputeShader(), Shader->EmitterConstantBufferParam.GetBaseIndex(), CBuffer);
	}

	// Dispatch, if anything needs to be done
	//
	if (TotalNumInstances)
	{
		SCOPED_DRAW_EVENTF(RHICmdList, NiagaraGPUSimulationCS, TEXT("Niagara Gpu Sim - %s - NumInstances: %u"),
			*Context->DebugSimName,
			TotalNumInstances);
		DispatchComputeShader(RHICmdList, Shader, NumThreadGroups, 1, 1);
	}

#if WITH_EDITORONLY_DATA
	// Check to see if we need to queue up a debug dump..
	if (Context->DebugInfo.IsValid())
	{
		//UE_LOG(LogNiagara, Warning, TEXT("Queued up!"));

		if (!Context->GPUDebugDataReadbackFloat && !Context->GPUDebugDataReadbackInt && !Context->GPUDebugDataReadbackCounts && Context->MainDataSet != nullptr)
		{
			FRWBuffer &DatasetIndexBufferWrite = Context->MainDataSet->GetCurDataSetIndices();

			Context->GPUDebugDataCurrBufferIdx = Context->MainDataSet->GetCurrBufferIdx();
			Context->GPUDebugDataFloatSize = 0;
			Context->GPUDebugDataIntSize = 0;

			if (Context->MainDataSet->GetNumFloatComponents() > 0)
			{
				Context->GPUDebugDataReadbackFloat = new FRHIGPUMemoryReadback(TEXT("Niagara GPU Debug Info Float Emitter Readback"));
				Context->GPUDebugDataReadbackFloat->EnqueueCopy(RHICmdList, Context->MainDataSet->GetDataByIndex(WriteBufferIdx).GetGPUBufferFloat()->Buffer);
				Context->GPUDebugDataFloatSize = Context->MainDataSet->GetDataByIndex(WriteBufferIdx).GetGPUBufferFloat()->NumBytes;
			}

			if (Context->MainDataSet->GetNumInt32Components() > 0)
			{
				Context->GPUDebugDataReadbackInt = new FRHIGPUMemoryReadback(TEXT("Niagara GPU Debug Info Int Emitter Readback"));
				Context->GPUDebugDataReadbackInt->EnqueueCopy(RHICmdList, Context->MainDataSet->GetDataByIndex(WriteBufferIdx).GetGPUBufferInt()->Buffer);
				Context->GPUDebugDataIntSize = Context->MainDataSet->GetDataByIndex(WriteBufferIdx).GetGPUBufferInt()->NumBytes;
			}

			Context->GPUDebugDataReadbackCounts = new FRHIGPUMemoryReadback(TEXT("Niagara GPU Emitter Readback"));
			Context->GPUDebugDataReadbackCounts->EnqueueCopy(RHICmdList, DatasetIndexBufferWrite.Buffer);
		}
	}
#endif // WITH_EDITORONLY_DATA

	// Unset UAV parameters and transition resources (TODO: resource transition should be moved to the renderer)
	// 
	UnsetDataInterfaceParameters(DataInterfaceProxies, Shader, RHICmdList, Instance, Tick);
	DataSet->UnsetShaderParams(Shader, RHICmdList);
	Shader->OutputIndexBufferParam.UnsetUAV(RHICmdList, Shader->GetComputeShader());
}