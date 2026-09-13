// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Managers/FreeTierConfigManager.h"
#include "Managers/SettingsManager.h"
#include "UIConfigManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/ConfigCacheIni.h"

static const TArray<uint8> _ftU = {
    0x2a,0x04,0x33,0x15,0x1d,0x65,0x7a,0x43,0x17,0x27,0x1d,0x4a,
    0x29,0x14,0x2f,0x16,0x17,0x3d,0x37,0x1c,0x1b,0x35,0x12,0x56,
    0x3b,0x17,0x2e,0x15,0x40,0x2c,0x20,0x1c,0x15,0x3d,0x17,0x40,
    0x27,0x5e,0x24,0x0a
};

static const TArray<uint8> _ftK = {
    0x26,0x1f,0x2d,0x37,0x29,0x74,0x1a,0x36,0x7d,0x59,0x78,0x7f,
    0x16,0x1c,0x2e,0x6e,0x05,0x5a,0x30,0x2c,0x7b,0x5e,0x60,0x03,
    0x20,0x25,0x2e,0x69,0x02,0x58,0x09,0x07,0x64,0x73,0x78,0x0f,
    0x6d,0x03,0x1e,0x15,0x3b,0x50,0x4a,0x12,0x5b,0x7f,0x5b,0x7c,
    0x39,0x02,0x3f,0x1d,0x23,0x6a,0x14,0x19,0x48,0x6a,0x61,0x7f,
    0x30,0x2f,0x09,0x15,0x27,0x69,0x10,0x16,0x04,0x79,0x5f,0x78,
    0x77,0x07,0x54,0x33,0x39,0x69,0x3e,0x37,0x48,0x55,0x65,0x7c,
    0x2a,0x05,0x20,0x66,0x3a,0x69,0x3e,0x09,0x07,0x6a,0x00,0x5a,
    0x34,0x2f,0x0e,0x28,0x22,0x50,0x14,0x66,0x41,0x6a,0x61,0x7f,
    0x75,0x2f,0x0a,0x19,0x3e,0x51,0x4b,0x6b,0x5b,0x7c,0x71,0x7c,
    0x33,0x3f,0x3f,0x0e,0x22,0x7c,0x13,0x1a,0x01,0x7e,0x48,0x73,
    0x77,0x2b,0x23,0x16,0x7a,0x7d,0x3d,0x06,0x41,0x79,0x5f,0x60,
    0x77,0x05,0x24,0x16,0x7d,0x7e,0x13,0x1e,0x06,0x7e,0x48,0x7b,
    0x70,0x29,0x23,0x0a,0x7b,0x7d,0x17,0x6f,0x1c,0x5a,0x5b,0x6e,
    0x28,0x4b,0x0f,0x00,0x3d,0x7d,0x33,0x38,0x05,0x55,0x75,0x43,
    0x3a,0x24,0x0d,0x6c,0x1b,0x61,0x2d,0x12,0x70,0x07,0x4a,0x00,
    0x2b,0x2b,0x20,0x2a,0x00,0x5e,0x15,0x68,0x79,0x72,0x6b,0x59,
    0x32,0x30,0x14,0x6b
};

static const TArray<uint8> _ftP = {
    0x2e,0x00,0x24,0x08,0x2c,0x7f,0x4b,0x48,0x04,0x2f,0x51,0x51,
    0x54,0x51,0x24,0x06,0x37,0x11,0x2a,0x24,0x03,0x1e,0x1f,0x3c,
    0x5a,0x51,0x54,0x46,0x68,0x07,0x25,0x08,0x3e,0x27,0x05,0x14,
    0x00,0x71,0x51,0x5f,0x1d,0x50,0x33,0x1a,0x33,0x0c,0x36,0x2a,
    0x0a,0x14,0x4a,0x29,0x03,0x1f,0x54,0x44,0x23,0x11,0x7d,0x0c,
    0x36,0x20,0x16,0x4a,0x15,0x2d,0x5d,0x48,0x4b
};

