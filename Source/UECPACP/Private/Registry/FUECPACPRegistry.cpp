// Copyright 2026, BlueprintsLab, All rights reserved

#include "Registry/FUECPACPRegistry.h"
#include "Registry/FUECPACPInstaller.h"
#include "UECPACPModule.h"
#include "Managers/SettingsManager.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Timespan.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	constexpr const TCHAR* RegistryUrl = TEXT("https://cdn.agentclientprotocol.com/registry/v1/latest/registry.json");
	constexpr int32 CacheTTLHours = 24;
}

FUECPACPRegistry::FUECPACPRegistry()
{
	LoadCacheFromDisk();
}

FUECPACPRegistry::~FUECPACPRegistry() = default;

const TArray<FUECPACPAgentEntry>& FUECPACPRegistry::GetAgents() const
{
	return Agents;
}

void FUECPACPRegistry::RefreshCatalog(bool bForce)
{
	if (bFetchInFlight) return;
	if (!bForce && !IsCacheStale() && Agents.Num() > 0) return;

	bFetchInFlight = true;

	TSharedRef<IHttpRequest> Req = FHttpModule::Get().CreateRequest();
	Req->SetVerb(TEXT("GET"));
	Req->SetURL(RegistryUrl);
	Req->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Req->OnProcessRequestComplete().BindRaw(this, &FUECPACPRegistry::OnCatalogResponse);
	Req->ProcessRequest();
}

bool FUECPACPRegistry::IsInstalled(const FString& AgentId) const
{
	return FPaths::FileExists(InstallMarkerPath(AgentId));
}

FUECPACPInstallMarker FUECPACPRegistry::GetInstallMarker(const FString& AgentId) const
{
	FUECPACPInstallMarker Marker;
	const FString Path = InstallMarkerPath(AgentId);
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path)) return Marker;

	TSharedPtr<FJsonObject> Obj;
	const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(R, Obj) || !Obj.IsValid()) return Marker;

	Marker.AgentId = Obj->GetStringField(TEXT("agentId"));
	Marker.Version = Obj->GetStringField(TEXT("version"));
	Marker.EntrypointCommand = Obj->GetStringField(TEXT("entrypointCommand"));
	Marker.InstalledAtUtc = Obj->GetStringField(TEXT("installedAtUtc"));

	int32 MethodInt = 0;
	Obj->TryGetNumberField(TEXT("method"), MethodInt);
	Marker.Method = static_cast<EUECPACPDistributionKind>(MethodInt);

	const TArray<TSharedPtr<FJsonValue>>* ArgsArr = nullptr;
	if (Obj->TryGetArrayField(TEXT("entrypointArgs"), ArgsArr) && ArgsArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *ArgsArr) Marker.EntrypointArgs.Add(V->AsString());
	}
	return Marker;
}

void FUECPACPRegistry::InstallAgent(const FString& AgentId,
	FUECPACPInstallProgress OnProgress,
	FUECPACPInstallComplete OnComplete)
{
	const FUECPACPAgentEntry* Entry = Agents.FindByPredicate(
		[&AgentId](const FUECPACPAgentEntry& E) { return E.Id == AgentId; });
	if (!Entry)
	{
		UE_LOG(LogUECPACP, Warning, TEXT("InstallAgent('%s') — not in catalog"), *AgentId);
		if (OnComplete.IsBound()) OnComplete.Execute(false, FUECPACPInstallMarker{});
		return;
	}
	FUECPACPInstaller::Install(*Entry, OnProgress, OnComplete);
}

bool FUECPACPRegistry::UninstallAgent(const FString& AgentId)
{
	return FUECPACPInstaller::Uninstall(AgentId);
}

void FUECPACPRegistry::LoadCacheFromDisk()
{
	const FString Path = CacheFilePath();
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		UE_LOG(LogUECPACP, Log, TEXT("ACP registry: no cache at %s (first-run fetch will populate)"), *Path);
		return;
	}
	CacheTimestampUtc = IFileManager::Get().GetTimeStamp(*Path);
	const bool bOk = ParseCatalogJson(Json);
	UE_LOG(LogUECPACP, Log, TEXT("ACP registry: loaded %d agents from cache (parse=%s path=%s)"),
		Agents.Num(), bOk ? TEXT("ok") : TEXT("FAIL"), *Path);
}

void FUECPACPRegistry::SaveCacheToDisk() const
{
}

bool FUECPACPRegistry::IsCacheStale() const
{
	if (CacheTimestampUtc == FDateTime{}) return true;
	const FTimespan Age = FDateTime::UtcNow() - CacheTimestampUtc;
	return Age.GetTotalHours() > CacheTTLHours;
}

FString FUECPACPRegistry::CacheFilePath() const
{
	return FSettingsManager::GetGlobalDataDir() / TEXT("acp/registry.json");
}

FString FUECPACPRegistry::InstallMarkerPath(const FString& AgentId) const
{
	return FSettingsManager::GetGlobalDataDir() / TEXT("acp/installs") / (AgentId + TEXT(".json"));
}

