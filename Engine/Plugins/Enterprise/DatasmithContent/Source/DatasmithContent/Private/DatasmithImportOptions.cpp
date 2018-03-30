// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "DatasmithImportOptions.h"

#include "UObject/UnrealType.h"

#include "HAL/FileManager.h"
#include "Editor/MainFrame/Public/Interfaces/IMainFrameModule.h"

#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"


FDatasmithImportBaseOptions::FDatasmithImportBaseOptions()
	: SceneHandling(EDatasmithImportScene::CurrentLevel)
	, bIncludeGeometry(true)
	, bIncludeMaterial(true)
	, bIncludeLight(true)
	, bIncludeCamera(true)
{
}

UDatasmithImportOptions::UDatasmithImportOptions(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, SearchPackagePolicy(EDatasmithImportSearchPackagePolicy::Current)
	, MaterialConflictPolicy(EDatasmithImportAssetConflictPolicy::Replace)
	, TextureConflictPolicy(EDatasmithImportAssetConflictPolicy::Replace)
	, ActorConflictPolicy(EDatasmithImportAssetConflictPolicy::Replace)
	, LightConflictPolicy(EDatasmithImportAssetConflictPolicy::Replace)
	, CameraConflictPolicy(EDatasmithImportAssetConflictPolicy::Replace)
	, MaterialQuality(EDatasmithImportMaterialQuality::UseNoFresnelCurves)
	, HierarchyHandling(EDatasmithImportHierarchy::UseMultipleActors)
	, bUseSameOptions(false)
{
}

void UDatasmithImportOptions::UpdateNotDisplayedConfig()
{
	// Update enum properties based on boolean values
	if (BaseOptions.bIncludeGeometry == true)
	{
		ActorConflictPolicy = EDatasmithImportAssetConflictPolicy::Replace;
	}
	else
	{
		ActorConflictPolicy = EDatasmithImportAssetConflictPolicy::Ignore;
	}

	if (BaseOptions.bIncludeMaterial == true)
	{
		MaterialConflictPolicy = EDatasmithImportAssetConflictPolicy::Replace;
		TextureConflictPolicy = EDatasmithImportAssetConflictPolicy::Replace;
	}
	else
	{
		MaterialConflictPolicy = EDatasmithImportAssetConflictPolicy::Ignore;
		TextureConflictPolicy = EDatasmithImportAssetConflictPolicy::Ignore;
	}

	if (BaseOptions.bIncludeLight == true)
	{
		LightConflictPolicy = EDatasmithImportAssetConflictPolicy::Replace;
	}
	else
	{
		LightConflictPolicy = EDatasmithImportAssetConflictPolicy::Ignore;
	}

	if (BaseOptions.bIncludeCamera == true)
	{
		CameraConflictPolicy = EDatasmithImportAssetConflictPolicy::Replace;
	}
	else
	{
		CameraConflictPolicy = EDatasmithImportAssetConflictPolicy::Ignore;
	}

	MaterialQuality = EDatasmithImportMaterialQuality::UseRealFresnelCurves;

	// For the time being, by default, search for existing components, Materials, etc, is done in the destination package
	SearchPackagePolicy = EDatasmithImportSearchPackagePolicy::Current;
}

FDatasmithStaticMeshImportOptions::FDatasmithStaticMeshImportOptions()
	: MinLightmapResolution( EDatasmithImportLightmapMin::LIGHTMAP_64 )
	, MaxLightmapResolution( EDatasmithImportLightmapMax::LIGHTMAP_512 )
	, bRemoveDegenerates( true )
{
}

int32 FDatasmithStaticMeshImportOptions::ConvertLightmapEnumToValue( EDatasmithImportLightmapMin EnumValue )
{
	int32 MinLightmapSize = 0;

	switch ( EnumValue )
	{
	case EDatasmithImportLightmapMin::LIGHTMAP_16:
		MinLightmapSize = 16;
		break;
	case EDatasmithImportLightmapMin::LIGHTMAP_32:
		MinLightmapSize = 32;
		break;
	case EDatasmithImportLightmapMin::LIGHTMAP_64:
		MinLightmapSize = 64;
		break;
	case EDatasmithImportLightmapMin::LIGHTMAP_128:
		MinLightmapSize = 128;
		break;
	case EDatasmithImportLightmapMin::LIGHTMAP_256:
		MinLightmapSize = 256;
		break;
	case EDatasmithImportLightmapMin::LIGHTMAP_512:
		MinLightmapSize = 512;
		break;
	default:
		MinLightmapSize = 32;
		break;
	}

	return MinLightmapSize;
}

int32 FDatasmithStaticMeshImportOptions::ConvertLightmapEnumToValue( EDatasmithImportLightmapMax EnumValue )
{
	int32 MaxLightmapSize = 0;

	switch ( EnumValue )
	{
	case EDatasmithImportLightmapMax::LIGHTMAP_64:
		MaxLightmapSize = 64;
		break;
	case EDatasmithImportLightmapMax::LIGHTMAP_128:
		MaxLightmapSize = 128;
		break;
	case EDatasmithImportLightmapMax::LIGHTMAP_256:
		MaxLightmapSize = 256;
		break;
	case EDatasmithImportLightmapMax::LIGHTMAP_512:
		MaxLightmapSize = 512;
		break;
	case EDatasmithImportLightmapMax::LIGHTMAP_1024:
		MaxLightmapSize = 1024;
		break;
	case EDatasmithImportLightmapMax::LIGHTMAP_2048:
		MaxLightmapSize = 2048;
		break;
	case EDatasmithImportLightmapMax::LIGHTMAP_4096:
		MaxLightmapSize = 4096;
		break;
	default:
		MaxLightmapSize = 512;
		break;
	}

	return MaxLightmapSize;
}
