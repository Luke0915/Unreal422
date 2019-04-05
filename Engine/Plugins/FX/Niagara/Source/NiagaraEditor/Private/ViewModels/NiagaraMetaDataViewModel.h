// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraTypes.h"

class UNiagaraNodeInput;
class UNiagaraGraph;
class UEdGraphNode;
class FStructOnScope;
struct FNiagaraVariable;
struct FNiagaraVariableMetaData;

/** A view model for a variable metadata. */
class FNiagaraMetaDataViewModel
{
public:
	DECLARE_MULTICAST_DELEGATE(FOnMetadataChanged);
public:
	/*
	 * Create a new variable metadata view model.
	 * @param InGraphVariable The variable which is owned by the graph which connects to this metadata.
	 * @param InGraph The graph where the metadata lives.
	 */
	FNiagaraMetaDataViewModel(const FNiagaraVariable& InGraphVariable, UNiagaraGraph& InGraph);
	
	virtual ~FNiagaraMetaDataViewModel();

	FName GetName() const;

	const FText& GetCategoryName() const;

	int32 GetEditorSortPriority() const;

	FNiagaraVariable GetVariable() const;
	
	/** Callback for when a change in the value structure needs to be copied into the graph*/
	void NotifyMetaDataChanged();

	/** Gets the FStructOnScope object that holds the editable value of the metadata*/
	TSharedRef<FStructOnScope> GetValueStruct();

	/** Gets a multicast delegate which is called whenever the metadata changes. */
	FOnMetadataChanged& OnMetadataChanged() { return OnMetadataChangedDelegate; }
	
	/** Refreshes the parameter value struct from the variable data. */
	void RefreshMetaDataValue();

private:
	const FNiagaraVariableMetaData& GetMetaData() const;

private:
	/** The graph variable which corresponds to the metadata in our viewmodel */
	FNiagaraVariable GraphVariable;
	
	/** graph who owns the metadata*/
	UNiagaraGraph* CurrentGraph;
	
	/** actual value being edited*/
	TSharedPtr<FStructOnScope> ValueStruct;
	
	/** A multicast delegate which is called whenever the metadata is changed. */
	FOnMetadataChanged OnMetadataChangedDelegate;
};