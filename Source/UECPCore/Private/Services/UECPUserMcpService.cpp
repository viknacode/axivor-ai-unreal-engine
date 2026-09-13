// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPUserMcpService.h"

#include "UECPCoreModule.h"
#include "Services/IUECPExtensionService.h"
#include "Services/IUECPSettingsService.h"
#include "Managers/SettingsManager.h"
#include "Mcp/UECPMcpClient.h"
#include "Mcp/UECPMcpOAuthClient.h"
#include "Mcp/UECPMcpOAuthTokenStore.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{

	const FName SettingsNamespace_UserMcp() { static const FName NS(TEXT("Extensions")); return NS; }

	FString MakeEnabledKey(FName ExtensionId)
	{
		return FString::Printf(TEXT("%s.Enabled"), *ExtensionId.ToString());
	}

	FString DoSanitiseId(const FString& In)
	{
		FString Out; Out.Reserve(In.Len());
		for (TCHAR C : In)
		{
			if (FChar::IsAlnum(C)) Out.AppendChar(FChar::ToLower(C));
			else if (C == TEXT('_') || C == TEXT('-')) Out.AppendChar(TEXT('_'));
		}
		while (Out.Len() > 0 && Out[0] == TEXT('_')) Out.RemoveAt(0, 1);
		while (Out.Len() > 0 && Out[Out.Len()-1] == TEXT('_')) Out.RemoveAt(Out.Len()-1, 1);
		return Out;
	}
}

FName IUECPUserMcpService::MakeExtensionId(const FString& Id)
{
	const FString Sanitised = DoSanitiseId(Id);
	if (Sanitised.IsEmpty()) return NAME_None;
	return FName(*FString::Printf(TEXT("usermcp_%s"), *Sanitised));
}

FString IUECPUserMcpService::TryGetUserMcpId(FName ExtensionId)
{
	const FString S = ExtensionId.ToString();
	static const FString Prefix(TEXT("usermcp_"));
	if (!S.StartsWith(Prefix)) return FString();
	return S.RightChop(Prefix.Len());
}

FUECPUserMcpServiceImpl::FUECPUserMcpServiceImpl() = default;

void FUECPUserMcpServiceImpl::Initialize()
{
	if (bInitialized) return;
	bInitialized = true;

	FUECPMcpOAuthTokenStore::Get().Initialize();

	LoadFromDisk();

	for (const FUECPUserMcpServerConfig& Cfg : Servers)
	{
		RegisterExtensionForConfig(Cfg);
	}

	UE_LOG(LogUECPCore, Log, TEXT("UserMcp: hydrated %d server(s) from disk"), Servers.Num());
}

TArray<FUECPUserMcpServerConfig> FUECPUserMcpServiceImpl::ListServers() const
{
	return Servers;
}

const FUECPUserMcpServerConfig* FUECPUserMcpServiceImpl::FindServer(const FString& Id) const
{
	const FString Lower = Id.ToLower();
	return Servers.FindByPredicate([&Lower](const FUECPUserMcpServerConfig& C)
	{
		return C.Id.ToLower() == Lower;
	});
}

bool FUECPUserMcpServiceImpl::ValidateId(const FString& Id, FString& OutError)
{
	if (Id.IsEmpty())
	{
		OutError = TEXT("Server id is empty.");
		return false;
	}
	if (Id.Contains(TEXT("__")))
	{
		OutError = TEXT("Server id cannot contain '__' (reserved for tool namespacing).");
		return false;
	}
	const FString Sanitised = DoSanitiseId(Id);
	if (Sanitised.IsEmpty())
	{
		OutError = TEXT("Server id must contain at least one alphanumeric character.");
		return false;
	}
	return true;
}

FString FUECPUserMcpServiceImpl::SanitiseId(const FString& In)
{
	return DoSanitiseId(In);
}

