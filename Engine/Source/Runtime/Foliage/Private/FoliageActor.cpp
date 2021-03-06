// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "FoliageActor.h"
#include "InstancedFoliageActor.h"
#include "FoliageType_Actor.h"

FName FoliageActorTag(TEXT("FoliageActorInstance"));

//
//
// FFoliageActor
void FFoliageActor::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	for (const TWeakObjectPtr<AActor>& ActorPtr : ActorInstances)
	{
		if (AActor* Actor = ActorPtr.Get(false))
		{
			Collector.AddReferencedObject(Actor, InThis);
		}
	}
}

void FFoliageActor::Serialize(FArchive& Ar)
{
	Ar << ActorInstances;
	Ar << ActorClass;
}

void FFoliageActor::DestroyActors(bool bOnLoad)
{
	for (const TWeakObjectPtr<AActor>& ActorPtr : ActorInstances)
	{
		if (AActor* Actor = ActorPtr.Get(false))
		{
			if (bOnLoad)
			{
				Actor->ConditionalPostLoad();
			}
			Actor->GetWorld()->DestroyActor(Actor);
		}
	}
}

#if WITH_EDITOR
bool FFoliageActor::IsInitialized() const
{
	return bInitialized;
}

void FFoliageActor::Initialize(AInstancedFoliageActor* IFA, const UFoliageType* FoliageType)
{
	check(!bInitialized);
	const UFoliageType_Actor* FoliageType_Actor = Cast<UFoliageType_Actor>(FoliageType);
	ActorClass = FoliageType_Actor->ActorClass;
	bInitialized = true;
}

void FFoliageActor::Uninitialize()
{
	check(bInitialized);
	DestroyActors(false);
	ActorInstances.Empty();
	ActorClass = nullptr;
	bInitialized = false;
}

AActor* FFoliageActor::Spawn(AInstancedFoliageActor* IFA, const FFoliageInstance& Instance)
{
	if (ActorClass == nullptr)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags = RF_Transactional;
	SpawnParameters.bHideFromSceneOutliner = true;
	SpawnParameters.OverrideLevel = IFA->GetLevel();
	AActor* NewActor = IFA->GetWorld()->SpawnActor<AActor>(ActorClass, Instance.GetInstanceWorldTransform(), SpawnParameters);
	if (NewActor)
	{
		NewActor->Tags.AddUnique(FoliageActorTag);
	}
	return NewActor;
}

TArray<AActor*> FFoliageActor::GetActorsFromSelectedIndices(const TSet<int32>& SelectedIndices) const
{
	TArray<AActor*> Selection;
	Selection.Reserve(SelectedIndices.Num());
	for (int32 i : SelectedIndices)
	{
		check(i < ActorInstances.Num());
		if (ActorInstances[i].IsValid())
		{
			Selection.Add(ActorInstances[i].Get());
		}
	}

	return Selection;
}

int32 FFoliageActor::GetInstanceCount() const
{
	return ActorInstances.Num();
}

void FFoliageActor::PreAddInstances(AInstancedFoliageActor* IFA, const UFoliageType* FoliageType, int32 Count)
{
	if (!IsInitialized())
	{
		Initialize(IFA, FoliageType);
		check(IsInitialized());
	}
}

void FFoliageActor::AddInstance(AInstancedFoliageActor* IFA, const FFoliageInstance& NewInstance)
{
	ActorInstances.Add(Spawn(IFA, NewInstance));
}

void FFoliageActor::RemoveInstance(int32 InstanceIndex)
{
	AActor* Actor = ActorInstances[InstanceIndex].Get(false);
	ActorInstances.RemoveAtSwap(InstanceIndex);

	if (Actor && Actor->GetWorld())
	{
		Actor->GetWorld()->DestroyActor(Actor, true);
	}
}

void FFoliageActor::SetInstanceWorldTransform(int32 InstanceIndex, const FTransform& Transform, bool bTeleport)
{
	if (AActor* Actor = ActorInstances[InstanceIndex].Get(false))
	{
		Actor->SetActorTransform(Transform);
	}
}

