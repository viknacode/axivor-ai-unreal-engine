// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace NiagaraSchemaTools
{

	UECPNIAGARAEXT_API void HandleGetSystemSchemaFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPNIAGARAEXT_API void HandleGetEmitterSchemaFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPNIAGARAEXT_API void HandleGetRendererSchemaFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPNIAGARAEXT_API void HandleGetDataInterfaceSchemaFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPNIAGARAEXT_API void HandleListRendererClassesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPNIAGARAEXT_API void HandleListDataInterfaceClassesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPNIAGARAEXT_API void HandleAddSetParametersModuleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
}
