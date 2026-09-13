// Copyright 2026, BlueprintsLab, All rights reserved

#include "SessionTokenStore.h"

#include "MCPToolsLog.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

FSessionTokenStore& FSessionTokenStore::Get()
{
	static FSessionTokenStore Instance;
	return Instance;
}

FString FSessionTokenStore::GetTokenFilePath()
{
	const FString Dir = FPaths::Combine(
		FPlatformProcess::UserSettingsDir(),
		TEXT("UECP"));

	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	if (!PF.DirectoryExists(*Dir))
	{
		PF.CreateDirectoryTree(*Dir);
	}
	return FPaths::Combine(Dir, TEXT("session_token.txt"));
}

FString FSessionTokenStore::GenerateNewToken()
{
	const FGuid A = FGuid::NewGuid();
	const FGuid B = FGuid::NewGuid();
	return FString::Printf(TEXT("%s%s"),
		*A.ToString(EGuidFormats::Digits),
		*B.ToString(EGuidFormats::Digits));
}

void FSessionTokenStore::Initialize()
{
	FScopeLock Lock(&TokenLock);
	if (bInitialized) return;

	const FString Path = GetTokenFilePath();
	FString Loaded;
	if (FFileHelper::LoadFileToString(Loaded, *Path))
	{
		Loaded = Loaded.TrimStartAndEnd();
		bool bValidShape = (Loaded.Len() == 64);
		if (bValidShape)
		{
			for (TCHAR Ch : Loaded)
			{
				if (!FChar::IsHexDigit(Ch)) { bValidShape = false; break; }
			}
		}
		if (bValidShape)
		{
			Token = MoveTemp(Loaded);
			bInitialized = true;
			UE_LOG(LogMCPTool, Log, TEXT("Session token loaded from %s"), *Path);
			return;
		}
		UE_LOG(LogMCPTool, Warning,
			TEXT("Session token file at %s was malformed; regenerating."), *Path);
	}

	Token = GenerateNewToken();
	if (!FFileHelper::SaveStringToFile(Token, *Path,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogMCPTool, Warning, TEXT("Session token: write failed for %s"), *Path);
	}
	bInitialized = true;
	UE_LOG(LogMCPTool, Log, TEXT("Session token issued + persisted at %s"), *Path);
}

FString FSessionTokenStore::GetToken() const
{
	FScopeLock Lock(&TokenLock);
	return Token;
}

bool FSessionTokenStore::ValidateBearer(const FString& IncomingToken) const
{
	FScopeLock Lock(&TokenLock);

	if (IncomingToken.Len() != Token.Len() || Token.IsEmpty())
	{
		return false;
	}

	uint32 Diff = 0;
	const TCHAR* A = *Token;
	const TCHAR* B = *IncomingToken;
	for (int32 i = 0; i < Token.Len(); ++i)
	{
		Diff |= (uint32)(A[i] ^ B[i]);
	}
	return Diff == 0;
}

FString FSessionTokenStore::Rotate()
{
	FScopeLock Lock(&TokenLock);

	const FString NewToken = GenerateNewToken();
	const FString Path = GetTokenFilePath();
	if (!FFileHelper::SaveStringToFile(NewToken, *Path,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogMCPTool, Warning, TEXT("Session token: rotate write failed for %s"), *Path);
	}
	Token = NewToken;
	bInitialized = true;
	UE_LOG(LogMCPTool, Log, TEXT("Session token rotated (length=%d)"), Token.Len());
	return Token;
}
