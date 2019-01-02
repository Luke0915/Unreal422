// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LevelEditorSequencerIntegration.h"
#include "SequencerEdMode.h"
#include "Styling/SlateIconFinder.h"
#include "PropertyHandle.h"
#include "IDetailKeyframeHandler.h"
#include "GameDelegates.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Editor/PropertyEditor/Public/PropertyEditorModule.h"
#include "Editor/LevelEditor/Public/ILevelEditor.h"
#include "Editor/LevelEditor/Public/ILevelViewport.h"
#include "Editor/LevelEditor/Public/LevelEditor.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailsView.h"
#include "ISequencer.h"
#include "KeyPropertyParams.h"
#include "Engine/Selection.h"
#include "Sequencer.h"
#include "EditorModeManager.h"
#include "SequencerCommands.h"
#include "MovieSceneSequence.h"
#include "MovieScene.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "MovieSceneSequenceID.h"
#include "Evaluation/MovieSceneEvaluationTemplateInstance.h"
#include "SequencerSettings.h"
#include "SequencerInfoColumn.h"
#include "LevelEditorViewport.h"
#include "Modules/ModuleManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "PropertyEditorModule.h"
#include "SceneOutlinerFwd.h"
#include "SceneOutlinerModule.h"
#include "SceneOutlinerPublicTypes.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "UObject/ObjectKey.h"
#include "UnrealEdGlobals.h"
#include "UnrealEdMisc.h"
#include "Editor/UnrealEdEngine.h"
#include "EditorSupportDelegates.h"

#define LOCTEXT_NAMESPACE "LevelEditorSequencerIntegration"

class FDetailKeyframeHandlerWrapper : public IDetailKeyframeHandler
{
public:

	void Add(TWeakPtr<ISequencer> InSequencer)
	{
		Sequencers.Add(InSequencer);
	}

	void Remove(TWeakPtr<ISequencer> InSequencer)
	{
		Sequencers.Remove(InSequencer);
	}

	virtual bool IsPropertyKeyable(UClass* InObjectClass, const IPropertyHandle& InPropertyHandle) const
	{
		FCanKeyPropertyParams CanKeyPropertyParams(InObjectClass, InPropertyHandle);

		for (const TWeakPtr<ISequencer>& WeakSequencer : Sequencers)
		{
			TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
			if (Sequencer.IsValid() && Sequencer->CanKeyProperty(CanKeyPropertyParams))
			{
				return true;
			}
		}
		return false;
	}

	virtual bool IsPropertyKeyingEnabled() const
	{
		for (const TWeakPtr<ISequencer>& WeakSequencer : Sequencers)
		{
			TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
			if (Sequencer.IsValid() && Sequencer->GetFocusedMovieSceneSequence() && Sequencer->GetAllowEditsMode() != EAllowEditsMode::AllowLevelEditsOnly)
			{
				return true;
			}
		}
		return false;
	}

	virtual bool IsPropertyAnimated(const IPropertyHandle& PropertyHandle, UObject *ParentObject) const
	{
		
		for (const TWeakPtr<ISequencer>& WeakSequencer : Sequencers)
		{
			TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
			if (Sequencer.IsValid() && Sequencer->GetFocusedMovieSceneSequence())
			{
				FGuid ObjectHandle = Sequencer->GetHandleToObject(ParentObject);
				if (ObjectHandle.IsValid()) 
				{
					UMovieScene* MovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
					UProperty* Property = PropertyHandle.GetProperty();
					TSharedRef<FPropertyPath> PropertyPath = FPropertyPath::CreateEmpty();
					PropertyPath->AddProperty(FPropertyInfo(Property));
					FName PropertyName(*PropertyPath->ToString(TEXT(".")));
					TSubclassOf<UMovieSceneTrack> TrackClass; //use empty @todo find way to get the UMovieSceneTrack from the Property type.
					return MovieScene->FindTrack(TrackClass, ObjectHandle, PropertyName) != nullptr;
				}
				
				return false;
			}
		}
		return false;
	}
	virtual void OnKeyPropertyClicked(const IPropertyHandle& KeyedPropertyHandle)
	{
		TArray<UObject*> Objects;
		KeyedPropertyHandle.GetOuterObjects( Objects );
		FKeyPropertyParams KeyPropertyParams(Objects, KeyedPropertyHandle, ESequencerKeyMode::ManualKeyForced);

		for (const TWeakPtr<ISequencer>& WeakSequencer : Sequencers)
		{
			TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
			if (Sequencer.IsValid())
			{
				Sequencer->KeyProperty(KeyPropertyParams);
			}
		}
	}

private:
	TArray<TWeakPtr<ISequencer>> Sequencers;
};

static const FName DetailsTabIdentifiers[] = { "LevelEditorSelectionDetails", "LevelEditorSelectionDetails2", "LevelEditorSelectionDetails3", "LevelEditorSelectionDetails4" };

FLevelEditorSequencerIntegration::FLevelEditorSequencerIntegration()
	: bDeferUpdates(false)
{
	KeyFrameHandler = MakeShared<FDetailKeyframeHandlerWrapper>();
}

FLevelEditorSequencerIntegration& FLevelEditorSequencerIntegration::Get()
{
	static FLevelEditorSequencerIntegration Singleton;
	return Singleton;
}

void FLevelEditorSequencerIntegration::IterateAllSequencers(TFunctionRef<void(FSequencer&, const FLevelEditorSequencerIntegrationOptions& Options)> It) const
{
	for (const FSequencerAndOptions& SequencerAndOptions : BoundSequencers)
	{
		TSharedPtr<FSequencer> Pinned = SequencerAndOptions.Sequencer.Pin();
		if (Pinned.IsValid())
		{
			It(*Pinned, SequencerAndOptions.Options);
		}
	}
}

void FLevelEditorSequencerIntegration::Initialize()
{
	AcquiredResources.Release();

	// Register for saving the level so that the state of the scene can be restored before saving and updated after saving.
	{
		FDelegateHandle Handle = FEditorDelegates::PreSaveWorld.AddRaw(this, &FLevelEditorSequencerIntegration::OnPreSaveWorld);
		AcquiredResources.Add([=]{ FEditorDelegates::PreSaveWorld.Remove(Handle); });
	}
	{
		FDelegateHandle Handle = FEditorDelegates::PostSaveWorld.AddRaw(this, &FLevelEditorSequencerIntegration::OnPostSaveWorld);
		AcquiredResources.Add([=]{ FEditorDelegates::PostSaveWorld.Remove(Handle); });
	}
	{
		FDelegateHandle Handle = FEditorDelegates::PreBeginPIE.AddRaw(this, &FLevelEditorSequencerIntegration::OnPreBeginPIE);
		AcquiredResources.Add([=]{ FEditorDelegates::PreBeginPIE.Remove(Handle); });
	}
	{
		FDelegateHandle Handle = FEditorDelegates::EndPIE.AddRaw(this, &FLevelEditorSequencerIntegration::OnEndPIE);
		AcquiredResources.Add([=]{ FEditorDelegates::EndPIE.Remove(Handle); });
	}
	{
		FDelegateHandle Handle = FGameDelegates::Get().GetEndPlayMapDelegate().AddRaw(this, &FLevelEditorSequencerIntegration::OnEndPlayMap);
		AcquiredResources.Add([=]{ FGameDelegates::Get().GetEndPlayMapDelegate().Remove(Handle); });
	}
	{
		FDelegateHandle Handle = FWorldDelegates::LevelAddedToWorld.AddRaw(this, &FLevelEditorSequencerIntegration::OnLevelAdded);
		AcquiredResources.Add([=]{ FWorldDelegates::LevelAddedToWorld.Remove(Handle); });
	}
	{
		FDelegateHandle Handle = FWorldDelegates::LevelRemovedFromWorld.AddRaw(this, &FLevelEditorSequencerIntegration::OnLevelRemoved);
		AcquiredResources.Add([=]{ FWorldDelegates::LevelRemovedFromWorld.Remove(Handle); });
	}
	{
		FDelegateHandle Handle = FEditorDelegates::NewCurrentLevel.AddRaw(this, &FLevelEditorSequencerIntegration::OnNewCurrentLevel);
		AcquiredResources.Add([=]{ FEditorDelegates::NewCurrentLevel.Remove(Handle); });
	}
	{
		FDelegateHandle Handle = FEditorDelegates::OnMapOpened.AddRaw(this, &FLevelEditorSequencerIntegration::OnMapOpened);
		AcquiredResources.Add([=]{ FEditorDelegates::OnMapOpened.Remove(Handle); });
	}
	{
		FDelegateHandle Handle = FEditorDelegates::OnNewActorsDropped.AddRaw(this, &FLevelEditorSequencerIntegration::OnNewActorsDropped);
		AcquiredResources.Add([=]{ FEditorDelegates::OnNewActorsDropped.Remove(Handle); });
	}
	{
		FDelegateHandle Handle = USelection::SelectionChangedEvent.AddRaw( this, &FLevelEditorSequencerIntegration::OnActorSelectionChanged );
		AcquiredResources.Add([=]{ USelection::SelectionChangedEvent.Remove(Handle); });
	}

	{
		FDelegateHandle Handle = FCoreDelegates::OnActorLabelChanged.AddRaw(this, &FLevelEditorSequencerIntegration::OnActorLabelChanged);
		AcquiredResources.Add([=]{ FCoreDelegates::OnActorLabelChanged.Remove(Handle); });
	}

	AddLevelViewportMenuExtender();
	ActivateDetailHandler();
	ActivateSequencerEditorMode();
	BindLevelEditorCommands();
	AttachOutlinerColumn();

	{
		FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

		FDelegateHandle Handle = EditModule.OnPropertyEditorOpened().AddRaw(this, &FLevelEditorSequencerIntegration::OnPropertyEditorOpened);
		AcquiredResources.Add(
			[=]{
				FPropertyEditorModule* EditModulePtr = FModuleManager::Get().GetModulePtr<FPropertyEditorModule>("PropertyEditor");
				if (EditModulePtr)
				{
					EditModulePtr->OnPropertyEditorOpened().Remove(Handle);
				}
			}
		);
	}

	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::Get().GetModuleChecked<FLevelEditorModule>("LevelEditor");

		FDelegateHandle TabContentChanged = LevelEditorModule.OnTabContentChanged().AddRaw(this, &FLevelEditorSequencerIntegration::OnTabContentChanged);
		FDelegateHandle MapChanged = LevelEditorModule.OnMapChanged().AddRaw(this, &FLevelEditorSequencerIntegration::OnMapChanged);
		AcquiredResources.Add(
			[=]{
				FLevelEditorModule* LevelEditorModulePtr = FModuleManager::Get().GetModulePtr<FLevelEditorModule>("LevelEditor");
				if (LevelEditorModulePtr)
				{
					LevelEditorModulePtr->OnTabContentChanged().Remove(TabContentChanged);
					LevelEditorModulePtr->OnMapChanged().Remove(MapChanged);
				}
			}
		);
	}
}

void RenameSpawnableRecursive(FSequencer* Sequencer, FMovieSceneSequenceIDRef SequenceID, AActor* ChangedActor)
{
	FMovieSceneRootEvaluationTemplateInstance& RootInstance = Sequencer->GetEvaluationTemplate();

	// Find the sequence that corresponds to the sequence ID
	UMovieSceneSequence* Sequence = RootInstance.GetSequence(SequenceID);
	UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;

	if (MovieScene)
	{
		// Iterate all this movie scene's spawnables, renaming as appropriate
		for (int32 Index = 0; Index < MovieScene->GetSpawnableCount(); ++Index)
		{
			FGuid ThisGuid = MovieScene->GetSpawnable(Index).GetGuid();

			for (TWeakObjectPtr<> WeakObject : Sequencer->FindBoundObjects(ThisGuid, SequenceID))
			{
				AActor* Actor = Cast<AActor>(WeakObject.Get());
				if (Actor && Actor == ChangedActor)
				{
					MovieScene->GetSpawnable(Index).SetName(ChangedActor->GetActorLabel());
				}
			}
		}
	}

	// Recurse into child nodes
	const FMovieSceneSequenceHierarchyNode* Node = RootInstance.GetHierarchy().FindNode(SequenceID);
	if (Node)
	{
		for (FMovieSceneSequenceIDRef ChildID : Node->Children)
		{
			RenameSpawnableRecursive(Sequencer, ChildID, ChangedActor);
		}
	}
}

