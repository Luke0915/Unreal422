// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "IPAddress.h"
#include "OSCMessage.h"
#include "OSCBundle.h"
#include "OSCClient.generated.h"

class FSocket;

UCLASS(BlueprintType)
class OSC_API UOSCClient : public UObject
{
	GENERATED_BODY()

public:
	UOSCClient();
	virtual ~UOSCClient() {}

	/** Set's the OSC Client IP address and port. */
	void SetTarget(FIPv4Address InIPAddress, uint32_t Port);

	/** Send OSC message over the network with a specific address. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	void SendOSCMessage(FString Address, UPARAM(ref) FOSCMessage& Msg);

	/** Send OSC Bundle over the network. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	void SendOSCBundle(UPARAM(ref) FOSCBundle& Bundle);

protected:

	void BeginDestroy() override;
	
	/** Stop and tidy up network socket. */
	void Stop();
	
	/** Send OSC packet data. */
	void SendPacket(FOSCPacket* Packet, char* Data, int32 Size);

	/** Socket used to send the OSC packets. */
	FSocket* Socket;

	/** IP Address used by socket. */
	TSharedPtr<FInternetAddr> IPAddress;
};
