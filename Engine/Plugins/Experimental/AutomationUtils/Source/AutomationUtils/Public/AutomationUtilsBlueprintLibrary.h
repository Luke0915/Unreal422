// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/LatentActionManager.h"
#include "Kismet/BlueprintFunctionLibrary.h"


#include "AutomationUtilsBlueprintLibrary.generated.h"

UCLASS()
class AUTOMATIONUTILS_API UAutomationUtilsBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

	UFUNCTION(BlueprintCallable, Category = "Automation")
	static void TakeGameplayAutomationScreenshot(const FString ScreenshotName, float MaxGlobalError = .02, float MaxLocalError = .12);
};