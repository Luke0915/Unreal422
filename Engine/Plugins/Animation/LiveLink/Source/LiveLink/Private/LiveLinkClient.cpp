// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LiveLinkClient.h"

#include "Async/Async.h"
#include "Misc/App.h"
#include "Misc/Guid.h"
#include "Misc/ScopeLock.h"
#include "LiveLinkAnimationVirtualSubject.h"
#include "LiveLinkPreset.h"
#include "LiveLinkRole.h"
#include "LiveLinkRoleTrait.h"
#include "LiveLinkSettings.h"
#include "LiveLinkSourceCollection.h"
#include "LiveLinkSourceFactory.h"
#include "LiveLinkSourceSettings.h"
#include "LiveLinkSubject.h"
#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkAnimationTypes.h"
#include "Roles/LiveLinkCameraTypes.h"
#include "Roles/LiveLinkCameraRole.h"
#include "Stats/Stats.h"
#include "Stats/Stats2.h"
#include "TimeSynchronizationSource.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"


/**
 * Declare stats to see what takes up time in LiveLink
 */
DECLARE_CYCLE_STAT(TEXT("LiveLink - Push StaticData"), STAT_LiveLink_PushStaticData, STATGROUP_LiveLink);
DECLARE_CYCLE_STAT(TEXT("LiveLink - Push FrameData"), STAT_LiveLink_PushFrameData, STATGROUP_LiveLink);
DECLARE_CYCLE_STAT(TEXT("LiveLink - Client - Tick"), STAT_LiveLink_Client_Tick, STATGROUP_LiveLink);
DECLARE_CYCLE_STAT(TEXT("LiveLink - EvaluateFrame"), STAT_LiveLink_EvaluateFrame, STATGROUP_LiveLink);

DEFINE_LOG_CATEGORY(LogLiveLink);

FLiveLinkClient::FLiveLinkClient()
{
	Collection = MakeUnique<FLiveLinkSourceCollection>();
}

FLiveLinkClient::~FLiveLinkClient()
{
	if (Collection)
	{
		FScopeLock Lock(&CollectionAccessCriticalSection);
		Collection->Shutdown();
	}
}

void FLiveLinkClient::Tick(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_LiveLink_Client_Tick);

	FScopeLock Lock(&CollectionAccessCriticalSection);
	DoPendingWork();
	UpdateSources();
	BuildThisTicksSubjectSnapshot();
}

void FLiveLinkClient::DoPendingWork()
{
	check(Collection);

	// Remove Sources and Subjects
	Collection->RemovePendingKill();

	{
		// Add new Subject static data
		for (FPendingSubjectStatic& SubjectStatic : SubjectStaticToPush)
		{
			PushSubjectStaticData_Internal(MoveTemp(SubjectStatic));
		}
		SubjectStaticToPush.Reset();

		// Add new Subject frame data
		for (FPendingSubjectFrame& SubjectFrame : SubjectFrameToPush)
		{
			PushSubjectFrameData_Internal(MoveTemp(SubjectFrame));
		}
		SubjectFrameToPush.Reset();
	}
}

void FLiveLinkClient::UpdateSources()
{
	for (FLiveLinkCollectionSourceItem& SourceItem : Collection->GetSources())
	{
		SourceItem.Source->Update();
	}
}

void FLiveLinkClient::BuildThisTicksSubjectSnapshot()
{
	check(Collection);

	EnabledSubjects.Reset();

	// Update the Live Subject before the Virtual Subject
	for (const FLiveLinkCollectionSubjectItem& SubjectItem : Collection->GetSubjects())
	{
		if (FLiveLinkSubject* LiveSubject = SubjectItem.GetLiveSubject())
		{
			if (SubjectItem.bEnabled)
			{
				LiveSubject->CacheSettings(GetSourceSettings(SubjectItem.Key.Source), SubjectItem.GetLinkSettings());
				LiveSubject->Update();
				EnabledSubjects.Add(SubjectItem.Key.SubjectName, SubjectItem.Key);
			}
			else
			{
				LiveSubject->ClearFrames();
			}
		}
	}

	for (const FLiveLinkCollectionSubjectItem& SubjectItem : Collection->GetSubjects())
	{
		if (ULiveLinkVirtualSubject* VSubject = SubjectItem.GetVirtualSubject())
		{
			if (SubjectItem.bEnabled)
			{
				VSubject->Update();
				EnabledSubjects.Add(SubjectItem.Key.SubjectName, SubjectItem.Key);
			}
			else
			{
				VSubject->ClearFrames();
			}
		}
	}
}

FGuid FLiveLinkClient::AddSource(TSharedPtr<ILiveLinkSource> InSource)
{
	check(Collection);

	FGuid Guid;
	if (Collection->FindSource(InSource) == nullptr)
	{
		Guid = FGuid::NewGuid();

		FLiveLinkCollectionSourceItem Data;
		Data.Guid = Guid;
		Data.Source = InSource;
		{
			UClass* CustomSettingsClass = InSource->GetCustomSettingsClass();
			if (CustomSettingsClass && !CustomSettingsClass->IsChildOf<ULiveLinkSourceSettings>())
			{
				UE_LOG(LogLiveLink, Warning, TEXT("Custom Setting Failure: Source '%s' settings class '%s' does not derive from ULiveLinkSourceSettings"), *InSource->GetSourceType().ToString(), *CustomSettingsClass->GetName());
				CustomSettingsClass = nullptr;
			}

			UClass* SettingsClass = CustomSettingsClass ? CustomSettingsClass : ULiveLinkSourceSettings::StaticClass();
			Data.Setting = NewObject<ULiveLinkSourceSettings>(GetTransientPackage(), SettingsClass);
		}
		Collection->AddSource(Data);

		InSource->ReceiveClient(this, Data.Guid);
		InSource->InitializeSettings(Data.Setting);
	}
	return Guid;
}

