// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "OverlayRenderingParameters.h"

#include "Components/SceneCaptureComponent2D.h"
#include "TextureResource.h"

#include "ReprojectionBlendingData.generated.h"


//////////////////////////////////////////////////////////////////////////////////////////////
// Blend inputs
//////////////////////////////////////////////////////////////////////////////////////////////

USTRUCT(BlueprintType, Category = "PICP")
struct FPicpFrameBlendingParameters
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	TArray<USceneCaptureComponent2D*> SourceFrames;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	UTextureRenderTarget2D* DestinationFrame;		
};

USTRUCT(BlueprintType, Category = "PICP")
struct FPicpOverlayFrameBlendingPair
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	FString Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	UTextureRenderTarget2D* SourceFrameCapture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	ECameraOverlayRenderMode OverlayBlendMode;
};

USTRUCT(BlueprintType, Category = "PICP")
struct FPicpOverlayFrameBlendingParameters
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	UTextureRenderTarget2D* CameraOverlayFrame;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	FVector  SoftEdge;    // Basic soft edges values

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	USceneCaptureComponent2D* CameraOverlayFrameCapture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	TArray<struct FPicpOverlayFrameBlendingPair> OverlayBlendFrames;
};