void FLevelEditorSequencerIntegration::OnActorLabelChanged(AActor* ChangedActor)
{
	for (const FSequencerAndOptions& SequencerAndOptions : BoundSequencers)
	{
		TSharedPtr<FSequencer> Pinned = SequencerAndOptions.Sequencer.Pin();
		if (Pinned.IsValid())
		{
			RenameSpawnableRecursive(Pinned.Get(), MovieSceneSequenceID::Root, ChangedActor);
		}
	}
}

void FLevelEditorSequencerIntegration::OnPreSaveWorld(uint32 SaveFlags, class UWorld* World)
{
	// Restore the saved state so that the level save can save that instead of the animated state.
	IterateAllSequencers(
		[](FSequencer& In, const FLevelEditorSequencerIntegrationOptions& Options)
		{
			if (Options.bRequiresLevelEvents)
			{
				In.RestorePreAnimatedState();
			}
		}
	);
}

void FLevelEditorSequencerIntegration::OnPostSaveWorld(uint32 SaveFlags, class UWorld* World, bool bSuccess)
{
	// Reset the time after saving so that an update will be triggered to put objects back to their animated state.
	IterateAllSequencers(
		[](FSequencer& In, const FLevelEditorSequencerIntegrationOptions& Options)
		{
			if (Options.bRequiresLevelEvents)
			{
				In.ForceEvaluate();
			}
		}
	);
}

void FLevelEditorSequencerIntegration::OnNewCurrentLevel()
{
	ActivateSequencerEditorMode();
}

void FLevelEditorSequencerIntegration::OnMapOpened(const FString& Filename, bool bLoadAsTemplate)
{
	ActivateSequencerEditorMode();
}

void FLevelEditorSequencerIntegration::OnLevelAdded(ULevel* InLevel, UWorld* InWorld)
{
	IterateAllSequencers(
		[](FSequencer& In, const FLevelEditorSequencerIntegrationOptions& Options)
		{
			if (Options.bRequiresLevelEvents)
			{
				In.State.ClearObjectCaches(In);
			}
		}
	);
}

void FLevelEditorSequencerIntegration::OnLevelRemoved(ULevel* InLevel, UWorld* InWorld)
{
	IterateAllSequencers(
		[](FSequencer& In, const FLevelEditorSequencerIntegrationOptions& Options)
		{
			if (Options.bRequiresLevelEvents)
			{
				In.State.ClearObjectCaches(In);
			}
		}
	);
}

void FLevelEditorSequencerIntegration::OnActorSelectionChanged( UObject* )
{
	IterateAllSequencers(
		[](FSequencer& In, const FLevelEditorSequencerIntegrationOptions& Options)
		{
			if (Options.bRequiresActorEvents)
			{
				if (In.GetSequencerSettings()->GetShowSelectedNodesOnly())
				{
					In.RefreshTree();
				}

				In.ExternalSelectionHasChanged();
			}
		}
	);
}

void FLevelEditorSequencerIntegration::OnNewActorsDropped(const TArray<UObject*>& DroppedObjects, const TArray<AActor*>& DroppedActors)
{
	IterateAllSequencers(
		[&](FSequencer& In, const FLevelEditorSequencerIntegrationOptions& Options)
		{
			if (Options.bRequiresActorEvents)
			{
				In.OnNewActorsDropped(DroppedObjects, DroppedActors);
			}
		}
	);
}

void FLevelEditorSequencerIntegration::OnSequencerEvaluated()
{
	// Redraw if not in PIE/simulate
	const bool bIsInPIEOrSimulate = GEditor->PlayWorld != NULL || GEditor->bIsSimulatingInEditor;
	if (bIsInPIEOrSimulate)
	{
		return;
	}

	// Request a single real-time frame to be rendered to ensure that we tick the world and update the viewport
	for (FEditorViewportClient* LevelVC : GEditor->AllViewportClients)
	{
		if (LevelVC && !LevelVC->IsRealtime())
		{
			LevelVC->RequestRealTimeFrames(1);
		}
	}

	if (!bDeferUpdates)
	{
		UpdateDetails();
	}

	// If realtime is off, this needs to be called to update the pivot location when scrubbing.
	GUnrealEd->UpdatePivotLocationForSelection();

	// Redraw
	FEditorSupportDelegates::RedrawAllViewports.Broadcast();
}

void FLevelEditorSequencerIntegration::OnBeginDeferUpdates()
{
	bDeferUpdates = true;
}

void FLevelEditorSequencerIntegration::OnEndDeferUpdates()
{
	bDeferUpdates = false;
	UpdateDetails();
}

bool FLevelEditorSequencerIntegration::IsBindingVisible(const FMovieSceneBinding& InBinding)
{
	// If nothing selected, show all nodes
	if (GEditor->GetSelectedActorCount() == 0)
	{
		return true;
	}

	// Disregard if not a level sequence (ie. a control rig sequence)
	for (FSequencerAndOptions& SequencerAndOptions : BoundSequencers)
	{
		TSharedPtr<FSequencer> Pinned = SequencerAndOptions.Sequencer.Pin();
		if (Pinned.IsValid())
		{
			if (UMovieSceneSequence* RootSequence = Pinned->GetRootMovieSceneSequence())
			{
				if (RootSequence->GetClass()->GetName() != TEXT("LevelSequence"))
				{
					return true;
				}
			}
		}
	}

	for( FSelectionIterator SelectionIt( *GEditor->GetSelectedActors() ); SelectionIt; ++SelectionIt )
	{
		AActor* SelectedActor = CastChecked<AActor>( *SelectionIt );
		
		if (SelectedActor->GetActorLabel() == InBinding.GetName())
		{
			return true;
		}
	}

	return false;
}

void FLevelEditorSequencerIntegration::OnMovieSceneBindingsChanged()
{
	for (FSequencerAndOptions& SequencerAndOptions : BoundSequencers)
	{
		SequencerAndOptions.BindingData.Get().bActorBindingsDirty = true;
	}
}

