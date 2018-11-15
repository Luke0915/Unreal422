// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "NiagaraNodeOp.h"
#include "NiagaraHlslTranslator.h"
#include "GraphEditorSettings.h"

#include "EdGraphSchema_Niagara.h"

#define LOCTEXT_NAMESPACE "NiagaraNodeOp"

UNiagaraNodeOp::UNiagaraNodeOp(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UNiagaraNodeOp::AllocateDefaultPins()
{
	const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();

	const FNiagaraOpInfo* OpInfo = FNiagaraOpInfo::GetOpInfo(OpName);
	check(OpInfo);

	// Create input pins from the op
	for (int32 SrcIndex = 0; SrcIndex < OpInfo->Inputs.Num(); ++SrcIndex)
	{
		const FNiagaraOpInOutInfo& InOutInfo = OpInfo->Inputs[SrcIndex];
		UEdGraphPin* Pin = CreatePin(EGPD_Input, Schema->TypeDefinitionToPinType(InOutInfo.DataType), *InOutInfo.FriendlyName.ToString());
		check(Pin);
		Pin->bDefaultValueIsIgnored = false;
		Pin->bDefaultValueIsReadOnly = false;
		Pin->bNotConnectable = false;
		Pin->DefaultValue = InOutInfo.Default;
		Pin->AutogeneratedDefaultValue = InOutInfo.Default;
		Pin->PinToolTip = InOutInfo.Description.ToString();
	}

	// Restore pins added by the user
	for (int32 SrcIndex = 0; SrcIndex < AddedPins.Num(); ++SrcIndex)
	{
		FAddedPinData& OldPinData = AddedPins[SrcIndex];
		UEdGraphPin* Pin = CreatePin(EGPD_Input, OldPinData.PinType, OldPinData.PinName);
		check(Pin);
		Pin->bDefaultValueIsIgnored = false;
		Pin->bDefaultValueIsReadOnly = false;
		Pin->bNotConnectable = false;
	}
	
	// Create output pins from the op
	for (int32 OutIdx = 0; OutIdx < OpInfo->Outputs.Num(); ++OutIdx)
	{
		const FNiagaraOpInOutInfo& InOutInfo = OpInfo->Outputs[OutIdx];
		UEdGraphPin* Pin = CreatePin(EGPD_Output, Schema->TypeDefinitionToPinType(InOutInfo.DataType), *InOutInfo.FriendlyName.ToString());
		check(Pin);
		Pin->PinToolTip = InOutInfo.Description.ToString();
	}

	if (OpInfo->bSupportsAddedInputs)
	{
		CreateAddPin(EGPD_Input);
	}
}

FText UNiagaraNodeOp::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	const FNiagaraOpInfo* OpInfo = FNiagaraOpInfo::GetOpInfo(OpName);
	if (!OpInfo)
	{
		FString Strn = "Unknown";
		return FText::FromString(Strn);
	}
	return OpInfo->FriendlyName;
}

FText UNiagaraNodeOp::GetTooltipText()const
{
	const FNiagaraOpInfo* OpInfo = FNiagaraOpInfo::GetOpInfo(OpName);
	if (!OpInfo)
	{
		FString Strn = "Unknown";
		return FText::FromString(Strn);
	}
	return OpInfo->Description;
}

FLinearColor UNiagaraNodeOp::GetNodeTitleColor() const
{
	return GetDefault<UGraphEditorSettings>()->FunctionCallNodeTitleColor;
}

void UNiagaraNodeOp::Compile(class FHlslNiagaraTranslator* Translator, TArray<int32>& Outputs)
{
	const FNiagaraOpInfo* OpInfo = FNiagaraOpInfo::GetOpInfo(OpName);
	if (!OpInfo)
	{
		return;
	}

	int32 NumInputs = OpInfo->bSupportsAddedInputs ? Pins.Num() : OpInfo->Inputs.Num();
	int32 NumOutputs = OpInfo->Outputs.Num();

	TArray<int32> Inputs;
	bool bError = false;
	for (int32 i = 0; i < NumInputs; ++i)
	{
		UEdGraphPin *Pin = Pins[i];
		if (Pin->Direction != EGPD_Input || IsAddPin(Pin))
		{
			continue;
		}
		int32 CompiledInput = Translator->CompilePin(Pin);
		if (CompiledInput == INDEX_NONE)
		{
			bError = true;
			FFormatNamedArguments Args;
			Args.Add(TEXT("OpName"), GetNodeTitle(ENodeTitleType::FullTitle));
			FText Format = LOCTEXT("InputErrorFormat", "Error compiling input on {OpName} node.");
			Translator->Error(FText::Format(Format, Args), this, Pin);
		}
		Inputs.Add(CompiledInput);
	}
	
	Translator->Operation(this, Inputs, Outputs);
}

