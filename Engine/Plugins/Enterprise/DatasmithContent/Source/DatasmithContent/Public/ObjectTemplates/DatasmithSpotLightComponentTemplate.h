// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ObjectTemplates/DatasmithObjectTemplate.h"

#include "DatasmithSpotLightComponentTemplate.generated.h"

UCLASS()
class UDatasmithSpotLightComponentTemplate : public UDatasmithObjectTemplate
{
	GENERATED_BODY()

public:
	UDatasmithSpotLightComponentTemplate();

	UPROPERTY()
	float InnerConeAngle;

	UPROPERTY()
	float OuterConeAngle;

	virtual void Apply( UObject* Destination, bool bForce = false ) override;
	virtual void Load( const UObject* Source ) override;
};