bool FLiveLinkClient::CreateSource(const FLiveLinkSourcePreset& InSourcePreset)
{
	check(Collection);

	if (InSourcePreset.Settings == nullptr)
	{
		UE_LOG(LogLiveLink, Warning, TEXT("Create Source Failure: The settings are not defined."));
		return false;
	}

	if (InSourcePreset.Settings->Factory.Get() == nullptr || InSourcePreset.Settings->Factory.Get() == ULiveLinkSourceFactory::StaticClass())
	{
		UE_LOG(LogLiveLink, Warning, TEXT("Create Source Failure: The factory is not defined."));
		return false;
	}

	if (InSourcePreset.Guid == FLiveLinkSourceCollection::VirtualSubjectGuid)
	{
		UE_LOG(LogLiveLink, Warning, TEXT("Create Source Failure: Can't create a virtual source."));
		return false;
	}

	if (!InSourcePreset.Guid.IsValid())
	{
		UE_LOG(LogLiveLink, Warning, TEXT("Create Source Failure: The guid is invalid."));
		return false;
	}

	if (Collection->FindSource(InSourcePreset.Guid) != nullptr)
	{
		UE_LOG(LogLiveLink, Warning, TEXT("Create Source Failure: The guid already exist."));
		return false;
	}

	FLiveLinkCollectionSourceItem Data;
	Data.Guid = InSourcePreset.Guid;
	Data.Source = InSourcePreset.Settings->Factory.Get()->GetDefaultObject<ULiveLinkSourceFactory>()->CreateSource(InSourcePreset.Settings->ConnectionString);
	if (!Data.Source.IsValid())
	{
		UE_LOG(LogLiveLink, Warning, TEXT("Create Source Failure: The source couldn't be created by the factory."));
		return false;
	}

	UClass* CustomSettingsClass = Data.Source->GetCustomSettingsClass();
	UClass* SettingsClass = CustomSettingsClass ? CustomSettingsClass : ULiveLinkSourceSettings::StaticClass();
	if (!InSourcePreset.Settings->GetClass()->IsChildOf(SettingsClass))
	{
		UE_LOG(LogLiveLink, Warning, TEXT("Custom Setting Failure: Settings class does not derive from the source custom settings class."));
		return false;
	}
	Data.Setting = DuplicateObject<ULiveLinkSourceSettings>(InSourcePreset.Settings, GetTransientPackage());
	
	Collection->AddSource(Data);

	Data.Source->ReceiveClient(this, Data.Guid);
	Data.Source->InitializeSettings(Data.Setting);

	return true;
}

void FLiveLinkClient::RemoveSource(TSharedPtr<ILiveLinkSource> InSource)
{
	check(Collection);
	if (FLiveLinkCollectionSourceItem* SourceItem = Collection->FindSource(InSource))
	{
		SourceItem->bPendingKill = true;
	}
}

void FLiveLinkClient::RemoveSource(FGuid InEntryGuid)
{
	check(Collection);
	if (FLiveLinkCollectionSourceItem* SourceItem = Collection->FindSource(InEntryGuid))
	{
		SourceItem->bPendingKill = true;
	}
}

void FLiveLinkClient::RemoveAllSources()
{
	check(Collection);
	for (FLiveLinkCollectionSourceItem& SourceItem : Collection->GetSources())
	{
		SourceItem.bPendingKill = true;
	}
}

bool FLiveLinkClient::HasSourceBeenAdded(TSharedPtr<ILiveLinkSource> InSource) const
{
	check(Collection);
	if (FLiveLinkCollectionSourceItem* SourceItem = Collection->FindSource(InSource))
	{
		return !SourceItem->bPendingKill;
	}
	return false;
}

TArray<FGuid> FLiveLinkClient::GetSources() const
{
	check(Collection);

	TArray<FGuid> Result;
	for (const FLiveLinkCollectionSourceItem& SourceItem : Collection->GetSources())
	{
		if (!SourceItem.bPendingKill && !SourceItem.IsVirtualSource())
		{
			Result.Add(SourceItem.Guid);
		}
	}
	return Result;
}

FLiveLinkSourcePreset FLiveLinkClient::GetSourcePreset(FGuid InSourceGuid, UObject* InDuplicatedObjectOuter) const
{
	check(Collection);

	UObject* DuplicatedObjectOuter = InDuplicatedObjectOuter ? InDuplicatedObjectOuter : GetTransientPackage();

	FLiveLinkSourcePreset SourcePreset;
	if (FLiveLinkCollectionSourceItem* SourceItem = Collection->FindSource(InSourceGuid))
	{
		if (!SourceItem->IsVirtualSource() && SourceItem->Setting)
		{
			SourcePreset.Guid = SourceItem->Guid;
			SourcePreset.Settings = DuplicateObject<ULiveLinkSourceSettings>(SourceItem->Setting, DuplicatedObjectOuter);
		}
	}
	return SourcePreset;
}

void FLiveLinkClient::PushSubjectStaticData_AnyThread(const FLiveLinkSubjectKey& InSubjectKey, TSubclassOf<ULiveLinkRole> InRole, FLiveLinkStaticDataStruct&& InStaticData)
{
	FPendingSubjectStatic SubjectStatic{ InSubjectKey, InRole, MoveTemp(InStaticData) };

	FScopeLock Lock(&CollectionAccessCriticalSection);
	SubjectStaticToPush.Add(MoveTemp(SubjectStatic));
}

