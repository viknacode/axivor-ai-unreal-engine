// Copyright 2026, BlueprintsLab, All rights reserved

#include "InstanceRegistryWatcher.h"

#include "MCPToolsLog.h"
#include "../Auth/InstanceRegistryFile.h"

#include "DirectoryWatcherModule.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "IDirectoryWatcher.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"

FInstanceRegistryWatcher::FInstanceRegistryWatcher() = default;

FInstanceRegistryWatcher::~FInstanceRegistryWatcher()
{
	Stop();
}

void FInstanceRegistryWatcher::Start()
{
	WatchedDir = InstanceRegistry::GetRegistryDir();
	Rescan();

	FDirectoryWatcherModule& DWM = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(
		TEXT("DirectoryWatcher"));
	Watcher = DWM.Get();
	if (!Watcher)
	{
		UE_LOG(LogMCPTool, Warning, TEXT("Broker: DirectoryWatcher unavailable; instance registry will not auto-refresh"));
		return;
	}

	const auto Delegate = IDirectoryWatcher::FDirectoryChanged::CreateRaw(
		this, &FInstanceRegistryWatcher::OnDirectoryChanged);
	Watcher->RegisterDirectoryChangedCallback_Handle(WatchedDir, Delegate, WatchHandle, 0);
	UE_LOG(LogMCPTool, Log, TEXT("Broker: watching %s for instance registry changes"), *WatchedDir);
}

void FInstanceRegistryWatcher::Stop()
{
	if (Watcher && WatchHandle.IsValid())
	{
		Watcher->UnregisterDirectoryChangedCallback_Handle(WatchedDir, WatchHandle);
		WatchHandle.Reset();
	}
	Watcher = nullptr;
	Entries.Reset();
}

void FInstanceRegistryWatcher::OnDirectoryChanged(const TArray<FFileChangeData>& )
{
	Rescan();
}

void FInstanceRegistryWatcher::Rescan()
{
	Entries.Reset();

	IFileManager& FM = IFileManager::Get();
	TArray<FString> Files;
	FM.FindFiles(Files, *(WatchedDir / TEXT("*.json")),  true,  false);

	for (const FString& Filename : Files)
	{
		const FString FullPath = WatchedDir / Filename;
		FString Body;
		if (!FFileHelper::LoadFileToString(Body, *FullPath)) continue;

		TSharedPtr<FJsonObject> Obj;
		const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Body);
		if (!FJsonSerializer::Deserialize(R, Obj) || !Obj.IsValid()) continue;

		FBrokerInstanceEntry Entry;
		Obj->TryGetStringField(TEXT("project_path"),  Entry.ProjectPath);
		Obj->TryGetStringField(TEXT("project_name"),  Entry.ProjectName);
		Obj->TryGetStringField(TEXT("started_at"),    Entry.StartedAt);
		double PortNum = 0;
		double PidNum  = 0;
		Obj->TryGetNumberField(TEXT("mcp_http_port"), PortNum);
		Obj->TryGetNumberField(TEXT("pid"),           PidNum);
		Entry.McpHttpPort = (int32)PortNum;
		Entry.ProcessId   = (int32)PidNum;

		if (Entry.ProcessId <= 0 || !IsPidAlive(Entry.ProcessId)) continue;

		if (Entry.McpHttpPort <= 0) continue;

		const FString Key = Entry.ProjectName.ToLower();
		if (Key.IsEmpty()) continue;
		Entries.Add(Key, MoveTemp(Entry));
	}

	UE_LOG(LogMCPTool, Verbose, TEXT("Broker: registry rescan — %d live instance(s)"), Entries.Num());
}

const FBrokerInstanceEntry* FInstanceRegistryWatcher::FindByProject(const FString& ProjectName) const
{
	const FBrokerInstanceEntry* Hit = Entries.Find(ProjectName.ToLower());
	if (!Hit) return nullptr;

	if (!IsPidAlive(Hit->ProcessId))
	{
		Entries.Remove(ProjectName.ToLower());
		return nullptr;
	}
	return Hit;
}

bool FInstanceRegistryWatcher::IsPidAlive(int32 Pid)
{
	if (Pid <= 0) return false;
	return FPlatformProcess::IsApplicationRunning((uint32)Pid);
}