void FLevelEditorSequencerIntegration::OnMovieSceneDataChanged(EMovieSceneDataChangeType DataChangeType)
{
	if (DataChangeType == EMovieSceneDataChangeType::MovieSceneStructureItemAdded ||
		DataChangeType == EMovieSceneDataChangeType::MovieSceneStructureItemRemoved ||
		DataChangeType == EMovieSceneDataChangeType::MovieSceneStructureItemsChanged ||
		DataChangeType == EMovieSceneDataChangeType::RefreshAllImmediately ||
		DataChangeType == EMovieSceneDataChangeType::ActiveMovieSceneChanged)
	{
		UpdateDetails();
	}
}

void FLevelEditorSequencerIntegration::OnAllowEditsModeChanged(EAllowEditsMode AllowEditsMode)
{
	UpdateDetails(true);
}

void FLevelEditorSequencerIntegration::UpdateDetails(bool bForceRefresh)
{
	bool bNeedsRefresh = bForceRefresh;

	for (FSequencerAndOptions& SequencerAndOptions : BoundSequencers)
	{
		TSharedPtr<FSequencer> Pinned = SequencerAndOptions.Sequencer.Pin();
		if (Pinned.IsValid())
		{
			SequencerAndOptions.BindingData.Get().bPropertyBindingsDirty = true;
		
			if (Pinned.Get()->GetAllowEditsMode() == EAllowEditsMode::AllowLevelEditsOnly)
			{
				bNeedsRefresh = true;
			}
		}
	}

	if (bNeedsRefresh)
	{
		FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		
		for (const FName& DetailsTabIdentifier : DetailsTabIdentifiers)
		{
			TSharedPtr<IDetailsView> DetailsView = EditModule.FindDetailView(DetailsTabIdentifier);
			if(DetailsView.IsValid())
			{
				DetailsView->ForceRefresh();
			}
		}
	}
}


void FLevelEditorSequencerIntegration::ActivateSequencerEditorMode()
{
	// Release the sequencer mode if we already enabled it
	FName ResourceName("SequencerMode");
	AcquiredResources.Release(ResourceName);

	GLevelEditorModeTools().ActivateMode( FSequencerEdMode::EM_SequencerMode );
	FSequencerEdMode* SequencerEdMode = (FSequencerEdMode*)GLevelEditorModeTools().GetActiveMode(FSequencerEdMode::EM_SequencerMode);

	for (const FSequencerAndOptions& SequencerAndOptions : BoundSequencers)
	{
		TSharedPtr<FSequencer> Pinned = SequencerAndOptions.Sequencer.Pin();
		if (Pinned.IsValid())
		{
			SequencerEdMode->AddSequencer(Pinned);
		}
	}

	// Acquire the resource, which allows us to deactivate the mode later
	AcquiredResources.Add(
		ResourceName,
		[] {
			if (GLevelEditorModeTools().IsModeActive(FSequencerEdMode::EM_SequencerMode))
			{
				GLevelEditorModeTools().DeactivateMode(FSequencerEdMode::EM_SequencerMode);
			}
		}
	);
}


void FLevelEditorSequencerIntegration::OnPreBeginPIE(bool bIsSimulating)
{
	bool bReevaluate = (!bIsSimulating && GetDefault<ULevelEditorPlaySettings>()->bBindSequencerToPIE) || (bIsSimulating && GetDefault<ULevelEditorPlaySettings>()->bBindSequencerToSimulate);

	IterateAllSequencers(
		[=](FSequencer& In, const FLevelEditorSequencerIntegrationOptions& Options)
		{
			if (Options.bRequiresLevelEvents)
			{
				In.GetEvaluationTemplate().ResetDirectorInstances();
				In.RestorePreAnimatedState();
				In.State.ClearObjectCaches(In);

				if (bReevaluate)
				{
					// Notify data changed to enqueue an evaluate
					In.NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::Unknown);
				}
			}
		}
	);
}

void FLevelEditorSequencerIntegration::OnEndPlayMap()
{
	IterateAllSequencers(
		[](FSequencer& In, const FLevelEditorSequencerIntegrationOptions& Options)
		{
			if (Options.bRequiresLevelEvents)
			{
				// Update and clear any stale bindings 
				In.GetEvaluationTemplate().ResetDirectorInstances();
				In.State.ClearObjectCaches(In);
				In.ForceEvaluate();
			}
		}
	);
}

void FLevelEditorSequencerIntegration::OnEndPIE(bool bIsSimulating)
{
	OnEndPlayMap();
}

void FLevelEditorSequencerIntegration::AddLevelViewportMenuExtender()
{
	typedef FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors DelegateType;

	FLevelEditorModule& LevelEditorModule = FModuleManager::Get().LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	auto& MenuExtenders = LevelEditorModule.GetAllLevelViewportContextMenuExtenders();
	MenuExtenders.Add(DelegateType::CreateRaw(this, &FLevelEditorSequencerIntegration::GetLevelViewportExtender));

	FDelegateHandle LevelViewportExtenderHandle = MenuExtenders.Last().GetHandle();
	AcquiredResources.Add(
		[LevelViewportExtenderHandle]{
			FLevelEditorModule* LevelEditorModulePtr = FModuleManager::Get().GetModulePtr<FLevelEditorModule>("LevelEditor");
			if (LevelEditorModulePtr)
			{
				LevelEditorModulePtr->GetAllLevelViewportContextMenuExtenders().RemoveAll([=](const DelegateType& In){ return In.GetHandle() == LevelViewportExtenderHandle; });
			}
		}
	);
}

TSharedRef<FExtender> FLevelEditorSequencerIntegration::GetLevelViewportExtender(const TSharedRef<FUICommandList> CommandList, const TArray<AActor*> InActors)
{
	TSharedRef<FExtender> Extender = MakeShareable(new FExtender);

	FText ActorName;
	if (InActors.Num() == 1)
	{
		ActorName = FText::Format(LOCTEXT("ActorNameSingular", "\"{0}\""), FText::FromString(InActors[0]->GetActorLabel()));
	}
	else if (InActors.Num() > 1)
	{
		ActorName = FText::Format(LOCTEXT("ActorNamePlural", "{0} Actors"), FText::AsNumber(InActors.Num()));
	}

	TSharedRef<FUICommandList> LevelEditorCommandBindings  = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetGlobalLevelEditorActions();
	Extender->AddMenuExtension("ActorControl", EExtensionHook::After, LevelEditorCommandBindings, FMenuExtensionDelegate::CreateLambda(
		[ActorName](FMenuBuilder& MenuBuilder){
			MenuBuilder.BeginSection("SequenceRecording", LOCTEXT("SequenceRecordingHeading", "Sequence Recording"));
			MenuBuilder.AddMenuEntry(FSequencerCommands::Get().RecordSelectedActors, NAME_None, FText::Format(LOCTEXT("RecordSelectedActorsText", "Record {0} In Sequencer"), ActorName));
			MenuBuilder.EndSection();
		}
	));

	return Extender;
}

