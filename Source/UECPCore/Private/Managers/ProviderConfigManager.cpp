// Copyright 2026, BlueprintsLab, All rights reserved

#include "Managers/ProviderConfigManager.h"
#include "Managers/SettingsManager.h"
#include "UIConfigManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

FProviderConfigManager::FProviderConfigManager()
{
}

FProviderConfigManager& FProviderConfigManager::Get()
{
	static FProviderConfigManager Instance;
	return Instance;
}

void FProviderConfigManager::Initialize()
{
	if (bInitialized) return;
	bInitialized = true;

	{
		FScopeLock ScopeLock(&Lock);
		if (!bHasValidData)
		{
			CachedProviders = GetHardcodedProviders();
			CachedAgents    = GetHardcodedAgents();
			bHasValidData   = true;
		}
	}

	LoadCache();

	if (IsCacheStale())
	{
		FetchRemoteConfig();
	}
}

void FProviderConfigManager::RefreshConfig()
{
	FetchRemoteConfig();
}

bool FProviderConfigManager::HasValidData() const
{
	FScopeLock ScopeLock(&Lock);
	return bHasValidData;
}

TArray<FString> FProviderConfigManager::GetModelsForProvider(const FString& ProviderId) const
{
	FScopeLock ScopeLock(&Lock);
	for (const FProviderInfo& P : CachedProviders)
	{
		if (P.ProviderId.Equals(ProviderId, ESearchCase::IgnoreCase) && P.bIsActive)
		{
			TArray<FString> Out;
			for (const FProviderModel& M : P.Models)
				Out.Add(M.Id);
			return Out;
		}
	}
	return {};
}

FString FProviderConfigManager::GetDefaultModelForProvider(const FString& ProviderId) const
{
	FScopeLock ScopeLock(&Lock);
	for (const FProviderInfo& P : CachedProviders)
	{
		if (P.ProviderId.Equals(ProviderId, ESearchCase::IgnoreCase) && P.bIsActive)
			return P.DefaultModel;
	}
	return TEXT("");
}

FString FProviderConfigManager::GetEndpointForProvider(const FString& ProviderId) const
{
	FScopeLock ScopeLock(&Lock);
	for (const FProviderInfo& P : CachedProviders)
	{
		if (P.ProviderId.Equals(ProviderId, ESearchCase::IgnoreCase) && P.bIsActive)
			return P.ApiEndpoint;
	}
	return TEXT("");
}

bool FProviderConfigManager::IsModelVisionCapable(const FString& ProviderId, const FString& ModelId) const
{
	FScopeLock ScopeLock(&Lock);
	for (const FProviderInfo& P : CachedProviders)
	{
		if (!P.ProviderId.Equals(ProviderId, ESearchCase::IgnoreCase)) continue;
		for (const FProviderModel& M : P.Models)
		{
			if (M.Id.Equals(ModelId, ESearchCase::IgnoreCase))
				return M.bSupportsVision;
		}
	}
	return true;
}

FString FProviderConfigManager::GetAgentDefaultModel(const FString& AgentId) const
{
	FScopeLock ScopeLock(&Lock);
	for (const FAgentInfo& A : CachedAgents)
	{
		if (A.AgentId.Equals(AgentId, ESearchCase::IgnoreCase) && A.bIsActive)
			return A.DefaultModel;
	}
	return TEXT("");
}

TArray<FString> FProviderConfigManager::GetAgentModels(const FString& AgentId) const
{
	FScopeLock ScopeLock(&Lock);
	for (const FAgentInfo& A : CachedAgents)
	{
		if (A.AgentId.Equals(AgentId, ESearchCase::IgnoreCase) && A.bIsActive)
		{
			TArray<FString> Out;
			for (const FProviderModel& M : A.Models)
				Out.Add(M.Id);
			return Out;
		}
	}
	return {};
}