bool FUECPUserMcpServiceImpl::UpsertServer(const FUECPUserMcpServerConfig& Config, FString& OutError)
{
	if (!ValidateId(Config.Id, OutError)) return false;
	if (Config.Command.IsEmpty() && Config.Url.IsEmpty())
	{
		OutError = TEXT("Server needs either a 'command' (stdio) or a 'url' (http).");
		return false;
	}
	if (!Config.Url.IsEmpty() && !Config.Url.StartsWith(TEXT("http://")) && !Config.Url.StartsWith(TEXT("https://")))
	{
		OutError = FString::Printf(TEXT("URL must start with http:// or https:// (got '%s')."), *Config.Url);
		return false;
	}

	FUECPUserMcpServerConfig Stored = Config;
	if (Stored.DisplayName.IsEmpty()) Stored.DisplayName = Stored.Id;

	const int32 ExistingIdx = Servers.IndexOfByPredicate([&Stored](const FUECPUserMcpServerConfig& C)
	{
		return C.Id.ToLower() == Stored.Id.ToLower();
	});
	if (ExistingIdx != INDEX_NONE)
	{
		Servers[ExistingIdx] = MoveTemp(Stored);
	}
	else
	{
		Servers.Add(MoveTemp(Stored));
	}

	SaveToDisk();
	RegisterExtensionForConfig(Servers[ExistingIdx != INDEX_NONE ? ExistingIdx : Servers.Num() - 1]);

	if (IUECPCoreModule::IsAvailable())
	{
		const FUECPUserMcpServerConfig& Persisted = Servers[ExistingIdx != INDEX_NONE ? ExistingIdx : Servers.Num() - 1];
		const FName ExtId = MakeExtensionId(Persisted.Id);
		IUECPCoreModule::Get().GetSettingsService().SetBool(
			SettingsNamespace_UserMcp(), MakeEnabledKey(ExtId), Persisted.bEnabled);

		IUECPCoreModule::Get().GetExtensionService().SetExtensionEnabledAsync(
			ExtId, Persisted.bEnabled, {});
	}

	ChangeDelegate.Broadcast(Config.Id);
	return true;
}

namespace
{

	void ParseEntryInto(const FString& Id, const TSharedPtr<FJsonObject>& Entry, FUECPUserMcpServerConfig& Cfg)
	{
		Cfg.Id = Id;
		Entry->TryGetStringField(TEXT("displayName"), Cfg.DisplayName);
		Entry->TryGetStringField(TEXT("description"), Cfg.Description);
		Entry->TryGetStringField(TEXT("command"),     Cfg.Command);
		Entry->TryGetStringField(TEXT("cwd"),         Cfg.Cwd);

		Entry->TryGetStringField(TEXT("url"),     Cfg.Url);
		if (Cfg.Url.IsEmpty()) Entry->TryGetStringField(TEXT("httpUrl"),  Cfg.Url);
		if (Cfg.Url.IsEmpty()) Entry->TryGetStringField(TEXT("endpoint"), Cfg.Url);

		const TArray<TSharedPtr<FJsonValue>>* ArgsArr = nullptr;
		if (Entry->TryGetArrayField(TEXT("args"), ArgsArr))
		{
			for (const TSharedPtr<FJsonValue>& V : *ArgsArr)
			{
				if (V.IsValid() && V->Type == EJson::String) Cfg.Args.Add(V->AsString());
			}
		}
		else
		{
			FString SingleArg;
			if (Entry->TryGetStringField(TEXT("args"), SingleArg) && !SingleArg.IsEmpty())
			{
				Cfg.Args.Add(SingleArg);
			}
		}

		const TSharedPtr<FJsonObject>* EnvObj = nullptr;
		if (Entry->TryGetObjectField(TEXT("env"), EnvObj) && EnvObj && EnvObj->IsValid())
		{
			for (const auto& E : (*EnvObj)->Values)
			{
				const FString K(*E.Key);
				const FString V = E.Value.IsValid() && E.Value->Type == EJson::String ? E.Value->AsString() : FString();
				Cfg.Env.Add(TPair<FString, FString>(K, V));
			}
		}

		const TSharedPtr<FJsonObject>* HeadersObj = nullptr;
		if (Entry->TryGetObjectField(TEXT("headers"),     HeadersObj) ||
			Entry->TryGetObjectField(TEXT("httpHeaders"), HeadersObj))
		{
			if (HeadersObj && HeadersObj->IsValid())
			{
				for (const auto& H : (*HeadersObj)->Values)
				{
					const FString K(*H.Key);
					const FString V = H.Value.IsValid() && H.Value->Type == EJson::String ? H.Value->AsString() : FString();
					Cfg.Headers.Add(TPair<FString, FString>(K, V));
				}
			}
		}

		Entry->TryGetBoolField(TEXT("enabled"),       Cfg.bEnabled);
		Entry->TryGetBoolField(TEXT("alwaysConfirm"), Cfg.bAlwaysConfirm);

		Entry->TryGetBoolField(TEXT("useOAuth"), Cfg.bUseOAuth);
		const TSharedPtr<FJsonObject>* OAuthObj = nullptr;
		if (Entry->TryGetObjectField(TEXT("oauth"), OAuthObj) && OAuthObj && OAuthObj->IsValid())
		{
			Cfg.bUseOAuth = true;
			(*OAuthObj)->TryGetStringField(TEXT("issuer"),   Cfg.OAuthIssuer);
			(*OAuthObj)->TryGetStringField(TEXT("clientId"), Cfg.OAuthClientId);
		}
	}

