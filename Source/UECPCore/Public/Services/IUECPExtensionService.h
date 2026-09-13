// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Misc/Optional.h"
#include "Mcp/UECPMcpClient.h"

enum class EUECPExtensionState : uint8
{

	Disabled,

	PreconditionsFailed,

	LicenseLocked,

	Loaded,

	Connecting,

	Failed,

	EngineVersionMismatch,
};

struct FUECPExtensionDescriptor
{

	int32 ApiVersion = 0;

	FName ExtensionId;

	FText DisplayName;
	FText Description;

	FText Category;

	TArray<FString> RequiredPlugins;

	TArray<FString> RequiredExtensions;

	FString MinEngineVersion;

	FString MaxEngineVersion;

	FName RequiredModuleName;

	bool bRequiresLicense = false;

	FName LicenseFeatureId;

	bool bDefaultEnabled = false;

	TArray<FName> OwnedUmbrellas;

	TArray<FName> OwnedTools;

	TFunction<void()> ApplyProjectSetup;

	TFunction<void()> RemoveProjectSetup;

	bool bIsThirdParty = false;

	FString AuthorName;

	FString WebsiteUrl;

	FString IconUrl;

	TOptional<FUECPMcpServerSpec> McpServer;

	TMap<FName, FString> UmbrellaDocs;

	FString CatalogBlurb;
};

struct UECPCORE_API FUECPToolCatalogEntry
{

	FString Name;

	FString Description;

	bool    bIsUmbrella = true;

	TSharedPtr<FJsonObject> InputSchema;

	FName   OwningExtensionId;
};

class UECPCORE_API IUECPExtensionService
{
public:
	virtual ~IUECPExtensionService() = default;

	virtual void RegisterExtension(FUECPExtensionDescriptor Descriptor) = 0;

	virtual void UnregisterExtension(FName ExtensionId) = 0;

	// Returns a snapshot copy taken under the registry lock. Returning by value (rather
	// than a reference/pointer into internal storage) means a concurrent RegisterExtension
	// that reallocates the backing array can never dangle a caller's descriptor.
	virtual TArray<FUECPExtensionDescriptor> GetExtensions() const = 0;

	virtual TOptional<FUECPExtensionDescriptor> FindExtension(FName ExtensionId) const = 0;

	virtual TOptional<FUECPExtensionDescriptor> FindExtensionByUmbrella(FName UmbrellaName) const = 0;

	virtual TOptional<FUECPExtensionDescriptor> FindExtensionByTool(FName ToolName) const = 0;

	virtual EUECPExtensionState GetExtensionState(FName ExtensionId) const = 0;

	virtual bool IsUmbrellaProvidedByLoadedExtension(FName UmbrellaName) const = 0;

	virtual bool IsExtensionEnabled(FName ExtensionId) const = 0;

	virtual bool SetExtensionEnabled(FName ExtensionId, bool bEnabled, FString& OutError,
		TArray<FString>* OutMissingPlugins = nullptr, bool bRunRemoveSetup = false) = 0;

	virtual void SetExtensionEnabledAsync(FName ExtensionId, bool bEnable,
		TFunction<void(bool , FString , TArray<FString> )> OnComplete,
		bool bRunRemoveSetup = false)
	{
		FString Err;
		TArray<FString> Missing;
		const bool bOk = SetExtensionEnabled(ExtensionId, bEnable, Err, &Missing, bRunRemoveSetup);
		if (OnComplete) OnComplete(bOk, MoveTemp(Err), MoveTemp(Missing));
	}

	virtual void RefreshAll() = 0;

	struct FPendingChange
	{
		FName             ExtensionId;
		bool              bEnabling = true;
		TArray<FString>   RequiredPluginsToEnable;
	};

	virtual void QueueExtensionChange(FName ExtensionId, bool bEnable) = 0;

	virtual void UnqueueExtensionChange(FName ExtensionId) = 0;

	virtual void CancelPendingChanges() = 0;

	virtual TArray<FPendingChange> GetPendingChanges() const = 0;

	virtual bool PendingChangesRequireRestart() const = 0;

	virtual bool ApplyPendingChanges(FString& OutError) = 0;

	virtual TSharedPtr<class FUECPMcpClient> GetMcpClient(FName ExtensionId) const = 0;

	virtual FString GetMcpLastError(FName ExtensionId) const = 0;

	virtual bool ShouldShowUmbrella(FName UmbrellaName) const = 0;

	virtual TArray<FUECPToolCatalogEntry> GetExtensionCatalogEntries(
		const TSet<FName>& ExcludeUmbrellaNames = {}) const = 0;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnExtensionStateChanged, FName );

	virtual FOnExtensionStateChanged& OnExtensionStateChanged() = 0;
};

UECPCORE_API const TCHAR* LexToString(EUECPExtensionState State);
