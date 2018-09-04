// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingSettings.h"

UPixelStreamingSettings::UPixelStreamingSettings(const FObjectInitializer& ObjectInitlaizer)
	: Super(ObjectInitlaizer)
{

}

FName UPixelStreamingSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

#if WITH_EDITOR
FText UPixelStreamingSettings::GetSectionText() const
{
	return NSLOCTEXT("PixelStreamingPlugin", "PixelStreamingSettingsSection", "PixelStreaming");
}
#endif

#if WITH_EDITOR
void UPixelStreamingSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif


