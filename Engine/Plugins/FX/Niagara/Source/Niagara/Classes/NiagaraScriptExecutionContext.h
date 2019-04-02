// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*==============================================================================
NiagaraEmitterInstance.h: Niagara emitter simulation class
==============================================================================*/
#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "NiagaraCommon.h"
#include "NiagaraDataSet.h"
#include "NiagaraEvents.h"
#include "NiagaraCollision.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraEmitter.h"
#include "NiagaraScriptExecutionParameterStore.h"
#include "NiagaraTypes.h"
#include "RHIGPUReadback.h"

struct FNiagaraDataInterfaceProxy;

struct FNiagaraDataSetExecutionInfo
{
	FNiagaraDataSetExecutionInfo()
		: DataSet(nullptr)
		, StartInstance(0)
		, bAllocate(false)
		, bUpdateInstanceCount(false)
	{
	}

	FNiagaraDataSetExecutionInfo(FNiagaraDataSet* InDataSet, int32 InStartInstance, bool bInAllocate, bool bInUpdateInstanceCount)
		: DataSet(InDataSet)
		, StartInstance(InStartInstance)
		, bAllocate(bInAllocate)
		, bUpdateInstanceCount(bInUpdateInstanceCount)
	{}

	FNiagaraDataSet* DataSet;
	int32 StartInstance;
	bool bAllocate;
	bool bUpdateInstanceCount;
};

struct FNiagaraScriptExecutionContext
{
	UNiagaraScript* Script;

	/** Table of external function delegates called from the VM. */
	TArray<FVMExternalFunction> FunctionTable;

	/** Table of instance data for data interfaces that require it. */
	TArray<void*> DataInterfaceInstDataTable;

	/** Parameter store. Contains all data interfaces and a parameter buffer that can be used directly by the VM or GPU. */
	FNiagaraScriptExecutionParameterStore Parameters;

	TArray<FDataSetMeta> DataSetMetaTable;

	static uint32 TickCounter;

	FNiagaraScriptExecutionContext();
	~FNiagaraScriptExecutionContext();

	bool Init(UNiagaraScript* InScript, ENiagaraSimTarget InTarget);
	

	bool Tick(class FNiagaraSystemInstance* Instance, ENiagaraSimTarget SimTarget = ENiagaraSimTarget::CPUSim);
	void PostTick();

	bool Execute(uint32 NumInstances, TArray<FNiagaraDataSetExecutionInfo, TInlineAllocator<8>>& DataSetInfos);

	const TArray<UNiagaraDataInterface*>& GetDataInterfaces()const { return Parameters.GetDataInterfaces(); }

	void DirtyDataInterfaces();

	bool CanExecute()const;
};

struct FNiagaraComputeExecutionContext
{
	FNiagaraComputeExecutionContext()
		: MainDataSet(nullptr)
		, CBufferLayout(TEXT("Niagara Compute Sim CBuffer"))
		, GPUDataReadback(nullptr)
		, AccumulatedSpawnRate(0)
		, NumIndicesPerInstance(0)
#if WITH_EDITORONLY_DATA
		, GPUDebugDataReadbackFloat(nullptr)
		, GPUDebugDataReadbackInt(nullptr)
		, GPUDebugDataReadbackCounts(nullptr)
		, GPUDebugDataCurrBufferIdx(0xFFFFFFFF)
		, GPUDebugDataFloatSize(0)
		, GPUDebugDataIntSize(0)
#endif	  
	{
	}

	~FNiagaraComputeExecutionContext()
	{
		checkf(IsInRenderingThread(), TEXT("Can only delete the gpu readback from the render thread"));
		if (GPUDataReadback)
		{
			delete GPUDataReadback;
			GPUDataReadback = nullptr;
		}

#if WITH_EDITORONLY_DATA
		if (GPUDebugDataReadbackFloat)
		{
			delete GPUDebugDataReadbackFloat;
			GPUDebugDataReadbackFloat = nullptr;
		}
		if (GPUDebugDataReadbackInt)
		{
			delete GPUDebugDataReadbackInt;
			GPUDebugDataReadbackInt = nullptr;
		}
		if (GPUDebugDataReadbackCounts)
		{
			delete GPUDebugDataReadbackCounts;
			GPUDebugDataReadbackCounts = nullptr;
		}
#endif
	}

