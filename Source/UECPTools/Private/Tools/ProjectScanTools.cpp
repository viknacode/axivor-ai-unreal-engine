// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/ProjectScanTools.h"
#include "Dom/JsonObject.h"
#include "Services/IUECPScannerService.h"
#include "UECPCoreModule.h"

namespace ProjectScanTools
{

void HandleScanAndIndexProject(FString& OutError)
{
	IUECPCoreModule::Get().GetScannerService().ScanProjectAsync(
		[](bool bSuccess, const FString& Message)
		{
			if (!bSuccess)
			{
				UE_LOG(LogTemp, Warning, TEXT("scan_project: %s"), *Message);
			}
		});
	OutError.Empty();
}

bool QueryProjectIndex(const FString& Query, FString& OutResult, FString& OutError)
{
	return IUECPCoreModule::Get().GetScannerService().QueryIndex(Query, OutResult, OutError);
}

void HandleScanAndIndexProjectFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJsonString, FString& )
{
	FString Unused;
	HandleScanAndIndexProject(Unused);
	OutJsonString = TEXT("{\"success\":true,\"pending\":true,\"message\":\"Project scan started in background. Check the index file in Saved/AI/ProjectIndex.json when complete.\"}");
}

void HandleQueryProjectIndexFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Query;
	if (!Args->TryGetStringField(TEXT("query"), Query) || Query.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: query");
		return;
	}
	FString QueryError;
	if (!QueryProjectIndex(Query, OutJsonString, QueryError))
	{
		OutError = QueryError;
	}
}

}
