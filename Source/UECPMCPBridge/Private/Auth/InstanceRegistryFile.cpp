// Copyright 2026, BlueprintsLab, All rights reserved

#include "InstanceRegistryFile.h"

#include "MCPToolsLog.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace InstanceRegistry
{

FString GetRegistryDir()
{
	const FString Dir = FPaths::Combine(
		FPlatformProcess::UserSettingsDir(),
		TEXT("UECP"),
		TEXT("instances"));

	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	if (!PF.DirectoryExists(*Dir))
	{
		PF.CreateDirectoryTree(*Dir);
	}
	return Dir;
}

FString GetInstanceFilePath()
{
	const uint32 Pid = FPlatformProcess::GetCurrentProcessId();
	return FPaths::Combine(GetRegistryDir(), FString::Printf(TEXT("%u.json"), Pid));
}

bool WriteEntry(const FInstanceRegistryEntry& Entry)
{
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("project_path"),  Entry.ProjectPath);
	Obj->SetStringField(TEXT("project_name"),  Entry.ProjectName);
	Obj->SetNumberField(TEXT("mcp_http_port"), Entry.McpHttpPort);
	Obj->SetNumberField(TEXT("pid"),           Entry.ProcessId);
	Obj->SetStringField(TEXT("started_at"),    Entry.StartedAt);

	FString Body;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
	if (!FJsonSerializer::Serialize(Obj, Writer))
	{
		UE_LOG(LogMCPTool, Warning, TEXT("InstanceRegistry: serialise failed"));
		return false;
	}

	const FString Path = GetInstanceFilePath();
	if (!FFileHelper::SaveStringToFile(Body, *Path,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogMCPTool, Warning, TEXT("InstanceRegistry: write failed for %s"), *Path);
		return false;
	}

	UE_LOG(LogMCPTool, Log, TEXT("InstanceRegistry: wrote %s"), *Path);
	return true;
}

void RemoveEntry()
{
	const FString Path = GetInstanceFilePath();
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	if (PF.FileExists(*Path))
	{
		if (PF.DeleteFile(*Path))
		{
			UE_LOG(LogMCPTool, Log, TEXT("InstanceRegistry: removed %s"), *Path);
		}
		else
		{
			UE_LOG(LogMCPTool, Warning, TEXT("InstanceRegistry: delete failed for %s"), *Path);
		}
	}
}

}