	bool LooksLikeServerEntry(const TSharedPtr<FJsonObject>& Obj)
	{
		if (!Obj.IsValid()) return false;
		return Obj->HasField(TEXT("command")) || Obj->HasField(TEXT("url")) || Obj->HasField(TEXT("httpUrl"));
	}
}

TArray<FString> FUECPUserMcpServiceImpl::AddFromMcpServersBlock(const FString& JsonBlock,
	TMap<FString, FString>& OutErrors)
{
	OutErrors.Reset();
	TArray<FString> Added;

	const FString TrimmedInput = JsonBlock.TrimStartAndEnd();
	if (TrimmedInput.IsEmpty())
	{
		OutErrors.Add(TEXT("(parse)"), TEXT("The paste was empty."));
		return Added;
	}

	auto TryParse = [](const FString& In, TSharedPtr<FJsonObject>& Out) -> bool
	{
		const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(In);
		return FJsonSerializer::Deserialize(R, Out) && Out.IsValid();
	};

	TSharedPtr<FJsonObject> Root;
	bool bParsed = TryParse(TrimmedInput, Root);

	if (!bParsed)
	{
		const FString Wrapped = FString(TEXT("{")) + TrimmedInput + TEXT("}");
		bParsed = TryParse(Wrapped, Root);
	}
	if (!bParsed)
	{
		OutErrors.Add(TEXT("(parse)"),
			TEXT("Could not parse the JSON. Check for trailing commas or unmatched braces, or paste an mcpServers block from your existing client config."));
		return Added;
	}

	TSharedPtr<FJsonObject> ServersObj = Root;
	if (Root->HasTypedField<EJson::Object>(TEXT("mcpServers")))
	{
		ServersObj = Root->GetObjectField(TEXT("mcpServers"));
	}
	else if (Root->HasTypedField<EJson::Object>(TEXT("mcp")))
	{
		const TSharedPtr<FJsonObject> McpObj = Root->GetObjectField(TEXT("mcp"));
		if (McpObj.IsValid() && McpObj->HasTypedField<EJson::Object>(TEXT("servers")))
		{
			ServersObj = McpObj->GetObjectField(TEXT("servers"));
		}
		else if (McpObj.IsValid())
		{
			ServersObj = McpObj;
		}
	}
	else if (Root->HasTypedField<EJson::Array>(TEXT("servers")))
	{
		const TArray<TSharedPtr<FJsonValue>>& Arr = Root->GetArrayField(TEXT("servers"));
		for (const TSharedPtr<FJsonValue>& V : Arr)
		{
			const TSharedPtr<FJsonObject> EntryObj = V.IsValid() ? V->AsObject() : nullptr;
			if (!EntryObj.IsValid()) continue;
			FString EntryName;
			EntryObj->TryGetStringField(TEXT("name"), EntryName);
			if (EntryName.IsEmpty()) EntryObj->TryGetStringField(TEXT("id"), EntryName);
			if (EntryName.IsEmpty()) EntryName = FString::Printf(TEXT("server%d"), Servers.Num() + Added.Num() + 1);

			FUECPUserMcpServerConfig Cfg;
			ParseEntryInto(EntryName, EntryObj, Cfg);
			FString Err;
			if (UpsertServer(Cfg, Err)) Added.Add(Cfg.Id);
			else                        OutErrors.Add(EntryName, Err);
		}
		return Added;
	}

	if (!ServersObj.IsValid())
	{
		OutErrors.Add(TEXT("(parse)"), TEXT("No mcpServers entries found in the JSON block."));
		return Added;
	}

	if (LooksLikeServerEntry(ServersObj))
	{
		FString EntryName;
		ServersObj->TryGetStringField(TEXT("name"), EntryName);
		if (EntryName.IsEmpty()) ServersObj->TryGetStringField(TEXT("id"), EntryName);
		if (EntryName.IsEmpty()) EntryName = FString::Printf(TEXT("server%d"), Servers.Num() + 1);

		FUECPUserMcpServerConfig Cfg;
		ParseEntryInto(EntryName, ServersObj, Cfg);
		FString Err;
		if (UpsertServer(Cfg, Err)) Added.Add(Cfg.Id);
		else                        OutErrors.Add(EntryName, Err);
		return Added;
	}

	bool bAnyHandled = false;
	for (const auto& Pair : ServersObj->Values)
	{
		const FString Id(*Pair.Key);
		const TSharedPtr<FJsonObject> Entry = Pair.Value.IsValid() ? Pair.Value->AsObject() : nullptr;
		if (!Entry.IsValid())
		{
			OutErrors.Add(Id, TEXT("Entry is not a JSON object."));
			continue;
		}
		bAnyHandled = true;

		FUECPUserMcpServerConfig Cfg;
		ParseEntryInto(Id, Entry, Cfg);
		FString Err;
		if (UpsertServer(Cfg, Err)) Added.Add(Cfg.Id);
		else                        OutErrors.Add(Id, Err);
	}

	if (!bAnyHandled && OutErrors.Num() == 0)
	{
		OutErrors.Add(TEXT("(parse)"),
			TEXT("Couldn't find any server entries — expected an object whose keys are server names, or an mcpServers/mcp block."));
	}

	return Added;
}

