// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraMetaDataViewModel.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraTypes.h"
#include "NiagaraNodeInput.h"
#include "NiagaraGraph.h"
#include "NiagaraScriptSource.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "Containers/Array.h"
#include "UObject/StructOnScope.h"

#define LOCTEXT_NAMESPACE "NiagaraMetaDataViewModel"

FNiagaraMetaDataViewModel::FNiagaraMetaDataViewModel(const FNiagaraVariable& InGraphVariable, UNiagaraGraph& InGraph)
	: GraphVariable(InGraphVariable)
	, CurrentGraph(&InGraph)
{
	RefreshMetaDataValue();
}

FNiagaraMetaDataViewModel::~FNiagaraMetaDataViewModel()
{
	CurrentGraph = nullptr;
}

FName FNiagaraMetaDataViewModel::GetName() const
{
	return GraphVariable.GetName();
}

const FText& FNiagaraMetaDataViewModel::GetCategoryName() const
{
	return GetMetaData().CategoryName;
}

int32 FNiagaraMetaDataViewModel::GetEditorSortPriority() const
{
	return GetMetaData().CallSortPriority;
}

void FNiagaraMetaDataViewModel::NotifyMetaDataChanged()
{
	FScopedTransaction ScopedTransaction(LOCTEXT("EditVariableMetadata", "Edit variable metadata"));
	CurrentGraph->Modify();
	
	FNiagaraVariableMetaData* ValueData = (FNiagaraVariableMetaData*)ValueStruct->GetStructMemory();
	CurrentGraph->SetMetaData(GraphVariable, *ValueData);
	
	OnMetadataChangedDelegate.Broadcast();
}

const FNiagaraVariableMetaData& FNiagaraMetaDataViewModel::GetMetaData() const
{
	return *(FNiagaraVariableMetaData*)ValueStruct->GetStructMemory();
}

FNiagaraVariable FNiagaraMetaDataViewModel::GetVariable() const
{
	return GraphVariable;
}

void FNiagaraMetaDataViewModel::RefreshMetaDataValue()
{
	ValueStruct = MakeShareable(new FStructOnScope(FNiagaraVariableMetaData::StaticStruct()));
	TOptional<FNiagaraVariableMetaData> GraphMetaData = CurrentGraph->GetMetaData(GraphVariable); 
	if(GraphMetaData.IsSet())
	{
		FNiagaraVariableMetaData* MetaData = (FNiagaraVariableMetaData*)ValueStruct->GetStructMemory();
		*MetaData = GraphMetaData.GetValue(); 
	}
}

TSharedRef<FStructOnScope> FNiagaraMetaDataViewModel::GetValueStruct()
{
	return ValueStruct.ToSharedRef();
}

#undef LOCTEXT_NAMESPACE // "NiagaraMetaDataViewModel"