void FLevelEditorSequencerIntegration::ActivateDetailHandler()
{
	FName DetailHandlerName("DetailHandler");

	AcquiredResources.Release(DetailHandlerName);

	// Add sequencer detail keyframe handler
	FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	for (const FName& DetailsTabIdentifier : DetailsTabIdentifiers)
	{
		TSharedPtr<IDetailsView> DetailsView = EditModule.FindDetailView(DetailsTabIdentifier);
		if(DetailsView.IsValid())
		{
			DetailsView->SetKeyframeHandler(KeyFrameHandler);
			DetailsView->SetIsPropertyReadOnlyDelegate(FIsPropertyReadOnly::CreateRaw(this, &FLevelEditorSequencerIntegration::IsPropertyReadOnly));
		}
	}

	FDelegateHandle OnPropertyEditorOpenedHandle = EditModule.OnPropertyEditorOpened().AddRaw(this, &FLevelEditorSequencerIntegration::OnPropertyEditorOpened);

	auto DeactivateDetailKeyframeHandler =
		[this, OnPropertyEditorOpenedHandle]
		{
			FPropertyEditorModule* EditModulePtr = FModuleManager::Get().GetModulePtr<FPropertyEditorModule>("PropertyEditor");
			if (!EditModulePtr)
			{
				return;
			}

			EditModulePtr->OnPropertyEditorOpened().Remove(OnPropertyEditorOpenedHandle);

			for (const FName& DetailsTabIdentifier : DetailsTabIdentifiers)
			{
				TSharedPtr<IDetailsView> DetailsView = EditModulePtr->FindDetailView(DetailsTabIdentifier);
				if (DetailsView.IsValid())
				{
					if (DetailsView->GetKeyframeHandler() == KeyFrameHandler)
					{
						DetailsView->SetKeyframeHandler(nullptr);
					}

					DetailsView->GetIsPropertyReadOnlyDelegate().Unbind();
				}
			}
		};

	FName DetailHandlerRefreshName("DetailHandlerRefresh");
	auto RefreshDetailHandler =
		[]
		{
			FPropertyEditorModule* EditModulePtr = FModuleManager::Get().GetModulePtr<FPropertyEditorModule>("PropertyEditor");
			if (!EditModulePtr)
			{
				return;
			}

			for (const FName& DetailsTabIdentifier : DetailsTabIdentifiers)
			{
				TSharedPtr<IDetailsView> DetailsView = EditModulePtr->FindDetailView(DetailsTabIdentifier);
				if (DetailsView.IsValid())
				{
					DetailsView->ForceRefresh();
				}
			}
		};

	AcquiredResources.Add(DetailHandlerName, DeactivateDetailKeyframeHandler);
	AcquiredResources.Add(DetailHandlerRefreshName, RefreshDetailHandler);
}

void FLevelEditorSequencerIntegration::OnPropertyEditorOpened()
{
	ActivateDetailHandler();
}

void FLevelEditorSequencerIntegration::BindLevelEditorCommands()
{
	FLevelEditorModule& LevelEditor = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	LevelEditor.GetGlobalLevelEditorActions()->MapAction(
		FSequencerCommands::Get().RecordSelectedActors,
		FExecuteAction::CreateRaw(this, &FLevelEditorSequencerIntegration::RecordSelectedActors));

	AcquiredResources.Add(
		[]
		{
			FLevelEditorModule* LevelEditorPtr = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor"));
			if (LevelEditorPtr)
			{
				LevelEditorPtr->GetGlobalLevelEditorActions()->UnmapAction(FSequencerCommands::Get().RecordSelectedActors);
			}
		}
	);
}

void FLevelEditorSequencerIntegration::RecordSelectedActors()
{
	IterateAllSequencers(
		[&](FSequencer& In, const FLevelEditorSequencerIntegrationOptions& Options)
		{
			if (Options.bCanRecord)
			{
				In.RecordSelectedActors();
			}
		}
	);
}

namespace FaderConstants
{	
	/** The opacity when we are hovered */
	const float HoveredOpacity = 1.0f;
	/** The opacity when we are not hovered */
	const float NonHoveredOpacity = 0.75f;
	/** The amount of time spent actually fading in or out */
	const float FadeTime = 0.15f;
}

/** Wrapper widget allowing us to fade widgets in and out on hover state */
class SFader : public SBorder
{
public:
	SLATE_BEGIN_ARGS(SFader)
		: _Content()
	{}

	SLATE_DEFAULT_SLOT(FArguments, Content)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		FadeInSequence = FCurveSequence(0.0f, FaderConstants::FadeTime);
		FadeOutSequence = FCurveSequence(0.0f, FaderConstants::FadeTime);
		FadeOutSequence.JumpToEnd();

		SBorder::Construct(SBorder::FArguments()
			.BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
			.Padding(0.0f)
			.VAlign(VAlign_Center)
			.ColorAndOpacity(this, &SFader::GetColorAndOpacity)
			.Content()
			[
				InArgs._Content.Widget
			]);
	}

	FLinearColor GetColorAndOpacity() const
	{
		FLinearColor Color = FLinearColor::White;
	
		if(FadeOutSequence.IsPlaying() || !bIsHovered)
		{
			Color.A = FMath::Lerp(FaderConstants::HoveredOpacity, FaderConstants::NonHoveredOpacity, FadeOutSequence.GetLerp());
		}
		else
		{
			Color.A = FMath::Lerp(FaderConstants::NonHoveredOpacity, FaderConstants::HoveredOpacity, FadeInSequence.GetLerp());
		}

		return Color;
	}

	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if(!FSlateApplication::Get().IsUsingHighPrecisionMouseMovment())
		{
			bIsHovered = true;
			if(FadeOutSequence.IsPlaying())
			{
				// Fade out is already playing so just force the fade in curve to the end so we don't have a "pop" 
				// effect from quickly resetting the alpha
				FadeInSequence.JumpToEnd();
			}
			else
			{
				FadeInSequence.Play(AsShared());
			}
		}
	}

	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override
	{
		if(!FSlateApplication::Get().IsUsingHighPrecisionMouseMovment())
		{
			bIsHovered = false;
			FadeOutSequence.Play(AsShared());
		}
	}

