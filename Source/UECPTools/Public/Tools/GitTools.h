// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"

namespace GitTools
{
	UECPTOOLS_API FString FindGitBinary();

	UECPTOOLS_API bool IsGitRepository(const FString& ProjectDir);

	UECPTOOLS_API bool RunGitCommand(const FString& Args, const FString& WorkingDir,
		FString& OutStdOut, FString& OutStdErr, int32& OutReturnCode);

	UECPTOOLS_API void HandleGitStatus(const FString& Path, FString& OutJson, FString& OutError);
	UECPTOOLS_API void HandleGitLog(const FString& FilePath, int32 MaxCount, FString& OutJson, FString& OutError);
	UECPTOOLS_API void HandleGitCommit(const FString& Message, const TArray<FString>& Files, bool bStageAll, FString& OutJson, FString& OutError);
	UECPTOOLS_API void HandleGitDiff(const FString& FilePath, const FString& Revision, FString& OutJson, FString& OutError);
	UECPTOOLS_API void HandleGitRevert(const TArray<FString>& Files, FString& OutJson, FString& OutError);
	UECPTOOLS_API void HandleGitInit(const FString& Path, FString& OutJson, FString& OutError);
	UECPTOOLS_API void HandleGitBranch(const FString& SubAction, const FString& BranchName, FString& OutJson, FString& OutError);
	UECPTOOLS_API void HandleGitStash(const FString& SubAction, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleRunCommand(const FString& Command, const FString& WorkDir, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleGitFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
}
