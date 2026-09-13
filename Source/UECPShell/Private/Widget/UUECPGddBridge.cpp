// Copyright 2026, BlueprintsLab, All rights reserved

#include "Widget/UUECPGddBridge.h"
#include "UECPCoreModule.h"
#include "Services/IUECPGddService.h"

FString UUECPGddBridge::BuildGddOverlayHtml() const
{
	return IUECPCoreModule::Get().GetGddService().BuildOverlayHtml();
}

void UUECPGddBridge::LoadGddFiles()
{
	PushOverlayHtml(BuildGddOverlayHtml());
}

void UUECPGddBridge::SaveGddFile(const FString& FileId, const FString& Content)
{
	IUECPCoreModule::Get().GetGddService().SaveFile(FileId, Content);
}

void UUECPGddBridge::ImportGddFiles()
{
	IUECPCoreModule::Get().GetGddService().ImportFiles();
}

void UUECPGddBridge::DeleteGddFile(const FString& FileId)
{
	IUECPCoreModule::Get().GetGddService().DeleteFile(FileId);
}

void UUECPGddBridge::ToggleGddFile(const FString& FileId, bool bEnabled)
{
	IUECPCoreModule::Get().GetGddService().ToggleFile(FileId, bEnabled);
}

void UUECPGddBridge::LoadGddFileContent(const FString& FileId)
{
	IUECPCoreModule::Get().GetGddService().LoadFileContent(FileId);
}