private:
	/** Curve sequence for fading out the widget */
	FCurveSequence FadeOutSequence;
	/** Curve sequence for fading in the widget */
	FCurveSequence FadeInSequence;
};

TSharedRef< ISceneOutlinerColumn > FLevelEditorSequencerIntegration::CreateSequencerInfoColumn( ISceneOutliner& SceneOutliner ) const
{
	//@todo only supports the first bound sequencer
	check(BoundSequencers.Num() > 0);
	check(BoundSequencers[0].Sequencer.IsValid());

	return MakeShareable( new Sequencer::FSequencerInfoColumn( SceneOutliner, *BoundSequencers[0].Sequencer.Pin(), BoundSequencers[0].BindingData.Get() ) );
}


void FLevelEditorSequencerIntegration::AttachOutlinerColumn()
{
	FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::LoadModuleChecked< FSceneOutlinerModule >("SceneOutliner");

	SceneOutliner::FColumnInfo ColumnInfo(SceneOutliner::EColumnVisibility::Visible, 15, 
		FCreateSceneOutlinerColumn::CreateRaw( this, &FLevelEditorSequencerIntegration::CreateSequencerInfoColumn));

	SceneOutlinerModule.RegisterDefaultColumnType< Sequencer::FSequencerInfoColumn >(SceneOutliner::FDefaultColumnInfo(ColumnInfo));

	AcquiredResources.Add([=]{ this->DetachOutlinerColumn(); });
}

void FLevelEditorSequencerIntegration::DetachOutlinerColumn()
{
	FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::LoadModuleChecked< FSceneOutlinerModule >("SceneOutliner");

	SceneOutlinerModule.UnRegisterColumnType< Sequencer::FSequencerInfoColumn >();

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");

	// @todo reopen the scene outliner so that is refreshed without the sequencer info column
	TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();
	if (LevelEditorTabManager->FindExistingLiveTab(FName("LevelEditorSceneOutliner")).IsValid())
	{
		if (LevelEditorTabManager.IsValid() && LevelEditorTabManager.Get())
		{
			if (LevelEditorTabManager->GetOwnerTab().IsValid())
			{
				LevelEditorTabManager->InvokeTab(FName("LevelEditorSceneOutliner"))->RequestCloseTab();			
			}
		}
		
		if (LevelEditorTabManager.IsValid() && LevelEditorTabManager.Get())
		{
			if (LevelEditorTabManager->GetOwnerTab().IsValid())
			{
				LevelEditorTabManager->InvokeTab(FName("LevelEditorSceneOutliner"));
			}
		}
	}
}

void FLevelEditorSequencerIntegration::ActivateRealtimeViewports()
{
	for (const FSequencerAndOptions& SequencerAndOptions : BoundSequencers)
	{
		TSharedPtr<FSequencer> Pinned = SequencerAndOptions.Sequencer.Pin();
		if (Pinned.IsValid())
		{
			if (!Pinned.Get()->GetSequencerSettings()->ShouldActivateRealtimeViewports())
			{
				return;
			}
		}
	}

	for (int32 i = 0; i < GEditor->LevelViewportClients.Num(); ++i)
	{
		FLevelEditorViewportClient* LevelVC = GEditor->LevelViewportClients[i];
		if (LevelVC)
		{
			// If there is a director group, set the perspective viewports to realtime automatically.
			if (LevelVC->IsPerspective() && LevelVC->AllowsCinematicControl())
			{				
				// Ensure Realtime is turned on and store the original setting so we can restore it later.
				LevelVC->SetRealtime(true, true);
			}
		}
	}

	AcquiredResources.Add([=]{ this->RestoreRealtimeViewports(); });
}

void FLevelEditorSequencerIntegration::RestoreRealtimeViewports()
{
	// Undo any weird settings to editor level viewports.
	for (int32 i = 0; i < GEditor->LevelViewportClients.Num(); ++i)
	{
		FLevelEditorViewportClient* LevelVC =GEditor->LevelViewportClients[i];
		if (LevelVC)
		{
			// Turn off realtime when exiting.
			if( LevelVC->IsPerspective() && LevelVC->AllowsCinematicControl() )
			{				
				// Specify true so RestoreRealtime will allow us to disable Realtime if it was original disabled
				LevelVC->RestoreRealtime(true);
			}
		}
	}
}

TSharedRef<FExtender> FLevelEditorSequencerIntegration::OnExtendLevelEditorViewMenu(const TSharedRef<FUICommandList> CommandList)
{
	// This would be where you added an extension to the Level Editor dropdown menus if needed.
	TSharedRef<FExtender> Extender(new FExtender());
	return Extender;
}

void FLevelEditorSequencerIntegration::OnTabContentChanged()
{

}

void FLevelEditorSequencerIntegration::OnMapChanged(UWorld* World, EMapChangeType MapChangeType)
{
	if (MapChangeType == EMapChangeType::TearDownWorld)
	{
		IterateAllSequencers(
			[=](FSequencer& In, const FLevelEditorSequencerIntegrationOptions& Options)
		{
			if (Options.bRequiresLevelEvents)
			{
				In.GetEvaluationTemplate().ResetDirectorInstances();
				In.RestorePreAnimatedState();
				In.State.ClearObjectCaches(In);

				// Notify data changed to enqueue an evaluate
				In.NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::Unknown);
			}
		}
		);
	}
}