void FLiveLinkClient::PushSubjectStaticData_Internal(FPendingSubjectStatic&& SubjectStaticData)
{
	SCOPE_CYCLE_COUNTER(STAT_LiveLink_PushStaticData);

	check(Collection);

	if (!FLiveLinkRoleTrait::Validate(SubjectStaticData.Role, SubjectStaticData.StaticData))
	{
		if (SubjectStaticData.Role == nullptr)
		{
			UE_LOG(LogLiveLink, Warning, TEXT("Trying to add unsupported frame data type."));
		}
		else
		{
			UE_LOG(LogLiveLink, Warning, TEXT("Trying to add unsupported frame data type to role '%s'."), *SubjectStaticData.Role->GetName());
		}
		return;
	}
	
	const FLiveLinkCollectionSourceItem* SourceItem = Collection->FindSource(SubjectStaticData.SubjectKey.Source);
	if (SourceItem == nullptr || SourceItem->bPendingKill)
	{
		return;
	}

	FLiveLinkSubject* LiveLinkSubject = nullptr;
	if (FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(SubjectStaticData.SubjectKey))
	{
		LiveLinkSubject = SubjectItem->GetLiveSubject();

		if (LiveLinkSubject->GetRole() != SubjectStaticData.Role)
		{
			UE_LOG(LogLiveLink, Warning, TEXT("Subject '%s' of role '%s' is trying to change its role to '%s'."), *SubjectStaticData.SubjectKey.SubjectName.ToString(), *LiveLinkSubject->GetRole().GetDefaultObject()->GetDisplayName().ToString(), *SubjectStaticData.Role.GetDefaultObject()->GetDisplayName().ToString());
			return;
		}
	}
	else
	{
		const FLiveLinkRoleProjectSetting DefaultSetting = GetDefault<ULiveLinkSettings>()->GetDefaultSettingForRole(SubjectStaticData.Role.Get());

		UClass* SettingClass = DefaultSetting.SettingClass.Get();
		if (SettingClass == nullptr)
		{
			SettingClass = ULiveLinkSubjectSettings::StaticClass();
		}

		ULiveLinkSubjectSettings* SubjectSettings = NewObject<ULiveLinkSubjectSettings>(GetTransientPackage(), SettingClass);

		SubjectSettings->Role = SubjectStaticData.Role;
		if (DefaultSetting.FrameInterpolationProcessor != nullptr)
		{
			SubjectSettings->InterpolationProcessor = NewObject<ULiveLinkFrameInterpolationProcessor>(GetTransientPackage(), DefaultSetting.FrameInterpolationProcessor.Get());
		}
		for (TSubclassOf<ULiveLinkFramePreProcessor> PreProcessor : DefaultSetting.FramePreProcessors)
		{
			if (PreProcessor != nullptr)
			{
				SubjectSettings->PreProcessors.Add(NewObject<ULiveLinkFramePreProcessor>(GetTransientPackage(), PreProcessor.Get()));
			}
		}

		bool bEnabled = Collection->FindEnabledSubject(SubjectStaticData.SubjectKey.SubjectName) == nullptr;
		FLiveLinkCollectionSubjectItem CollectionSubjectItem(SubjectStaticData.SubjectKey, MakeUnique<FLiveLinkSubject>(), SubjectSettings, bEnabled);
		CollectionSubjectItem.GetLiveSubject()->Initialize(SubjectStaticData.SubjectKey, SubjectStaticData.Role.Get(), this);

		LiveLinkSubject = CollectionSubjectItem.GetLiveSubject();

		Collection->AddSubject(MoveTemp(CollectionSubjectItem));
	}

	if (LiveLinkSubject)
	{
		if (const FSubjectFramesReceivedHandles* Handles = SubjectFrameReceivedHandles.Find(SubjectStaticData.SubjectKey.SubjectName))
		{
			Handles->OnStaticDataReceived.Broadcast(SubjectStaticData.SubjectKey, SubjectStaticData.Role, SubjectStaticData.StaticData);
		}

		LiveLinkSubject->SetStaticData(SubjectStaticData.Role, MoveTemp(SubjectStaticData.StaticData));
	}
}

void FLiveLinkClient::PushSubjectFrameData_AnyThread(const FLiveLinkSubjectKey& InSubjectKey, FLiveLinkFrameDataStruct&& InFrameData)
{
	FPendingSubjectFrame SubjectFrame{ InSubjectKey, MoveTemp(InFrameData) };

	FScopeLock Lock(&CollectionAccessCriticalSection);
	SubjectFrameToPush.Add(MoveTemp(SubjectFrame));
}

void FLiveLinkClient::PushSubjectFrameData_Internal(FPendingSubjectFrame&& SubjectFrameData)
{
	SCOPE_CYCLE_COUNTER(STAT_LiveLink_PushFrameData);

	check(Collection);

	const FLiveLinkCollectionSourceItem* SourceItem = Collection->FindSource(SubjectFrameData.SubjectKey.Source);
	if (SourceItem == nullptr || SourceItem->bPendingKill)
	{
		return;
	}

	if (FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(SubjectFrameData.SubjectKey))
	{
		if (SubjectItem->bEnabled && !SubjectItem->bPendingKill)
		{
			if (FLiveLinkSubject* LinkSubject = SubjectItem->GetLiveSubject())
			{
				if (const FSubjectFramesReceivedHandles* Handles = SubjectFrameReceivedHandles.Find(SubjectFrameData.SubjectKey.SubjectName))
				{
					Handles->OnFrameDataReceived.Broadcast(SubjectItem->Key, SubjectItem->GetSubject()->GetRole(), SubjectFrameData.FrameData);
				}

				LinkSubject->AddFrameData(MoveTemp(SubjectFrameData.FrameData));
			}
		}
	}
}

