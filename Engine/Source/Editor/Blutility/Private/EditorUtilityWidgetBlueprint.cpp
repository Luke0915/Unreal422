// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EditorUtilityWidgetBlueprint.h"
#include "WidgetBlueprint.h"
#include "Editor.h"
#include "EditorUtilityWidget.h"
#include "IBlutilityModule.h"
#include "Modules/ModuleManager.h"
#include "LevelEditor.h"




/////////////////////////////////////////////////////
// UEditorUtilityWidgetBlueprint

UEditorUtilityWidgetBlueprint::UEditorUtilityWidgetBlueprint(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

void UEditorUtilityWidgetBlueprint::BeginDestroy()
{
	// prevent the cleanup script from running on editor shutdown
	if (!GIsRequestingExit)
	{
		IBlutilityModule* BlutilityModule = FModuleManager::GetModulePtr<IBlutilityModule>("Blutility");
		if (BlutilityModule)
		{
			BlutilityModule->RemoveLoadedScriptUI(this);
		}

		FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor"));
		if (LevelEditorModule)
		{
			TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule->GetLevelEditorTabManager();
			if (LevelEditorTabManager.IsValid())
			{
				LevelEditorTabManager->UnregisterTabSpawner(RegistrationName);
			}
		}
	}

	Super::BeginDestroy();
}


TSharedRef<SDockTab> UEditorUtilityWidgetBlueprint::SpawnEditorUITab(const FSpawnTabArgs& SpawnTabArgs)
{
	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab);

	TSharedRef<SWidget> TabWidget = CreateUtilityWidget();
	SpawnedTab->SetContent(TabWidget);
	
	SpawnedTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateUObject(this, &UEditorUtilityWidgetBlueprint::UpdateRespawnListIfNeeded));
	CreatedTab = SpawnedTab;
	
	GEditor->OnBlueprintReinstanced().AddUObject(this, &UEditorUtilityWidgetBlueprint::RegenerateCreatedTab);

	return SpawnedTab;
}

TSharedRef<SWidget> UEditorUtilityWidgetBlueprint::CreateUtilityWidget()
{
	TSharedRef<SWidget> TabWidget = SNullWidget::NullWidget;

	UClass* BlueprintClass = GeneratedClass;
	TSubclassOf<UEditorUtilityWidget> WidgetClass = BlueprintClass;
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!CreatedUMGWidget && World)
	{
		CreatedUMGWidget = CreateWidget<UEditorUtilityWidget>(World, WidgetClass);
	}

	if (CreatedUMGWidget)
	{
		TabWidget = SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.HAlign(HAlign_Fill)
			[
				CreatedUMGWidget->TakeWidget()
			];
	}
	return TabWidget;
}

void UEditorUtilityWidgetBlueprint::RegenerateCreatedTab()
{
	if (CreatedTab.IsValid())
	{
		TSharedRef<SWidget> TabWidget = CreateUtilityWidget();
		CreatedTab.Pin()->SetContent(TabWidget);
	}
}

void UEditorUtilityWidgetBlueprint::UpdateRespawnListIfNeeded(TSharedRef<SDockTab> TabBeingClosed)
{
	const UEditorUtilityWidget* EditorUtilityWidget = GeneratedClass->GetDefaultObject<UEditorUtilityWidget>();
	if (EditorUtilityWidget && EditorUtilityWidget->ShouldAlwaysReregisterWithWindowsMenu() == false)
	{
		IBlutilityModule* BlutilityModule = FModuleManager::GetModulePtr<IBlutilityModule>("Blutility");
		BlutilityModule->RemoveLoadedScriptUI(this);
	}
	CreatedUMGWidget = nullptr;
}

void UEditorUtilityWidgetBlueprint::GetReparentingRules(TSet< const UClass* >& AllowedChildrenOfClasses, TSet< const UClass* >& DisallowedChildrenOfClasses) const
{
	AllowedChildrenOfClasses.Empty();
	AllowedChildrenOfClasses.Add(UEditorUtilityWidget::StaticClass());
}

#if WITH_EDITORONLY_DATA
void UEditorUtilityWidgetBlueprint::LoadModulesRequiredForCompilation()
{
	Super::LoadModulesRequiredForCompilation();

	static const FName ModuleName(TEXT("Blutility"));
	FModuleManager::Get().LoadModule(ModuleName);
}
#endif //WITH_EDITORONLY_DATA