bool FUECPUserMcpServiceImpl::SetServerEnabled(const FString& Id, bool bEnabled, FString& OutError)
{
	const int32 Idx = Servers.IndexOfByPredicate([&Id](const FUECPUserMcpServerConfig& C)
	{
		return C.Id.ToLower() == Id.ToLower();
	});
	if (Idx == INDEX_NONE)
	{
		OutError = TEXT("Unknown server id.");
		return false;
	}

	Servers[Idx].bEnabled = bEnabled;
	SaveToDisk();

	if (IUECPCoreModule::IsAvailable())
	{
		const FName ExtId = MakeExtensionId(Servers[Idx].Id);
		IUECPCoreModule::Get().GetSettingsService().SetBool(
			SettingsNamespace_UserMcp(), MakeEnabledKey(ExtId), bEnabled);
		IUECPCoreModule::Get().GetExtensionService().SetExtensionEnabledAsync(
			ExtId, bEnabled, {});
	}

	ChangeDelegate.Broadcast(Id);
	return true;
}

void FUECPUserMcpServiceImpl::RestartServer(const FString& Id)
{
	const FUECPUserMcpServerConfig* Cfg = FindServer(Id);
	if (!Cfg || !Cfg->bEnabled || !IUECPCoreModule::IsAvailable()) return;

	const FName ExtId = MakeExtensionId(Cfg->Id);
	IUECPExtensionService& Svc = IUECPCoreModule::Get().GetExtensionService();

	FString DropError;
	Svc.SetExtensionEnabled(ExtId, false, DropError);
	Svc.SetExtensionEnabledAsync(ExtId, true, {});

	ChangeDelegate.Broadcast(Id);
}