void FProviderConfigManager::FetchRemoteConfig()
{
	{
		FScopeLock ScopeLock(&Lock);
		if (bFetchInFlight) return;
		bFetchInFlight = true;
	}

	static const TArray<uint8> _pU = {
	    0x2f,0x10,0x17,0x36,0x27,0x65,0x1d,0x1f,0x53,0x46,0x3c,0x0a,
	    0x57,0x20,0x06,0x11,0x21,0x3d,0x2a,0x53,0x57,0x4b,0x4c,0x3c,
	    0x03,0x50,0x21,0x14,0x4d,0x35,0x21,0x2f,0x53,0x52,0x53,0x45,
	    0x3a,0x45,0x52,0x28
	};
	static const TArray<uint8> _pK = {
	    0x22,0x1d,0x29,0x2e,0x36,0x18,0x51,0x59,0x7d,0x5f,0x15,0x22,
	    0x64,0x3d,0x2d,0x52,0x08,0x3d,0x16,0x41,0x79,0x5c,0x64,0x6a,
	    0x08,0x72,0x0e,0x52,0x2a,0x2d,0x24,0x07,0x64,0x73,0x78,0x0f,
	    0x71,0x0e,0x48,0x0d,0x14,0x00,0x75,0x19,0x36,0x7d,0x59,0x78,
	    0x4c,0x3b,0x33,0x73,0x2f,0x3d,0x0e,0x00,0x2e,0x05,0x61,0x79,
	    0x41,0x7f,0x31,0x21,0x5d,0x1d,0x0d,0x2a,0x70,0x1d,0x32,0x74,
	    0x47,0x6b,0x04,0x19,0x06,0x6b,0x75,0x2e,0x1a,0x1c,0x66,0x33,
	    0x03,0x69,0x65,0x52,0x6a,0x0e,0x5c,0x09,0x0b,0x3a,0x11,0x0e,
	    0x28,0x7b,0x59,0x45,0x5f,0x3c,0x06,0x08,0x34,0x3e,0x30,0x0f,
	    0x62,0x16,0x5f,0x76,0x47,0x54,0x6d,0x5f,0x58,0x0b,0x27,0x29,
	    0x36,0x0d,0x07,0x63,0x59,0x7d,0x5c,0x1a,0x58,0x7f,0x3d,0x25,
	    0x56,0x09,0x10,0x38,0x4b,0x7d,0x66,0x7b,0x2c,0x22,0x5c,0x11,
	    0x50,0x00,0x05,0x1d,0x69,0x7f,0x5a,0x73,0x02,0x11,0x01,0x64,
	    0x75,0x2a,0x27,0x0f,0x2c,0x12,0x01,0x00,0x1c,0x65,0x2b,0x04,
	    0x78,0x2d,0x16,0x51,0x20,0x1c,0x37,0x71,0x52,0x05,0x02,0x25,
	    0x18,0x7b,0x03,0x29,0x3b,0x76,0x63,0x05,0x6b,0x09,0x5e,0x61,
	    0x3c,0x52,0x77,0x28,0x07,0x1b,0x33,0x02,0x39,0x60,0x5b,0x6d,
	    0x44,0x15,0x05,0x46
	};
	static const auto _pBuildSalt = []() {
	    const uint8 a[] = { 0x74, 0x57, 0x50, 0x75, 0x67, 0x6C, 0x01, 0x03, 0x01, 0x05, 0x6C, 0x58, 0x02 };
	    FString K; for (uint8 v : a) K += (TCHAR)(v ^ 0x33); return K;
	};
	static const FString _pDispatchSalt = _pBuildSalt();
	auto _compose = [](const TArray<uint8>& Enc, const FString& Key) -> FString {
	    FString R; TArray<uint8> KB; FTCHARToUTF8 C(*Key); KB.Append((const uint8*)C.Get(), C.Length());
	    for (int32 i = 0; i < Enc.Num(); i++) R += static_cast<TCHAR>(Enc[i] ^ KB[i % KB.Num()]);
	    return R;
	};
	FString BaseUrl = _compose(_pU, _pDispatchSalt);
	FString AnonKey = _compose(_pK, _pDispatchSalt);

	if (BaseUrl.IsEmpty() || AnonKey.IsEmpty())
	{
		FScopeLock ScopeLock(&Lock);
		bFetchInFlight = false;
		return;
	}

	auto MakeRequest = [&](const FString& Endpoint, TFunction<void(const FString&)> OnSuccess)
	{
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
		Req->SetURL(BaseUrl + Endpoint);
		Req->SetVerb(TEXT("GET"));
		Req->SetHeader(TEXT("apikey"),        AnonKey);
		Req->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *AnonKey));
		Req->SetHeader(TEXT("Content-Type"),  TEXT("application/json"));
		Req->SetTimeout(15.0f);
		Req->OnProcessRequestComplete().BindLambda(
			[this, OnSuccess](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
			{
				if (bOk && Response.IsValid() && Response->GetResponseCode() == 200)
				{
					OnSuccess(Response->GetContentAsString());
				}
				else
				{
					int32 Code = Response.IsValid() ? Response->GetResponseCode() : -1;
					UE_LOG(LogTemp, Warning, TEXT("ProviderConfigManager: fetch failed (HTTP %d)"), Code);
				}
			});
		Req->ProcessRequest();
	};

	struct FJoinState
	{
		FCriticalSection M;
		int32 Done = 0;
		FString ProvidersJson;
		FString AgentsJson;
		FString SoundGenJson;
	};
	TSharedPtr<FJoinState> Join = MakeShared<FJoinState>();

	auto TryFinalize = [this, Join]()
	{
		FScopeLock L(&Join->M);
		if (Join->Done < 3) return;
		ProcessProviderResponse(Join->ProvidersJson);
		ProcessAgentResponse(Join->AgentsJson);
		{
			FScopeLock ScopeLock(&Lock);
			if (!Join->SoundGenJson.IsEmpty())
				CachedSoundGenJson = Join->SoundGenJson;
			LastFetchTime  = FDateTime::UtcNow();
			bFetchInFlight = false;
		}
		SaveCache();
	};

	MakeRequest(
		TEXT("/rest/v1/provider_config?select=provider_id,display_name,api_endpoint,api_format,models,default_model,is_active,sort_order&order=sort_order.asc"),
		[Join, TryFinalize](const FString& Body)
		{
			FScopeLock L(&Join->M);
			Join->ProvidersJson = Body;
			Join->Done++;
			TryFinalize();
		});

	MakeRequest(
		TEXT("/rest/v1/agent_config?select=agent_id,display_name,default_model,models,is_active"),
		[Join, TryFinalize](const FString& Body)
		{
			FScopeLock L(&Join->M);
			Join->AgentsJson = Body;
			Join->Done++;
			TryFinalize();
		});

	MakeRequest(
		TEXT("/rest/v1/sound_gen_config?select=provider_id,display_name,api_endpoint,models,voices,supports_tts,supports_sfx,is_active,sort_order&order=sort_order.asc"),
		[Join, TryFinalize](const FString& Body)
		{
			FScopeLock L(&Join->M);
			Join->SoundGenJson = Body;
			Join->Done++;
			TryFinalize();
		});
}