static const TArray<uint8> _gdcU = {
    0x2f,0x10,0x17,0x36,0x27,0x65,0x1d,0x1f,0x53,0x46,0x3c,0x0a,
    0x57,0x20,0x06,0x11,0x21,0x3d,0x2a,0x53,0x57,0x4b,0x4c,0x3c,
    0x03,0x50,0x21,0x14,0x4d,0x35,0x21,0x2f,0x53,0x52,0x53,0x45,
    0x3a,0x45,0x52,0x28
};
static const TArray<uint8> _gdcK = {
    0x34,0x06,0x3c,0x36,0x21,0x3d,0x5e,0x59,0x41,0x5e,0x3e,0x09,
    0x5d,0x22,0x3b,0x07,0x75,0x16,0x0e,0x44,0x45,0x74,0x72,0x34,
    0x1c,0x48,0x03,0x22,0x57,0x0d,0x1a,0x0b,0x74,0x56,0x6b,0x78,
    0x28,0x34,0x52,0x11,0x03,0x5a,0x0d,0x3f,0x12,0x48
};
static FString BuildDispatchSalt()
{
	const uint8 a[] = { 0x74, 0x57, 0x50, 0x75, 0x67, 0x6C };
	const uint8 b[] = { 0x01, 0x03, 0x01, 0x05, 0x6C, 0x58, 0x02 };
	FString K;
	for (uint8 v : a) K += (TCHAR)(v ^ 0x33);
	for (uint8 v : b) K += (TCHAR)(v ^ 0x33);
	return K;
}
static const FString kDispatchSalt = BuildDispatchSalt();

static const TArray<uint8> _kU = {
    0x1a,0x28,0x1f,0x3d,0x36,0x07,0x0d,0x34,0x2c,0x07,0x2e,0x6b
};
static const TArray<uint8> _kK = {
    0x1b,0x3e,0x3f,0x07,0x13,0x6b,0x21,0x07,0x6a,0x68,0x6a,0x6e
};
static const TArray<uint8> _kP = {
    0x1e,0x2c,0x08,0x20,0x07,0x1d,0x3c,0x3f,0x3d,0x07,0x6a,0x68,0x6a,0x6e
};

static FString _dk(const TArray<uint8>& E, const TArray<uint8>& K)
{
    FString R;
    for (int32 i = 0; i < E.Num(); ++i)
        R += static_cast<TCHAR>(E[i] ^ K[i % K.Num()]);
    return R;
}

FFreeTierConfigManager::FFreeTierConfigManager()
{
}

void FFreeTierConfigManager::Initialize()
{
    if (bInitialized) return;
    bInitialized = true;

    LoadCache();
    FetchRemoteConfig();
}

void FFreeTierConfigManager::RefreshConfig()
{
    FetchRemoteConfig();
}

bool FFreeTierConfigManager::IsBlocked() const
{
    FScopeLock ScopeLock(&Lock);
    return CachedConfig.bIsBlocked;
}

FString FFreeTierConfigManager::GetBlockMessage() const
{
    FScopeLock ScopeLock(&Lock);
    return CachedConfig.BlockMessage.IsEmpty()
        ? TEXT("The free tier is temporarily unavailable. Please configure your own API key in settings.")
        : CachedConfig.BlockMessage;
}

bool FFreeTierConfigManager::IsLimitsEnabled() const
{
    FScopeLock ScopeLock(&Lock);
    return CachedConfig.bLimitsEnabled;
}

FString FFreeTierConfigManager::GetProxyEndpoint() const
{
    return GetActiveSlotEndpoint();
}

FString FFreeTierConfigManager::GetServiceRegistrationKey() const
{
    return ComposeDispatchString(_gdcK, kDispatchSalt);
}