bool FUECPUserMcpServiceImpl::ShouldConfirmUpstreamTool(const FString& NamespacedToolName,
	bool& bOutIsUpstreamTool) const
{
	bOutIsUpstreamTool = false;
	if (!IUECPCoreModule::IsAvailable()) return false;

	const int32 SeparatorPos = NamespacedToolName.Find(TEXT("__"));
	if (SeparatorPos == INDEX_NONE) return false;

	const FString FullExtId = NamespacedToolName.Left(SeparatorPos);
	const FString ToolPart  = NamespacedToolName.RightChop(SeparatorPos + 2);

	const FString UserId = TryGetUserMcpId(FName(*FullExtId));
	if (UserId.IsEmpty()) return false;

	const FUECPUserMcpServerConfig* Cfg = Servers.FindByPredicate(
		[&UserId](const FUECPUserMcpServerConfig& C)
		{
			return DoSanitiseId(C.Id) == UserId;
		});
	if (!Cfg) return false;

	bOutIsUpstreamTool = true;

	if (Cfg->bAlwaysConfirm) return true;

	const FName ExtId = MakeExtensionId(Cfg->Id);
	const TSharedPtr<FUECPMcpClient> Client = IUECPCoreModule::Get().GetExtensionService().GetMcpClient(ExtId);
	if (!Client.IsValid()) return false;

	for (const FUECPMcpToolDescriptor& T : Client->GetTools())
	{
		if (T.Name == ToolPart)
		{
			if (T.bDestructiveHint) return true;
			if (T.bReadOnlyHint)    return false;
			return false;
		}
	}
	return false;
}

void FUECPUserMcpServiceImpl::RemoveServer(const FString& Id)
{
	const int32 Idx = Servers.IndexOfByPredicate([&Id](const FUECPUserMcpServerConfig& C)
	{
		return C.Id.ToLower() == Id.ToLower();
	});
	if (Idx == INDEX_NONE) return;

	UnregisterExtensionForConfig(Servers[Idx].Id);
	FUECPMcpOAuthClient::Disconnect(Servers[Idx].Id);
	Servers.RemoveAt(Idx);
	SaveToDisk();

	ChangeDelegate.Broadcast(Id);
}

void FUECPUserMcpServiceImpl::SignInOAuth(const FString& Id,
	TFunction<void(bool, FString)> OnComplete)
{
	const FUECPUserMcpServerConfig* Cfg = FindServer(Id);
	if (!Cfg)
	{
		if (OnComplete) OnComplete(false, TEXT("Unknown server id."));
		return;
	}
	if (Cfg->Url.IsEmpty())
	{
		if (OnComplete) OnComplete(false, TEXT("OAuth sign-in applies to HTTP servers only."));
		return;
	}

	const FString Url      = Cfg->Url;
	const FString ClientId = Cfg->OAuthClientId;
	const FString Issuer   = Cfg->OAuthIssuer;

	FUECPMcpOAuthClient::BeginInteractiveAuth(Id, Url, ClientId, Issuer,
		[Id, OnComplete](const FUECPMcpOAuthClient::FAuthResult& R)
		{
			if (R.bOk && IUECPCoreModule::IsAvailable())
			{
				IUECPUserMcpService& Svc = IUECPCoreModule::Get().GetUserMcpService();
				if (const FUECPUserMcpServerConfig* Cur = Svc.FindServer(Id))
				{
					FUECPUserMcpServerConfig Updated = *Cur;
					Updated.bUseOAuth     = true;
					Updated.bEnabled      = true;
					Updated.OAuthIssuer   = R.Issuer;
					Updated.OAuthClientId = R.ClientId;
					FString UpErr;
					Svc.UpsertServer(Updated, UpErr);
				}
				Svc.RestartServer(Id);
			}
			if (OnComplete) OnComplete(R.bOk, R.Error);
		});
}

void FUECPUserMcpServiceImpl::SignOutOAuth(const FString& Id)
{
	FUECPMcpOAuthClient::Disconnect(Id);
	RestartServer(Id);
	ChangeDelegate.Broadcast(Id);
}

bool FUECPUserMcpServiceImpl::HasOAuthGrant(const FString& Id) const
{
	return FUECPMcpOAuthClient::HasUsableGrant(Id);
}