bool FLiveLinkClient::CreateSubject(const FLiveLinkSubjectPreset& InSubjectPreset)
{
	check(Collection);

	if (InSubjectPreset.Role.Get() == nullptr || InSubjectPreset.Role.Get() == ULiveLinkRole::StaticClass())
	{
		UE_LOG(LogLiveLink, Warning, TEXT("Create Subject Failure: The role is not defined."));
		return false;
	}

	if (InSubjectPreset.Key.Source == FLiveLinkSourceCollection::VirtualSubjectGuid && InSubjectPreset.VirtualSubject == nullptr)
	{
		UE_LOG(LogLiveLink, Warning, TEXT("Create Source Failure: Can't create an empty virtual source."));
		return false;
	}

	if (InSubjectPreset.Key.Source != FLiveLinkSourceCollection::VirtualSubjectGuid && InSubjectPreset.VirtualSubject != nullptr)
	{
		UE_LOG(LogLiveLink, Warning, TEXT("Create Source Failure: Can't create a virtual source in another source."));
		return false;
	}

	if (InSubjectPreset.Key.SubjectName.IsNone())
	{
		UE_LOG(LogLiveLink, Warning, TEXT("Create Subject Failure: The subject name is invalid."));
		return false;
	}

	FLiveLinkCollectionSourceItem* SourceItem = Collection->FindSource(InSubjectPreset.Key.Source);
	if (SourceItem == nullptr || SourceItem->bPendingKill)
	{
		UE_LOG(LogLiveLink, Warning, TEXT("Create Subject Failure: The source doesn't exist."));
		return false;
	}

	FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(InSubjectPreset.Key);
	if (SubjectItem != nullptr)
	{
		FScopeLock Lock(&CollectionAccessCriticalSection);
		if (SubjectItem->bPendingKill)
		{
			Collection->RemoveSubject(InSubjectPreset.Key);
		}
		else
		{
			UE_LOG(LogLiveLink, Warning, TEXT("Create Subject Failure: The subject already exist."));
			return false;
		}
	}

	if (InSubjectPreset.VirtualSubject)
	{
		bool bEnabled = false;
		ULiveLinkVirtualSubject* VSubject = DuplicateObject<ULiveLinkVirtualSubject>(InSubjectPreset.VirtualSubject, GetTransientPackage());
		FLiveLinkCollectionSubjectItem VSubjectData(InSubjectPreset.Key.SubjectName, VSubject, bEnabled);
		VSubject->Initialize(VSubjectData.Key, VSubject->GetRole(), this);

		FScopeLock Lock(&CollectionAccessCriticalSection);
		Collection->AddSubject(MoveTemp(VSubjectData));
		Collection->SetSubjectEnabled(InSubjectPreset.Key, InSubjectPreset.bEnabled);
	}
	else
	{
		ULiveLinkSubjectSettings* SubjectSettings = nullptr;
		if (InSubjectPreset.Settings)
		{
			SubjectSettings = DuplicateObject<ULiveLinkSubjectSettings>(InSubjectPreset.Settings, GetTransientPackage());
		}
		else
		{
			SubjectSettings = NewObject<ULiveLinkSubjectSettings>();
		}

		bool bEnabled = false;
		FLiveLinkCollectionSubjectItem CollectionSubjectItem(InSubjectPreset.Key, MakeUnique<FLiveLinkSubject>(), SubjectSettings, bEnabled);
		CollectionSubjectItem.GetLiveSubject()->Initialize(InSubjectPreset.Key, InSubjectPreset.Role.Get(), this);

		FScopeLock Lock(&CollectionAccessCriticalSection);
		Collection->AddSubject(MoveTemp(CollectionSubjectItem));
		Collection->SetSubjectEnabled(InSubjectPreset.Key, InSubjectPreset.bEnabled);
	}
	return true;
}

void FLiveLinkClient::RemoveSubject_AnyThread(const FLiveLinkSubjectKey& InSubjectKey)
{
	FScopeLock Lock(&CollectionAccessCriticalSection);

	if (Collection)
	{
		if (FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(InSubjectKey))
		{
			SubjectItem->bPendingKill = true;
		}
	}
}

void FLiveLinkClient::AddVirtualSubject(FLiveLinkSubjectName InNewVirtualSubjectName, TSubclassOf<ULiveLinkVirtualSubject> InVirtualSubjectClass)
{
	if (Collection && !InNewVirtualSubjectName.IsNone() && InVirtualSubjectClass != nullptr)
	{
		bool bFoundVirtualSubjectWithSameName = nullptr != Collection->GetSubjects().FindByPredicate(
			[InNewVirtualSubjectName](const FLiveLinkCollectionSubjectItem& Other)
		{
			return Other.Key.SubjectName == InNewVirtualSubjectName && Other.GetVirtualSubject() != nullptr;
		});

		if (!bFoundVirtualSubjectWithSameName)
		{
			ULiveLinkVirtualSubject* VSubject = NewObject<ULiveLinkVirtualSubject>(GetTransientPackage(), InVirtualSubjectClass.Get());
			bool bEnabled = Collection->FindEnabledSubject(InNewVirtualSubjectName) == nullptr;
			FLiveLinkCollectionSubjectItem VSubjectData(InNewVirtualSubjectName, VSubject, bEnabled);

			VSubject->Initialize(VSubjectData.Key, VSubject->GetRole(), this);

			FScopeLock Lock(&CollectionAccessCriticalSection);
			Collection->AddSubject(MoveTemp(VSubjectData));
		}
		else
		{
			UE_LOG(LogLiveLink, Warning, TEXT("The virtual subject '%s' could not be created."), *InNewVirtualSubjectName.Name.ToString());
		}
	}
}

void FLiveLinkClient::ClearSubjectsFrames_AnyThread(FLiveLinkSubjectName InSubjectName)
{
	FScopeLock Lock(&CollectionAccessCriticalSection);

	// Use the subject enabled for at this frame
	if (FLiveLinkSubjectKey* SubjectKey = EnabledSubjects.Find(InSubjectName))
	{
		ClearSubjectsFrames_AnyThread(*SubjectKey);
	}
}

void FLiveLinkClient::ClearSubjectsFrames_AnyThread(const FLiveLinkSubjectKey& InSubjectKey)
{
	FScopeLock Lock(&CollectionAccessCriticalSection);

	if (Collection)
	{
		if (FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(InSubjectKey))
		{
			SubjectItem->GetSubject()->ClearFrames();
		}
	}
}

void FLiveLinkClient::ClearAllSubjectsFrames_AnyThread()
{
	FScopeLock Lock(&CollectionAccessCriticalSection);

	if (Collection)
	{
		for (const FLiveLinkCollectionSubjectItem& SubjectItem : Collection->GetSubjects())
		{
			SubjectItem.GetSubject()->ClearFrames();
		}
	}
}