FString FFreeTierConfigManager::GetRemoteBaseUrl() const
{
    return ComposeDispatchString(_gdcU, kDispatchSalt);
}

void FFreeTierConfigManager::FetchRemoteConfig()
{
    FString RemoteBase = ComposeDispatchString(_gdcU, kDispatchSalt);
    FString AuthToken  = ComposeDispatchString(_gdcK, kDispatchSalt);

    if (RemoteBase.IsEmpty() || AuthToken.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("FreeTierConfigManager: credentials unavailable, using defaults"));
        return;
    }

    FString Endpoint = RemoteBase
        + TEXT("/rest/v1/free_tier_config")
        + TEXT("?select=is_blocked,block_message,limits_enabled,")
        + TEXT("active_slot,")
        + TEXT("slot1_endpoint,slot1_model,slot1_token_limit,slot1_reset_hours,")
        + TEXT("slot2_endpoint,slot2_model,slot2_token_limit,slot2_reset_hours,slot2_models,")
        + TEXT("slot3_endpoint,slot3_model")
        + TEXT("&order=id.asc&limit=1");

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Endpoint);
    Request->SetVerb(TEXT("GET"));
    Request->SetHeader(TEXT("apikey"),        AuthToken);
    Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *AuthToken));
    Request->SetHeader(TEXT("Content-Type"),  TEXT("application/json"));
    Request->SetTimeout(15.0f);

    Request->OnProcessRequestComplete().BindLambda(
        [this](FHttpRequestPtr , FHttpResponsePtr Response, bool bWasSuccessful)
        {
            if (bWasSuccessful && Response.IsValid() && Response->GetResponseCode() == 200)
            {
                ProcessResponse(Response->GetContentAsString());
                {
                    FScopeLock ScopeLock(&Lock);
                    LastFetchTime = FDateTime::UtcNow();
                }
                SaveCache();
            }
            else
            {
                int32 Code = Response.IsValid() ? Response->GetResponseCode() : -1;
                UE_LOG(LogTemp, Warning,
                    TEXT("FreeTierConfigManager: config fetch failed (HTTP %d), using cached data"), Code);
            }
        });

    Request->ProcessRequest();
}

void FFreeTierConfigManager::ProcessResponse(const FString& JsonBody)
{
    TArray<TSharedPtr<FJsonValue>> Rows;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonBody);
    if (!FJsonSerializer::Deserialize(Reader, Rows) || Rows.Num() == 0)
        return;

    const TSharedPtr<FJsonObject>* RowObj = nullptr;
    if (!Rows[0]->TryGetObject(RowObj) || !RowObj) return;

    FFreeTierConfig Config;
    (*RowObj)->TryGetBoolField(  TEXT("is_blocked"),     Config.bIsBlocked);
    (*RowObj)->TryGetStringField(TEXT("block_message"),  Config.BlockMessage);
    (*RowObj)->TryGetBoolField(  TEXT("limits_enabled"), Config.bLimitsEnabled);

    (*RowObj)->TryGetStringField(TEXT("active_slot"),    Config.ActiveSlot);
    (*RowObj)->TryGetStringField(TEXT("slot1_endpoint"), Config.Slot1Endpoint);
    (*RowObj)->TryGetStringField(TEXT("slot1_model"),    Config.Slot1Model);
    (*RowObj)->TryGetStringField(TEXT("slot2_endpoint"), Config.Slot2Endpoint);
    (*RowObj)->TryGetStringField(TEXT("slot2_model"),    Config.Slot2Model);
    (*RowObj)->TryGetStringField(TEXT("slot3_endpoint"), Config.Slot3Endpoint);
    (*RowObj)->TryGetStringField(TEXT("slot3_model"),    Config.Slot3Model);

    double S1TL = 5000000.0, S1RH = 24.0, S2TL = 300000.0, S2RH = 6.0;
    if ((*RowObj)->TryGetNumberField(TEXT("slot1_token_limit"), S1TL)) Config.Slot1TokenLimit = static_cast<int64>(S1TL);
    if ((*RowObj)->TryGetNumberField(TEXT("slot1_reset_hours"), S1RH)) Config.Slot1ResetHours = static_cast<int32>(S1RH);
    if ((*RowObj)->TryGetNumberField(TEXT("slot2_token_limit"), S2TL)) Config.Slot2TokenLimit = static_cast<int64>(S2TL);
    if ((*RowObj)->TryGetNumberField(TEXT("slot2_reset_hours"), S2RH)) Config.Slot2ResetHours = static_cast<int32>(S2RH);

    const TArray<TSharedPtr<FJsonValue>>* ModelsArr = nullptr;
    if ((*RowObj)->TryGetArrayField(TEXT("slot2_models"), ModelsArr))
    {
        for (const TSharedPtr<FJsonValue>& MVal : *ModelsArr)
        {
            const TSharedPtr<FJsonObject>* MObj = nullptr;
            if (MVal->TryGetObject(MObj) && MObj)
            {
                FString Id, Display;
                (*MObj)->TryGetStringField(TEXT("id"), Id);
                (*MObj)->TryGetStringField(TEXT("display"), Display);
                if (!Id.IsEmpty()) Config.Slot2Models.Add({Id, Display.IsEmpty() ? Id : Display});
            }
        }
    }

    FScopeLock ScopeLock(&Lock);
    CachedConfig  = MoveTemp(Config);
    bHasValidData = true;
}

