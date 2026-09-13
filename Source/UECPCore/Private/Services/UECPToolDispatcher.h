// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "Services/IUECPToolDispatcher.h"

class FUECPToolDispatcherImpl final : public IUECPToolDispatcher
{
public:
	virtual void RegisterHandler(FName ToolName, FToolHandler Handler,
		EUECPToolThreadAffinity Affinity = EUECPToolThreadAffinity::GameThread) override;
	virtual void UnregisterHandler(FName ToolName) override;
	virtual void SetRegistrantContext(FName ExtensionId) override;
	virtual FUECPToolResult ExecuteFromArgs(FName ToolName, const TSharedPtr<FJsonObject>& Args) override;
	virtual void ExecuteFromArgsAsync(FName ToolName, const TSharedPtr<FJsonObject>& Args,
		TFunction<void(FUECPToolResult)> OnComplete) override;
	virtual TArray<FName> ListTools() const override;
	virtual bool IsRegistered(FName ToolName) const override;
	virtual void RegisterToolMetadata(const FUECPToolMeta& Meta) override;
	virtual void GetAllToolMetadata(TArray<FUECPToolMeta>& Out) const override;
	virtual void RemoveToolMetadataForOwner(FName ExtensionId) override;
	virtual TSharedPtr<FJsonObject> GetUmbrellaSchema(FName Umbrella) const override;
	virtual void RemoveHandlersForOwner(FName ExtensionId) override;
	virtual TArray<FName> GetToolsForOwner(FName ExtensionId) const override;

private:
	// Builds the umbrella schema from ToolMeta. Caller must hold RegistryLock.
	TSharedPtr<FJsonObject> BuildUmbrellaSchema_AssumesLocked(FName Umbrella) const;

	struct FRegistration
	{
		FToolHandler              Handler;
		EUECPToolThreadAffinity   Affinity = EUECPToolThreadAffinity::GameThread;
		FName                     Owner;
	};
	mutable FCriticalSection RegistryLock;

	TMap<FName, FRegistration> Handlers;

	FName CurrentRegistrant;

	TMap<FName, FUECPToolMeta> ToolMeta;

	TMap<FName, FName> ToolMetaOwners;

	// Umbrella -> derived JSON schema. Guarded by RegistryLock; entries are dropped whenever
	// metadata for that umbrella is added or removed and rebuilt lazily on the next request.
	mutable TMap<FName, TSharedPtr<FJsonObject>> UmbrellaSchemaCache;

	mutable FCriticalSection   UnknownLock;
	TMap<FName, int32>         UnknownToolHits;
};
