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

struct FUECPACPAuthMethod
{
	FString Id;
	FString Name;
	FString Description;

	bool bTerminal = false;

	FString TerminalCommand;

	TArray<FString> TerminalArgs;
};

enum class EUECPACPAuthState : uint8
{
	Unknown,
	Connected,
	Disconnected
};

// Never holds a credential: only what the agent reports about who is signed in.
struct FUECPACPAuthStatus
{
	EUECPACPAuthState State = EUECPACPAuthState::Unknown;

	// "probe", "login", "logout", or empty when idle.
	FString BusyOp;

	// "starting" until the agent has answered `initialize`, then "working".
	FString BusyStage;

	FString Email;
	FString Plan;

	bool bSupportsLogout = false;
	TArray<FUECPACPAuthMethod> Methods;

	FString LoginUrl;
	FString Error;

	double LastProbeAt = 0.0;
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
