// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "HoloLens/HoloLensSplash.h"
#include "HoloLens/HoloLensPlatformApplicationMisc.h"

void FHoloLensSplash::Show()
{
	//@todo.HoloLens: Implement me
	FHoloLensPlatformApplicationMisc::PumpMessages(true);
}

void FHoloLensSplash::Hide()
{
	//@todo.HoloLens: Implement me
	FHoloLensPlatformApplicationMisc::PumpMessages(true);
}

void FHoloLensSplash::SetSplashText( const SplashTextType::Type InType, const TCHAR* InText )
{
	//@todo.HoloLens: Implement me
}
