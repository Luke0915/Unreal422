// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "CurveEditorTypes.h"

template<>
struct TListTypeTraits<FCurveEditorTreeItemID>
{
public:
	struct NullableType : FCurveEditorTreeItemID
	{
		NullableType(TYPE_OF_NULLPTR){}
		NullableType(FCurveEditorTreeItemID Other) : FCurveEditorTreeItemID(Other) {}
	};

	using MapKeyFuncs = TDefaultMapHashableKeyFuncs<FCurveEditorTreeItemID, TSharedRef<ITableRow>, false>;
	using MapKeyFuncsSparse = TDefaultMapHashableKeyFuncs<FCurveEditorTreeItemID, FSparseItemInfo, false>;
	using SetKeyFuncs = DefaultKeyFuncs<FCurveEditorTreeItemID>;

	static void AddReferencedObjects(FReferenceCollector&, TArray<FCurveEditorTreeItemID>&, TSet<FCurveEditorTreeItemID>&) {}

	static bool IsPtrValid(NullableType InPtr)
	{
		return InPtr.IsValid();
	}

	static void ResetPtr(NullableType& InPtr)
	{
		InPtr = nullptr;
	}

	static NullableType MakeNullPtr()
	{
		return nullptr;
	}

	static FCurveEditorTreeItemID NullableItemTypeConvertToItemType(NullableType InPtr)
	{
		return InPtr;
	}

	static FString DebugDump(FCurveEditorTreeItemID InPtr)
	{
		return FString::Printf(TEXT("%d"), InPtr.GetValue());
	}

	class SerializerType{};
};

template <>
struct TIsValidListItem<FCurveEditorTreeItemID>
{
	enum
	{
		Value = true
	};
};

