// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

enum class EUECPACPToolKind : uint8
{
	Read,
	Edit,
	Delete,
	Move,
	Search,
	Execute,
	Think,
	Fetch,
	Other
};

enum class EUECPACPPermissionKind : uint8
{
	AllowOnce,
	AllowAlways,
	RejectOnce,
	RejectAlways
};

enum class EUECPACPDistributionKind : uint8
{
	None,
	Npx,
	Uvx,
	Binary
};

struct FUECPACPBinaryDistribution
{

	FString Platform;

	FString ArchiveUrl;

	FString Cmd;

	TArray<FString> Args;
};

struct FUECPACPAgentEntry
{
	FString Id;
	FString Name;
	FString Version;
	FString Description;
	FString IconUrl;
	FString RepositoryUrl;

	FString NpxPackage;
	TArray<FString> NpxArgs;

	TArray<FUECPACPBinaryDistribution> Binaries;
};

struct FUECPACPInstallMarker
{
	FString AgentId;
	FString Version;
	EUECPACPDistributionKind Method = EUECPACPDistributionKind::None;

	FString EntrypointCommand;

	TArray<FString> EntrypointArgs;

	FString InstalledAtUtc;
};

struct FUECPACPConfigOption
{
	FString Id;
	FString Name;
	FString Description;
};

struct FUECPACPAgentConfigSection
{
	FString Id;
	FString Name;
	FString CurrentValue;
	TArray<FUECPACPConfigOption> Options;
};

struct FUECPACPAgentConfigSnapshot
{
	FString AgentId;
	TArray<FUECPACPConfigOption> Models;
	TArray<FUECPACPConfigOption> Modes;
	FString CurrentModel;
	FString CurrentMode;

	TArray<FUECPACPAgentConfigSection> OtherSections;
};