FLiveLinkSubjectPreset FLiveLinkClient::GetSubjectPreset(const FLiveLinkSubjectKey& InSubjectKey, UObject* InDuplicatedObjectOuter) const
{
	UObject* DuplicatedObjectOuter = InDuplicatedObjectOuter ? InDuplicatedObjectOuter : GetTransientPackage();

	FLiveLinkSubjectPreset SubjectPreset;
	if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(InSubjectKey))
	{
		SubjectPreset.Key = SubjectItem->Key;
		SubjectPreset.Role = SubjectItem->GetSubject()->GetRole();
		SubjectPreset.bEnabled = SubjectItem->bEnabled;
		if (SubjectItem->GetVirtualSubject() != nullptr)
		{
			SubjectPreset.VirtualSubject = DuplicateObject<ULiveLinkVirtualSubject>(SubjectItem->GetVirtualSubject(), DuplicatedObjectOuter);
		}
		else
		{
			SubjectPreset.Settings = DuplicateObject<ULiveLinkSubjectSettings>(SubjectItem->GetLinkSettings(), DuplicatedObjectOuter);
		}
	}
	return SubjectPreset;
}

TArray<FLiveLinkSubjectKey> FLiveLinkClient::GetSubjects(bool bIncludeDisabledSubject, bool bIncludeVirtualSubject) const
{
	TArray<FLiveLinkSubjectKey> SubjectEntries;
	SubjectEntries.Reserve(Collection->GetSubjects().Num());

	for (const FLiveLinkCollectionSubjectItem& SubjectItem : Collection->GetSubjects())
	{
		if ((SubjectItem.bEnabled || bIncludeDisabledSubject) && (bIncludeVirtualSubject || SubjectItem.GetVirtualSubject() == nullptr))
		{
			SubjectEntries.Add(SubjectItem.Key);
		}
	}

	return SubjectEntries;
}

bool FLiveLinkClient::IsSubjectValid(const FLiveLinkSubjectKey& InSubjectKey) const
{
	if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(InSubjectKey))
	{
		if (SubjectItem->GetSubject()->HasValidFrameSnapshot())
		{
			if (FLiveLinkSubject* LiveSubject = SubjectItem->GetLiveSubject())
			{
				return (FApp::GetCurrentTime() - LiveSubject->GetLastPushTime()) < GetDefault<ULiveLinkSettings>()->GetTimeWithoutFrameToBeConsiderAsInvalid();
			}
		}
	}
	return false;
}

bool FLiveLinkClient::IsSubjectValid(FLiveLinkSubjectName InSubjectName) const
{
	if (const FLiveLinkSubjectKey* FoundSubjectKey = EnabledSubjects.Find(InSubjectName))
	{
		return IsSubjectValid(*FoundSubjectKey);
	}
	return false;
}

bool FLiveLinkClient::IsSubjectEnabled(const FLiveLinkSubjectKey& InSubjectKey) const
{
	return Collection->IsSubjectEnabled(InSubjectKey);
}

bool FLiveLinkClient::IsSubjectEnabled(FLiveLinkSubjectName InSubjectName) const
{
	return EnabledSubjects.Find(InSubjectName) != nullptr;
}

void FLiveLinkClient::SetSubjectEnabled(const FLiveLinkSubjectKey& InSubjectKey, bool bInEnabled)
{
	Collection->SetSubjectEnabled(InSubjectKey, bInEnabled);
}

bool FLiveLinkClient::IsSubjectTimeSynchronized(const FLiveLinkSubjectKey& InSubjectKey) const
{
	if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(InSubjectKey))
	{
		if (FLiveLinkSubject* LinkSubject = SubjectItem->GetLiveSubject())
		{
			return LinkSubject->IsTimeSynchronized();
		}
	}
	return false;
}

bool FLiveLinkClient::IsSubjectTimeSynchronized(FLiveLinkSubjectName InSubjectName) const
{
	if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindEnabledSubject(InSubjectName))
	{
		if (FLiveLinkSubject* LinkSubject = SubjectItem->GetLiveSubject())
		{
			return LinkSubject->IsTimeSynchronized();
		}
	}
	return false;
}

TSubclassOf<ULiveLinkRole> FLiveLinkClient::GetSubjectRole(const FLiveLinkSubjectKey& InSubjectKey) const
{
	if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(InSubjectKey))
	{
		return SubjectItem->GetSubject()->GetRole();
	}

	return TSubclassOf<ULiveLinkRole>();
}

TSubclassOf<ULiveLinkRole> FLiveLinkClient::GetSubjectRole(FLiveLinkSubjectName InSubjectName) const
{
	if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindEnabledSubject(InSubjectName))
	{
		return SubjectItem->GetSubject()->GetRole();
	}

	return TSubclassOf<ULiveLinkRole>();
}

bool FLiveLinkClient::DoesSubjectSupportsRole(const FLiveLinkSubjectKey& InSubjectKey, TSubclassOf<ULiveLinkRole> InSupportedRole) const
{
	if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(InSubjectKey))
	{
		return SubjectItem->GetSubject()->SupportsRole(InSupportedRole);
	}

	return false;
}

TArray<FLiveLinkSubjectKey> FLiveLinkClient::GetSubjectsSupportingRole(TSubclassOf<ULiveLinkRole> InSupportedRole, bool bIncludeDisabledSubject, bool bIncludeVirtualSubject) const
{
	TArray<FLiveLinkSubjectKey> SubjectKeys;
	for (const FLiveLinkCollectionSubjectItem& SubjectItem : Collection->GetSubjects())
	{
		if (SubjectItem.GetSubject()->SupportsRole(InSupportedRole))
		{
			if ((SubjectItem.bEnabled || bIncludeDisabledSubject) && (bIncludeVirtualSubject || SubjectItem.GetVirtualSubject() == nullptr))
			{
				SubjectKeys.Add(SubjectItem.Key);
			}
		}
	}
	return SubjectKeys;
}

bool FLiveLinkClient::EvaluateFrame_AnyThread(FLiveLinkSubjectName InSubjectName, TSubclassOf<ULiveLinkRole> InDesiredRole, FLiveLinkSubjectFrameData& OutFrame)
{
	SCOPE_CYCLE_COUNTER(STAT_LiveLink_EvaluateFrame);

	FScopeLock Lock(&CollectionAccessCriticalSection);

	// Used the cached enabled list
	if (FLiveLinkSubjectKey* FoundSubjectKey = EnabledSubjects.Find(InSubjectName))
	{
		if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(*FoundSubjectKey))
		{
			return SubjectItem->GetSubject()->EvaluateFrame(InDesiredRole, OutFrame);
		}
	}

	return false;
}

