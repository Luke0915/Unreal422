// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "LocalizationDashboardPrivatePCH.h"
#include "SLocalizationDashboard.h"
#include "SLocalizationDashboardTargetRow.h"
#include "SHyperlink.h"
#include "SLocalizationTargetEditor.h"
#include "SLocalizationTargetStatusButton.h"
#include "LocalizationConfigurationScript.h"
#include "LocalizationCommandletTasks.h"

#define LOCTEXT_NAMESPACE "LocalizationDashboardTargetRow"

TMap< TWeakObjectPtr<ULocalizationTarget>, TWeakPtr<SDockTab> > SLocalizationDashboardTargetRow::TargetToTabMap;

void SLocalizationDashboardTargetRow::Construct(const FTableRowArgs& InArgs, const TSharedRef<STableViewBase>& OwnerTableView, const TSharedRef<IPropertyUtilities>& InPropertyUtilities, const TSharedRef<IPropertyHandle>& InTargetObjectPropertyHandle)
{
	PropertyUtilities = InPropertyUtilities;
	TargetObjectPropertyHandle = InTargetObjectPropertyHandle;

	FSuperRowType::Construct(InArgs, OwnerTableView);
}

TSharedRef<SWidget> SLocalizationDashboardTargetRow::GenerateWidgetForColumn( const FName& ColumnName )
{
	TSharedPtr<SWidget> Return;

	if (ColumnName == "Target")
	{
		// Target Name
		Return = SNew(SHyperlink)
			.Text(this, &SLocalizationDashboardTargetRow::GetTargetName)
			.OnNavigate(this, &SLocalizationDashboardTargetRow::OnNavigate);
	}
	else if(ColumnName == "Status")
	{
		ULocalizationTarget* const LocalizationTarget = GetTarget();
		if (LocalizationTarget)
		{
			// Status icon button.
			Return = SNew(SLocalizationTargetStatusButton, *LocalizationTarget);
		}
	}
	else if (ColumnName == "Cultures")
	{
		// Culture Names
		Return = SNew(STextBlock)
			.Text(this, &SLocalizationDashboardTargetRow::GetCulturesText);
	}
	else if(ColumnName == "WordCount")
	{
		// Progress Bar and Word Counts
		Return = SNew(STextBlock)
			.Text(this, &SLocalizationDashboardTargetRow::GetWordCountText);
	}
	else if(ColumnName == "Actions")
	{
		TSharedRef<SHorizontalBox> HorizontalBox = SNew(SHorizontalBox);
		Return = HorizontalBox;

		// Gather
		HorizontalBox->AddSlot()
			.FillWidth(1.0f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle( FEditorStyle::Get(), TEXT("HoverHintOnly") )
				.ToolTipText( LOCTEXT("GatherButtonLabel", "Gather") )
				.OnClicked(this, &SLocalizationDashboardTargetRow::Gather)
				.Content()
				[
					SNew(SImage)
					.Image( FEditorStyle::GetBrush("LocalizationDashboard.GatherTarget") )
				]
			];

		// Refresh Word Counts
		HorizontalBox->AddSlot()
			.FillWidth(1.0f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle( FEditorStyle::Get(), TEXT("HoverHintOnly") )
				.ToolTipText( LOCTEXT("RefreshWordCountButtonLabel", "Refresh") )
				.OnClicked(this, &SLocalizationDashboardTargetRow::RefreshWordCount)
				.Content()
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("LocalizationDashboard.RefreshWordCount"))
				]
			];

		// Import All
		HorizontalBox->AddSlot()
			.FillWidth(1.0f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle( FEditorStyle::Get(), TEXT("HoverHintOnly") )
				.ToolTipText( LOCTEXT("ImportAllButtonLabel", "Import All") )
				.OnClicked(this, &SLocalizationDashboardTargetRow::ImportAll)
				.Content()
				[
					SNew(SImage)
					.Image( FEditorStyle::GetBrush("LocalizationDashboard.ImportForAllCultures") )
				]
			];

		// Export All
		HorizontalBox->AddSlot()
			.FillWidth(1.0f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle( FEditorStyle::Get(), TEXT("HoverHintOnly") )
				.ToolTipText(LOCTEXT("ExportAllButtonLabel", "Export All"))
				.OnClicked(this, &SLocalizationDashboardTargetRow::ExportAll)
				.Content()
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("LocalizationDashboard.ExportForAllCultures"))
				]
			];

		// Delete Target
		HorizontalBox->AddSlot()
			.FillWidth(1.0f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle( FEditorStyle::Get(), TEXT("HoverHintOnly") )
				.ToolTipText(LOCTEXT("DeleteButtonLabel", "Delete"))
				.OnClicked(this, &SLocalizationDashboardTargetRow::EnqueueDeletion)
				.Content()
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("LocalizationDashboard.DeleteTarget"))
				]
			];
	}

	return Return.IsValid() ? Return.ToSharedRef() : SNullWidget::NullWidget;
}