void UNiagaraNodeOp::PostLoad()
{
	Super::PostLoad();

	FName OriginalOpName = OpName;

	if (OpName == TEXT("Numeric::Cos"))
	{
		OpName = TEXT("Numeric::Cosine(Radians)");
	}
	else if (OpName == TEXT("Numeric::Sin"))
	{
		OpName = TEXT("Numeric::Sine(Radians)");
	}
	else if (OpName == TEXT("Numeric::Tan"))
	{
		OpName = TEXT("Numeric::Tangent(Radians)");
	}
	else if (OpName == TEXT("Numeric::ASin"))
	{
		OpName = TEXT("Numeric::ArcSine(Radians)");
	}
	else if (OpName == TEXT("Numeric::ACos"))
	{
		OpName = TEXT("Numeric::ArcCosine(Radians)");
	}
	else if (OpName == TEXT("Numeric::ATan"))
	{
		OpName = TEXT("Numeric::ArcTangent(Radians)");
	}
	else if (OpName == TEXT("Numeric::ATan2"))
	{
		OpName = TEXT("Numeric::ArcTangent2(Radians)");
	}

	if (OpName != OriginalOpName)
	{
		UE_LOG(LogNiagaraEditor, Log, TEXT("OpNode: Converted %s to %s, Package: %s"), *OriginalOpName.ToString(), *OpName.ToString(), *GetOutermost()->GetName());
	}

}

bool UNiagaraNodeOp::RefreshFromExternalChanges()
{
	// TODO - Leverage code in reallocate pins to determine if any pins have changed...
	ReallocatePins();
	return true;
}

ENiagaraNumericOutputTypeSelectionMode UNiagaraNodeOp::GetNumericOutputTypeSelectionMode() const
{
	const FNiagaraOpInfo* OpInfo = FNiagaraOpInfo::GetOpInfo(OpName);
	check(OpInfo);
	return OpInfo->NumericOuputTypeSelectionMode;
}

bool UNiagaraNodeOp::AllowNiagaraTypeForAddPin(const FNiagaraTypeDefinition& InType)
{
	const FNiagaraOpInfo* OpInfo = FNiagaraOpInfo::GetOpInfo(OpName);
	if (!OpInfo)
	{
		return false;
	}
	if (OpInfo->AddedInputTypeRestrictions.Num() == 0)
	{
		return true;
	}
	return OpInfo->AddedInputTypeRestrictions.Contains(InType);
}

void UNiagaraNodeOp::OnPinRemoved(UEdGraphPin* PinToRemove)
{
	auto FindPredicate = [=](const FAddedPinData& PinData)
	{
		return PinData.PinName == PinToRemove->PinName && PinData.PinType == PinToRemove->PinType; 
	};
	AddedPins.RemoveAll(FindPredicate);
	ReallocatePins();
}

bool UNiagaraNodeOp::AllowDynamicPins() const
{
	const FNiagaraOpInfo* OpInfo = FNiagaraOpInfo::GetOpInfo(OpName);
	return OpInfo && OpInfo->bSupportsAddedInputs;
}

void UNiagaraNodeOp::OnNewTypedPinAdded(UEdGraphPin* NewPin)
{
	FName UniqueName = GetUniqueAdditionalPinName();

	FAddedPinData PinData;
	PinData.PinType = NewPin->PinType;
	PinData.PinName = UniqueName;
	NewPin->PinName = UniqueName;
	AddedPins.Add(PinData);
}

void UNiagaraNodeOp::OnPinRenamed(UEdGraphPin* RenamedPin, const FString& OldName)
{
	TSet<FName> ExistingNames;
	for (const FAddedPinData& PinData : AddedPins)
	{
		ExistingNames.Add(PinData.PinName);
	}
	FName UniqueName = FNiagaraUtilities::GetUniqueName(*RenamedPin->PinName.ToString(), ExistingNames);
	FName OldPinName(*OldName);

	for (FAddedPinData& PinData : AddedPins)
	{
		if (PinData.PinName == OldPinName && PinData.PinType == RenamedPin->PinType)
		{
			PinData.PinName = UniqueName;
			RenamedPin->PinName = UniqueName;
			break;
		}
	}
}

FName UNiagaraNodeOp::GetUniqueAdditionalPinName() const
{
	// count existing input pins
	FString Name;
	TSet<FName> ExistingNames;
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin->Direction == EEdGraphPinDirection::EGPD_Input && !IsAddPin(Pin))
		{
			ExistingNames.Add(Pin->PinName);
		}
	}
	
	// create a new name based on the existing pins (A, B, ... AA, AB, ...)
	static FString Alphabet = TEXT("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
	for (int32 Remaining = ExistingNames.Num(); Remaining > 0; Remaining = Remaining / 26)
	{
		int32 CharIndex = Remaining < 26 ? Remaining - 1 : Remaining % 26;
		Name = FString() + (*Alphabet)[CharIndex] + Name;
	}

	return FNiagaraUtilities::GetUniqueName(*Name, ExistingNames);
}

#undef LOCTEXT_NAMESPACE
