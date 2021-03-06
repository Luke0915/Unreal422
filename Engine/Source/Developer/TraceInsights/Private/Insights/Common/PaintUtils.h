// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/DrawElements.h"

#define MAKE_PAINT_GEOMETRY_PT(Geometry, X, Y)       Geometry.ToPaintGeometry(FSlateLayoutTransform(1.0f, FVector2D(X, Y)))
#define MAKE_PAINT_GEOMETRY_RC(Geometry, X, Y, W, H) Geometry.ToPaintGeometry(FVector2D(W, H), FSlateLayoutTransform(1.0f, FVector2D(X, Y)))

struct FGeometry;
class FSlateRect;
class WidgetStyle;
enum class ESlateDrawEffect : uint8;
class FSlateWindowElementList;

/**
 * Holds current state provided by OnPaint function, used to simplify drawing.
 */
struct FDrawContext
{
	FDrawContext(const FGeometry& InGeometry,
				 const FSlateRect& InCullingRect,
				 const FWidgetStyle& InWidgetStyle,
				 const ESlateDrawEffect InDrawEffects,
				 FSlateWindowElementList& InOutElementList,
				 int32& InOutLayerId)
		: Geometry(InGeometry)
		, CullingRect(InCullingRect)
		, WidgetStyle(InWidgetStyle)
		, DrawEffects(InDrawEffects)
		, ElementList(InOutElementList)
		, LayerId(InOutLayerId)
	{}

	/**
	 * Non-copyable
	 */
	FDrawContext(const FDrawContext&) = delete;
	FDrawContext& operator=(const FDrawContext&) = delete;

	inline void DrawBox(const float X, const float Y, const float W, const float H, const FSlateBrush* Brush, const FLinearColor& Color) const
	{
		FSlateDrawElement::MakeBox(ElementList, LayerId, MAKE_PAINT_GEOMETRY_RC(Geometry, X, Y, W, H), Brush, DrawEffects, Color);
	}

	inline void DrawBox(const int32 InLayer, const float X, const float Y, const float W, const float H, const FSlateBrush* Brush, const FLinearColor& Color) const
	{
		FSlateDrawElement::MakeBox(ElementList, InLayer, MAKE_PAINT_GEOMETRY_RC(Geometry, X, Y, W, H), Brush, DrawEffects, Color);
	}

	inline void DrawRotatedBox(const float X, const float Y, const float W, const float H, const FSlateBrush* Brush, const FLinearColor& Color, float Angle, TOptional<FVector2D> RotationPoint) const
	{
		FSlateDrawElement::MakeRotatedBox(ElementList, LayerId, MAKE_PAINT_GEOMETRY_RC(Geometry, X, Y, W, H), Brush, DrawEffects, Angle, RotationPoint, FSlateDrawElement::RelativeToElement, Color);
	}

	inline void DrawText(const float X, const float Y, const FString& Text, const FSlateFontInfo& Font, const FLinearColor& Color) const
	{
		FSlateDrawElement::MakeText(ElementList, LayerId, MAKE_PAINT_GEOMETRY_PT(Geometry, X, Y), Text, Font, DrawEffects, Color);
	}

	inline void DrawText(const int32 InLayer, const float X, const float Y, const FString& Text, const FSlateFontInfo& Font, const FLinearColor& Color) const
	{
		FSlateDrawElement::MakeText(ElementList, InLayer, MAKE_PAINT_GEOMETRY_PT(Geometry, X, Y), Text, Font, DrawEffects, Color);
	}

	inline void DrawText(const float X, const float Y, const FString& Text, const int32 StartIndex, const int32 EndIndex, const FSlateFontInfo& Font, const FLinearColor& Color) const
	{
		FSlateDrawElement::MakeText(ElementList, LayerId, MAKE_PAINT_GEOMETRY_PT(Geometry, X, Y), Text, StartIndex, EndIndex, Font, DrawEffects, Color);
	}

	inline void DrawText(const int32 InLayer, const float X, const float Y, const FString& Text, const int32 StartIndex, const int32 EndIndex, const FSlateFontInfo& Font, const FLinearColor& Color) const
	{
		FSlateDrawElement::MakeText(ElementList, InLayer, MAKE_PAINT_GEOMETRY_PT(Geometry, X, Y), Text, StartIndex, EndIndex, Font, DrawEffects, Color);
	}

	//inline void IncrementLayer() const { ++LayerId; }

	// Accessors
	const FGeometry& Geometry;
	const FSlateRect& CullingRect;
	const FWidgetStyle& WidgetStyle;
	const ESlateDrawEffect DrawEffects;

	FSlateWindowElementList& ElementList;
	int32& LayerId;
};
