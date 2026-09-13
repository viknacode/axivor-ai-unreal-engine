// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Misc/DateTime.h"

class IDirectoryWatcher;
struct FFileChangeData;

struct FBrokerInstanceEntry
{
	FString ProjectPath;
	FString ProjectName;
	int32   McpHttpPort = 0;
	int32   ProcessId   = 0;
	FString StartedAt;
};

class FInstanceRegistryWatcher
{
public:
	FInstanceRegistryWatcher();
	~FInstanceRegistryWatcher();

	void Start();

	void Stop();

	const FBrokerInstanceEntry* FindByProject(const FString& ProjectName) const;

	const TMap<FString, FBrokerInstanceEntry>& GetEntries() const { return Entries; }

	void Rescan();

private:
	void OnDirectoryChanged(const TArray<FFileChangeData>& Changes);
	void RemoveDeadEntries();
	static bool IsPidAlive(int32 Pid);

	mutable TMap<FString, FBrokerInstanceEntry> Entries;

	IDirectoryWatcher* Watcher = nullptr;
	FDelegateHandle    WatchHandle;
	FString            WatchedDir;
};
