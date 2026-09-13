// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

#include "IPAddress.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

namespace UECP
{

	inline bool IsLocalPortHeld(int32 Port)
	{
		ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (!SS) return false;

		const TSharedRef<FInternetAddr> Addr = SS->CreateInternetAddr();
		bool bIpValid = false;
		Addr->SetIp(TEXT("127.0.0.1"), bIpValid);
		Addr->SetPort(Port);
		if (!bIpValid) return false;

		FSocket* Sock = SS->CreateSocket(NAME_Stream, TEXT("UECPPortProbe"), false);
		if (!Sock) return false;

		Sock->SetNonBlocking(true);
		Sock->Connect(*Addr);
		const bool bWritable = Sock->Wait(ESocketWaitConditions::WaitForWrite,
			FTimespan::FromMilliseconds(100));
		const bool bConnected = bWritable && Sock->GetConnectionState() == SCS_Connected;
		Sock->Close();
		SS->DestroySocket(Sock);
		return bConnected;
	}
}
