// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"

#include "LandscapeBPCustomBrush.generated.h"

UCLASS(Abstract, Blueprintable, hidecategories = (Replication, Input, LOD, Actor, Cooking, Rendering))
class LANDSCAPE_API ALandscapeBlueprintCustomBrush : public AActor
{
	GENERATED_UCLASS_BODY()

private:
	UPROPERTY(Category= "Settings", EditAnywhere)
	bool AffectHeightmap;

	UPROPERTY(Category= "Settings", EditAnywhere)
	bool AffectWeightmap;

	UPROPERTY(Category = "Settings", EditAnywhere)
	TArray<FName> AffectedWeightmapLayers;

#if WITH_EDITORONLY_DATA
	UPROPERTY(DuplicateTransient)
	class ALandscape* OwningLandscape;

	UPROPERTY(NonTransactional, DuplicateTransient)
	bool bIsCommited;

	UPROPERTY(Transient)
	bool bIsVisible;
#endif
public:

	virtual bool ShouldTickIfViewportsOnly() const override;
	virtual void Tick(float DeltaSeconds) override;

	bool IsAffectingHeightmap() const { return AffectHeightmap; }
	bool IsAffectingWeightmap() const { return AffectWeightmap; }
	bool IsAffectingWeightmapLayer(const FName& InLayerName) const;

	UFUNCTION(BlueprintImplementableEvent)
	UTextureRenderTarget2D* Render(bool InIsHeightmap, UTextureRenderTarget2D* InCombinedResult);

	UFUNCTION(BlueprintImplementableEvent)
	void Initialize(const FTransform& InLandscapeTransform, const FIntPoint& InLandscapeSize, const FIntPoint& InLandscapeRenderTargetSize);

	UFUNCTION(BlueprintCallable, Category="Landscape")
	void RequestLandscapeUpdate();

#if WITH_EDITOR
	void SetCommitState(bool InCommited);
	bool IsCommited() const { return bIsCommited; }

	bool IsVisible() const { return bIsVisible; }
	void SetIsVisible(bool bInIsVisible);

	void SetAffectsHeightmap(bool bInAffectsHeightmap);
	void SetAffectsWeightmap(bool bInAffectsWeightmap);

	void SetOwningLandscape(class ALandscape* InOwningLandscape);
	class ALandscape* GetOwningLandscape() const;

	virtual void PostEditMove(bool bFinished) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	virtual void Destroyed() override;
#endif
};