ULocalizationTarget* SLocalizationDashboardTargetRow::GetTarget() const
{
	if (TargetObjectPropertyHandle.IsValid() && TargetObjectPropertyHandle->IsValidHandle())
	{
		UObject* Object = nullptr;
		if(FPropertyAccess::Result::Success == TargetObjectPropertyHandle->GetValue(Object))
		{
			return Cast<ULocalizationTarget>(Object);
		}
	}
	return nullptr;
}

FLocalizationTargetSettings* SLocalizationDashboardTargetRow::GetTargetSettings() const
{
	ULocalizationTarget* const LocalizationTarget = GetTarget();
	return LocalizationTarget ? &(LocalizationTarget->Settings) : nullptr;
}

FText SLocalizationDashboardTargetRow::GetTargetName() const
{
	ULocalizationTarget* const LocalizationTarget = GetTarget();
	return LocalizationTarget ? FText::FromString(LocalizationTarget->Settings.Name) : FText::GetEmpty();
}

void SLocalizationDashboardTargetRow::OnNavigate()
{
	ULocalizationTarget* const LocalizationTarget = GetTarget();
	if (LocalizationTarget)
	{
		// Create tab if not existent.
		TWeakPtr<SDockTab>& TargetEditorDockTab = TargetToTabMap.FindOrAdd(TWeakObjectPtr<ULocalizationTarget>(LocalizationTarget));

		if (!TargetEditorDockTab.IsValid())
		{
			UProjectLocalizationSettings* ProjectSettings = nullptr;
			const TSharedPtr<IPropertyHandle> TargetObjectsPropertyHandle = TargetObjectPropertyHandle->GetParentHandle(); // TargetObjects
			if (TargetObjectsPropertyHandle.IsValid() && TargetObjectsPropertyHandle->IsValidHandle())
			{
				TArray<UObject*> OuterObjects;
				TargetObjectsPropertyHandle->GetOuterObjects(OuterObjects);
				ProjectSettings = Cast<UProjectLocalizationSettings>(OuterObjects.Top());
			}

			const TSharedRef<SLocalizationTargetEditor> OurTargetEditor = SNew(SLocalizationTargetEditor, ProjectSettings, LocalizationTarget);
			const TSharedRef<SDockTab> NewTargetEditorTab = SNew(SDockTab)
				.TabRole(ETabRole::DocumentTab)
				.Label_Lambda( [LocalizationTarget]
			{
				return LocalizationTarget ? FText::FromString(LocalizationTarget->Settings.Name) : FText::GetEmpty();
			})
				[
					OurTargetEditor
				];

			FGlobalTabmanager::Get()->InsertNewDocumentTab( SLocalizationDashboard::TabName, FTabManager::ESearchPreference::RequireClosedTab, NewTargetEditorTab );
			TargetEditorDockTab = NewTargetEditorTab;
		}
		else
		{
			const TSharedPtr<SDockTab> OldTargetEditorTab = TargetEditorDockTab.Pin();
			FGlobalTabmanager::Get()->DrawAttention(OldTargetEditorTab.ToSharedRef());
		}
	}
}

FText SLocalizationDashboardTargetRow::GetCulturesText() const
{
	FLocalizationTargetSettings* const TargetSettings = GetTargetSettings();
	if (TargetSettings)
	{
		TArray<FString> OrderedCultureNames;
		OrderedCultureNames.Add(TargetSettings->NativeCultureStatistics.CultureName);
		for (auto& ForeignCultureStatistics : TargetSettings->SupportedCulturesStatistics)
		{
			OrderedCultureNames.Add(ForeignCultureStatistics.CultureName);
		}

		FString Result;
		for (FString& CultureName : OrderedCultureNames)
		{
			const FCulturePtr Culture = FInternationalization::Get().GetCulture(CultureName);
			if (Culture.IsValid())
			{
				const FString CultureDisplayName = Culture->GetDisplayName();

				if (!Result.IsEmpty())
				{
					Result.Append(TEXT(", "));
				}

				Result.Append(CultureDisplayName);
			}
		}

		return FText::FromString(Result);
	}

	return FText::GetEmpty();
}