bool FLiveLinkClient::EvaluateFrameAtWorldTime_AnyThread(FLiveLinkSubjectName InSubjectName, double InWorldTime, TSubclassOf<ULiveLinkRole> InDesiredRole, FLiveLinkSubjectFrameData& OutFrame)
{
	SCOPE_CYCLE_COUNTER(STAT_LiveLink_EvaluateFrame);

	FScopeLock Lock(&CollectionAccessCriticalSection);

	// Used the cached enabled list
	if (FLiveLinkSubjectKey* FoundSubjectKey = EnabledSubjects.Find(InSubjectName))
	{
		if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(*FoundSubjectKey))
		{
			if (FLiveLinkSubject* LinkSubject = SubjectItem->GetLiveSubject())
			{
				return LinkSubject->EvaluateFrameAtWorldTime(InWorldTime, InDesiredRole, OutFrame);
			}
			else
			{
				return SubjectItem->GetSubject()->EvaluateFrame(InDesiredRole, OutFrame);
			}
		}
	}

	return false;
}

bool FLiveLinkClient::EvaluateFrameAtSceneTime_AnyThread(FLiveLinkSubjectName InSubjectName, const FTimecode& InSceneTime, TSubclassOf<ULiveLinkRole> InDesiredRole, FLiveLinkSubjectFrameData& OutFrame)
{
	SCOPE_CYCLE_COUNTER(STAT_LiveLink_EvaluateFrame);

	FScopeLock Lock(&CollectionAccessCriticalSection);

	// Used the cached enabled list
	if (FLiveLinkSubjectKey* FoundSubjectKey = EnabledSubjects.Find(InSubjectName))
	{
		if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(*FoundSubjectKey))
		{
			if (FLiveLinkSubject* LinkSubject = SubjectItem->GetLiveSubject())
			{
				return LinkSubject->EvaluateFrameAtSceneTime(InSceneTime, InDesiredRole, OutFrame);
			}
			else
			{
				return SubjectItem->GetSubject()->EvaluateFrame(InDesiredRole, OutFrame);
			}
		}
	}

	return false;
}

TArray<FGuid> FLiveLinkClient::GetDisplayableSources() const
{
	TArray<FGuid> Results;

	const TArray<FLiveLinkCollectionSourceItem>& PresetSources = Collection->GetSources();
	Results.Reserve(PresetSources.Num());

	for (const FLiveLinkCollectionSourceItem& Data : PresetSources)
	{
		if (Data.Source->CanBeDisplayedInUI())
		{
			Results.Add(Data.Guid);
		}
	}

	return Results;
}

FLiveLinkSubjectTimeSyncData FLiveLinkClient::GetTimeSyncData(FLiveLinkSubjectName InSubjectName)
{
	FScopeLock Lock(&CollectionAccessCriticalSection);

	if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindEnabledSubject(InSubjectName))
	{
		if (FLiveLinkSubject* LinkSubject = SubjectItem->GetLiveSubject())
		{
			return LinkSubject->GetTimeSyncData();
		}
	}

	return FLiveLinkSubjectTimeSyncData();
}

FText FLiveLinkClient::GetSourceType(FGuid InEntryGuid) const
{
	if (const FLiveLinkCollectionSourceItem* SourceItem = Collection->FindSource(InEntryGuid))
	{
		return SourceItem->Source->GetSourceType();
	}
	return FText(NSLOCTEXT("TempLocTextLiveLink", "InvalidSourceType", "Invalid Source Type"));
}

FText FLiveLinkClient::GetSourceMachineName(FGuid InEntryGuid) const
{
	if (const FLiveLinkCollectionSourceItem* SourceItem = Collection->FindSource(InEntryGuid))
	{
		return SourceItem->Source->GetSourceMachineName();
	}
	return FText(NSLOCTEXT("TempLocTextLiveLink", "InvalidSourceMachineName", "Invalid Source Machine Name"));
}

FText FLiveLinkClient::GetSourceStatus(FGuid InEntryGuid) const
{
	if (const FLiveLinkCollectionSourceItem* SourceItem = Collection->FindSource(InEntryGuid))
	{
		return SourceItem->Source->GetSourceStatus();
	}
	return FText(NSLOCTEXT("TempLocTextLiveLink", "InvalidSourceStatus", "Invalid Source Status"));
}

bool FLiveLinkClient::IsVirtualSubject(const FLiveLinkSubjectKey& InSubjectKey) const
{
	if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(InSubjectKey))
	{
		return SubjectItem->GetVirtualSubject() != nullptr;
	}
	return false;
}

void FLiveLinkClient::OnPropertyChanged(FGuid InEntryGuid, const FPropertyChangedEvent& InPropertyChangedEvent)
{
	if (const FLiveLinkCollectionSourceItem* SourceItem = Collection->FindSource(InEntryGuid))
	{
		SourceItem->Source->OnSettingsChanged(SourceItem->Setting, InPropertyChangedEvent);
	}
}

ULiveLinkSourceSettings* FLiveLinkClient::GetSourceSettings(FGuid InEntryGuid) const
{
	if (const FLiveLinkCollectionSourceItem* SourceItem = Collection->FindSource(InEntryGuid))
	{
		if (!SourceItem->IsVirtualSource())
		{
			return SourceItem->Setting;
		}
	}
	return nullptr;
}

UObject* FLiveLinkClient::GetSubjectSettings(const FLiveLinkSubjectKey& InSubjectKey) const
{
	if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(InSubjectKey))
	{
		return SubjectItem->GetSettings();
	}
	return nullptr;
}