	void Reset()
	{
		FNiagaraComputeExecutionContext* Context = this;
		ENQUEUE_RENDER_COMMAND(ResetRT)(
			[Context](FRHICommandListImmediate& RHICmdList)
			{
				Context->ResetInternal();
			}
		);
	}

	void InitParams(UNiagaraScript* InGPUComputeScript, ENiagaraSimTarget InSimTarget, const FString& InDebugSimName)
	{
		DebugSimName = InDebugSimName;
		GPUScript = InGPUComputeScript;
		CombinedParamStore.InitFromOwningContext(InGPUComputeScript, InSimTarget, true);

#if DO_CHECK
		FNiagaraShader *Shader = InGPUComputeScript->GetRenderThreadScript()->GetShaderGameThread();
		DIParamInfo.Empty();
		if (Shader)
		{
			for (FNiagaraDataInterfaceParamRef& DIParams : Shader->GetDIParameters())
			{
				DIParamInfo.Add(DIParams.ParameterInfo);
			}
		}
		else
		{
			DIParamInfo = InGPUComputeScript->GetRenderThreadScript()->GetDataInterfaceParamInfo();
		}
#endif
	}

	void DirtyDataInterfaces()
	{
		CombinedParamStore.MarkInterfacesDirty();
	}

	bool Tick(FNiagaraSystemInstance* ParentSystemInstance)
	{
		if (CombinedParamStore.GetInterfacesDirty())
		{
#if DO_CHECK
			const TArray<UNiagaraDataInterface*> &DataInterfaces = CombinedParamStore.GetDataInterfaces();
			// We must make sure that the data interfaces match up between the original script values and our overrides...
			if (DIParamInfo.Num() != DataInterfaces.Num())
			{
				UE_LOG(LogNiagara, Warning, TEXT("Mismatch between Niagara GPU Execution Context data interfaces and those in its script!"));
				return false;
			}

			for (int32 i=0; i<DIParamInfo.Num(); ++i)
			{
				FString UsedClassName = DataInterfaces[i]->GetClass()->GetName();
				if (DIParamInfo[i].DIClassName != UsedClassName)
				{
					UE_LOG(LogNiagara, Warning, TEXT("Mismatched class between Niagara GPU Execution Context data interfaces and those in its script!\nIndex:%d\nShader:%s\nScript:%s")
						, i, *DIParamInfo[i].DIClassName, *UsedClassName);
				}
			}
#endif

			CombinedParamStore.Tick();
		}

		return true;
	}

	void PostTick()
	{
		//If we're for interpolated spawn, copy over the previous frame's parameters into the Prev parameters.
		if (GPUScript && GPUScript->GetComputedVMCompilationId().HasInterpolatedParameters())
		{
			CombinedParamStore.CopyCurrToPrev();
		}
	}

private:
	void ResetInternal()
	{
		checkf(IsInRenderingThread(), TEXT("Can only reset the gpu context from the render thread"));
		AccumulatedSpawnRate = 0;
		if (GPUDataReadback)
		{
			delete GPUDataReadback;
			GPUDataReadback = nullptr;
		}

#if WITH_EDITORONLY_DATA
		if (GPUDebugDataReadbackFloat)
		{
			delete GPUDebugDataReadbackFloat;
			GPUDebugDataReadbackFloat = nullptr;
		}
		if (GPUDebugDataReadbackInt)
		{
			delete GPUDebugDataReadbackInt;
			GPUDebugDataReadbackInt = nullptr;
		}
		if (GPUDebugDataReadbackCounts)
		{
			delete GPUDebugDataReadbackCounts;
			GPUDebugDataReadbackCounts = nullptr;
		}
#endif
	}

public:
	FString DebugSimName;
	class FNiagaraDataSet *MainDataSet;
	UNiagaraScript* GPUScript;
	class FNiagaraShaderScript*  GPUScript_RT;
	FRHIUniformBufferLayout CBufferLayout; // Persistent layouts used to create Compute Sim CBuffer
	//TArray<uint8, TAlignedHeapAllocator<16>> ParamData_RT;		// RT side copy of the parameter data
	FNiagaraScriptExecutionParameterStore CombinedParamStore;
	static uint32 TickCounter;
#if DO_CHECK
	TArray< FNiagaraDataInterfaceGPUParamInfo >  DIParamInfo;
#endif