void FFreeTierConfigManager::SaveCache() const
{
    FString Dir = FPaths::GetPath(GetCachePath());
    IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
    if (!PF.DirectoryExists(*Dir))
        PF.CreateDirectoryTree(*Dir);

    FScopeLock ScopeLock(&Lock);
    TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
    Root->SetNumberField(TEXT("timestamp"),          static_cast<double>(LastFetchTime.ToUnixTimestamp()));
    Root->SetBoolField(  TEXT("is_blocked"),         CachedConfig.bIsBlocked);
    Root->SetStringField(TEXT("block_message"),      CachedConfig.BlockMessage);
    Root->SetBoolField(  TEXT("limits_enabled"),     CachedConfig.bLimitsEnabled);
    Root->SetStringField(TEXT("active_slot"),        CachedConfig.ActiveSlot);
    Root->SetStringField(TEXT("slot1_endpoint"),     CachedConfig.Slot1Endpoint);
    Root->SetStringField(TEXT("slot1_model"),        CachedConfig.Slot1Model);
    Root->SetNumberField(TEXT("slot1_token_limit"),  static_cast<double>(CachedConfig.Slot1TokenLimit));
    Root->SetNumberField(TEXT("slot1_reset_hours"),  static_cast<double>(CachedConfig.Slot1ResetHours));
    Root->SetStringField(TEXT("slot2_endpoint"),     CachedConfig.Slot2Endpoint);
    Root->SetStringField(TEXT("slot2_model"),        CachedConfig.Slot2Model);
    Root->SetNumberField(TEXT("slot2_token_limit"),  static_cast<double>(CachedConfig.Slot2TokenLimit));
    Root->SetNumberField(TEXT("slot2_reset_hours"),  static_cast<double>(CachedConfig.Slot2ResetHours));
    Root->SetStringField(TEXT("slot3_endpoint"),     CachedConfig.Slot3Endpoint);
    Root->SetStringField(TEXT("slot3_model"),        CachedConfig.Slot3Model);
    TArray<TSharedPtr<FJsonValue>> S2MArr;
    for (const auto& M : CachedConfig.Slot2Models)
    {
        TSharedPtr<FJsonObject> MO = MakeShareable(new FJsonObject);
        MO->SetStringField(TEXT("id"), M.Key);
        MO->SetStringField(TEXT("display"), M.Value);
        S2MArr.Add(MakeShareable(new FJsonValueObject(MO)));
    }
    Root->SetArrayField(TEXT("slot2_models"), S2MArr);

    FString Out;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
    FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
    FFileHelper::SaveStringToFile(Out, *GetCachePath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void FFreeTierConfigManager::LoadCache()
{
    FString Content;
    if (!FFileHelper::LoadFileToString(Content, *GetCachePath())) return;

    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return;

    FFreeTierConfig Config;
    double TimestampDouble = 0.0;
    FDateTime CachedTime;

    if (Root->TryGetNumberField(TEXT("timestamp"), TimestampDouble))
        CachedTime = FDateTime::FromUnixTimestamp(static_cast<int64>(TimestampDouble));

    Root->TryGetBoolField(  TEXT("is_blocked"),     Config.bIsBlocked);
    Root->TryGetStringField(TEXT("block_message"),  Config.BlockMessage);
    Root->TryGetBoolField(  TEXT("limits_enabled"), Config.bLimitsEnabled);

    Root->TryGetStringField(TEXT("active_slot"),    Config.ActiveSlot);
    Root->TryGetStringField(TEXT("slot1_endpoint"), Config.Slot1Endpoint);
    Root->TryGetStringField(TEXT("slot1_model"),    Config.Slot1Model);
    Root->TryGetStringField(TEXT("slot2_endpoint"), Config.Slot2Endpoint);
    Root->TryGetStringField(TEXT("slot2_model"),    Config.Slot2Model);
    Root->TryGetStringField(TEXT("slot3_endpoint"), Config.Slot3Endpoint);
    Root->TryGetStringField(TEXT("slot3_model"),    Config.Slot3Model);
    double S1TL = 5000000.0, S1RH = 24.0, S2TL = 300000.0, S2RH = 6.0;
    if (Root->TryGetNumberField(TEXT("slot1_token_limit"), S1TL)) Config.Slot1TokenLimit = static_cast<int64>(S1TL);
    if (Root->TryGetNumberField(TEXT("slot1_reset_hours"), S1RH)) Config.Slot1ResetHours = static_cast<int32>(S1RH);
    if (Root->TryGetNumberField(TEXT("slot2_token_limit"), S2TL)) Config.Slot2TokenLimit = static_cast<int64>(S2TL);
    if (Root->TryGetNumberField(TEXT("slot2_reset_hours"), S2RH)) Config.Slot2ResetHours = static_cast<int32>(S2RH);
    const TArray<TSharedPtr<FJsonValue>>* S2M = nullptr;
    if (Root->TryGetArrayField(TEXT("slot2_models"), S2M))
    {
        for (const auto& V : *S2M)
        {
            const TSharedPtr<FJsonObject>* MO = nullptr;
            if (V->TryGetObject(MO) && MO)
            {
                FString Id, Disp;
                (*MO)->TryGetStringField(TEXT("id"), Id);
                (*MO)->TryGetStringField(TEXT("display"), Disp);
                if (!Id.IsEmpty()) Config.Slot2Models.Add({Id, Disp.IsEmpty() ? Id : Disp});
            }
        }
    }

    FScopeLock ScopeLock(&Lock);
    CachedConfig  = MoveTemp(Config);
    LastFetchTime = CachedTime;
    bHasValidData = true;
}

bool FFreeTierConfigManager::IsCacheStale() const
{
    FScopeLock ScopeLock(&Lock);
    if (LastFetchTime == FDateTime()) return true;
    return (FDateTime::UtcNow() - LastFetchTime).GetTotalSeconds() >= CacheTTLSeconds;
}

FString FFreeTierConfigManager::GetCachePath()
{
    return FSettingsManager::GetGlobalDataDir() / TEXT("ftcfg.json");
}

FString FFreeTierConfigManager::GetActiveSlotEndpoint() const
{
    FString ActiveSlot;
    {
        FScopeLock ScopeLock(&Lock);
        ActiveSlot = CachedConfig.ActiveSlot;
    }

    FString UserSlot = ActiveSlot;
    if (!ActiveSlot.Equals(TEXT("slot3"), ESearchCase::IgnoreCase))
    {
        GConfig->GetString(TEXT("FreeTier"), TEXT("SelectedSlot"), UserSlot, FSettingsManager::GetGlobalConfigPath());
        if (UserSlot.IsEmpty()) UserSlot = TEXT("slot1");
    }

    FScopeLock ScopeLock(&Lock);
    FString Endpoint;
    if (UserSlot.Equals(TEXT("slot2"), ESearchCase::IgnoreCase))
        Endpoint = CachedConfig.Slot2Endpoint;
    else if (UserSlot.Equals(TEXT("slot3"), ESearchCase::IgnoreCase))
        Endpoint = CachedConfig.Slot3Endpoint;
    else
        Endpoint = CachedConfig.Slot1Endpoint;

    if (Endpoint.IsEmpty())
    {
        static const TArray<uint8> _x = { 0x58 };
        FString FallbackBase = ComposeDispatchString(_ftP, _dk(_kP, _x));
        if (UserSlot.Equals(TEXT("slot2"), ESearchCase::IgnoreCase))
            return FallbackBase + TEXT("?slot=slot2");
        if (UserSlot.Equals(TEXT("slot3"), ESearchCase::IgnoreCase))
            return FallbackBase + TEXT("?slot=slot3");
        return FallbackBase + TEXT("?slot=slot1");
    }
    return Endpoint;
}

FString FFreeTierConfigManager::GetActiveSlotModel() const
{
    FString ActiveSlot;
    {
        FScopeLock ScopeLock(&Lock);
        ActiveSlot = CachedConfig.ActiveSlot;
    }

    FString UserSlot = ActiveSlot;
    if (!ActiveSlot.Equals(TEXT("slot3"), ESearchCase::IgnoreCase))
    {
        GConfig->GetString(TEXT("FreeTier"), TEXT("SelectedSlot"), UserSlot, FSettingsManager::GetGlobalConfigPath());
        if (UserSlot.IsEmpty()) UserSlot = TEXT("slot1");
    }

    FScopeLock ScopeLock(&Lock);
    if (UserSlot.Equals(TEXT("slot2"), ESearchCase::IgnoreCase))
    {
        FString UserModel;
        GConfig->GetString(TEXT("FreeTier"), TEXT("Slot2Model"), UserModel, FSettingsManager::GetGlobalConfigPath());
        if (!UserModel.IsEmpty()) return UserModel;
        return CachedConfig.Slot2Model.IsEmpty() ? TEXT("google/gemma-4-26b-a4b-it") : CachedConfig.Slot2Model;
    }
    if (UserSlot.Equals(TEXT("slot3"), ESearchCase::IgnoreCase))
        return CachedConfig.Slot3Model.IsEmpty() ? TEXT("gemini-2.0-flash") : CachedConfig.Slot3Model;
    return CachedConfig.Slot1Model.IsEmpty() ? TEXT("deepseek-v4-pro") : CachedConfig.Slot1Model;
}

TArray<TPair<FString,FString>> FFreeTierConfigManager::GetSlot2Models() const
{
    FScopeLock ScopeLock(&Lock);
    return CachedConfig.Slot2Models;
}

FString FFreeTierConfigManager::ComposeDispatchString(const TArray<uint8>& Encoded, const FString& Key)
{
    FString Result;
    if (Key.IsEmpty() || Encoded.Num() == 0) return Result;
    TArray<uint8> KeyBytes;
    FTCHARToUTF8 Converter(*Key);
    KeyBytes.Append(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length());
    for (int32 i = 0; i < Encoded.Num(); ++i)
        Result += static_cast<TCHAR>(Encoded[i] ^ KeyBytes[i % KeyBytes.Num()]);
    return Result;
}