FTransform FFoliageActor::GetInstanceWorldTransform(int32 InstanceIndex) const
{
	if (AActor* Actor = ActorInstances[InstanceIndex].Get(false))
	{
		return Actor->GetTransform();
	}

	return FTransform::Identity;
}

bool FFoliageActor::IsOwnedComponent(const UPrimitiveComponent* Component) const
{
	const AActor* Owner = Component->GetOwner();

	return ActorInstances.Contains(Owner);
}

int32 FFoliageActor::FindIndex(const AActor* InActor) const
{
	return ActorInstances.IndexOfByKey(InActor);
}

int32 FFoliageActor::FindIndex(const UPrimitiveComponent* Component) const
{
	return FindIndex(Component->GetOwner());
}

void FFoliageActor::Refresh(AInstancedFoliageActor* IFA, const TArray<FFoliageInstance>& Instances, bool Async, bool Force)
{
	for (int32 i = 0; i < Instances.Num(); ++i)
	{
		if (ActorInstances[i] == nullptr || ActorInstances[i]->IsPendingKill())
		{
			ActorInstances[i] = Spawn(IFA, Instances[i]);
		}
	}
}

void FFoliageActor::OnHiddenEditorViewMaskChanged(uint64 InHiddenEditorViews)
{
	for (const TWeakObjectPtr<AActor>& ActorPtr : ActorInstances)
	{
		if (AActor* Actor = ActorPtr.Get(false))
		{
			if (Actor->HiddenEditorViews != InHiddenEditorViews)
			{
				Actor->HiddenEditorViews = InHiddenEditorViews;
				Actor->MarkComponentsRenderStateDirty();
			}
		}
	}
}

void FFoliageActor::PostEditUndo(AInstancedFoliageActor* IFA, UFoliageType* FoliageType, const TArray<FFoliageInstance>& Instances, const TSet<int32>& SelectedIndices)
{
	for (int32 i = 0; i < Instances.Num(); ++i)
	{
		SetInstanceWorldTransform(i, Instances[i].GetInstanceWorldTransform(), true);
	}
}

void FFoliageActor::NotifyFoliageTypeChanged(AInstancedFoliageActor* IFA, UFoliageType* FoliageType, const TArray<FFoliageInstance>& Instances, const TSet<int32>& SelectedIndices, bool bSourceChanged)
{
	if (bSourceChanged)
	{
		Reapply(IFA, FoliageType, Instances);
		ApplySelection(true, SelectedIndices);
	}
}

void FFoliageActor::Reapply(AInstancedFoliageActor* IFA, const UFoliageType* FoliageType, const TArray<FFoliageInstance>& Instances)
{
	IFA->Modify();
	DestroyActors(false);
	ActorInstances.Empty();

	if (IsInitialized())
	{
		Uninitialize();
	}
	Initialize(IFA, FoliageType);

	for (int32 i = 0; i < Instances.Num(); ++i)
	{
		ActorInstances.Add(Spawn(IFA, Instances[i]));
	}
}

void FFoliageActor::SelectInstances(bool bSelect, int32 InstanceIndex, int32 Count)
{
	TArray<AActor*> Selection;
	Selection.Reserve(Count);
	check(ActorInstances.Num() >= InstanceIndex + Count);
	for (int32 i = InstanceIndex; i < (InstanceIndex + Count); ++i)
	{
		if (ActorInstances[i].IsValid())
		{
			Selection.Add(ActorInstances[i].Get());
		}
	}
	AInstancedFoliageActor::SelectionChanged.Broadcast(bSelect, Selection);
}

void FFoliageActor::ApplySelection(bool bApply, const TSet<int32>& SelectedIndices)
{
	if (bApply && SelectedIndices.Num() > 0)
	{
		AInstancedFoliageActor::SelectionChanged.Broadcast(true, GetActorsFromSelectedIndices(SelectedIndices));
	}
}

void FFoliageActor::ClearSelection(const TSet<int32>& SelectedIndices)
{
	AInstancedFoliageActor::SelectionChanged.Broadcast(false, GetActorsFromSelectedIndices(SelectedIndices));
}

#endif // WITH_EDITOR