// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace UECPACPSchema
{

	TSharedRef<FJsonObject> BuildInitializeParams(const FString& ClientName,
		const FString& ClientTitle, const FString& ClientVersion);

	struct FMcpServerSpec
	{

		FString Name;

		FString HttpUrl;
		TArray<TPair<FString, FString>> HttpHeaders;

		FString StdioCommand;
		TArray<FString> StdioArgs;
		TArray<TPair<FString, FString>> StdioEnv;
	};

	TSharedRef<FJsonObject> BuildSessionNewParams(const FString& Cwd,
		const TArray<FMcpServerSpec>& McpSpecs);

	TSharedRef<FJsonObject> BuildSessionPromptParams(const FString& SessionId,
		const FString& UserText);

	struct FPromptContentBlock
	{

		FString Type;
		FString Text;
		FString Base64Data;
		FString MimeType;
	};

	TSharedRef<FJsonObject> BuildSessionPromptParamsRich(const FString& SessionId,
		const TArray<FPromptContentBlock>& Blocks);

	TSharedRef<FJsonObject> BuildSessionCancelParams(const FString& SessionId);

	TSharedRef<FJsonObject> BuildSessionSetModeParams(const FString& SessionId, const FString& ModeId);

	TSharedRef<FJsonObject> BuildSessionSetModelParams(const FString& SessionId, const FString& ModelId);

	TSharedRef<FJsonObject> BuildSessionSetOptionParams(const FString& SessionId,
		const FString& OptionId, const FString& Value);

	bool ReadInitializeResult(const TSharedPtr<FJsonObject>& Result,
		int32& OutNegotiatedVersion, FString& OutAgentName, FString& OutAgentVersion);

	bool ReadSessionNewResult(const TSharedPtr<FJsonObject>& Result, FString& OutSessionId);

	struct FConfigOption
	{
		FString Id;
		FString Name;
		FString Description;
	};

	struct FConfigSection
	{
		FString Id;
		FString Name;
		FString CurrentValue;
		TArray<FConfigOption> Options;
	};

	struct FConfigSnapshot
	{
		TArray<FConfigOption> Models;
		TArray<FConfigOption> Modes;
		FString CurrentModel;
		FString CurrentMode;
		TArray<FConfigSection> OtherSections;
	};
	bool ReadSessionNewConfig(const TSharedPtr<FJsonObject>& Result, FConfigSnapshot& OutConfig);

	bool ReadSessionUpdateDiscriminator(const TSharedPtr<FJsonObject>& NotifParams,
		FString& OutDiscriminator, TSharedPtr<FJsonObject>& OutUpdateObj);

	bool ReadPermissionRequestParams(const TSharedPtr<FJsonObject>& Params,
		FString& OutToolCallId, FString& OutToolTitle, FString& OutToolKind,
		TArray<TPair<FString, FString>>& OutOptions);

	TSharedRef<FJsonObject> BuildPermissionResultSelected(const FString& OptionId);
	TSharedRef<FJsonObject> BuildPermissionResultCancelled();
}