void FProviderConfigManager::ProcessProviderResponse(const FString& JsonBody)
{
	if (JsonBody.IsEmpty()) return;

	TArray<TSharedPtr<FJsonValue>> Rows;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonBody);
	if (!FJsonSerializer::Deserialize(Reader, Rows) || Rows.Num() == 0) return;

	TArray<FProviderInfo> Providers;
	for (const TSharedPtr<FJsonValue>& Val : Rows)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Val->TryGetObject(Obj) || !Obj) continue;

		FProviderInfo P;
		(*Obj)->TryGetStringField(TEXT("provider_id"),   P.ProviderId);
		(*Obj)->TryGetStringField(TEXT("display_name"),  P.DisplayName);
		(*Obj)->TryGetStringField(TEXT("api_endpoint"),  P.ApiEndpoint);
		(*Obj)->TryGetStringField(TEXT("api_format"),    P.ApiFormat);
		(*Obj)->TryGetStringField(TEXT("default_model"), P.DefaultModel);
		(*Obj)->TryGetBoolField(  TEXT("is_active"),     P.bIsActive);
		double SortD = 0.0;
		if ((*Obj)->TryGetNumberField(TEXT("sort_order"), SortD))
			P.SortOrder = static_cast<int32>(SortD);

		const TArray<TSharedPtr<FJsonValue>>* ModelsArr = nullptr;
		if ((*Obj)->TryGetArrayField(TEXT("models"), ModelsArr))
		{
			for (const TSharedPtr<FJsonValue>& MVal : *ModelsArr)
			{
				const TSharedPtr<FJsonObject>* MObj = nullptr;
				if (!MVal->TryGetObject(MObj) || !MObj) continue;
				FProviderModel M;
				(*MObj)->TryGetStringField(TEXT("id"),      M.Id);
				(*MObj)->TryGetStringField(TEXT("display"), M.DisplayName);
				bool bVision = true;
				(*MObj)->TryGetBoolField(TEXT("supports_vision"), bVision);
				M.bSupportsVision = bVision;
				if (!M.Id.IsEmpty()) P.Models.Add(M);
			}
		}

		if (!P.ProviderId.IsEmpty()) Providers.Add(MoveTemp(P));
	}

	if (Providers.Num() > 0)
	{
		FScopeLock ScopeLock(&Lock);
		CachedProviders = MoveTemp(Providers);
		bHasValidData   = true;
	}
}

