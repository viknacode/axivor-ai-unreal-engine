// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

#if PLATFORM_MAC

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include <sys/stat.h>

namespace UECPMacIOSDetectionFix
{

	inline FString GetIdeviceIdPath()
	{
		return FPaths::ConvertRelativePathToFull(
			FPaths::EngineDir() / TEXT("Extras/ThirdPartyNotUE/libimobiledevice/Mac/idevice_id"));
	}

	inline bool IsUniversalMachO(const FString& Path)
	{
		IFileHandle* H = FPlatformFileManager::Get().GetPlatformFile().OpenRead(*Path);
		if (!H) return false;
		uint8 Magic[4] = {0};
		const bool bRead = H->Read(Magic, sizeof(Magic));
		delete H;
		if (!bRead) return false;
		return Magic[0] == 0xCA && Magic[1] == 0xFE && Magic[2] == 0xBA
			&& (Magic[3] == 0xBE || Magic[3] == 0xBF);
	}

	inline bool IsShellScript(const FString& Path)
	{
		IFileHandle* H = FPlatformFileManager::Get().GetPlatformFile().OpenRead(*Path);
		if (!H) return false;
		uint8 Magic[2] = {0};
		const bool bRead = H->Read(Magic, sizeof(Magic));
		delete H;
		return bRead && Magic[0] == '#' && Magic[1] == '!';
	}

	inline void ApplyIfNeeded()
	{
		const FString BinPath = GetIdeviceIdPath();

		IFileManager& FM = IFileManager::Get();
		if (!FM.FileExists(*BinPath))
		{
			return;
		}

		if (IsShellScript(BinPath))
		{
			return;
		}

		if (IsUniversalMachO(BinPath))
		{
			return;
		}

		const FString BackupPath = BinPath + TEXT(".orig");
		if (!FM.FileExists(*BackupPath))
		{
			if (FM.Copy(*BackupPath, *BinPath,  false) != COPY_OK)
			{
				UE_LOG(LogUECPCore, Warning,
					TEXT("Mac iOS-detection fix: failed to back up '%s'. Skipping fix."), *BinPath);
				return;
			}
		}

		const FString Stub = TEXT("#!/bin/sh\nexit 0\n");
		if (!FFileHelper::SaveStringToFile(Stub, *BinPath, FFileHelper::EEncodingOptions::ForceAnsi))
		{
			UE_LOG(LogUECPCore, Warning,
				TEXT("Mac iOS-detection fix: failed to write stub at '%s'. ")
				TEXT("Restoring backup. Editor will crash on iOS device poll."), *BinPath);
			FM.Copy(*BinPath, *BackupPath,  true);
			return;
		}

		chmod(TCHAR_TO_ANSI(*BinPath), 0755);

		UE_LOG(LogUECPCore, Display,
			TEXT("Mac iOS-detection fix: stubbed Epic's x86_64-only idevice_id at '%s'. ")
			TEXT("Original backed up to '%s.orig'. This prevents the Apple Silicon iOS-detection crash."),
			*BinPath, *BinPath);
	}
}

#endif
