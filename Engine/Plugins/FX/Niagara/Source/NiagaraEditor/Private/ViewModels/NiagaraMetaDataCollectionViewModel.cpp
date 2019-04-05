// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraMetaDataCollectionViewModel.h"
#include "NiagaraTypes.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSourceBase.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraConstants.h"
#include "NiagaraMetaDataViewModel.h"
#include "GraphEditAction.h"
#include "EdGraphSchema_Niagara.h"

#define LOCTEXT_NAMESPACE "NiagaraMetaDataCollectionViewModel"

FNiagaraMetaDataCollectionViewModel::FNiagaraMetaDataCollectionViewModel()
	: ModuleGraph(nullptr)
	, bNeedsRefresh(false)
	, bInternalGraphChange(false)
{
}

void FNiagaraMetaDataCollectionViewModel::Tick(float DeltaTime)
{
	if (bNeedsRefresh)
	{
		Refresh();
		bNeedsRefresh = false;
	}
}

bool FNiagaraMetaDataCollectionViewModel::IsTickable() const
{
	return true;
}

TStatId FNiagaraMetaDataCollectionViewModel::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FNiagaraMetaDataCollectionViewModel, STATGROUP_Tickables);
}

void FNiagaraMetaDataCollectionViewModel::SetGraph(UNiagaraGraph* InGraph)
{
	if (ModuleGraph != nullptr)
	{
		Cleanup();
	}

	if (InGraph == nullptr)
	{
		return;
	}
	// now build variables
	ModuleGraph = InGraph;
	
	Refresh();

	OnGraphChangedHandle = ModuleGraph->AddOnGraphChangedHandler(FOnGraphChanged::FDelegate::CreateRaw(this, &FNiagaraMetaDataCollectionViewModel::OnGraphChanged));
	OnRecompileHandle = ModuleGraph->AddOnGraphNeedsRecompileHandler(FOnGraphChanged::FDelegate::CreateRaw(this, &FNiagaraMetaDataCollectionViewModel::OnGraphChanged));
}

TArray<TSharedRef<FNiagaraMetaDataViewModel>>& FNiagaraMetaDataCollectionViewModel::GetVariableModels()
{
	return MetaDataViewModels;
}

void FNiagaraMetaDataCollectionViewModel::RequestRefresh()
{
	bNeedsRefresh = true;
}

void FNiagaraMetaDataCollectionViewModel::OnGraphChanged(const struct FEdGraphEditAction& InAction)
{
	if (!bInternalGraphChange)
	{
		RequestRefresh();
	}
} 

void FNiagaraMetaDataCollectionViewModel::ChildMetadataChanged()
{
	TGuardValue<bool> UpdateGuard(bInternalGraphChange, true);
	OnCollectionChangedDelegate.Broadcast();
	ModuleGraph->NotifyGraphNeedsRecompile();
}

