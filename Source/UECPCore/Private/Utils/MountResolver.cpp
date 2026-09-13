// Copyright 2026, BlueprintsLab, All rights reserved

#include "Utils/MountResolver.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/PackageName.h"

namespace UECPMountResolver
{
	namespace
	{
		void AppendPluginMounts(TArray<FString>& Out, bool bProjectOnly)
		{
			IPluginManager& PM = IPluginManager::Get();
			for (const TSharedRef<IPlugin>& Plugin : PM.GetEnabledPluginsWithContent())
			{
				if (bProjectOnly && Plugin->GetLoadedFrom() != EPluginLoadedFrom::Project)
				{
					continue;
				}
				FString Mount = Plugin->GetMountedAssetPath();
				if (Mount.EndsWith(TEXT("/")))
				{
					Mount.LeftChopInline(1, EAllowShrinking::No);
				}
				if (!Mount.IsEmpty())
				{
					Out.AddUnique(Mount);
				}
			}
		}
	}

	TArray<FString> GetUserContentMounts()
	{
		TArray<FString> Out;
		Out.Add(TEXT("/Game"));
		AppendPluginMounts(Out,  true);
		return Out;
	}

	TArray<FString> GetAllContentMounts()
	{
		TArray<FString> Out;
		Out.Add(TEXT("/Game"));
		AppendPluginMounts(Out,  false);
		return Out;
	}

	bool IsPathUnderMount(const FString& PackagePath, const TArray<FString>& Mounts)
	{
		for (const FString& Mount : Mounts)
		{
			if (PackagePath.Equals(Mount, ESearchCase::IgnoreCase)) return true;
			if (PackagePath.StartsWith(Mount + TEXT("/"), ESearchCase::IgnoreCase)) return true;
		}
		return false;
	}

	bool IsValidMountedPath(const FString& PackagePath)
	{
		if (PackagePath.IsEmpty() || !PackagePath.StartsWith(TEXT("/"))) return false;

		const FName MountPoint = FPackageName::GetPackageMountPoint(PackagePath,  false);
		return !MountPoint.IsNone();
	}
}