void FUECPUserMcpServiceImpl::RegisterExtensionForConfig(const FUECPUserMcpServerConfig& Cfg)
{
	if (!IUECPCoreModule::IsAvailable()) return;

	FUECPExtensionDescriptor Desc;
	Desc.ExtensionId      = MakeExtensionId(Cfg.Id);
	Desc.DisplayName      = FText::FromString(Cfg.DisplayName.IsEmpty() ? Cfg.Id : Cfg.DisplayName);
	Desc.Description      = FText::FromString(Cfg.Description);
	Desc.Category         = FText::FromString(TEXT("MCP Servers"));
	Desc.bIsThirdParty    = true;
	Desc.bDefaultEnabled  = true;
	Desc.bRequiresLicense = false;

	FUECPMcpServerSpec Spec;
	Spec.Command = Cfg.Command;
	Spec.Args    = Cfg.Args;
	Spec.Env     = Cfg.Env;
	Spec.Cwd     = Cfg.Cwd;
	Spec.Url     = Cfg.Url;
	Spec.Headers = Cfg.Headers;
	Spec.LogTag  = Cfg.Id;
	Spec.bUseOAuth = Cfg.bUseOAuth && !Cfg.Url.IsEmpty();
	Spec.OAuthKey  = Cfg.Id;
	Desc.McpServer = MoveTemp(Spec);

	IUECPCoreModule::Get().GetExtensionService().RegisterExtension(MoveTemp(Desc));
}

void FUECPUserMcpServiceImpl::UnregisterExtensionForConfig(const FString& Id)
{
	if (!IUECPCoreModule::IsAvailable()) return;
	const FName ExtId = MakeExtensionId(Id);
	if (ExtId.IsNone()) return;

	IUECPExtensionService& Svc = IUECPCoreModule::Get().GetExtensionService();
	FString DropError;
	Svc.SetExtensionEnabled(ExtId, false, DropError);
	Svc.UnregisterExtension(ExtId);
}

FString FUECPUserMcpServiceImpl::GetSavePath()
{
	return FPaths::Combine(FSettingsManager::GetGlobalDataDir(), TEXT("user_mcp_servers.json"));
}

bool FUECPUserMcpServiceImpl::LoadFromDisk()
{
	Servers.Reset();

	const FString Path = GetSavePath();

	if (!IFileManager::Get().FileExists(*Path))
	{
		const FString LegacyPath = FPaths::Combine(FPaths::ProjectSavedDir(),
			TEXT("BpGeneratorUltimate"), TEXT("user_mcp_servers.json"));
		if (IFileManager::Get().FileExists(*LegacyPath))
		{
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
			IFileManager::Get().Copy(*Path, *LegacyPath);
			UE_LOG(LogUECPCore, Log, TEXT("UserMcp: migrated %s → %s"), *LegacyPath, *Path);
		}
	}

	if (!IFileManager::Get().FileExists(*Path)) return true;

	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		UE_LOG(LogUECPCore, Warning, TEXT("UserMcp: failed to read %s"), *Path);
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogUECPCore, Warning, TEXT("UserMcp: invalid JSON in %s — starting empty"), *Path);
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* ServerArr = nullptr;
	if (!Root->TryGetArrayField(TEXT("servers"), ServerArr) || !ServerArr) return true;

	for (const TSharedPtr<FJsonValue>& V : *ServerArr)
	{
		const TSharedPtr<FJsonObject> Entry = V.IsValid() ? V->AsObject() : nullptr;
		if (!Entry.IsValid()) continue;

		FUECPUserMcpServerConfig Cfg;
		Entry->TryGetStringField(TEXT("id"),          Cfg.Id);
		Entry->TryGetStringField(TEXT("displayName"), Cfg.DisplayName);
		Entry->TryGetStringField(TEXT("description"), Cfg.Description);
		Entry->TryGetStringField(TEXT("command"),     Cfg.Command);
		Entry->TryGetStringField(TEXT("cwd"),         Cfg.Cwd);
		Entry->TryGetStringField(TEXT("url"),         Cfg.Url);
		Entry->TryGetBoolField  (TEXT("enabled"),       Cfg.bEnabled);
		Entry->TryGetBoolField  (TEXT("alwaysConfirm"), Cfg.bAlwaysConfirm);
		Entry->TryGetBoolField  (TEXT("useOAuth"),      Cfg.bUseOAuth);
		Entry->TryGetStringField(TEXT("oauthIssuer"),   Cfg.OAuthIssuer);
		Entry->TryGetStringField(TEXT("oauthClientId"), Cfg.OAuthClientId);

		const TArray<TSharedPtr<FJsonValue>>* ArgsArr = nullptr;
		if (Entry->TryGetArrayField(TEXT("args"), ArgsArr))
		{
			for (const TSharedPtr<FJsonValue>& A : *ArgsArr)
			{
				if (A.IsValid() && A->Type == EJson::String) Cfg.Args.Add(A->AsString());
			}
		}

		const TSharedPtr<FJsonObject>* EnvObj = nullptr;
		if (Entry->TryGetObjectField(TEXT("env"), EnvObj) && EnvObj && EnvObj->IsValid())
		{
			for (const auto& E : (*EnvObj)->Values)
			{
				const FString K(*E.Key);
				const FString Val = E.Value.IsValid() && E.Value->Type == EJson::String ? E.Value->AsString() : FString();
				Cfg.Env.Add(TPair<FString, FString>(K, Val));
			}
		}

		const TSharedPtr<FJsonObject>* HeadersObj = nullptr;
		if (Entry->TryGetObjectField(TEXT("headers"), HeadersObj) && HeadersObj && HeadersObj->IsValid())
		{
			for (const auto& H : (*HeadersObj)->Values)
			{
				const FString K(*H.Key);
				const FString Val = H.Value.IsValid() && H.Value->Type == EJson::String ? H.Value->AsString() : FString();
				Cfg.Headers.Add(TPair<FString, FString>(K, Val));
			}
		}

		if (Cfg.Id.IsEmpty() || (Cfg.Command.IsEmpty() && Cfg.Url.IsEmpty())) continue;
		Servers.Add(MoveTemp(Cfg));
	}

	return true;
}

