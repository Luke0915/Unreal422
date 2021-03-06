// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/FrameRate.h"
#include "Math/Axis.h"

/**
 * Utility struct that acts as a cache of the current snapping metrics for the curve editor
 */
struct FCurveEditorSnapMetrics
{
	FCurveEditorSnapMetrics()
	{
		bSnapOutputValues = 0;
		bSnapInputValues = 0;
		OutputSnapInterval = 1.0;
	}

	/** Whether we are snapping to the output snap interval */
	uint8 bSnapOutputValues : 1;

	/** Whether we are snapping to the input snap rate */
	uint8 bSnapInputValues : 1;

	/** The output snap interval */
	double OutputSnapInterval;

	/** The input snap rate */
	FFrameRate InputSnapRate;

	/** Snap the specified input time to the input snap rate if necessary */
	FORCEINLINE double SnapInputSeconds(double InputTime)
	{
		return bSnapInputValues && InputSnapRate.IsValid() ? (InputTime * InputSnapRate).RoundToFrame() / InputSnapRate : InputTime;
	}

	/** Snap the specified output value to the output snap interval if necessary */
	FORCEINLINE double SnapOutput(double OutputValue)
	{
		return bSnapOutputValues && OutputSnapInterval != 0.f ? FMath::RoundToDouble(OutputValue / OutputSnapInterval) * OutputSnapInterval : OutputValue;
	}
};

/**
 * Utility struct that acts as a way to control snapping to a specific axis based on UI settings, or shift key.
 */
struct FCurveEditorAxisSnap
{
	/** Can be set to either X, Y, or None to control which axis GetSnappedPosition snaps to. User can override None by pressing shift. */
	EAxisList::Type RestrictedAxisList;

	FCurveEditorAxisSnap()
	{
		RestrictedAxisList = EAxisList::None;
	}

	FVector2D GetSnappedPosition(FVector2D InitialPosition, FVector2D CurrentPosition, const FPointerEvent& MouseEvent, const bool bIgnoreAxisLock = false)
	{
		check(RestrictedAxisList == EAxisList::Type::None || RestrictedAxisList == EAxisList::Type::X || RestrictedAxisList == EAxisList::Type::Y);

		// If we're ignoring axis lock (such as for UI) we allow them to use shift anyways
		bool bCanUseShift = bIgnoreAxisLock || RestrictedAxisList == EAxisList::Type::None;

		FVector2D MouseLockVector = FVector2D::UnitVector;
		if (bCanUseShift && MouseEvent.IsShiftDown())
		{
			// Figure out which axis is currently greater and snap to that 
			FVector2D DragDelta = CurrentPosition - InitialPosition;
			if (FMath::Abs(DragDelta.X) > FMath::Abs(DragDelta.Y))
			{
				MouseLockVector.Y = 0;
			}
			else
			{
				MouseLockVector.X = 0;
			}
		}
		else if (!bIgnoreAxisLock && RestrictedAxisList == EAxisList::Type::X)
		{
			MouseLockVector.Y = 0;
		}
		else if (!bIgnoreAxisLock && RestrictedAxisList == EAxisList::Type::Y)
		{
			MouseLockVector.X = 0;
		}

		return InitialPosition + (CurrentPosition - InitialPosition) * MouseLockVector;
	}
};