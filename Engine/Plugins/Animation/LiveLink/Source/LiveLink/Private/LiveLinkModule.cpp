﻿// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ILiveLinkModule.h"

#include "Features/IModularFeatures.h"
#include "LiveLinkMotionController.h"

#include "LiveLinkMessageBusDiscoveryManager.h"

#include "LiveLinkClient.h"

/**
 * Implements the Messaging module.
 */

#define LOCTEXT_NAMESPACE "LiveLinkModule"


class FLiveLinkModule
	: public ILiveLinkModule
{
public:
	FLiveLinkClient LiveLinkClient;
	FLiveLinkMotionController LiveLinkMotionController;

	FLiveLinkModule()
		: LiveLinkClient()
		, LiveLinkMotionController(LiveLinkClient)
	{}
	// IModuleInterface interface

	virtual void StartupModule() override
	{
		IModularFeatures::Get().RegisterModularFeature(FLiveLinkClient::ModularFeatureName, &LiveLinkClient);
		LiveLinkMotionController.RegisterController();
		// Create a HeartbeatManager Instance
		FLiveLinkMessageBusDiscoveryManager::Get();
	}

	virtual void ShutdownModule() override
	{
		LiveLinkMotionController.UnregisterController();
		IModularFeatures::Get().UnregisterModularFeature(FLiveLinkClient::ModularFeatureName, &LiveLinkClient);
		delete FLiveLinkMessageBusDiscoveryManager::Get();
	}

	virtual bool SupportsDynamicReloading() override
	{
		return false;
	}
};

IMPLEMENT_MODULE(FLiveLinkModule, LiveLink);

#undef LOCTEXT_NAMESPACE