bool FUECPUserMcpServiceImpl::SaveToDisk() const
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("version"), 1);

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FUECPUserMcpServerConfig& C : Servers)
	{
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("id"),            C.Id);
		Entry->SetStringField(TEXT("displayName"),   C.DisplayName);
		Entry->SetStringField(TEXT("description"),   C.Description);
		Entry->SetStringField(TEXT("command"),       C.Command);
		Entry->SetStringField(TEXT("cwd"),           C.Cwd);
		Entry->SetStringField(TEXT("url"),           C.Url);
		Entry->SetBoolField  (TEXT("enabled"),       C.bEnabled);
		Entry->SetBoolField  (TEXT("alwaysConfirm"), C.bAlwaysConfirm);
		Entry->SetBoolField  (TEXT("useOAuth"),      C.bUseOAuth);
		Entry->SetStringField(TEXT("oauthIssuer"),   C.OAuthIssuer);
		Entry->SetStringField(TEXT("oauthClientId"), C.OAuthClientId);

		TArray<TSharedPtr<FJsonValue>> ArgVals;
		for (const FString& A : C.Args) ArgVals.Add(MakeShared<FJsonValueString>(A));
		Entry->SetArrayField(TEXT("args"), ArgVals);

		const TSharedRef<FJsonObject> EnvObj = MakeShared<FJsonObject>();
		for (const TPair<FString, FString>& E : C.Env) EnvObj->SetStringField(E.Key, E.Value);
		Entry->SetObjectField(TEXT("env"), EnvObj);

		const TSharedRef<FJsonObject> HeadersObj = MakeShared<FJsonObject>();
		for (const TPair<FString, FString>& H : C.Headers) HeadersObj->SetStringField(H.Key, H.Value);
		Entry->SetObjectField(TEXT("headers"), HeadersObj);

		Arr.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Root->SetArrayField(TEXT("servers"), Arr);

	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	if (!FJsonSerializer::Serialize(Root, Writer)) return false;

	const FString Path = GetSavePath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	if (!FFileHelper::SaveStringToFile(Out, *Path))
	{
		UE_LOG(LogUECPCore, Warning, TEXT("UserMcp: failed to write %s"), *Path);
		return false;
	}
	return true;
}