uint32 SLocalizationDashboardTargetRow::GetWordCount() const
{
	FLocalizationTargetSettings* const TargetSettings = GetTargetSettings();
	return TargetSettings ? TargetSettings->NativeCultureStatistics.WordCount : 0;
}

uint32 SLocalizationDashboardTargetRow::GetNativeWordCount() const
{
	FLocalizationTargetSettings* const TargetSettings = GetTargetSettings();
	return TargetSettings ? TargetSettings->NativeCultureStatistics.WordCount : 0;
}

FText SLocalizationDashboardTargetRow::GetWordCountText() const
{
	return FText::AsNumber(GetWordCount());
}

void SLocalizationDashboardTargetRow::UpdateTargetFromReports()
{
	ULocalizationTarget* const LocalizationTarget = GetTarget();
	if (LocalizationTarget)
	{
		//TArray< TSharedPtr<IPropertyHandle> > WordCountPropertyHandles;

		//const TSharedPtr<IPropertyHandle> TargetSettingsPropertyHandle = TargetEditor->GetTargetSettingsPropertyHandle();
		//if (TargetSettingsPropertyHandle.IsValid() && TargetSettingsPropertyHandle->IsValidHandle())
		//{
		//	const TSharedPtr<IPropertyHandle> NativeCultureStatisticsPropertyHandle = TargetSettingsPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLocalizationTargetSettings, NativeCultureStatistics));
		//	if (NativeCultureStatisticsPropertyHandle.IsValid() && NativeCultureStatisticsPropertyHandle->IsValidHandle())
		//	{
		//		const TSharedPtr<IPropertyHandle> WordCountPropertyHandle = NativeCultureStatisticsPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FCultureStatistics, WordCount));
		//		if (WordCountPropertyHandle.IsValid() && WordCountPropertyHandle->IsValidHandle())
		//		{
		//			WordCountPropertyHandles.Add(WordCountPropertyHandle);
		//		}
		//	}

		//	const TSharedPtr<IPropertyHandle> SupportedCulturesStatisticsPropertyHandle = TargetSettingsPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLocalizationTargetSettings, SupportedCulturesStatistics));
		//	if (SupportedCulturesStatisticsPropertyHandle.IsValid() && SupportedCulturesStatisticsPropertyHandle->IsValidHandle())
		//	{
		//		uint32 SupportedCultureCount = 0;
		//		SupportedCulturesStatisticsPropertyHandle->GetNumChildren(SupportedCultureCount);
		//		for (uint32 i = 0; i < SupportedCultureCount; ++i)
		//		{
		//			const TSharedPtr<IPropertyHandle> ElementPropertyHandle = SupportedCulturesStatisticsPropertyHandle->GetChildHandle(i);
		//			if (ElementPropertyHandle.IsValid() && ElementPropertyHandle->IsValidHandle())
		//			{
		//				const TSharedPtr<IPropertyHandle> WordCountPropertyHandle = SupportedCulturesStatisticsPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FCultureStatistics, WordCount));
		//				if (WordCountPropertyHandle.IsValid() && WordCountPropertyHandle->IsValidHandle())
		//				{
		//					WordCountPropertyHandles.Add(WordCountPropertyHandle);
		//				}
		//			}
		//		}
		//	}
		//}

		//for (const TSharedPtr<IPropertyHandle>& WordCountPropertyHandle : WordCountPropertyHandles)
		//{
		//	WordCountPropertyHandle->NotifyPreChange();
		//}
		LocalizationTarget->Settings.UpdateWordCountsFromCSV();
		LocalizationTarget->Settings.UpdateStatusFromConflictReport();
		//for (const TSharedPtr<IPropertyHandle>& WordCountPropertyHandle : WordCountPropertyHandles)
		//{
		//	WordCountPropertyHandle->NotifyPostChange();
		//}
	}
}

FReply SLocalizationDashboardTargetRow::RefreshWordCount()
{
	bool HasSucceeded = false;

	ULocalizationTarget* const LocalizationTarget = GetTarget();
	if (LocalizationTarget)
	{
		const TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
		LocalizationCommandletTasks::GenerateReportsForTarget(ParentWindow.ToSharedRef(), LocalizationTarget->Settings);

		UpdateTargetFromReports();
	}

	return FReply::Handled();
}