void FProviderConfigManager::ProcessAgentResponse(const FString& JsonBody)
{
	if (JsonBody.IsEmpty()) return;

	TArray<TSharedPtr<FJsonValue>> Rows;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonBody);
	if (!FJsonSerializer::Deserialize(Reader, Rows) || Rows.Num() == 0) return;

	TArray<FAgentInfo> Agents;
	for (const TSharedPtr<FJsonValue>& Val : Rows)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Val->TryGetObject(Obj) || !Obj) continue;

		FAgentInfo A;
		(*Obj)->TryGetStringField(TEXT("agent_id"),      A.AgentId);
		(*Obj)->TryGetStringField(TEXT("display_name"),  A.DisplayName);
		(*Obj)->TryGetStringField(TEXT("default_model"), A.DefaultModel);
		(*Obj)->TryGetBoolField(  TEXT("is_active"),     A.bIsActive);

		const TArray<TSharedPtr<FJsonValue>>* ModelsArr = nullptr;
		if ((*Obj)->TryGetArrayField(TEXT("models"), ModelsArr))
		{
			for (const TSharedPtr<FJsonValue>& MVal : *ModelsArr)
			{
				const TSharedPtr<FJsonObject>* MObj = nullptr;
				if (!MVal->TryGetObject(MObj) || !MObj) continue;
				FProviderModel M;
				(*MObj)->TryGetStringField(TEXT("id"),      M.Id);
				(*MObj)->TryGetStringField(TEXT("display"), M.DisplayName);
				if (!M.Id.IsEmpty()) A.Models.Add(M);
			}
		}

		if (!A.AgentId.IsEmpty()) Agents.Add(MoveTemp(A));
	}

	if (Agents.Num() > 0)
	{
		FScopeLock ScopeLock(&Lock);
		CachedAgents  = MoveTemp(Agents);
		bHasValidData = true;
	}
}

FString FProviderConfigManager::GetCachePath()
{
	return FSettingsManager::GetGlobalDataDir() / TEXT("provider_cfg.json");
}

