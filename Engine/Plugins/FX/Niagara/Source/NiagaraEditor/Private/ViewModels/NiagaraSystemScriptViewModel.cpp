// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraSystemScriptViewModel.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraTypes.h"
#include "NiagaraScriptSource.h"
#include "NiagaraScriptViewModel.h"
#include "NiagaraScriptGraphViewModel.h"
#include "NiagaraScriptInputCollectionViewModel.h"
#include "NiagaraNodeEmitter.h"
#include "NiagaraNodeInput.h"
#include "NiagaraDataInterface.h"
#include "NiagaraScriptSourceBase.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraNodeOutput.h"
#include "GraphEditAction.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/NiagaraEmitterViewModel.h"

FNiagaraSystemScriptViewModel::FNiagaraSystemScriptViewModel(UNiagaraSystem& InSystem, FNiagaraSystemViewModel* InParent)
	: FNiagaraScriptViewModel(InSystem.GetSystemSpawnScript(), NSLOCTEXT("SystemScriptViewModel", "GraphName", "System"), ENiagaraParameterEditMode::EditAll)
	, Parent(InParent)
	, System(InSystem)
{
	Scripts.Add(InSystem.GetSystemUpdateScript());

	if (GetGraphViewModel()->GetGraph())
	{
		OnGraphChangedHandle = GetGraphViewModel()->GetGraph()->AddOnGraphChangedHandler(
			FOnGraphChanged::FDelegate::CreateRaw(this, &FNiagaraSystemScriptViewModel::OnGraphChanged));

		OnRecompileHandle = GetGraphViewModel()->GetGraph()->AddOnGraphNeedsRecompileHandler(
			FOnGraphChanged::FDelegate::CreateRaw(this, &FNiagaraSystemScriptViewModel::OnGraphChanged));
		
		GetGraphViewModel()->SetErrorTextToolTip("");
	}

	System.OnSystemCompiled().AddRaw(this, &FNiagaraSystemScriptViewModel::OnSystemVMCompiled);
}

FNiagaraSystemScriptViewModel::~FNiagaraSystemScriptViewModel()
{
	System.OnSystemCompiled().RemoveAll(this);
	if (GetGraphViewModel()->GetGraph())
	{
		GetGraphViewModel()->GetGraph()->RemoveOnGraphChangedHandler(OnGraphChangedHandle);
		GetGraphViewModel()->GetGraph()->RemoveOnGraphNeedsRecompileHandler(OnRecompileHandle);
		
	}
}

void FNiagaraSystemScriptViewModel::OnSystemVMCompiled(UNiagaraSystem* InSystem)
{
	if (InSystem != &System)
	{
		return;
	}

	TArray<ENiagaraScriptCompileStatus> InCompileStatuses;
	TArray<FString> InCompileErrors;
	TArray<FString> InCompilePaths;
	TArray<TPair<ENiagaraScriptUsage, int32> > InUsages;

	ENiagaraScriptCompileStatus AggregateStatus = ENiagaraScriptCompileStatus::NCS_UpToDate;
	FString AggregateErrors;

	TArray<UNiagaraScript*> SystemScripts;
	SystemScripts.Add(InSystem->GetSystemSpawnScript());
	SystemScripts.Add(InSystem->GetSystemUpdateScript());
	for (const FNiagaraEmitterHandle& Handle : InSystem->GetEmitterHandles())
	{
		Handle.GetInstance()->GetScripts(SystemScripts, true);
	}

	int32 EventsFound = 0;
	for (int32 i = 0; i < SystemScripts.Num(); i++)
	{
		UNiagaraScript* Script = SystemScripts[i];
		if (Script != nullptr && Script->GetVMExecutableData().IsValid())
		{
			InCompileStatuses.Add(Script->GetVMExecutableData().LastCompileStatus);
			InCompileErrors.Add(Script->GetVMExecutableData().ErrorMsg);
			InCompilePaths.Add(Script->GetPathName());

			if (Script->GetUsage() == ENiagaraScriptUsage::ParticleEventScript)
			{
				InUsages.Add(TPair<ENiagaraScriptUsage, int32>(Script->GetUsage(), EventsFound));
				EventsFound++;
			}
			else
			{
				InUsages.Add(TPair<ENiagaraScriptUsage, int32>(Script->GetUsage(), 0));
			}
		}
		else
		{
			InCompileStatuses.Add(ENiagaraScriptCompileStatus::NCS_Unknown);
			InCompileErrors.Add(TEXT("Invalid script pointer!"));
			InCompilePaths.Add(TEXT("Unknown..."));
			InUsages.Add(TPair<ENiagaraScriptUsage, int32>(ENiagaraScriptUsage::Function, 0));
		}
	}

	for (int32 i = 0; i < InCompileStatuses.Num(); i++)
	{
		AggregateStatus = FNiagaraEditorUtilities::UnionCompileStatus(AggregateStatus, InCompileStatuses[i]);
		AggregateErrors += InCompilePaths[i] + TEXT(" ") + FNiagaraEditorUtilities::StatusToText(InCompileStatuses[i]).ToString() + TEXT("\n");
		AggregateErrors += InCompileErrors[i] + TEXT("\n");
	}

	UpdateCompileStatus(AggregateStatus, AggregateErrors, InCompileStatuses, InCompileErrors, InCompilePaths, SystemScripts);

	LastCompileStatus = AggregateStatus;

	if (OnSystemCompiledDelegate.IsBound())
	{
		OnSystemCompiledDelegate.Broadcast();
	}
}

FNiagaraSystemScriptViewModel::FOnSystemCompiled& FNiagaraSystemScriptViewModel::OnSystemCompiled()
{
	return OnSystemCompiledDelegate;
}

void FNiagaraSystemScriptViewModel::CompileSystem(bool bForce)
{
	System.RequestCompile(bForce);
}

void FNiagaraSystemScriptViewModel::OnGraphChanged(const struct FEdGraphEditAction& InAction)
{
	if (InAction.Action == GRAPHACTION_SelectNode)
	{
		return;
	}
}
