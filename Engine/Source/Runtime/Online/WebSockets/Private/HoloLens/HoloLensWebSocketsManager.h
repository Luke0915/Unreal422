//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#pragma once

#if WITH_WEBSOCKETS

#include "IWebSocketsManager.h"
#include "HoloLensWebSocket.h"

class FHoloLensWebSocketsManager
	: public IWebSocketsManager
{
private:
	// IWebSocketsManager
	virtual void InitWebSockets(TArrayView<const FString> Protocols) override {}
	virtual void ShutdownWebSockets() override {}
	virtual TSharedRef<IWebSocket> CreateWebSocket(const FString& Url, const TArray<FString>& Protocols, const TMap<FString, FString>& UpgradeHeaders) override;
	// End IWebSocketsManager
};

#endif
