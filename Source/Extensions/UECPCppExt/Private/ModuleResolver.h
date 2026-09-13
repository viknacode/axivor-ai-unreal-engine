// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

namespace ModuleResolver
{
	struct FModuleInfo
	{
		FString Name;
		FString BuildCsPath;
		FString ModuleRoot;
		FString Type;
		bool    bInPlugin = false;
		FString PluginName;

		TArray<FString> PublicDependencyModuleNames;
		TArray<FString> PrivateDependencyModuleNames;
		TArray<FString> PublicIncludePaths;
		TArray<FString> PrivateIncludePaths;
	};

	UECPCPPEXT_API TArray<FModuleInfo> EnumerateProjectModules();

	UECPCPPEXT_API const FModuleInfo* FindPrimaryGameModule(const TArray<FModuleInfo>& Modules);

	UECPCPPEXT_API const FModuleInfo* FindModuleByName(const TArray<FModuleInfo>& Modules, const FString& Name);

	UECPCPPEXT_API const FModuleInfo* FindOwningModule(const TArray<FModuleInfo>& Modules, const FString& AbsoluteFilePath);

	UECPCPPEXT_API FString GetApiMacro(const FString& ModuleName);

	struct FBuildCsPatch
	{
		TArray<FString> AddPublic;
		TArray<FString> AddPrivate;
		TArray<FString> RemovePublic;
		TArray<FString> RemovePrivate;
	};

	UECPCPPEXT_API FString PatchBuildCs(const FString& OriginalBuildCs, const FBuildCsPatch& Patch, bool& bChanged);
}
