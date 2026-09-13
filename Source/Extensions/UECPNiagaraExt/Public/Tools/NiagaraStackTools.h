// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace NiagaraStackTools
{

	UECPNIAGARAEXT_API void HandleGetStackIssuesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPNIAGARAEXT_API void HandleApplyStackIssueFixFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPNIAGARAEXT_API void HandleGetSystemCompileStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
}