void FLevelEditorSequencerIntegration::AddSequencer(TSharedRef<ISequencer> InSequencer, const FLevelEditorSequencerIntegrationOptions& Options)
{
	if (!BoundSequencers.Num())
	{
		Initialize();
	}

	KeyFrameHandler->Add(InSequencer);

	auto DerivedSequencerPtr = StaticCastSharedRef<FSequencer>(InSequencer);
	BoundSequencers.Add(FSequencerAndOptions{ DerivedSequencerPtr, Options, FAcquiredResources(), MakeShareable(new FLevelEditorSequencerBindingData) });

	{
		TWeakPtr<ISequencer> WeakSequencer = InSequencer;

		// Set up a callback for when this sequencer changes its time to redraw any non-realtime viewports
		FDelegateHandle EvalHandle = InSequencer->OnGlobalTimeChanged().AddRaw(this, &FLevelEditorSequencerIntegration::OnSequencerEvaluated);

		// Set up a callback for when this sequencer changes to update the sequencer data mapping
		FDelegateHandle BindingsHandle = InSequencer->OnMovieSceneBindingsChanged().AddRaw(this, &FLevelEditorSequencerIntegration::OnMovieSceneBindingsChanged);
		FDelegateHandle DataHandle = InSequencer->OnMovieSceneDataChanged().AddRaw(this, &FLevelEditorSequencerIntegration::OnMovieSceneDataChanged);
		FDelegateHandle AllowEditsModeHandle = InSequencer->GetSequencerSettings()->GetOnAllowEditsModeChanged().AddRaw(this, &FLevelEditorSequencerIntegration::OnAllowEditsModeChanged);

		FDelegateHandle PlayHandle = InSequencer->OnPlayEvent().AddRaw(this, &FLevelEditorSequencerIntegration::OnBeginDeferUpdates);
		FDelegateHandle StopHandle = InSequencer->OnStopEvent().AddRaw(this, &FLevelEditorSequencerIntegration::OnEndDeferUpdates);
		FDelegateHandle BeginScrubbingHandle = InSequencer->OnBeginScrubbingEvent().AddRaw(this, &FLevelEditorSequencerIntegration::OnBeginDeferUpdates);
		FDelegateHandle EndScrubbingHandle = InSequencer->OnEndScrubbingEvent().AddRaw(this, &FLevelEditorSequencerIntegration::OnEndDeferUpdates);

		InSequencer->OnGetIsBindingVisible().BindRaw(this, &FLevelEditorSequencerIntegration::IsBindingVisible);

		BoundSequencers.Last().AcquiredResources.Add(
			[=]
			{
				TSharedPtr<ISequencer> Pinned = WeakSequencer.Pin();
				if (Pinned.IsValid())
				{
					Pinned->OnGlobalTimeChanged().Remove(EvalHandle);
					Pinned->OnMovieSceneBindingsChanged().Remove(BindingsHandle);
					Pinned->OnMovieSceneDataChanged().Remove(DataHandle);
					Pinned->GetSequencerSettings()->GetOnAllowEditsModeChanged().Remove(AllowEditsModeHandle);
					Pinned->OnPlayEvent().Remove(PlayHandle);
					Pinned->OnStopEvent().Remove(StopHandle);
					Pinned->OnBeginScrubbingEvent().Remove(BeginScrubbingHandle);
					Pinned->OnEndScrubbingEvent().Remove(EndScrubbingHandle);
				}
			}
		);
	}

	FSequencerEdMode* SequencerEdMode = (FSequencerEdMode*)(GLevelEditorModeTools().GetActiveMode(FSequencerEdMode::EM_SequencerMode));
	if (SequencerEdMode)
	{
		SequencerEdMode->AddSequencer(DerivedSequencerPtr);
	}

	ActivateRealtimeViewports();
}

void FLevelEditorSequencerIntegration::OnSequencerReceivedFocus(TSharedRef<ISequencer> InSequencer)
{
	FSequencerEdMode* SequencerEdMode = (FSequencerEdMode*)(GLevelEditorModeTools().GetActiveMode(FSequencerEdMode::EM_SequencerMode));
	if (SequencerEdMode)
	{
		SequencerEdMode->OnSequencerReceivedFocus(StaticCastSharedRef<FSequencer>(InSequencer));
	}
}

void FLevelEditorSequencerIntegration::RemoveSequencer(TSharedRef<ISequencer> InSequencer)
{
	// Remove any instances of this sequencer in the array of bound sequencers, along with its resources
	BoundSequencers.RemoveAll(
		[=](const FSequencerAndOptions& In)
		{
			return In.Sequencer == InSequencer;
		}
	);

	FSequencerEdMode* SequencerEdMode = (FSequencerEdMode*)(GLevelEditorModeTools().GetActiveMode(FSequencerEdMode::EM_SequencerMode));
	if (SequencerEdMode)
	{
		SequencerEdMode->RemoveSequencer(StaticCastSharedRef<FSequencer>(InSequencer));
	}

	KeyFrameHandler->Remove(InSequencer);

	auto IsValidSequencer = [](const FSequencerAndOptions& In){ return In.Sequencer.IsValid(); };
	if (!BoundSequencers.FindByPredicate(IsValidSequencer))
	{
		AcquiredResources.Release();
	}
}

