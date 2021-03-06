// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataPrepOperationsLibrary.h"

#include "ActorEditorUtils.h"
#include "AssetDeleteModel.h"
#include "AssetRegistryModule.h"
#include "Camera/CameraActor.h"
#include "Editor.h"
#include "Engine/Light.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Layers/ILayers.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "MeshTypes.h"
#include "Misc/FileHelper.h"
#include "ObjectTools.h"
#include "UObject/SoftObjectPath.h"
#include "AssetDeleteModel.h"

DEFINE_LOG_CATEGORY(LogDataprep);

#define LOCTEXT_NAMESPACE "DataprepOperationsLibrary"

extern UNREALED_API UEditorEngine* GEditor;

namespace DataprepOperationsLibraryUtil
{
	TArray<UStaticMesh*> GetSelectedMeshes(const TArray<AActor*>& SelectedActors)
	{
		TSet<UStaticMesh*> SelectedMeshes;

		for (AActor* Actor : SelectedActors)
		{
			if (Actor)
			{
				TInlineComponentArray<UStaticMeshComponent*> StaticMeshComponents(Actor);
				for (UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
				{
					SelectedMeshes.Add(StaticMeshComponent->GetStaticMesh());
				}
			}
		}

		return SelectedMeshes.Array();
	}

	TArray<UStaticMesh*> GetSelectedMeshes(const TArray<UObject*>& SelectedObjects)
	{
		TSet<UStaticMesh*> SelectedMeshes;
		TArray<AActor*> SelectedActors;

		// Create LODs but do not commit changes
		for (UObject* Object : SelectedObjects)
		{
			if (Object->IsA(UStaticMesh::StaticClass()))
			{
				SelectedMeshes.Add(Cast<UStaticMesh>(Object));
			}
			else if (Object->IsA(AActor::StaticClass()))
			{
				TInlineComponentArray<UStaticMeshComponent*> StaticMeshComponents(Cast<AActor>(Object));
				for (UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
				{
					SelectedMeshes.Add(StaticMeshComponent->GetStaticMesh());
				}
			}
		}

		return SelectedMeshes.Array();
	}

	TArray<UMaterialInterface*> GetUsedMaterials(const TArray<UObject*>& SelectedObjects)
	{
		TSet<UMaterialInterface*> MaterialSet;

		for (UObject* Object : SelectedObjects)
		{
			if (AActor* Actor = Cast< AActor >(Object))
			{
				// Find the materials by iterating over every mesh component.
				TInlineComponentArray<UMeshComponent*> MeshComponents(Actor);
				for (UMeshComponent* MeshComponent : MeshComponents)
				{
					int32 MaterialCount = FMath::Max( MeshComponent->GetNumOverrideMaterials(), MeshComponent->GetNumMaterials() );

					for (int32 Index = 0; Index < MaterialCount; ++Index)
					{
						MaterialSet.Add(MeshComponent->GetMaterial(Index));
					}
				}
			}
			else if (UStaticMesh* StaticMesh = Cast< UStaticMesh >(Object))
			{
				for (int32 Index = 0; Index < StaticMesh->StaticMaterials.Num(); ++Index)
				{
					MaterialSet.Add(StaticMesh->GetMaterial(Index));
				}
			}
		}

		return MaterialSet.Array();
	}

	class FScopedStaticMeshEdit final
	{
	public:
		FScopedStaticMeshEdit( UStaticMesh* InStaticMesh )
			: StaticMesh( InStaticMesh )
		{
			BuildSettingsBackup = PreventStaticMeshBuild( StaticMesh );
		}

		FScopedStaticMeshEdit( const FScopedStaticMeshEdit& Other ) = default;
		FScopedStaticMeshEdit( FScopedStaticMeshEdit&& Other ) = default;

		FScopedStaticMeshEdit& operator=( const FScopedStaticMeshEdit& Other ) = default;
		FScopedStaticMeshEdit& operator=( FScopedStaticMeshEdit&& Other ) = default;

		~FScopedStaticMeshEdit()
		{
			RestoreStaticMeshBuild( StaticMesh, MoveTemp( BuildSettingsBackup ) );
		}

	public:
		static TArray< FMeshBuildSettings > PreventStaticMeshBuild( UStaticMesh* StaticMesh )
		{
			if ( !StaticMesh )
			{
				return {};
			}

			TArray< FMeshBuildSettings > BuildSettingsBackup;

			for ( FStaticMeshSourceModel& SourceModel : StaticMesh->SourceModels )
			{
				BuildSettingsBackup.Add( SourceModel.BuildSettings );

				// These were done in the PreBuild step
				SourceModel.BuildSettings.bGenerateLightmapUVs = false;
				SourceModel.BuildSettings.bRecomputeNormals = false;
				SourceModel.BuildSettings.bRecomputeTangents = false;
			}

			return BuildSettingsBackup;
		}

		static void RestoreStaticMeshBuild( UStaticMesh* StaticMesh, const TArray< FMeshBuildSettings >& BuildSettingsBackup )
		{
			if ( !StaticMesh )
			{
				return;
			}

			// Restore StaticMesh's build settings
			for ( int32 LODIndex = 0; LODIndex < BuildSettingsBackup.Num() ; ++LODIndex )
			{
				// Update only LODs which were cached
				if (StaticMesh->SourceModels.IsValidIndex( LODIndex ))
				{
					const FMeshBuildSettings& CachedBuildSettings = BuildSettingsBackup[ LODIndex ];
					FMeshBuildSettings& BuildSettings = StaticMesh->SourceModels[LODIndex].BuildSettings;

					// Restore only the properties which were modified
					BuildSettings.bGenerateLightmapUVs = CachedBuildSettings.bGenerateLightmapUVs;
					BuildSettings.bRecomputeNormals = CachedBuildSettings.bRecomputeNormals;
					BuildSettings.bRecomputeTangents = CachedBuildSettings.bRecomputeTangents;
				}
			}
		}

	private:
		TArray< FMeshBuildSettings > BuildSettingsBackup;
		UStaticMesh* StaticMesh;
	};

	int32 GetActorDepth(AActor* Actor)
	{
		return Actor ? 1 + GetActorDepth(Actor->GetAttachParentActor()) : 0;
	}
} // ns DataprepOperationsLibraryUtil

void UDataprepOperationsLibrary::SetLods(const TArray<UObject*>& SelectedObjects, const FEditorScriptingMeshReductionOptions& ReductionOptions)
{
	TArray<UStaticMesh*> SelectedMeshes = DataprepOperationsLibraryUtil::GetSelectedMeshes(SelectedObjects);

	// Create LODs but do not commit changes
	for (UStaticMesh* StaticMesh : SelectedMeshes)
	{
		if (StaticMesh)
		{
			DataprepOperationsLibraryUtil::FScopedStaticMeshEdit StaticMeshEdit( StaticMesh );

			UEditorStaticMeshLibrary::SetLodsWithNotification(StaticMesh, ReductionOptions, false);
		}
	}
}

void UDataprepOperationsLibrary::SetSimpleCollision(const TArray<UObject*>& SelectedObjects, const EScriptingCollisionShapeType ShapeType)
{
	TArray<UStaticMesh*> SelectedMeshes = DataprepOperationsLibraryUtil::GetSelectedMeshes(SelectedObjects);

	// Create LODs but do not commit changes
	for (UStaticMesh* StaticMesh : SelectedMeshes)
	{
		if (StaticMesh)
		{
			DataprepOperationsLibraryUtil::FScopedStaticMeshEdit StaticMeshEdit( StaticMesh );

			// Remove existing simple collisions
			UEditorStaticMeshLibrary::RemoveCollisionsWithNotification(StaticMesh, false);

			UEditorStaticMeshLibrary::AddSimpleCollisionsWithNotification(StaticMesh, ShapeType, false);
		}
	}
}

void UDataprepOperationsLibrary::SetConvexDecompositionCollision(const TArray<UObject*>& SelectedObjects, int32 HullCount, int32 MaxHullVerts, int32 HullPrecision)
{
	TArray<UStaticMesh*> SelectedMeshes = DataprepOperationsLibraryUtil::GetSelectedMeshes(SelectedObjects);

	// Build complex collision
	for (UStaticMesh* StaticMesh : SelectedMeshes)
	{
		if (StaticMesh)
		{
			DataprepOperationsLibraryUtil::FScopedStaticMeshEdit StaticMeshEdit( StaticMesh );

			UEditorStaticMeshLibrary::SetConvexDecompositionCollisionsWithNotification(StaticMesh, HullCount, MaxHullVerts, HullPrecision, false);
		}
	}
}

void UDataprepOperationsLibrary::SetGenerateLightmapUVs( const TArray< UObject* >& Assets, bool bGenerateLightmapUVs )
{
	TArray<UStaticMesh*> SelectedMeshes = DataprepOperationsLibraryUtil::GetSelectedMeshes(Assets);

	for (UStaticMesh* StaticMesh : SelectedMeshes)
	{
		if (StaticMesh)
		{
			for ( FStaticMeshSourceModel& SourceModel : StaticMesh->SourceModels )
			{
				SourceModel.BuildSettings.bGenerateLightmapUVs = bGenerateLightmapUVs;
			}
		}
	}
}

void UDataprepOperationsLibrary::SubstituteMaterial(const TArray<UObject*>& SelectedObjects, const FString& MaterialSearch, EEditorScriptingStringMatchType StringMatch, UMaterialInterface* MaterialSubstitute)
{
	TArray<UMaterialInterface*> MaterialsUsed = DataprepOperationsLibraryUtil::GetUsedMaterials(SelectedObjects);

	SubstituteMaterial(SelectedObjects, MaterialSearch, StringMatch, MaterialsUsed, MaterialSubstitute);
}

void UDataprepOperationsLibrary::SubstituteMaterialsByTable(const TArray<UObject*>& SelectedObjects, const UDataTable* DataTable)
{
	if (DataTable == nullptr || DataTable->GetRowStruct() == nullptr || !DataTable->GetRowStruct()->IsChildOf(FMaterialSubstitutionDataTable::StaticStruct()))
	{
		return;
	}

	TArray<UMaterialInterface*> MaterialsUsed = DataprepOperationsLibraryUtil::GetUsedMaterials(SelectedObjects);

	const TMap<FName, uint8*>&  MaterialTableRowMap = DataTable->GetRowMap();
	for (auto& MaterialTableRowEntry : MaterialTableRowMap)
	{
		const FMaterialSubstitutionDataTable* MaterialRow = (const FMaterialSubstitutionDataTable*)MaterialTableRowEntry.Value;
		if (MaterialRow != nullptr && MaterialRow->MaterialReplacement != nullptr)
		{
			SubstituteMaterial(SelectedObjects, MaterialRow->SearchString, MaterialRow->StringMatch, MaterialsUsed, MaterialRow->MaterialReplacement);
		}
	}
}

void UDataprepOperationsLibrary::SubstituteMaterial(const TArray<UObject*>& SelectedObjects, const FString& MaterialSearch, EEditorScriptingStringMatchType StringMatch, const TArray<UMaterialInterface*>& MaterialList, UMaterialInterface* MaterialSubstitute)
{
	TArray<UObject*> MatchingObjects = UEditorFilterLibrary::ByIDName(TArray<UObject*>(MaterialList), MaterialSearch, StringMatch, EEditorScriptingFilterType::Include);

	TArray<UMaterialInterface*> MaterialsToReplace;
	for (UObject* Object : MatchingObjects)
	{
		if (UMaterialInterface* MaterialInterface = Cast<UMaterialInterface>(Object))
		{
			MaterialsToReplace.Add(MaterialInterface);
		}
	}

	for (UMaterialInterface* MaterialToReplace : MaterialsToReplace)
	{
		for (UObject* Object : SelectedObjects)
		{
			if (AActor* Actor = Cast< AActor >(Object))
			{
				// Find the materials by iterating over every mesh component.
				TInlineComponentArray<UMeshComponent*> MeshComponents(Actor);
				for (UMeshComponent* MeshComponent : MeshComponents)
				{
					int32 MaterialCount = FMath::Max( MeshComponent->GetNumOverrideMaterials(), MeshComponent->GetNumMaterials() );

					for (int32 Index = 0; Index < MaterialCount; ++Index)
					{
						if (MeshComponent->GetMaterial(Index) == MaterialToReplace)
						{
							MeshComponent->SetMaterial(Index, MaterialSubstitute);
						}
					}
				}
			}
			else if (UStaticMesh* StaticMesh = Cast< UStaticMesh >(Object))
			{
				DataprepOperationsLibraryUtil::FScopedStaticMeshEdit StaticMeshEdit( StaticMesh );

				TArray<FStaticMaterial>& StaticMaterials = StaticMesh->StaticMaterials;
				for (int32 Index = 0; Index < StaticMesh->StaticMaterials.Num(); ++Index)
				{
					if (StaticMesh->GetMaterial(Index) == MaterialToReplace)
					{
						StaticMesh->SetMaterial(Index, MaterialSubstitute);
					}
				}
			}
		}
	}
}

void UDataprepOperationsLibrary::SetMobility( const TArray< UObject* >& SelectedObjects, EComponentMobility::Type MobilityType )
{
	for (UObject* Object : SelectedObjects)
	{
		if (AActor* Actor = Cast< AActor >(Object))
		{
			// Find the materials by iterating over every mesh component.
			TInlineComponentArray<USceneComponent*> SceneComponents(Actor);
			for (USceneComponent* SceneComponent : SceneComponents)
			{
				SceneComponent->SetMobility(MobilityType);
			}
		}
	}
}

void UDataprepOperationsLibrary::SetMaterial( const TArray< UObject* >& SelectedObjects, UMaterialInterface* MaterialSubstitute )
{
	for (UObject* Object : SelectedObjects)
	{
		if (AActor* Actor = Cast< AActor >(Object))
		{
			// Find the materials by iterating over every mesh component.
			TInlineComponentArray<UMeshComponent*> MeshComponents(Actor);
			for (UMeshComponent* MeshComponent : MeshComponents)
			{
				int32 MaterialCount = FMath::Max( MeshComponent->GetNumOverrideMaterials(), MeshComponent->GetNumMaterials() );

				for (int32 Index = 0; Index < MaterialCount; ++Index)
				{
					MeshComponent->SetMaterial(Index, MaterialSubstitute);
				}
			}
		}
		else if (UStaticMesh* StaticMesh = Cast< UStaticMesh >(Object))
		{
			DataprepOperationsLibraryUtil::FScopedStaticMeshEdit StaticMeshEdit( StaticMesh );

			TArray<FStaticMaterial>& StaticMaterials = StaticMesh->StaticMaterials;
			for (int32 Index = 0; Index < StaticMesh->StaticMaterials.Num(); ++Index)
			{
				StaticMesh->SetMaterial(Index, MaterialSubstitute);
			}
		}
	}
}

void UDataprepOperationsLibrary::SetLODGroup( const TArray<UObject*>& SelectedObjects, FName& LODGroupName )
{
	TArray<FName> LODGroupNames;
	UStaticMesh::GetLODGroups( LODGroupNames );

	if ( LODGroupNames.Find( LODGroupName ) != INDEX_NONE )
	{
		TArray<UStaticMesh*> SelectedMeshes = DataprepOperationsLibraryUtil::GetSelectedMeshes(SelectedObjects);

		// Apply the new LODGroup without rebuilding the static mesh
		for (UStaticMesh* StaticMesh : SelectedMeshes)
		{
			StaticMesh->SetLODGroup( LODGroupName, false);
		}
	}
}

void UDataprepOperationsLibrary::RemoveObjects(const TArray< UObject* >& Objects)
{
	// Implementation based on DatasmithImporterImpl::DeleteActorsMissingFromScene, UEditorLevelLibrary::DestroyActor
	struct FActorAndDepth
	{
		AActor* Actor;
		int32 Depth;
	};

	TArray<FActorAndDepth> ActorsToDelete;
	ActorsToDelete.Reserve(Objects.Num());

	TArray<UObject*> AssetsToDelete;
	AssetsToDelete.Reserve(Objects.Num());

	for (UObject* Object : Objects)
	{
		if ( !ensure(Object) || Object->IsPendingKill() )
		{
			continue;
		}

		if (AActor* Actor = Cast< AActor >( Object ))
		{
			ActorsToDelete.Add(FActorAndDepth{Actor, DataprepOperationsLibraryUtil::GetActorDepth(Actor)});
			// #ueent_todo if rem children option, add them here
		}
		else
		{
			AssetsToDelete.Add(Object);
		}
	}

	// Sort actors by decreasing depth (in order to delete children first)
	ActorsToDelete.Sort([](const FActorAndDepth& Lhs, const FActorAndDepth& Rhs){ return Lhs.Depth > Rhs.Depth; });

	bool bSelectionAffected = false;
	for (const FActorAndDepth& ActorInfo : ActorsToDelete)
	{
		AActor* Actor = ActorInfo.Actor;

		// Reattach our children to our parent
		TArray< USceneComponent* > AttachChildren = Actor->GetRootComponent()->GetAttachChildren(); // Make a copy because the array in RootComponent will get modified during the process
		USceneComponent* AttachParent = Actor->GetRootComponent()->GetAttachParent();

		for ( USceneComponent* ChildComponent : AttachChildren )
		{
			// skip component with invalid or condemned owner
			AActor* Owner = ChildComponent->GetOwner();
			if ( Owner == nullptr || Owner == Actor || Owner->IsPendingKill() || Objects.Contains(Owner))
			{
				continue;
			}

			ChildComponent->AttachToComponent( AttachParent, FAttachmentTransformRules::KeepWorldTransform );
		}

		// Actual deletion of the actor
		{
			Actor->Rename();
			if (Actor->IsSelected())
			{
				GEditor->SelectActor(Actor, false, false);
				bSelectionAffected = true;
			}

			if (GEditor->Layers)
			{
				GEditor->Layers->DisassociateActorFromLayers(Actor);
			}

			if (UWorld* World = Actor->GetWorld())
			{
				World->EditorDestroyActor(Actor, true);
			}
		}
	}

	if (bSelectionAffected)
	{
		GEditor->NoteSelectionChange();
	}

	// Delete assets
	{
		const int32 AssetsToDeleteCount = AssetsToDelete.Num();
		const int32 NonDeletedAssetsCount = ObjectTools::DeleteObjects(AssetsToDelete, false) - AssetsToDeleteCount;

		// Warn user that not all objects have been deleted
		if(NonDeletedAssetsCount > 0)
		{
			UE_LOG(LogDataprep, Warning, TEXT("RemoveObjects: %d objects out of %d were not deleted"), NonDeletedAssetsCount, AssetsToDeleteCount);
		}
	}
}

#undef LOCTEXT_NAMESPACE
