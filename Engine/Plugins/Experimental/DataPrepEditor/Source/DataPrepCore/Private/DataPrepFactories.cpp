// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataPrepFactories.h"

#include "DataPrepAsset.h"
#include "DataPrepContentConsumer.h"

#include "AssetRegistryModule.h"
#include "AssetTypeCategories.h"
#include "UObject/UObjectIterator.h"

UDataprepAssetFactory::UDataprepAssetFactory()
{
	SupportedClass = UDataprepAsset::StaticClass();

	bCreateNew = true;
	bText = false;
	bEditorImport = false;
}

UObject * UDataprepAssetFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext *Warn)
{
	check( InClass->IsChildOf( UDataprepAsset::StaticClass() ) );
	UDataprepAsset* DataprepAsset = NewObject<UDataprepAsset>(InParent, InClass, InName, Flags | RF_Transactional);

	// Set DataprepAsset's consumer to the first registered consumer
	for( TObjectIterator< UClass > It ; It ; ++It )
	{
		UClass* CurrentClass = (*It);

		if ( !CurrentClass->HasAnyClassFlags( CLASS_Abstract ) )
		{
			if( CurrentClass->IsChildOf( UDataprepContentConsumer::StaticClass() ) )
			{
				DataprepAsset->Consumer = NewObject< UDataprepContentConsumer >( DataprepAsset->GetOutermost(), CurrentClass, NAME_None, RF_Transactional );
				check( DataprepAsset->Consumer );

				FAssetRegistryModule::AssetCreated( DataprepAsset->Consumer );
				DataprepAsset->Consumer->MarkPackageDirty();

				break;
			}
		}
	}

	// #ueent_todo: Temp code for the nodes development

	return DataprepAsset;
}