FReply SLocalizationDashboardTargetRow::Gather()
{
	ULocalizationTarget* const LocalizationTarget = GetTarget();
	if (LocalizationTarget)
	{
		// Save unsaved packages.
		bool DidPackagesNeedSaving;
		const bool WerePackagesSaved = FEditorFileUtils::SaveDirtyPackages(true, true, true, false, false, &DidPackagesNeedSaving);

		if (DidPackagesNeedSaving && !WerePackagesSaved)
		{
			// Give warning dialog.
			const FText MessageText = NSLOCTEXT("LocalizationCultureActions", "UnsavedPackagesWarningDialogMessage", "There are unsaved changes. These changes may not be gathered from correctly.");
			const FText TitleText = NSLOCTEXT("LocalizationCultureActions", "UnsavedPackagesWarningDialogTitle", "Unsaved Changes Before Gather");
			switch(FMessageDialog::Open(EAppMsgType::OkCancel, MessageText, &TitleText))
			{
			case EAppReturnType::Cancel:
				{
					return FReply::Handled();
				}
				break;
			}
		}

		// Execute gather.
		const TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
		LocalizationCommandletTasks::GatherTarget(ParentWindow.ToSharedRef(), LocalizationTarget->Settings);

		UpdateTargetFromReports();
	}

	return FReply::Handled();
}

FReply SLocalizationDashboardTargetRow::ImportAll()
{
	ULocalizationTarget* const LocalizationTarget = GetTarget();
	if (LocalizationTarget)
	{
		const TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
		LocalizationCommandletTasks::ImportTarget(ParentWindow.ToSharedRef(), LocalizationTarget->Settings);

		UpdateTargetFromReports();
	}

	return FReply::Handled();
}

FReply SLocalizationDashboardTargetRow::ExportAll()
{
	ULocalizationTarget* const LocalizationTarget = GetTarget();
	if (LocalizationTarget)
	{
		const TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
		LocalizationCommandletTasks::ExportTarget(ParentWindow.ToSharedRef(), LocalizationTarget->Settings);
	}

	return FReply::Handled();
}

FReply SLocalizationDashboardTargetRow::EnqueueDeletion()
{
	PropertyUtilities->EnqueueDeferredAction(FSimpleDelegate::CreateSP(this, &SLocalizationDashboardTargetRow::Delete));
	return FReply::Handled();
}

void SLocalizationDashboardTargetRow::Delete()
{
	static bool IsExecuting = false;
	if (!IsExecuting)
	{
		TGuardValue<bool> ReentranceGuard(IsExecuting, true);

		ULocalizationTarget* const LocalizationTarget = GetTarget();
		if (LocalizationTarget)
		{
			FText TitleText;
			FText MessageText;

			// Setup dialog for deleting target.
			const FText FormatPattern = NSLOCTEXT("LocalizationDashboard", "DeleteTargetConfirmationDialogMessage", "This action can not be undone and data will be permanently lost. Are you sure you would like to delete {TargetName}?");
			FFormatNamedArguments Arguments;
			Arguments.Add(TEXT("TargetName"), FText::FromString(LocalizationTarget->Settings.Name));
			MessageText = FText::Format(FormatPattern, Arguments);
			TitleText = NSLOCTEXT("LocalizationDashboard", "DeleteTargetConfirmationDialogTitle", "Confirm Target Deletion");

			switch(FMessageDialog::Open(EAppMsgType::OkCancel, MessageText, &TitleText))
			{
			case EAppReturnType::Ok:
				{
					LocalizationTarget->Settings.DeleteFiles();

					// Close target editor.
					TWeakPtr<SDockTab>& TargetEditorDockTab = TargetToTabMap.FindOrAdd(TWeakObjectPtr<ULocalizationTarget>(LocalizationTarget));
					if (TargetEditorDockTab.IsValid())
					{
						const TSharedPtr<SDockTab> OldTargetEditorTab = TargetEditorDockTab.Pin();
						OldTargetEditorTab->RequestCloseTab();
					}

					// Remove this element from the parent array.
					const TSharedPtr<IPropertyHandle> ParentPropertyHandle = TargetObjectPropertyHandle->GetParentHandle();
					if (ParentPropertyHandle.IsValid() && ParentPropertyHandle->IsValidHandle())
					{
						const TSharedPtr<IPropertyHandleArray> ParentArrayPropertyHandle = ParentPropertyHandle->AsArray();
						if (ParentArrayPropertyHandle.IsValid())
						{
							ParentArrayPropertyHandle->DeleteItem(TargetObjectPropertyHandle->GetIndexInArray());
						}
					}
				}
				break;
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE