// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "HAL/FileManager.h"
#include "Widgets/SWidget.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Managers/SettingsManager.h"

enum class EUIConfigCategory : uint8
{
	Branding,
	Tabs,
	Footer,
	Buttons,
	Notices,
	HowTo,
	Common,
	Tooltips,
	Messages,
	Labels,
	Status,
	Errors
};

struct FUIConfigEntry
{
	FString Key;
	FString Value;
	FString Category;
	FString DefaultValue;
	bool bIsValid = false;

	FString ValueEn, ValueEs, ValueFr, ValueDe, ValueZh, ValueJa, ValueRu, ValuePt;
	FString ValueKo, ValueIt, ValueAr, ValueNl, ValueTr;
	FString ValuePl, ValueVi, ValueId, ValueHi, ValueRo, ValueTh, ValueUk;

	FString GetValue(const FString& LangCode) const
	{
		if (LangCode == TEXT("es")) return !ValueEs.IsEmpty() ? ValueEs : ValueEn;
		if (LangCode == TEXT("fr")) return !ValueFr.IsEmpty() ? ValueFr : ValueEn;
		if (LangCode == TEXT("de")) return !ValueDe.IsEmpty() ? ValueDe : ValueEn;
		if (LangCode == TEXT("zh")) return !ValueZh.IsEmpty() ? ValueZh : ValueEn;
		if (LangCode == TEXT("ja")) return !ValueJa.IsEmpty() ? ValueJa : ValueEn;
		if (LangCode == TEXT("ru")) return !ValueRu.IsEmpty() ? ValueRu : ValueEn;
		if (LangCode == TEXT("pt")) return !ValuePt.IsEmpty() ? ValuePt : ValueEn;
		if (LangCode == TEXT("ko")) return !ValueKo.IsEmpty() ? ValueKo : ValueEn;
		if (LangCode == TEXT("it")) return !ValueIt.IsEmpty() ? ValueIt : ValueEn;
		if (LangCode == TEXT("ar")) return !ValueAr.IsEmpty() ? ValueAr : ValueEn;
		if (LangCode == TEXT("nl")) return !ValueNl.IsEmpty() ? ValueNl : ValueEn;
		if (LangCode == TEXT("tr")) return !ValueTr.IsEmpty() ? ValueTr : ValueEn;
		if (LangCode == TEXT("pl")) return !ValuePl.IsEmpty() ? ValuePl : ValueEn;
		if (LangCode == TEXT("vi")) return !ValueVi.IsEmpty() ? ValueVi : ValueEn;
		if (LangCode == TEXT("id")) return !ValueId.IsEmpty() ? ValueId : ValueEn;
		if (LangCode == TEXT("hi")) return !ValueHi.IsEmpty() ? ValueHi : ValueEn;
		if (LangCode == TEXT("ro")) return !ValueRo.IsEmpty() ? ValueRo : ValueEn;
		if (LangCode == TEXT("th")) return !ValueTh.IsEmpty() ? ValueTh : ValueEn;
		if (LangCode == TEXT("uk")) return !ValueUk.IsEmpty() ? ValueUk : ValueEn;
		return ValueEn;
	}
};

DECLARE_MULTICAST_DELEGATE(FOnUIConfigRefreshed);

class FUIConfigManager
{
public:
	static FUIConfigManager& Get()
	{
		static FUIConfigManager Instance;
		return Instance;
	}

	FOnUIConfigRefreshed OnConfigRefreshed;

	void Initialize()
	{
		if (bInitialized) return;
		bInitialized = true;
		LoadCachedConfig();
		LoadLanguagePreference();
		FetchRemoteConfig();
	}

	void SetLanguage(const FString& LanguageCode)
	{
		FScopeLock Lock(&ConfigCriticalSection);
		if (CurrentLanguage != LanguageCode)
		{
			CurrentLanguage = LanguageCode;
			SaveLanguagePreference();
			UE_LOG(LogTemp, Log, TEXT("UIConfigManager: Language changed to: %s"), *LanguageCode);
		}
	}

	FString GetLanguage() const
	{
		return CurrentLanguage;
	}

	static FString GetLanguageColumnName(const FString& LangCode)
	{
		if (LangCode == TEXT("en")) return TEXT("element_value_en");
		if (LangCode == TEXT("es")) return TEXT("element_value_es");
		if (LangCode == TEXT("fr")) return TEXT("element_value_fr");
		if (LangCode == TEXT("de")) return TEXT("element_value_de");
		if (LangCode == TEXT("zh")) return TEXT("element_value_zh");
		if (LangCode == TEXT("ja")) return TEXT("element_value_ja");
		if (LangCode == TEXT("ru")) return TEXT("element_value_ru");
		if (LangCode == TEXT("pt")) return TEXT("element_value_pt");
		if (LangCode == TEXT("ko")) return TEXT("element_value_ko");
		if (LangCode == TEXT("it")) return TEXT("element_value_it");
		if (LangCode == TEXT("ar")) return TEXT("element_value_ar");
		if (LangCode == TEXT("nl")) return TEXT("element_value_nl");
		if (LangCode == TEXT("tr")) return TEXT("element_value_tr");
		if (LangCode == TEXT("pl")) return TEXT("element_value_pl");
		if (LangCode == TEXT("vi")) return TEXT("element_value_vi");
		if (LangCode == TEXT("id")) return TEXT("element_value_id");
		if (LangCode == TEXT("hi")) return TEXT("element_value_hi");
		if (LangCode == TEXT("ro")) return TEXT("element_value_ro");
		if (LangCode == TEXT("th")) return TEXT("element_value_th");
		if (LangCode == TEXT("uk")) return TEXT("element_value_uk");
		return TEXT("element_value_en");
	}

	FText GetText(const FString& Key, const FString& DefaultValue = TEXT(""))
	{
		FString Result = GetValue(Key, DefaultValue);
		return FText::FromString(Result);
	}

	FString GetAllValuesAsJson() const
	{
		FScopeLock Lock(&ConfigCriticalSection);
		FString Out = TEXT("{");
		bool bFirst = true;
		for (const auto& Pair : ConfigCache)
		{
			FString Val = Pair.Value.GetValue(CurrentLanguage);
			if (Val.IsEmpty()) continue;
			Val = Val.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""))
			         .Replace(TEXT("\n"), TEXT("\\n")).Replace(TEXT("\r"), TEXT(""));
			if (!bFirst) Out += TEXT(",");
			Out += FString::Printf(TEXT("\"%s\":\"%s\""), *Pair.Key, *Val);
			bFirst = false;
		}
		Out += TEXT("}");
		return Out;
	}

	FString GetValue(const FString& Key, const FString& DefaultValue = TEXT(""))
	{
		FScopeLock Lock(&ConfigCriticalSection);

		FUIConfigEntry* Entry = ConfigCache.Find(Key);
		if (Entry && Entry->bIsValid)
		{
			FString LangValue = Entry->GetValue(CurrentLanguage);
			if (!LangValue.IsEmpty())
			{
				UE_LOG(LogTemp, Verbose, TEXT("UIConfigManager: GetValue('%s') -> '%s' (lang: %s)"), *Key, *LangValue, *CurrentLanguage);
				return LangValue;
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("UIConfigManager: GetValue('%s') - Empty value for lang %s, using default"), *Key, *CurrentLanguage);
			}
		}
		else
		{
			UE_LOG(LogTemp, Verbose, TEXT("UIConfigManager: GetValue('%s') - Key not found in cache, using default"), *Key);
		}
		return DefaultValue;
	}

	FString GetURL(const FString& Key, const FString& DefaultValue = TEXT(""))
	{
		return GetValue(Key, DefaultValue);
	}

	bool GetBool(const FString& Key, bool DefaultValue = false)
	{
		FString Value = GetValue(Key, TEXT(""));
		if (Value.IsEmpty()) return DefaultValue;
		return Value.ToLower() == TEXT("true") || Value == TEXT("1");
	}

	void FetchRemoteConfig()
	{
		static const TArray<uint8> _uiU = {
			0x2f,0x10,0x17,0x36,0x27,0x65,0x1d,0x1f,0x53,0x46,0x3c,0x0a,
			0x57,0x20,0x06,0x11,0x21,0x3d,0x2a,0x53,0x57,0x4b,0x4c,0x3c,
			0x03,0x50,0x21,0x14,0x4d,0x35,0x21,0x2f,0x53,0x52,0x53,0x45,
			0x3a,0x45,0x52,0x28
		};
		static const TArray<uint8> _uiK = {
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
		static const auto MakeSaltA = []() {
			const uint8 a[] = { 0x74, 0x57, 0x50, 0x75, 0x67, 0x6C, 0x01, 0x03, 0x01, 0x05, 0x6C, 0x58, 0x02 };
			FString K; for (uint8 v : a) K += (TCHAR)(v ^ 0x33); return K;
		};
		static const FString _uiXK = MakeSaltA();
		FString SupabaseURL = ComposeDispatchString(_uiU, _uiXK);
		FString AnonKey     = ComposeDispatchString(_uiK, _uiXK);

		if (SupabaseURL.IsEmpty())
		{
			UE_LOG(LogTemp, Verbose, TEXT("UIConfigManager: No remote base configured, using defaults"));
			return;
		}

		FString ConfigEndpoint = SupabaseURL + TEXT("/rest/v1/ui_config?select=element_key,")
			TEXT("element_value_en,element_value_es,element_value_fr,element_value_de,")
			TEXT("element_value_zh,element_value_ja,element_value_ru,element_value_pt,")
			TEXT("element_value_ko,element_value_it,element_value_ar,element_value_nl,")
			TEXT("element_value_tr,element_value_pl,element_value_vi,element_value_id,")
			TEXT("element_value_hi,element_value_ro,element_value_th,element_value_uk,")
			TEXT("category&is_active=eq.true");

		TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
		Request->SetURL(ConfigEndpoint);
		Request->SetVerb(TEXT("GET"));
		Request->SetHeader(TEXT("apikey"), AnonKey);
		Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *AnonKey));
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

		Request->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr Req, FHttpResponsePtr Response, bool bSuccess)
		{
			if (bSuccess && Response.IsValid() && Response->GetResponseCode() == 200)
			{
				ProcessConfigResponse(Response->GetContentAsString());
				LastFetchTime = FDateTime::UtcNow();
				SaveCachedConfig();
				UE_LOG(LogTemp, Log, TEXT("UIConfigManager: Successfully fetched remote config"));
			}
			else
			{
				int32 Code = Response.IsValid() ? Response->GetResponseCode() : -1;
				UE_LOG(LogTemp, Warning, TEXT("UIConfigManager: Failed to fetch config (Code: %d)"), Code);
			}
		});

		Request->ProcessRequest();
	}

	void RefreshConfig()
	{
		FetchRemoteConfig();
	}

	void InvalidateCache()
	{
		FScopeLock Lock(&ConfigCriticalSection);
		ConfigCache.Empty();
	}

	bool IsCacheValid() const
	{
		if (CacheTTL <= 0) return true;
		FDateTime Now = FDateTime::UtcNow();
		FTimespan Age = Now - LastFetchTime;
		return Age.GetTotalSeconds() < CacheTTL;
	}

	void SetCacheTTL(int32 Seconds)
	{
		CacheTTL = Seconds;
	}

	void SetSupabaseConfig(const FString& URL, const FString& AnonKey)
	{
		ConfiguredSupabaseURL = URL;
		ConfiguredServiceKey = AnonKey;
	}

private:
	FUIConfigManager()
		: CurrentLanguage(TEXT("en"))
		, CacheTTL(0)
		, bInitialized(false)
	{}

	FUIConfigManager(const FUIConfigManager&) = delete;
	FUIConfigManager& operator=(const FUIConfigManager&) = delete;

	TMap<FString, FUIConfigEntry> ConfigCache;
	FString CurrentLanguage;
	int32 CacheTTL;
	FDateTime LastFetchTime;
	bool bInitialized;
	mutable FCriticalSection ConfigCriticalSection;

	FString ConfiguredSupabaseURL;
	FString ConfiguredServiceKey;

	void LoadLanguagePreference()
	{
		FString SavedLanguage;
		if (GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("Language"), SavedLanguage, FSettingsManager::GetGlobalConfigPath()))
		{
			if (!SavedLanguage.IsEmpty())
			{
				CurrentLanguage = SavedLanguage;
			}
		}
	}

	void SaveLanguagePreference()
	{
		GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("Language"), *CurrentLanguage, FSettingsManager::GetGlobalConfigPath());
		GConfig->Flush(false, FSettingsManager::GetGlobalConfigPath());
	}

	void ProcessConfigResponse(const FString& JSONResponse)
	{
		TArray<TSharedPtr<FJsonValue>> ConfigArray;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JSONResponse);

		if (!FJsonSerializer::Deserialize(Reader, ConfigArray) || ConfigArray.Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("UIConfigManager: Failed to parse config JSON or empty array"));
			return;
		}

		FScopeLock Lock(&ConfigCriticalSection);
		int32 AddedCount = 0;

		for (const TSharedPtr<FJsonValue>& EntryValue : ConfigArray)
		{
			const TSharedPtr<FJsonObject>* EntryObj;
			if (!EntryValue->TryGetObject(EntryObj)) continue;

			FString Key, Category, DefaultValue;
			(*EntryObj)->TryGetStringField(TEXT("element_key"), Key);
			(*EntryObj)->TryGetStringField(TEXT("category"), Category);
			(*EntryObj)->TryGetStringField(TEXT("default_value"), DefaultValue);

			if (!Key.IsEmpty())
			{
				FUIConfigEntry Entry;
				Entry.Key = Key;
				Entry.Category = Category;
				Entry.DefaultValue = DefaultValue;
				Entry.bIsValid = true;

				(*EntryObj)->TryGetStringField(TEXT("element_value_en"), Entry.ValueEn);
				(*EntryObj)->TryGetStringField(TEXT("element_value_es"), Entry.ValueEs);
				(*EntryObj)->TryGetStringField(TEXT("element_value_fr"), Entry.ValueFr);
				(*EntryObj)->TryGetStringField(TEXT("element_value_de"), Entry.ValueDe);
				(*EntryObj)->TryGetStringField(TEXT("element_value_zh"), Entry.ValueZh);
				(*EntryObj)->TryGetStringField(TEXT("element_value_ja"), Entry.ValueJa);
				(*EntryObj)->TryGetStringField(TEXT("element_value_ru"), Entry.ValueRu);
				(*EntryObj)->TryGetStringField(TEXT("element_value_pt"), Entry.ValuePt);
				(*EntryObj)->TryGetStringField(TEXT("element_value_ko"), Entry.ValueKo);
				(*EntryObj)->TryGetStringField(TEXT("element_value_it"), Entry.ValueIt);
				(*EntryObj)->TryGetStringField(TEXT("element_value_ar"), Entry.ValueAr);
				(*EntryObj)->TryGetStringField(TEXT("element_value_nl"), Entry.ValueNl);
				(*EntryObj)->TryGetStringField(TEXT("element_value_tr"), Entry.ValueTr);
				(*EntryObj)->TryGetStringField(TEXT("element_value_pl"), Entry.ValuePl);
				(*EntryObj)->TryGetStringField(TEXT("element_value_vi"), Entry.ValueVi);
				(*EntryObj)->TryGetStringField(TEXT("element_value_id"), Entry.ValueId);
				(*EntryObj)->TryGetStringField(TEXT("element_value_hi"), Entry.ValueHi);
				(*EntryObj)->TryGetStringField(TEXT("element_value_ro"), Entry.ValueRo);
				(*EntryObj)->TryGetStringField(TEXT("element_value_th"), Entry.ValueTh);
				(*EntryObj)->TryGetStringField(TEXT("element_value_uk"), Entry.ValueUk);

				Entry.Value = Entry.ValueEn;

				ConfigCache.Add(Key, Entry);
				AddedCount++;

				if (AddedCount <= 3)
				{
					UE_LOG(LogTemp, Log, TEXT("UIConfigManager: Loaded '%s' - EN:'%s' ES:'%s' FR:'%s'"),
						*Key, *Entry.ValueEn.Left(30), *Entry.ValueEs.Left(30), *Entry.ValueFr.Left(30));
				}
			}
		}

		UE_LOG(LogTemp, Log, TEXT("UIConfigManager: Processed %d config entries, cache size: %d"), AddedCount, ConfigCache.Num());

		OnConfigRefreshed.Broadcast();
	}

	void SaveCachedConfig()
	{
		FString ConfigPath = FSettingsManager::GetGlobalDataDir() / TEXT("uiconfig.json");
		FString Directory = FPaths::GetPath(ConfigPath);
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		if (!PlatformFile.DirectoryExists(*Directory))
		{
			PlatformFile.CreateDirectoryTree(*Directory);
		}

		TSharedPtr<FJsonObject> RootObj = MakeShareable(new FJsonObject);
		RootObj->SetNumberField(TEXT("timestamp"), LastFetchTime.ToUnixTimestamp());
		RootObj->SetStringField(TEXT("language"), CurrentLanguage);

		TArray<TSharedPtr<FJsonValue>> EntriesArray;
		for (const auto& Pair : ConfigCache)
		{
			TSharedPtr<FJsonObject> EntryObj = MakeShareable(new FJsonObject);
			EntryObj->SetStringField(TEXT("key"), Pair.Value.Key);
			EntryObj->SetStringField(TEXT("value"), Pair.Value.Value);
			EntryObj->SetStringField(TEXT("category"), Pair.Value.Category);
			EntryObj->SetStringField(TEXT("default_value"), Pair.Value.DefaultValue);
			EntryObj->SetStringField(TEXT("value_en"), Pair.Value.ValueEn);
			EntryObj->SetStringField(TEXT("value_es"), Pair.Value.ValueEs);
			EntryObj->SetStringField(TEXT("value_fr"), Pair.Value.ValueFr);
			EntryObj->SetStringField(TEXT("value_de"), Pair.Value.ValueDe);
			EntryObj->SetStringField(TEXT("value_zh"), Pair.Value.ValueZh);
			EntryObj->SetStringField(TEXT("value_ja"), Pair.Value.ValueJa);
			EntryObj->SetStringField(TEXT("value_ru"), Pair.Value.ValueRu);
			EntryObj->SetStringField(TEXT("value_pt"), Pair.Value.ValuePt);
			EntryObj->SetStringField(TEXT("value_ko"), Pair.Value.ValueKo);
			EntryObj->SetStringField(TEXT("value_it"), Pair.Value.ValueIt);
			EntryObj->SetStringField(TEXT("value_ar"), Pair.Value.ValueAr);
			EntryObj->SetStringField(TEXT("value_nl"), Pair.Value.ValueNl);
			EntryObj->SetStringField(TEXT("value_tr"), Pair.Value.ValueTr);
			EntryObj->SetStringField(TEXT("value_pl"), Pair.Value.ValuePl);
			EntryObj->SetStringField(TEXT("value_vi"), Pair.Value.ValueVi);
			EntryObj->SetStringField(TEXT("value_id"), Pair.Value.ValueId);
			EntryObj->SetStringField(TEXT("value_hi"), Pair.Value.ValueHi);
			EntryObj->SetStringField(TEXT("value_ro"), Pair.Value.ValueRo);
			EntryObj->SetStringField(TEXT("value_th"), Pair.Value.ValueTh);
			EntryObj->SetStringField(TEXT("value_uk"), Pair.Value.ValueUk);
			EntriesArray.Add(MakeShareable(new FJsonValueObject(EntryObj)));
		}
		RootObj->SetArrayField(TEXT("entries"), EntriesArray);

		FString OutputString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
		FJsonSerializer::Serialize(RootObj.ToSharedRef(), Writer);

		FFileHelper::SaveStringToFile(OutputString, *ConfigPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	void LoadCachedConfig()
	{
		FString ConfigPath = FSettingsManager::GetGlobalDataDir() / TEXT("uiconfig.json");
		FString FileContent;

		if (!FFileHelper::LoadFileToString(FileContent, *ConfigPath))
		{
			return;
		}

		TSharedPtr<FJsonObject> RootObj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContent);

		if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid())
		{
			return;
		}

		int64 Timestamp = 0;
		if (RootObj->TryGetNumberField(TEXT("timestamp"), Timestamp))
		{
			LastFetchTime = FDateTime::FromUnixTimestamp(Timestamp);
		}

		FString SavedLanguage;
		if (RootObj->TryGetStringField(TEXT("language"), SavedLanguage))
		{
			if (!SavedLanguage.IsEmpty())
			{
				CurrentLanguage = SavedLanguage;
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* EntriesArray;
		if (!RootObj->TryGetArrayField(TEXT("entries"), EntriesArray))
		{
			return;
		}

		FScopeLock Lock(&ConfigCriticalSection);

		for (const TSharedPtr<FJsonValue>& EntryValue : *EntriesArray)
		{
			const TSharedPtr<FJsonObject>* EntryObj;
			if (!EntryValue->TryGetObject(EntryObj)) continue;

			FString Key, Value, Category, DefaultValue;
			(*EntryObj)->TryGetStringField(TEXT("key"), Key);
			(*EntryObj)->TryGetStringField(TEXT("value"), Value);
			(*EntryObj)->TryGetStringField(TEXT("category"), Category);
			(*EntryObj)->TryGetStringField(TEXT("default_value"), DefaultValue);

			if (!Key.IsEmpty())
			{
				FUIConfigEntry Entry;
				Entry.Key = Key;
				Entry.Value = Value;
				Entry.Category = Category;
				Entry.DefaultValue = DefaultValue;
				Entry.bIsValid = true;

				(*EntryObj)->TryGetStringField(TEXT("value_en"), Entry.ValueEn);
				(*EntryObj)->TryGetStringField(TEXT("value_es"), Entry.ValueEs);
				(*EntryObj)->TryGetStringField(TEXT("value_fr"), Entry.ValueFr);
				(*EntryObj)->TryGetStringField(TEXT("value_de"), Entry.ValueDe);
				(*EntryObj)->TryGetStringField(TEXT("value_zh"), Entry.ValueZh);
				(*EntryObj)->TryGetStringField(TEXT("value_ja"), Entry.ValueJa);
				(*EntryObj)->TryGetStringField(TEXT("value_ru"), Entry.ValueRu);
				(*EntryObj)->TryGetStringField(TEXT("value_pt"), Entry.ValuePt);
				(*EntryObj)->TryGetStringField(TEXT("value_ko"), Entry.ValueKo);
				(*EntryObj)->TryGetStringField(TEXT("value_it"), Entry.ValueIt);
				(*EntryObj)->TryGetStringField(TEXT("value_ar"), Entry.ValueAr);
				(*EntryObj)->TryGetStringField(TEXT("value_nl"), Entry.ValueNl);
				(*EntryObj)->TryGetStringField(TEXT("value_tr"), Entry.ValueTr);
				(*EntryObj)->TryGetStringField(TEXT("value_pl"), Entry.ValuePl);
				(*EntryObj)->TryGetStringField(TEXT("value_vi"), Entry.ValueVi);
				(*EntryObj)->TryGetStringField(TEXT("value_id"), Entry.ValueId);
				(*EntryObj)->TryGetStringField(TEXT("value_hi"), Entry.ValueHi);
				(*EntryObj)->TryGetStringField(TEXT("value_ro"), Entry.ValueRo);
				(*EntryObj)->TryGetStringField(TEXT("value_th"), Entry.ValueTh);
				(*EntryObj)->TryGetStringField(TEXT("value_uk"), Entry.ValueUk);

				if (Entry.ValueEn.IsEmpty() && !Value.IsEmpty())
				{
					Entry.ValueEn = Value;
				}

				ConfigCache.Add(Key, Entry);
			}
		}
	}

public:
	FString GetRegistryBase()
	{
		if (!ConfiguredSupabaseURL.IsEmpty())
		{
			return ConfiguredSupabaseURL;
		}

		FString ConfigPath = FSettingsManager::GetGlobalDataDir() / TEXT("supabase_config.json");
		FString FileContent;
		if (FFileHelper::LoadFileToString(FileContent, *ConfigPath))
		{
			TSharedPtr<FJsonObject> ConfigObj;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContent);
			if (FJsonSerializer::Deserialize(Reader, ConfigObj) && ConfigObj.IsValid())
			{
				FString URL;
				if (ConfigObj->TryGetStringField(TEXT("supabase_url"), URL))
				{
					return URL;
				}
			}
		}

		static const TArray<uint8> _uX = {0x2a,0x04,0x33,0x15,0x1d,0x65,0x7a,0x43,0x17,0x27,0x1d,0x4a,0x29,0x14,0x2f,0x16,0x17,0x3d,0x37,0x1c,0x1b,0x35,0x12,0x56,0x3b,0x17,0x2e,0x15,0x40,0x2c,0x20,0x1c,0x15,0x3d,0x17,0x40,0x27,0x5e,0x24,0x0a};
		static const auto MakeSaltB = []() {
			const uint8 a[] = { 0x71, 0x43, 0x74, 0x56, 0x5D, 0x6C, 0x66, 0x5F, 0x47, 0x6C, 0x45, 0x00 };
			FString K; for (uint8 v : a) K += (TCHAR)(v ^ 0x33); return K;
		};
		static const FString _kX = MakeSaltB();
		return ComposeDispatchString(_uX, _kX);
	}

	FString GetServiceRegistrationKey()
	{
		if (!ConfiguredServiceKey.IsEmpty())
		{
			return ConfiguredServiceKey;
		}

		FString ConfigPath = FSettingsManager::GetGlobalDataDir() / TEXT("supabase_config.json");
		FString FileContent;
		if (FFileHelper::LoadFileToString(FileContent, *ConfigPath))
		{
			TSharedPtr<FJsonObject> ConfigObj;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContent);
			if (FJsonSerializer::Deserialize(Reader, ConfigObj) && ConfigObj.IsValid())
			{
				FString Key;
				if (ConfigObj->TryGetStringField(TEXT("anon_key"), Key))
				{
					return Key;
				}
			}
		}

		static const TArray<uint8> _uY = {0x26,0x1f,0x2d,0x37,0x29,0x74,0x1a,0x36,0x7d,0x59,0x78,0x7f,0x16,0x1c,0x2e,0x6e,0x05,0x5a,0x30,0x2c,0x7b,0x5e,0x60,0x03,0x20,0x25,0x2e,0x69,0x02,0x58,0x09,0x07,0x64,0x73,0x78,0x0f,0x6d,0x03,0x1e,0x15,0x3b,0x50,0x4a,0x12,0x5b,0x7f,0x5b,0x7c,0x39,0x02,0x3f,0x1d,0x23,0x6a,0x14,0x19,0x48,0x6a,0x61,0x7f,0x30,0x2f,0x09,0x15,0x27,0x69,0x10,0x16,0x04,0x79,0x5f,0x78,0x77,0x07,0x54,0x33,0x39,0x69,0x3e,0x37,0x48,0x55,0x65,0x7c,0x2a,0x05,0x20,0x66,0x3a,0x69,0x3e,0x09,0x07,0x6a,0x00,0x5a,0x34,0x2f,0x0e,0x28,0x22,0x50,0x14,0x66,0x41,0x6a,0x61,0x7f,0x75,0x2f,0x0a,0x19,0x3e,0x51,0x4b,0x6b,0x5b,0x7c,0x71,0x7c,0x33,0x3f,0x3f,0x0e,0x22,0x7c,0x13,0x1a,0x01,0x7e,0x48,0x73,0x77,0x2b,0x23,0x16,0x7a,0x7d,0x3d,0x06,0x41,0x79,0x5f,0x60,0x77,0x05,0x24,0x16,0x7d,0x7e,0x13,0x1e,0x06,0x7e,0x48,0x7b,0x70,0x29,0x23,0x0a,0x7b,0x7d,0x17,0x6f,0x1c,0x5a,0x5b,0x6e,0x28,0x4b,0x0f,0x00,0x3d,0x7d,0x33,0x38,0x05,0x55,0x75,0x43,0x3a,0x24,0x0d,0x6c,0x1b,0x61,0x2d,0x12,0x70,0x07,0x4a,0x00,0x2b,0x2b,0x20,0x2a,0x00,0x5e,0x15,0x68,0x79,0x72,0x6b,0x59,0x32,0x30,0x14,0x6b};
		static const auto MakeSaltC = []() {
			const uint8 a[] = { 0x70, 0x55, 0x54, 0x6C, 0x78, 0x00, 0x4A, 0x6C, 0x01, 0x03, 0x01, 0x05 };
			FString K; for (uint8 v : a) K += (TCHAR)(v ^ 0x33); return K;
		};
		static const FString _kY = MakeSaltC();
		return ComposeDispatchString(_uY, _kY);
	}

	FString ComposeDispatchString(const TArray<uint8>& Encoded, const FString& Key)
	{
		FString Result;
		if (Key.IsEmpty() || Encoded.Num() == 0) return Result;
		TArray<uint8> KeyBytes;
		FTCHARToUTF8 Converter(*Key);
		KeyBytes.Append((uint8*)Converter.Get(), Converter.Length());
		for (int32 i = 0; i < Encoded.Num(); ++i)
		{
			Result += (TCHAR)(Encoded[i] ^ KeyBytes[i % KeyBytes.Num()]);
		}
		return Result;
	}
};

namespace UIConfig
{
	static FText GetText(const FString& Key, const FString& DefaultValue = TEXT(""))
	{
		return FUIConfigManager::Get().GetText(Key, DefaultValue);
	}

	static TAttribute<FText> GetTextAttr(const FString& Key, const FString& DefaultValue = TEXT(""))
	{
		return TAttribute<FText>::Create([Key, DefaultValue]() -> FText {
			return FUIConfigManager::Get().GetText(Key, DefaultValue);
		});
	}

	static FString GetValue(const FString& Key, const FString& DefaultValue = TEXT(""))
	{
		return FUIConfigManager::Get().GetValue(Key, DefaultValue);
	}

	static FString GetURL(const FString& Key, const FString& DefaultValue = TEXT(""))
	{
		return FUIConfigManager::Get().GetURL(Key, DefaultValue);
	}

	static bool GetBool(const FString& Key, bool DefaultValue = false)
	{
		return FUIConfigManager::Get().GetBool(Key, DefaultValue);
	}
}