void FUECPACPRegistry::OnCatalogResponse(FHttpRequestPtr , FHttpResponsePtr Response, bool bOk)
{
	bFetchInFlight = false;

	if (!bOk || !Response.IsValid() || Response->GetResponseCode() != 200)
	{
		const int32 Code = Response.IsValid() ? Response->GetResponseCode() : 0;
		UE_LOG(LogUECPACP, Warning, TEXT("ACP registry fetch failed (code=%d); using on-disk cache"), Code);
		CatalogChangedDelegate.Broadcast( false);
		return;
	}

	const FString Body = Response->GetContentAsString();
	if (!ParseCatalogJson(Body))
	{
		UE_LOG(LogUECPACP, Warning, TEXT("ACP registry body did not parse; keeping prior cache"));
		CatalogChangedDelegate.Broadcast( false);
		return;
	}

	FFileHelper::SaveStringToFile(Body, *CacheFilePath());
	CacheTimestampUtc = FDateTime::UtcNow();
	UE_LOG(LogUECPACP, Log, TEXT("ACP registry refreshed — %d agents"), Agents.Num());
	CatalogChangedDelegate.Broadcast( true);
}

TArray<FUECPACPAgentEntry> FUECPACPRegistry::GetInstalledAgentsFromDisk() const
{
	TArray<FUECPACPAgentEntry> Out;

	const FString InstallsDir = FSettingsManager::GetGlobalDataDir() / TEXT("acp/installs");
	IFileManager& FM = IFileManager::Get();
	if (!FM.DirectoryExists(*InstallsDir)) return Out;

	TArray<FString> MarkerFiles;
	FM.FindFiles(MarkerFiles, *(InstallsDir / TEXT("*.json")),  true,  false);

	for (const FString& File : MarkerFiles)
	{
		const FString AgentId = FPaths::GetBaseFilename(File);
		if (AgentId.IsEmpty()) continue;

		FUECPACPAgentEntry E;
		E.Id = AgentId;
		E.Name = AgentId;

		const FUECPACPInstallMarker Marker = GetInstallMarker(AgentId);
		E.Version = Marker.Version;

		Out.Add(MoveTemp(E));
	}
	return Out;
}

bool FUECPACPRegistry::ParseCatalogJson(const FString& JsonText)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(R, Root) || !Root.IsValid())
	{
		UE_LOG(LogUECPACP, Error, TEXT("ACP registry: top-level JSON parse failed"));
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* AgentsArr = nullptr;
	if (!Root->TryGetArrayField(TEXT("agents"), AgentsArr) || !AgentsArr)
	{
		UE_LOG(LogUECPACP, Error, TEXT("ACP registry: no 'agents' array in payload"));
		return false;
	}

	TArray<FUECPACPAgentEntry> Parsed;
	Parsed.Reserve(AgentsArr->Num());

	for (const TSharedPtr<FJsonValue>& V : *AgentsArr)
	{
		const TSharedPtr<FJsonObject> AgentObj = V.IsValid() ? V->AsObject() : nullptr;
		if (!AgentObj.IsValid()) continue;

		FUECPACPAgentEntry E;
		AgentObj->TryGetStringField(TEXT("id"),          E.Id);
		AgentObj->TryGetStringField(TEXT("name"),        E.Name);
		AgentObj->TryGetStringField(TEXT("version"),     E.Version);
		AgentObj->TryGetStringField(TEXT("description"), E.Description);
		AgentObj->TryGetStringField(TEXT("icon"),        E.IconUrl);
		AgentObj->TryGetStringField(TEXT("repository"),  E.RepositoryUrl);

		const TSharedPtr<FJsonObject>* DistObjPtr = nullptr;
		if (AgentObj->TryGetObjectField(TEXT("distribution"), DistObjPtr) && DistObjPtr)
		{
			const TSharedPtr<FJsonObject>& DistObj = *DistObjPtr;

			const TSharedPtr<FJsonObject>* NpxObjPtr = nullptr;
			if (DistObj->TryGetObjectField(TEXT("npx"), NpxObjPtr) && NpxObjPtr)
			{
				(*NpxObjPtr)->TryGetStringField(TEXT("package"), E.NpxPackage);
				const TArray<TSharedPtr<FJsonValue>>* NpxArgsArr = nullptr;
				if ((*NpxObjPtr)->TryGetArrayField(TEXT("args"), NpxArgsArr) && NpxArgsArr)
				{
					for (const TSharedPtr<FJsonValue>& A : *NpxArgsArr)
					{
						if (A.IsValid()) E.NpxArgs.Add(A->AsString());
					}
				}
			}

			const TSharedPtr<FJsonObject>* BinObjPtr = nullptr;
			if (DistObj->TryGetObjectField(TEXT("binary"), BinObjPtr) && BinObjPtr)
			{
				for (const auto& Pair : (*BinObjPtr)->Values)
				{
					const TSharedPtr<FJsonObject> Platform = Pair.Value.IsValid() ? Pair.Value->AsObject() : nullptr;
					if (!Platform.IsValid()) continue;
					FUECPACPBinaryDistribution B;
					B.Platform = Pair.Key;
					Platform->TryGetStringField(TEXT("archive"), B.ArchiveUrl);
					Platform->TryGetStringField(TEXT("cmd"),     B.Cmd);
					const TArray<TSharedPtr<FJsonValue>>* BinArgsArr = nullptr;
					if (Platform->TryGetArrayField(TEXT("args"), BinArgsArr) && BinArgsArr)
					{
						for (const TSharedPtr<FJsonValue>& A : *BinArgsArr)
						{
							if (A.IsValid()) B.Args.Add(A->AsString());
						}
					}
					if (!B.ArchiveUrl.IsEmpty() && !B.Cmd.IsEmpty()) E.Binaries.Add(MoveTemp(B));
				}
			}
		}

		if (!E.Id.IsEmpty()) Parsed.Add(MoveTemp(E));
	}

	Agents = MoveTemp(Parsed);
	UE_LOG(LogUECPACP, Log, TEXT("ACP registry: parsed %d agents"), Agents.Num());
	return true;
}