void AddActorsToBindingsMapRecursive(FSequencer& Sequencer, FMovieSceneSequenceIDRef SequenceID, TMap<FObjectKey, FString>& ActorBindingsMap)
{
	FMovieSceneRootEvaluationTemplateInstance& RootInstance = Sequencer.GetEvaluationTemplate();

	// Find the sequence that corresponds to the sequence ID
	UMovieSceneSequence* Sequence = RootInstance.GetSequence(SequenceID);
	UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;

	if (MovieScene)
	{
		FString SequenceName = Sequence->GetDisplayName().ToString();

		// Search all possessables
		for (int32 Index = 0; Index < MovieScene->GetPossessableCount(); ++Index)
		{
			FGuid ThisGuid = MovieScene->GetPossessable(Index).GetGuid();

			for (TWeakObjectPtr<> WeakObject : Sequencer.FindBoundObjects(ThisGuid, SequenceID))
			{
				AActor* Actor = Cast<AActor>(WeakObject.Get());
				if (Actor != nullptr)
				{
					FObjectKey ActorKey(Actor);

					if (ActorBindingsMap.Contains(ActorKey))
					{
						ActorBindingsMap[ActorKey] = ActorBindingsMap[ActorKey] + TEXT(", ") + SequenceName;
					}
					else
					{
						ActorBindingsMap.Add(ActorKey, SequenceName);
					}
				}
			}
		}

		// Search all spawnables
		for (int32 Index = 0; Index < MovieScene->GetSpawnableCount(); ++Index)
		{
			FGuid ThisGuid = MovieScene->GetSpawnable(Index).GetGuid();

			for (TWeakObjectPtr<> WeakObject : Sequencer.FindBoundObjects(ThisGuid, SequenceID))
			{
				AActor* Actor = Cast<AActor>(WeakObject.Get());
				if (Actor != nullptr)
				{
					FObjectKey ActorKey(Actor);

					if (ActorBindingsMap.Contains(ActorKey))
					{
						ActorBindingsMap[ActorKey] = ActorBindingsMap[ActorKey] + TEXT(", ") + SequenceName;
					}
					else
					{
						ActorBindingsMap.Add(ActorKey, SequenceName);
					}
				}
			}
		}
	}

	// Recurse into child nodes
	const FMovieSceneSequenceHierarchyNode* Node = RootInstance.GetHierarchy().FindNode(SequenceID);
	if (Node)
	{
		for (FMovieSceneSequenceIDRef ChildID : Node->Children)
		{
			AddActorsToBindingsMapRecursive(Sequencer, ChildID, ActorBindingsMap);
		}
	}
}

void AddPropertiesToBindingsMap(TWeakPtr<FSequencer> Sequencer, UMovieSceneSequence* Sequence, FMovieSceneSequenceIDRef SequenceID, TMap<FObjectKey, TArray<FString> >& PropertyBindingsMap)
{
	UMovieScene* MovieScene = Sequence->GetMovieScene();

	for (FMovieSceneBinding Binding : MovieScene->GetBindings())
	{
		for (UMovieSceneTrack* Track : Binding.GetTracks())
		{
			if (Track->IsA(UMovieScenePropertyTrack::StaticClass()))
			{
				UMovieScenePropertyTrack* PropertyTrack = Cast<UMovieScenePropertyTrack>(Track);
				FName PropertyName = PropertyTrack->GetPropertyName();
				FString PropertyPath = PropertyTrack->GetPropertyPath();

				// Find the property for the given actor
				for (TWeakObjectPtr<> WeakObject : Sequencer.Pin()->FindBoundObjects(Binding.GetObjectGuid(), SequenceID))
				{
					if (WeakObject.IsValid())
					{
						FObjectKey ObjectKey(WeakObject.Get());

						if (!PropertyBindingsMap.Contains(ObjectKey))
						{
							PropertyBindingsMap.Add(ObjectKey);
						}

						PropertyBindingsMap[ObjectKey].Add(PropertyPath);
					}
				}
			}
		}
	}
}


FString FLevelEditorSequencerBindingData::GetLevelSequencesForActor(TWeakPtr<FSequencer> Sequencer, const AActor* InActor)
{
	if (bActorBindingsDirty)
	{
		UpdateActorBindingsData(Sequencer);
	}

	FObjectKey ActorKey(InActor);

	if (ActorBindingsMap.Contains(ActorKey))
	{
		return ActorBindingsMap[ActorKey];
	}
	return FString();
}
	
bool FLevelEditorSequencerBindingData::GetIsPropertyBound(TWeakPtr<FSequencer> Sequencer, const struct FPropertyAndParent& InPropertyAndParent)
{
	if (bPropertyBindingsDirty)
	{
		UpdatePropertyBindingsData(Sequencer);
	}

	for (auto Object : InPropertyAndParent.Objects)
	{
		FObjectKey ObjectKey(Object.Get());

		if (PropertyBindingsMap.Contains(ObjectKey))
		{
			return PropertyBindingsMap[ObjectKey].Contains(InPropertyAndParent.Property.GetName());
		}
	}

	return false;
}

void FLevelEditorSequencerBindingData::UpdateActorBindingsData(TWeakPtr<FSequencer> InSequencer)
{
	static bool bIsReentrant = false;

	if( !bIsReentrant )
	{
		ActorBindingsMap.Empty();
	
		// Finding the bound objects can cause bindings to be evaluated and changed, causing this to be invoked again
		TGuardValue<bool> ReentrantGuard(bIsReentrant, true);
		AddActorsToBindingsMapRecursive(*InSequencer.Pin(), MovieSceneSequenceID::Root, ActorBindingsMap);

		bActorBindingsDirty = false;

		ActorBindingsDataChanged.Broadcast();
	}
}


void FLevelEditorSequencerBindingData::UpdatePropertyBindingsData(TWeakPtr<FSequencer> InSequencer)
{
	static bool bIsReentrant = false;

	if( !bIsReentrant )
	{
		PropertyBindingsMap.Empty();
	
		// Finding the bound objects can cause bindings to be evaluated and changed, causing this to be invoked again
		TGuardValue<bool> ReentrantGuard(bIsReentrant, true);

		FMovieSceneRootEvaluationTemplateInstance& RootTemplate = InSequencer.Pin()->GetEvaluationTemplate();

		for (auto SequenceID : RootTemplate.GetThisFrameMetaData().ActiveSequences)
		{
			UMovieSceneSequence* Sequence = RootTemplate.GetSequence(SequenceID);
			
			AddPropertiesToBindingsMap(InSequencer, Sequence, SequenceID, PropertyBindingsMap);
		}

		bPropertyBindingsDirty = false;

		PropertyBindingsDataChanged.Broadcast();
	}
}

bool FLevelEditorSequencerIntegration::IsPropertyReadOnly(const FPropertyAndParent& InPropertyAndParent)
{
	for (const FSequencerAndOptions& SequencerAndOptions : BoundSequencers)
	{
		TSharedPtr<FSequencer> Pinned = SequencerAndOptions.Sequencer.Pin();
		if (Pinned.IsValid())
		{
			if (Pinned.Get()->GetAllowEditsMode() == EAllowEditsMode::AllowLevelEditsOnly)
			{
				if (SequencerAndOptions.BindingData.Get().GetIsPropertyBound(SequencerAndOptions.Sequencer, InPropertyAndParent))
				{
					return true;
				}
			}
		}
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