void FProviderConfigManager::SaveCache() const
{
	FString Dir = FPaths::GetPath(GetCachePath());
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	if (!PF.DirectoryExists(*Dir)) PF.CreateDirectoryTree(*Dir);

	FScopeLock ScopeLock(&Lock);

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetNumberField(TEXT("timestamp"), static_cast<double>(LastFetchTime.ToUnixTimestamp()));

	TArray<TSharedPtr<FJsonValue>> PArr;
	for (const FProviderInfo& P : CachedProviders)
	{
		TSharedPtr<FJsonObject> PObj = MakeShareable(new FJsonObject);
		PObj->SetStringField(TEXT("provider_id"),   P.ProviderId);
		PObj->SetStringField(TEXT("display_name"),  P.DisplayName);
		PObj->SetStringField(TEXT("api_endpoint"),  P.ApiEndpoint);
		PObj->SetStringField(TEXT("api_format"),    P.ApiFormat);
		PObj->SetStringField(TEXT("default_model"), P.DefaultModel);
		PObj->SetBoolField(  TEXT("is_active"),     P.bIsActive);
		PObj->SetNumberField(TEXT("sort_order"),    static_cast<double>(P.SortOrder));
		TArray<TSharedPtr<FJsonValue>> MArr;
		for (const FProviderModel& M : P.Models)
		{
			TSharedPtr<FJsonObject> MObj = MakeShareable(new FJsonObject);
			MObj->SetStringField(TEXT("id"),      M.Id);
			MObj->SetStringField(TEXT("display"), M.DisplayName);
			MObj->SetBoolField(  TEXT("supports_vision"), M.bSupportsVision);
			MArr.Add(MakeShareable(new FJsonValueObject(MObj)));
		}
		PObj->SetArrayField(TEXT("models"), MArr);
		PArr.Add(MakeShareable(new FJsonValueObject(PObj)));
	}
	Root->SetArrayField(TEXT("providers"), PArr);

	TArray<TSharedPtr<FJsonValue>> AArr;
	for (const FAgentInfo& A : CachedAgents)
	{
		TSharedPtr<FJsonObject> AObj = MakeShareable(new FJsonObject);
		AObj->SetStringField(TEXT("agent_id"),      A.AgentId);
		AObj->SetStringField(TEXT("display_name"),  A.DisplayName);
		AObj->SetStringField(TEXT("default_model"), A.DefaultModel);
		AObj->SetBoolField(  TEXT("is_active"),     A.bIsActive);
		TArray<TSharedPtr<FJsonValue>> MArr;
		for (const FProviderModel& M : A.Models)
		{
			TSharedPtr<FJsonObject> MObj = MakeShareable(new FJsonObject);
			MObj->SetStringField(TEXT("id"),      M.Id);
			MObj->SetStringField(TEXT("display"), M.DisplayName);
			MArr.Add(MakeShareable(new FJsonValueObject(MObj)));
		}
		AObj->SetArrayField(TEXT("models"), MArr);
		AArr.Add(MakeShareable(new FJsonValueObject(AObj)));
	}
	Root->SetArrayField(TEXT("agents"), AArr);

	Root->SetStringField(TEXT("sound_gen_json"), CachedSoundGenJson);

	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	FFileHelper::SaveStringToFile(Out, *GetCachePath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void FProviderConfigManager::LoadCache()
{
	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *GetCachePath())) return;

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return;

	double TimestampD = 0.0;
	if (Root->TryGetNumberField(TEXT("timestamp"), TimestampD))
	{
		FScopeLock ScopeLock(&Lock);
		LastFetchTime = FDateTime::FromUnixTimestamp(static_cast<int64>(TimestampD));
	}

	const TArray<TSharedPtr<FJsonValue>>* PArr = nullptr;
	if (Root->TryGetArrayField(TEXT("providers"), PArr) && PArr)
	{
		FString PJson;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&PJson);
		FJsonSerializer::Serialize(*PArr, W);
		ProcessProviderResponse(PJson);
	}

	const TArray<TSharedPtr<FJsonValue>>* AArr = nullptr;
	if (Root->TryGetArrayField(TEXT("agents"), AArr) && AArr)
	{
		FString AJson;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&AJson);
		FJsonSerializer::Serialize(*AArr, W);
		ProcessAgentResponse(AJson);
	}

	FString SgJson;
	if (Root->TryGetStringField(TEXT("sound_gen_json"), SgJson) && !SgJson.IsEmpty())
	{
		FScopeLock ScopeLock(&Lock);
		CachedSoundGenJson = SgJson;
	}
}

FString FProviderConfigManager::GetSoundGenProvidersJson() const
{
	FScopeLock ScopeLock(&Lock);
	return CachedSoundGenJson;
}

bool FProviderConfigManager::IsCacheStale() const
{
	FScopeLock ScopeLock(&Lock);
	if (LastFetchTime == FDateTime()) return true;
	return (FDateTime::UtcNow() - LastFetchTime).GetTotalSeconds() >= CacheTTLSeconds;
}

TArray<FProviderInfo> FProviderConfigManager::GetHardcodedProviders() const
{
	TArray<FProviderInfo> Out;

	auto Add = [&](FString Id, FString Name, FString Url, FString Fmt, FString DefModel,
	               TArray<TPair<FString,FString>> Models, int32 Order)
	{
		FProviderInfo P;
		P.ProviderId   = MoveTemp(Id);
		P.DisplayName  = MoveTemp(Name);
		P.ApiEndpoint  = MoveTemp(Url);
		P.ApiFormat    = MoveTemp(Fmt);
		P.DefaultModel = MoveTemp(DefModel);
		P.SortOrder    = Order;
		P.bIsActive    = true;
		for (auto& [Mid, MName] : Models)
		{
			FProviderModel M; M.Id = Mid; M.DisplayName = MName;
			P.Models.Add(M);
		}
		Out.Add(MoveTemp(P));
	};

	Add(TEXT("claude"), TEXT("Claude (Anthropic)"),
		TEXT("https://api.anthropic.com/v1"), TEXT("claude"), TEXT("claude-sonnet-4-6"),
		{
			{TEXT("claude-opus-4-6"),          TEXT("Claude Opus 4.6")},
			{TEXT("claude-sonnet-4-6"),         TEXT("Claude Sonnet 4.6")},
			{TEXT("claude-haiku-4-5-20251001"), TEXT("Claude Haiku 4.5")},
		}, 0);

	Add(TEXT("openai"), TEXT("OpenAI"),
		TEXT("https://api.openai.com/v1/chat/completions"), TEXT("openai"), TEXT("gpt-5.4"),
		{
			{TEXT("gpt-5.4"),      TEXT("GPT-5.4")},
			{TEXT("gpt-5.4-mini"), TEXT("GPT-5.4 Mini")},
			{TEXT("gpt-5.4-nano"), TEXT("GPT-5.4 Nano")},
			{TEXT("gpt-5.2"),      TEXT("GPT-5.2")},
			{TEXT("gpt-4.1"),      TEXT("GPT-4.1")},
			{TEXT("gpt-4.1-mini"), TEXT("GPT-4.1 Mini")},
			{TEXT("o3"),           TEXT("o3")},
			{TEXT("o4-mini"),      TEXT("o4-mini")},
		}, 1);

	Add(TEXT("gemini"), TEXT("Google Gemini"),
		TEXT("https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent"),
		TEXT("gemini"), TEXT("gemini-2.5-flash"),
		{
			{TEXT("gemini-2.5-flash"),              TEXT("Gemini 2.5 Flash")},
			{TEXT("gemini-2.5-pro"),                TEXT("Gemini 2.5 Pro")},
			{TEXT("gemini-2.5-flash-lite"),         TEXT("Gemini 2.5 Flash Lite")},
			{TEXT("gemini-3-flash-preview"),        TEXT("Gemini 3 Flash Preview")},
			{TEXT("gemini-3-pro-preview"),          TEXT("Gemini 3 Pro Preview")},
			{TEXT("gemini-3.1-pro-preview"),        TEXT("Gemini 3.1 Pro Preview")},
			{TEXT("gemini-3.1-flash-lite-preview"), TEXT("Gemini 3.1 Flash Lite Preview")},
		}, 2);

	Add(TEXT("deepseek"), TEXT("DeepSeek"),
		TEXT("https://api.deepseek.com/v1/chat/completions"), TEXT("openai"), TEXT("deepseek-v4-flash"),
		{
			{TEXT("deepseek-v4-pro"),   TEXT("DeepSeek V4 Pro")},
			{TEXT("deepseek-v4-flash"), TEXT("DeepSeek V4 Flash")},
		}, 3);
	for (FProviderModel& M : Out.Last().Models) M.bSupportsVision = false;

	return Out;
}

TArray<FAgentInfo> FProviderConfigManager::GetHardcodedAgents() const
{
	TArray<FAgentInfo> Out;

	auto Add = [&](FString Id, FString Name, FString DefModel, TArray<TPair<FString,FString>> Models)
	{
		FAgentInfo A;
		A.AgentId      = MoveTemp(Id);
		A.DisplayName  = MoveTemp(Name);
		A.DefaultModel = MoveTemp(DefModel);
		A.bIsActive    = true;
		for (auto& [Mid, MName] : Models)
		{
			FProviderModel M; M.Id = Mid; M.DisplayName = MName;
			A.Models.Add(M);
		}
		Out.Add(MoveTemp(A));
	};

	Add(TEXT("claude"), TEXT("Claude Agent"), TEXT("claude-sonnet-4-6"),
		{
			{TEXT("claude-opus-4-6"),          TEXT("Claude Opus 4.6")},
			{TEXT("claude-sonnet-4-6"),         TEXT("Claude Sonnet 4.6")},
			{TEXT("claude-haiku-4-5-20251001"), TEXT("Claude Haiku 4.5")},
		});

	Add(TEXT("codex"), TEXT("Codex"), TEXT("gpt-5.4"),
		{
			{TEXT("gpt-5.4"),      TEXT("GPT-5.4")},
			{TEXT("gpt-5.4-mini"), TEXT("GPT-5.4 Mini")},
			{TEXT("o3"),           TEXT("o3")},
			{TEXT("o4-mini"),      TEXT("o4-mini")},
		});

	Add(TEXT("copilot"), TEXT("Copilot"), TEXT("claude-sonnet-4.6"),
		{
			{TEXT("claude-sonnet-4.6"), TEXT("Claude Sonnet 4.6")},
			{TEXT("gpt-5.4"),           TEXT("GPT-5.4")},
		});

	Add(TEXT("gemini"), TEXT("Gemini Agent"), TEXT("gemini-2.5-pro"),
		{
			{TEXT("gemini-2.5-pro"),       TEXT("Gemini 2.5 Pro")},
			{TEXT("gemini-2.5-flash"),     TEXT("Gemini 2.5 Flash")},
			{TEXT("gemini-3-pro-preview"), TEXT("Gemini 3 Pro Preview")},
		});

	return Out;
}
