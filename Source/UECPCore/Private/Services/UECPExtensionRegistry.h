// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "Services/IUECPExtensionService.h"
#include "Mcp/UECPMcpClient.h"

class FUECPExtensionRegistryImpl final : public IUECPExtensionService
{
public:
	virtual void RegisterExtension(FUECPExtensionDescriptor Descriptor) override;
	virtual void UnregisterExtension(FName ExtensionId) override;
	virtual TArray<FUECPExtensionDescriptor> GetExtensions() const override;
	virtual TOptional<FUECPExtensionDescriptor> FindExtension(FName ExtensionId) const override;
	virtual TOptional<FUECPExtensionDescriptor> FindExtensionByUmbrella(FName UmbrellaName) const override;
	virtual TOptional<FUECPExtensionDescriptor> FindExtensionByTool(FName ToolName) const override;
	virtual EUECPExtensionState GetExtensionState(FName ExtensionId) const override;
	virtual bool IsUmbrellaProvidedByLoadedExtension(FName UmbrellaName) const override;
	virtual bool IsExtensionEnabled(FName ExtensionId) const override;
	virtual bool SetExtensionEnabled(FName ExtensionId, bool bEnabled, FString& OutError,
		TArray<FString>* OutMissingPlugins = nullptr, bool bRunRemoveSetup = false) override;
	virtual void SetExtensionEnabledAsync(FName ExtensionId, bool bEnable,
		TFunction<void(bool, FString, TArray<FString>)> OnComplete,
		bool bRunRemoveSetup = false) override;
	virtual void RefreshAll() override;

	virtual void QueueExtensionChange(FName ExtensionId, bool bEnable) override;
	virtual void UnqueueExtensionChange(FName ExtensionId) override;
	virtual void CancelPendingChanges() override;
	virtual TArray<FPendingChange> GetPendingChanges() const override;
	virtual bool PendingChangesRequireRestart() const override;
	virtual bool ApplyPendingChanges(FString& OutError) override;
	virtual TSharedPtr<FUECPMcpClient> GetMcpClient(FName ExtensionId) const override;
	virtual FString GetMcpLastError(FName ExtensionId) const override;
	virtual bool ShouldShowUmbrella(FName UmbrellaName) const override;
	virtual TArray<FUECPToolCatalogEntry> GetExtensionCatalogEntries(
		const TSet<FName>& ExcludeUmbrellaNames = {}) const override;
	virtual FOnExtensionStateChanged& OnExtensionStateChanged() override;

private:

	EUECPExtensionState EvaluateAndLoad(const FUECPExtensionDescriptor& Desc, FString& OutError,
		TArray<FString>* OutMissingPlugins);

	bool EnsureRequiredExtensionsLoaded(const FUECPExtensionDescriptor& Desc, FString& OutError);

	TArray<FString> FindEnabledDependents(FName ExtensionId) const;

	void SetState(FName ExtensionId, EUECPExtensionState NewState);

	static FName SettingsNamespace();

	static FString MakeEnabledKey(FName ExtensionId);

	static FName MakeProxyToolName(FName ExtensionId, const FString& McpToolName);

	bool StartMcpAndRegisterProxies(const FUECPExtensionDescriptor& Desc, FString& OutError);

	void StartMcpAndRegisterProxiesAsync(const FUECPExtensionDescriptor& Desc,
		TFunction<void(bool , FString )> OnComplete);

	void RegisterProxiesForBinding(FName ExtensionId, int32& OutCount);

	void StopMcpAndUnregisterProxies(FName ExtensionId);

	struct FMcpBinding
	{
		TSharedPtr<FUECPMcpClient> Client;
		TArray<FName>              RegisteredProxyNames;
		FString                    LastError;
	};

	mutable FCriticalSection RegistryLock;

	TArray<FUECPExtensionDescriptor> Extensions;
	TMap<FName, EUECPExtensionState> States;

	TSet<FName>                      ExtensionsLoading;
	TMap<FName, FMcpBinding>         McpBindings;
	FOnExtensionStateChanged StateChangedDelegate;

	TArray<FName>                                  PendingOrder;
	TMap<FName, FPendingChange>                    PendingByExtension;
};
