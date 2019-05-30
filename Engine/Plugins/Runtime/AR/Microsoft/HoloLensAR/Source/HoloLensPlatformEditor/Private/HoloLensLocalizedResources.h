#pragma once

#include "HoloLensLocalizedResources.generated.h"

USTRUCT()
struct FHoloLensCorePackageStringResources
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, config, Category = Packaging)
	FString PackageDisplayName;

	UPROPERTY(EditAnywhere, config, Category = Packaging)
	FString PublisherDisplayName;

	UPROPERTY(EditAnywhere, config, Category = Packaging)
	FString PackageDescription;

	UPROPERTY(EditAnywhere, config, Category = Packaging)
	FString ApplicationDisplayName;

	UPROPERTY(EditAnywhere, config, Category = Packaging)
	FString ApplicationDescription;
};

USTRUCT()
struct FHoloLensDlcStringResources
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, config, Category = Packaging)
	FString PackageDisplayName;

	UPROPERTY(EditAnywhere, config, Category = Packaging)
	FString PackageDescription;
};

USTRUCT()
struct FHoloLensCorePackageImageResources
{
	GENERATED_BODY()
};

USTRUCT()
struct FHoloLensDlcImageResources
{
	GENERATED_BODY()
};

USTRUCT()
struct FHoloLensCorePackageLocalizedResources
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, config, Category = Packaging)
	FString CultureId;

	UPROPERTY(EditAnywhere, config, Category = Packaging, Meta=(ShowOnlyInnerProperties))
	FHoloLensCorePackageStringResources Strings;

	UPROPERTY(EditAnywhere, config, Category = Packaging)
	FHoloLensCorePackageImageResources Images;
};

USTRUCT()
struct FHoloLensDlcLocalizedResources
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, config, Category = Packaging)
	FString AppliesToDlcPlugin;

	UPROPERTY(EditAnywhere, config, Category = Packaging)
	FString CultureId;

	UPROPERTY(EditAnywhere, config, Category = Packaging, Meta = (ShowOnlyInnerProperties))
	FHoloLensDlcStringResources Strings;

	UPROPERTY(EditAnywhere, config, Category = Packaging)
	FHoloLensDlcImageResources Images;
};