bool FLiveLinkClient::RegisterForSubjectFrames(FLiveLinkSubjectName InSubjectName, const FOnLiveLinkSubjectStaticDataReceived::FDelegate& InOnStaticDataReceived, const FOnLiveLinkSubjectFrameDataReceived::FDelegate& InOnFrameDataReceived, FDelegateHandle& OutStaticDataReceivedHandle, FDelegateHandle& OutFrameDataReceivedHandle, TSubclassOf<ULiveLinkRole>& OutSubjectRole, FLiveLinkStaticDataStruct* OutStaticData)
{
	if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindEnabledSubject(InSubjectName))
	{
		if (SubjectItem->GetSubject()->GetStaticData().IsValid())
		{
			//Register both delegates
			FSubjectFramesReceivedHandles& Handles = SubjectFrameReceivedHandles.FindOrAdd(InSubjectName);
			OutStaticDataReceivedHandle = Handles.OnStaticDataReceived.Add(InOnStaticDataReceived);
			OutFrameDataReceivedHandle = Handles.OnFrameDataReceived.Add(InOnFrameDataReceived);

			//Give back the current static data and role associated to the subject
			OutSubjectRole = SubjectItem->GetSubject()->GetRole();

			//Copy the current static data
			if (OutStaticData)
			{
				OutStaticData->InitializeWith(SubjectItem->GetSubject()->GetStaticData());
			}
		}

		return true;
	}

	return false;
}

void FLiveLinkClient::UnregisterSubjectFramesHandle(FLiveLinkSubjectName InSubjectName, FDelegateHandle InStaticDataReceivedHandle, FDelegateHandle InFrameDataReceivedHandle)
{
	if (FSubjectFramesReceivedHandles* Handles = SubjectFrameReceivedHandles.Find(InSubjectName))
	{
		Handles->OnStaticDataReceived.Remove(InStaticDataReceivedHandle);
		Handles->OnFrameDataReceived.Remove(InFrameDataReceivedHandle);
	}
}

void FLiveLinkClient::OnStartSynchronization(FLiveLinkSubjectName InSubjectName, const struct FTimeSynchronizationOpenData& OpenData, const int32 FrameOffset)
{
	FScopeLock Lock(&CollectionAccessCriticalSection);

	if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindEnabledSubject(InSubjectName))
	{
		if (FLiveLinkSubject* LinkSubject = SubjectItem->GetLiveSubject())
		{
			return LinkSubject->OnStartSynchronization(OpenData, FrameOffset);
		}
	}
}

void FLiveLinkClient::OnSynchronizationEstablished(FLiveLinkSubjectName InSubjectName, const struct FTimeSynchronizationStartData& StartData)
{
	FScopeLock Lock(&CollectionAccessCriticalSection);

	if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindEnabledSubject(InSubjectName))
	{
		if (FLiveLinkSubject* LinkSubject = SubjectItem->GetLiveSubject())
		{
			return LinkSubject->OnSynchronizationEstablished(StartData);
		}
	}
}

void FLiveLinkClient::OnStopSynchronization(FLiveLinkSubjectName InSubjectName)
{
	FScopeLock Lock(&CollectionAccessCriticalSection);

	if (const FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindEnabledSubject(InSubjectName))
	{
		if (FLiveLinkSubject* LinkSubject = SubjectItem->GetLiveSubject())
		{
			return LinkSubject->OnStopSynchronization();
		}
	}
}

FSimpleMulticastDelegate& FLiveLinkClient::OnLiveLinkSourcesChanged()
{
	return Collection->OnLiveLinkSourcesChanged();
}

FSimpleMulticastDelegate& FLiveLinkClient::OnLiveLinkSubjectsChanged()
{
	return Collection->OnLiveLinkSubjectsChanged();
}


FOnLiveLinkSourceChangedDelegate& FLiveLinkClient::OnLiveLinkSourceAdded()
{
	return Collection->OnLiveLinkSourceAdded();
}

FOnLiveLinkSourceChangedDelegate& FLiveLinkClient::OnLiveLinkSourceRemoved()
{
	return Collection->OnLiveLinkSourceRemoved();
}

FOnLiveLinkSubjectChangedDelegate& FLiveLinkClient::OnLiveLinkSubjectRemoved()
{
	return Collection->OnLiveLinkSubjectRemoved();
}