	TArray<FNiagaraDataInterfaceProxy*> DataInterfaceProxies;

	FRHIGPUMemoryReadback *GPUDataReadback;
	uint32 AccumulatedSpawnRate;
	uint32 NumIndicesPerInstance;	// how many vtx indices per instance the renderer is going to have for its draw call

	uint32 EventSpawnTotal_GT;
	uint32 SpawnRateInstances_GT;

#if WITH_EDITORONLY_DATA
	mutable FRHIGPUMemoryReadback *GPUDebugDataReadbackFloat;
	mutable FRHIGPUMemoryReadback *GPUDebugDataReadbackInt;
	mutable FRHIGPUMemoryReadback *GPUDebugDataReadbackCounts;
	mutable int32 GPUDebugDataCurrBufferIdx;
	mutable uint32 GPUDebugDataFloatSize;
	mutable uint32 GPUDebugDataIntSize;
	mutable TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe> DebugInfo;
#endif
	//void PostTick() 
	//{
	//	//If we're for interpolated spawn, copy over the previous frame's parameters into the Prev parameters.
	//	if (GPUScript_RT && GPUScript_RT->GetComputedVMCompilationId().HasInterpolatedParameters())
	//	{
	//		CombinedParamStore.CopyCurrToPrev();
	//	}
	//}
};

struct FNiagaraDataInterfaceInstanceData
{
	void* PerInstanceDataForRT;
	TMap<FNiagaraDataInterfaceProxy*, int32> InterfaceProxiesToOffsets;
	uint32 PerInstanceDataSize;
	uint32 Instances;

	~FNiagaraDataInterfaceInstanceData()
	{}
};

struct FNiagaraComputeInstanceData
{
	uint32 EventSpawnTotal;
	uint32 SpawnRateInstances;
	uint8* ParamData;
	FNiagaraComputeExecutionContext* Context;
	TArray<FNiagaraDataInterfaceProxy*> DataInterfaceProxies;

	FNiagaraComputeInstanceData()
		: EventSpawnTotal(0)
		, SpawnRateInstances(0)
		, ParamData(nullptr)
		, Context(nullptr)
	{}
};

/*
	Represents all the information needed to dispatch a single tick of a FNiagaraSystemInstance.
	This object will be created on the game thread and passed to the renderthread.

	It contains the PerInstance data buffer for every DataInterface referenced by the system as well
	as the Data required to dispatch updates for each Emitter in the system.

	DataInterface data is packed tightly. It includes a TMap that associates the data interface with
	the offset into the packed buffer. At that offset is the Per-Instance data for this System.

	InstanceData_ParamData_Packed packs FNiagaraComputeInstanceData and ParamData into one buffer.
	There is padding after the array of FNiagaraComputeInstanceData so we can upload ParamData directly into a UniformBuffer
	(it is 16 byte aligned).

*/
class FNiagaraGPUSystemTick
{
public:
	FNiagaraGPUSystemTick()
		: Count(0)
		, DIInstanceData(nullptr)
		, InstanceData_ParamData_Packed(nullptr)
	{}

	void Init(FNiagaraSystemInstance* InSystemInstance);

	void Destroy()
	{
		FNiagaraComputeInstanceData* Instances = GetInstanceData();
		for (uint32 i = 0; i < Count; i++)
		{
			FNiagaraComputeInstanceData& Instance = Instances[i];
			Instance.~FNiagaraComputeInstanceData();
		}

		FMemory::Free(InstanceData_ParamData_Packed);
		if (DIInstanceData)
		{
			FMemory::Free(DIInstanceData->PerInstanceDataForRT);
			delete DIInstanceData;
		}
	}

	bool IsValid()
	{
		return InstanceData_ParamData_Packed != nullptr;
	}

	FNiagaraComputeInstanceData* GetInstanceData()
	{
		return reinterpret_cast<FNiagaraComputeInstanceData*>(InstanceData_ParamData_Packed);
	}

	uint32 Count;
	FGuid SystemInstanceID;
	FNiagaraDataInterfaceInstanceData* DIInstanceData;
	uint8* InstanceData_ParamData_Packed;
	bool bRequiredDistanceFieldData = false;
};