void FNiagaraMetaDataCollectionViewModel::Refresh()
{
	if (!ModuleGraph)
	{
		CleanupMetadata();
		return;
	}

	TArray<TSharedRef<FNiagaraMetaDataViewModel>> OldMetaDataViewModels = MetaDataViewModels;
	MetaDataViewModels.Empty();

	// We only store metadata information if it's actually set but we want to display metadata for all module parameters so
	// we iterate over that collection here.
	const TMap<FNiagaraVariable, FNiagaraGraphParameterReferenceCollection>& ParameterReferenceMap = ModuleGraph->GetParameterReferenceMap();
	for (const auto& ParameterToReferences : ParameterReferenceMap)
	{
		const FNiagaraVariable& ParameterVariable = ParameterToReferences.Key;
		FNiagaraParameterHandle VariableHandle(ParameterVariable.GetName());
		if (VariableHandle.IsModuleHandle())
		{
			TSharedRef<FNiagaraMetaDataViewModel>* OldMetadataViewModelPtr = OldMetaDataViewModels.FindByPredicate(
				[&ParameterVariable](const TSharedRef<FNiagaraMetaDataViewModel>& OldMetaDataViewModel) { return OldMetaDataViewModel->GetVariable() == ParameterVariable; });

			if (OldMetadataViewModelPtr != nullptr)
			{
				TSharedRef<FNiagaraMetaDataViewModel> OldMetaDataViewModel = *OldMetadataViewModelPtr;
				OldMetaDataViewModel->RefreshMetaDataValue();
				MetaDataViewModels.Add(OldMetaDataViewModel);
				OldMetaDataViewModels.Remove(OldMetaDataViewModel);
			}
			else
			{
				TSharedRef<FNiagaraMetaDataViewModel> MetadataViewModel = MakeShared<FNiagaraMetaDataViewModel>(ParameterVariable, *ModuleGraph);
				MetadataViewModel->OnMetadataChanged().AddRaw(this, &FNiagaraMetaDataCollectionViewModel::ChildMetadataChanged);
				MetaDataViewModels.Add(MetadataViewModel);
			}
		}
	}

	// Remove delegates for any metadata view models which weren't reused.
	for (TSharedRef<FNiagaraMetaDataViewModel>& OldMetaDataViewModel : OldMetaDataViewModels)
	{
		OldMetaDataViewModel->OnMetadataChanged().RemoveAll(this);
	}

	SortViewModels();
	OnCollectionChangedDelegate.Broadcast();
}

void FNiagaraMetaDataCollectionViewModel::SortViewModels()
{
	TMap<FString, int32> CategoryPriorityMap;
	for (auto Metadata : MetaDataViewModels)
	{
		Metadata->RefreshMetaDataValue();
		FString CategoryName = Metadata->GetCategoryName().ToString();
		if (!CategoryName.IsEmpty() && 
			(!CategoryPriorityMap.Contains(CategoryName) || (CategoryPriorityMap.Contains(CategoryName) && CategoryPriorityMap[CategoryName] > Metadata->GetEditorSortPriority())))
		{
			CategoryPriorityMap.Add(CategoryName, Metadata->GetEditorSortPriority());
		}
	}
	auto SortVars = [&](const TSharedRef<FNiagaraMetaDataViewModel>& A, const TSharedRef<FNiagaraMetaDataViewModel>& B)
	{
		int32 CategoryPriorityA = CategoryPriorityMap.Contains(A->GetCategoryName().ToString()) ? CategoryPriorityMap[A->GetCategoryName().ToString()] : MIN_int32;
		int32 CategoryPriorityB = CategoryPriorityMap.Contains(B->GetCategoryName().ToString()) ? CategoryPriorityMap[B->GetCategoryName().ToString()] : MIN_int32;
		if (CategoryPriorityA < CategoryPriorityB)
		{
			return true;
		}
		else if (CategoryPriorityA > CategoryPriorityB)
		{
			return false;
		}
		if (A->GetEditorSortPriority() < B->GetEditorSortPriority())
		{
			return true;
		}
		else if (A->GetEditorSortPriority() > B->GetEditorSortPriority())
		{
			return false;
		}
		//If equal priority, sort alphabetically.
		return A->GetName().ToString() < B->GetName().ToString();
	};
	MetaDataViewModels.Sort(SortVars);
}

void FNiagaraMetaDataCollectionViewModel::Cleanup()
{
	CleanupMetadata();
	ModuleGraph->RemoveOnGraphChangedHandler(OnGraphChangedHandle);
	ModuleGraph->RemoveOnGraphNeedsRecompileHandler(OnRecompileHandle);
}

void FNiagaraMetaDataCollectionViewModel::CleanupMetadata()
{
	for (int32 i = 0; i < MetaDataViewModels.Num(); i++)
	{
		MetaDataViewModels[i]->OnMetadataChanged().RemoveAll(this);
	}
	MetaDataViewModels.Empty();
}

FNiagaraMetaDataCollectionViewModel::~FNiagaraMetaDataCollectionViewModel()
{
	Cleanup();
}

#undef LOCTEXT_NAMESPACE // NiagaraMetaDataCollectionViewModel