FOnLiveLinkSubjectChangedDelegate& FLiveLinkClient::OnLiveLinkSubjectAdded()
{
	return Collection->OnLiveLinkSubjectAdded();
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
/**
 * Function for Deprecation
 */
void FLiveLinkClient::AquireLock_Deprecation() 
{
	CollectionAccessCriticalSection.Lock();
}

void FLiveLinkClient::ReleaseLock_Deprecation()
{
	CollectionAccessCriticalSection.Unlock();
}

FLiveLinkSkeletonStaticData* FLiveLinkClient::GetSubjectAnimationStaticData_Deprecation(const FLiveLinkSubjectKey& InSubjectKey)
{
	FScopeLock Lock(&CollectionAccessCriticalSection);

	if (Collection)
	{
		if (FLiveLinkCollectionSubjectItem* SubjectItem = Collection->FindSubject(InSubjectKey))
		{
			if (SubjectItem->GetSubject()->GetRole() == ULiveLinkAnimationRole::StaticClass())
			{
				return SubjectItem->GetSubject()->GetStaticData().Cast<FLiveLinkSkeletonStaticData>();
			}
		}
	}

	return nullptr;
}


/**
 * Function that are now deprecated
 */
const TArray<FGuid>& FLiveLinkClient::GetSourceEntries() const
{
	static TArray<FGuid> CopiedSources;
	CopiedSources = GetSources();
	return CopiedSources;
}

void FLiveLinkClient::AddVirtualSubject(FName InNewVirtualSubjectName)
{
	AddVirtualSubject(InNewVirtualSubjectName, ULiveLinkAnimationVirtualSubject::StaticClass());
}

/**
 * Deprecated function
 */
void FLiveLinkClient_Base_DEPRECATED::PushSubjectSkeleton(FGuid SourceGuid, FName SubjectName, const FLiveLinkRefSkeleton& RefSkeleton)
{
	// Backward compatibility with old API. Default to pushing animation data

	FLiveLinkSubjectKey Key = FLiveLinkSubjectKey(SourceGuid, SubjectName);


	FLiveLinkStaticDataStruct StaticData(FLiveLinkSkeletonStaticData::StaticStruct());
	FLiveLinkSkeletonStaticData* SkeletonData = StaticData.Cast<FLiveLinkSkeletonStaticData>();
	SkeletonData->SetBoneNames(RefSkeleton.GetBoneNames());
	SkeletonData->SetBoneParents(RefSkeleton.GetBoneParents());
	PushSubjectStaticData_AnyThread(Key, ULiveLinkAnimationRole::StaticClass(), MoveTemp(StaticData));
}

void UpdateForAnimationStatic(TArray<FName>& InOutCurveNames, const TArray<FLiveLinkCurveElement>& InCurveElements)
{
	InOutCurveNames.Reset(InCurveElements.Num());
	for (const FLiveLinkCurveElement& Elem : InCurveElements)
	{
		InOutCurveNames.Add(Elem.CurveName);
	}
}

void FLiveLinkClient_Base_DEPRECATED::PushSubjectData(FGuid InSourceGuid, FName InSubjectName, const FLiveLinkFrameData& InFrameData)
{
	FLiveLinkSubjectKey SubjectKey(InSourceGuid, InSubjectName);

	//Update curve names in the static data for backward compatibility
	int32 NumberOfPropertyNames = 0;
	{
		AquireLock_Deprecation();

		FLiveLinkSkeletonStaticData* AnimationStaticData = GetSubjectAnimationStaticData_Deprecation(SubjectKey);
		if (AnimationStaticData)
		{
			NumberOfPropertyNames = AnimationStaticData->PropertyNames.Num();
			if (NumberOfPropertyNames == 0 && InFrameData.CurveElements.Num() > 0)
			{
				UpdateForAnimationStatic(AnimationStaticData->PropertyNames, InFrameData.CurveElements);
			}
		}

		ReleaseLock_Deprecation();
	}

	//Convert incoming data as animation data
	FLiveLinkFrameDataStruct AnimationStruct(FLiveLinkAnimationFrameData::StaticStruct());
	FLiveLinkAnimationFrameData& NewData = *AnimationStruct.Cast<FLiveLinkAnimationFrameData>();
	NewData.MetaData = InFrameData.MetaData;
	NewData.WorldTime = InFrameData.WorldTime;
	NewData.Transforms = InFrameData.Transforms;

	int32 MaxNumberOfProperties = FMath::Min(NumberOfPropertyNames, InFrameData.CurveElements.Num());
	NewData.PropertyValues.SetNumZeroed(MaxNumberOfProperties);
	for (int32 i = 0; i < MaxNumberOfProperties; ++i)
	{
		NewData.PropertyValues[i] = InFrameData.CurveElements[i].CurveValue;
	}
	PushSubjectFrameData_AnyThread(SubjectKey, MoveTemp(AnimationStruct));
}

void FLiveLinkClient_Base_DEPRECATED::ClearSubject(FName InSubjectName)
{
	bool bRemovedSubject = false;
	{
		TArray<FLiveLinkSubjectKey> AllSubjects = GetSubjects(false, true);
		for (const FLiveLinkSubjectKey& SubjectKey : AllSubjects)
		{
			if (SubjectKey.SubjectName == InSubjectName)
			{
				RemoveSubject_AnyThread(SubjectKey);
			}
		}
	}
}

void FLiveLinkClient_Base_DEPRECATED::GetSubjectNames(TArray<FName>& SubjectNames)
{
	TArray<FLiveLinkSubjectKey> SubjectKeys = GetSubjects(false, true);
	SubjectNames.Reset(SubjectKeys.Num());

	for (const FLiveLinkSubjectKey& SubjectKey : SubjectKeys)
	{
		SubjectNames.Add(SubjectKey.SubjectName);
	}
}

const FLiveLinkSubjectFrame* FLiveLinkClient_Base_DEPRECATED::GetSubjectData(FName InSubjectName)
{
	//Old getters default to Animation role and copies data into old data structure
	UE_LOG(LogLiveLink, Warning, TEXT("Upgrade your code. There is no way to deprecate GetSubjectData without creating new memory."));
	return nullptr;
}

const FLiveLinkSubjectFrame* FLiveLinkClient_Base_DEPRECATED::GetSubjectDataAtWorldTime(FName InSubjectName, double InWorldTime)
{
	//Old getters default to Animation role and copies data into old data structure
	UE_LOG(LogLiveLink, Warning, TEXT("Upgrade your code. There is no way to deprecate GetSubjectDataAtWorldTime without creating new memory."));
	return nullptr;
}

const FLiveLinkSubjectFrame* FLiveLinkClient_Base_DEPRECATED::GetSubjectDataAtSceneTime(FName InSubjectName, const FTimecode& InTimecode)
{
	//Old getters default to Animation role and copies data into old data structure
	UE_LOG(LogLiveLink, Warning, TEXT("Upgrade your code. There is no way to deprecate GetSubjectDataAtSceneTime without creating new memory."));
	return nullptr;
}

const TArray<FLiveLinkFrame>* FLiveLinkClient_Base_DEPRECATED::GetSubjectRawFrames(FName InSubjectName)
{
	UE_LOG(LogLiveLink, Warning, TEXT("Upgrade your code. There is no way to deprecate GetSubjectRawFrames without creating new memory."));
	return nullptr;
}

void FLiveLinkClient_Base_DEPRECATED::ClearSubjectsFrames(FName SubjectName)
{
	ClearSubjectsFrames_AnyThread(SubjectName);
}

void FLiveLinkClient_Base_DEPRECATED::ClearAllSubjectsFrames()
{
	ClearAllSubjectsFrames_AnyThread();
}

void FLiveLinkClient_Base_DEPRECATED::AddSourceToSubjectWhiteList(FName SubjectName, FGuid SourceGuid)
{
	SetSubjectEnabled({ SourceGuid, SubjectName }, true);
}

void FLiveLinkClient_Base_DEPRECATED::RemoveSourceFromSubjectWhiteList(FName SubjectName, FGuid SourceGuid)
{
	SetSubjectEnabled({ SourceGuid, SubjectName}, false);
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS