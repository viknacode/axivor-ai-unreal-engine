// Copyright 2026, BlueprintsLab, All rights reserved

#include "ApiKeyManager.h"
#include "Managers/ProviderConfigManager.h"
#include "Managers/SettingsManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformFileManager.h"
FApiKeyManager& FApiKeyManager::Get()
{
	static FApiKeyManager Instance;
	return Instance;
}
FApiKeyManager::FApiKeyManager()
{
	for (int32 i = 0; i < MAX_API_KEY_SLOTS; ++i)
	{
		Slots[i].SlotIndex = i;
	}
	ActiveSlotIndex = 0;
	LoadFromConfig();
}
FApiKeyManager::~FApiKeyManager()
{
	SaveToConfig();
}
FString FApiKeyManager::GetConfigFilePath()
{
	FString GlobalPath = FPaths::Combine(FSettingsManager::GetGlobalDataDir(), TEXT("ApiKeySlots.json"));

	if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*GlobalPath))
	{
		FString LegacyPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BpGeneratorUltimate"), TEXT("ApiKeySlots.json"));
		if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*LegacyPath))
		{
			FString Content;
			if (FFileHelper::LoadFileToString(Content, *LegacyPath))
			{
				FFileHelper::SaveStringToFile(Content, *GlobalPath);
				UE_LOG(LogTemp, Log, TEXT("BpGenerator: Migrated ApiKeySlots.json to %s"), *GlobalPath);
			}
		}
	}

	return GlobalPath;
}
void FApiKeyManager::LoadFromConfig()
{
	FString ConfigPath = GetConfigFilePath();
	if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*ConfigPath))
	{
		return;
	}
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *ConfigPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("ApiKeyManager: Could not read config file: %s"), *ConfigPath);
		return;
	}
	TSharedPtr<FJsonObject> RootObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("ApiKeyManager: Failed to parse JSON in config file: %s"), *ConfigPath);
		return;
	}
	bool bNeedsMigrationSave = false;
	{
		FScopeLock Lock(&SlotsLock);
		ActiveSlotIndex = RootObj->GetIntegerField(TEXT("ActiveSlot"));
		if (ActiveSlotIndex < 0 || ActiveSlotIndex >= MAX_API_KEY_SLOTS)
		{
			ActiveSlotIndex = 0;
		}
		const TArray<TSharedPtr<FJsonValue>>& SlotsArray = RootObj->GetArrayField(TEXT("Slots"));
		for (int32 i = 0; i < FMath::Min(SlotsArray.Num(), MAX_API_KEY_SLOTS); ++i)
		{
			const TSharedPtr<FJsonObject>& SlotObj = SlotsArray[i]->AsObject();
			if (SlotObj.IsValid())
			{
				Slots[i].Name = SlotObj->GetStringField(TEXT("Name"));
				Slots[i].Provider = SlotObj->GetStringField(TEXT("Provider"));
				FString EncryptedKey = SlotObj->GetStringField(TEXT("ApiKey"));
				Slots[i].ApiKey = FromBlob(EncryptedKey);
				Slots[i].CustomBaseURL = SlotObj->GetStringField(TEXT("CustomBaseURL"));
				Slots[i].CustomModelName = SlotObj->GetStringField(TEXT("CustomModelName"));
				Slots[i].GeminiModel = SlotObj->GetStringField(TEXT("GeminiModel"));
				if (Slots[i].GeminiModel.IsEmpty())
				{
					Slots[i].GeminiModel = TEXT("gemini-2.5-flash");
				}
				Slots[i].OpenAIModel = SlotObj->GetStringField(TEXT("OpenAIModel"));
				if (Slots[i].OpenAIModel.IsEmpty())
				{
					Slots[i].OpenAIModel = TEXT("gpt-5-mini-2025-08-07");
				}
				Slots[i].ClaudeModel = SlotObj->GetStringField(TEXT("ClaudeModel"));
				if (Slots[i].ClaudeModel.IsEmpty())
				{
					Slots[i].ClaudeModel = TEXT("claude-sonnet-4-6");
				}
				Slots[i].DeepSeekModel = SlotObj->GetStringField(TEXT("DeepSeekModel"));
				if (Slots[i].DeepSeekModel.IsEmpty())
				{
					Slots[i].DeepSeekModel = TEXT("deepseek-v4-flash");
				}
				Slots[i].CustomParams = SlotObj->GetStringField(TEXT("CustomParams"));
				FString EncryptedTexKey = SlotObj->GetStringField(TEXT("TextureGenApiKey"));
				Slots[i].TextureGenApiKey = FromBlob(EncryptedTexKey);
				Slots[i].TextureGenEndpoint = SlotObj->GetStringField(TEXT("TextureGenEndpoint"));
				Slots[i].TextureGenModel = SlotObj->GetStringField(TEXT("TextureGenModel"));
				bool bTextureModeVal = true;
				SlotObj->TryGetBoolField(TEXT("TextureGenMode"), bTextureModeVal);
				Slots[i].bTextureMode = bTextureModeVal;
				FString EncryptedMeshKey;
				SlotObj->TryGetStringField(TEXT("MeshGenApiKey"), EncryptedMeshKey);
				Slots[i].MeshGenApiKey = FromBlob(EncryptedMeshKey);
				{ FString Tmp; if (SlotObj->TryGetStringField(TEXT("MeshyAiModel"), Tmp)) Slots[i].MeshyAiModel = Tmp; }
				{ FString Tmp; if (SlotObj->TryGetStringField(TEXT("SoundGenApiKey"), Tmp)) Slots[i].SoundGenApiKey = FromBlob(Tmp); }
				{ FString Tmp; if (SlotObj->TryGetStringField(TEXT("SoundGenEndpoint"), Tmp)) Slots[i].SoundGenEndpoint = Tmp; }
				{ FString Tmp; if (SlotObj->TryGetStringField(TEXT("SoundGenModel"), Tmp)) Slots[i].SoundGenModel = Tmp; }
				{ FString Tmp; if (SlotObj->TryGetStringField(TEXT("SoundGenVoice"), Tmp)) Slots[i].SoundGenVoice = Tmp; }
				{ FString Tmp; if (SlotObj->TryGetStringField(TEXT("AgentModel"), Tmp)) Slots[i].AgentModel = Tmp; }
				{ FString Tmp; if (SlotObj->TryGetStringField(TEXT("AgentEffort"), Tmp)) Slots[i].AgentEffort = Tmp; }
				Slots[i].SlotIndex = i;

				const bool bUntouched =
					Slots[i].Name.IsEmpty() &&
					Slots[i].ApiKey.IsEmpty() &&
					Slots[i].CustomBaseURL.IsEmpty() &&
					Slots[i].CustomModelName.IsEmpty() &&
					Slots[i].AgentModel.IsEmpty();
				if (Slots[i].Provider.IsEmpty() || bUntouched)
				{
					Slots[i].Provider = TEXT("Free");
				}
			}
		}

		const TSharedPtr<FJsonObject>* ImgObj = nullptr;
		if (RootObj->TryGetObjectField(TEXT("imageGen"), ImgObj) && ImgObj && ImgObj->IsValid())
		{
			FString EncImgKey;
			(*ImgObj)->TryGetStringField(TEXT("ApiKey"), EncImgKey);
			ImageGen.ApiKey = FromBlob(EncImgKey);
			(*ImgObj)->TryGetStringField(TEXT("Endpoint"), ImageGen.Endpoint);
			(*ImgObj)->TryGetStringField(TEXT("Model"),    ImageGen.Model);
			bool bImgMode = true;
			(*ImgObj)->TryGetBoolField(TEXT("TextureMode"), bImgMode);
			ImageGen.bTextureMode = bImgMode;
		}
		else
		{
			const FApiKeySlot& Src = Slots[ActiveSlotIndex];
			ImageGen.ApiKey = Src.TextureGenApiKey;
			if (!Src.TextureGenEndpoint.IsEmpty()) ImageGen.Endpoint = Src.TextureGenEndpoint;
			if (!Src.TextureGenModel.IsEmpty())    ImageGen.Model    = Src.TextureGenModel;
			ImageGen.bTextureMode = Src.bTextureMode;
			bNeedsMigrationSave = true;
		}

		double AgentModelVersionD = 0.0;
		RootObj->TryGetNumberField(TEXT("AgentModelVersion"), AgentModelVersionD);
		int32 AgentModelVersion = (int32)AgentModelVersionD;
		if (AgentModelVersion < 1)
		{
			for (int32 i = 0; i < MAX_API_KEY_SLOTS; ++i)
			{
				Slots[i].AgentModel  = TEXT("");
				Slots[i].AgentEffort = TEXT("");
			}
			bNeedsMigrationSave = true;
		}
	}

	if (bNeedsMigrationSave)
	{
		SaveToConfig();
	}
}
void FApiKeyManager::SaveToConfig()
{
	FString ConfigPath = GetConfigFilePath();
	FString Directory = FPaths::GetPath(ConfigPath);
	if (!FPlatformFileManager::Get().GetPlatformFile().DirectoryExists(*Directory))
	{
		FPlatformFileManager::Get().GetPlatformFile().CreateDirectory(*Directory);
	}
	TSharedPtr<FJsonObject> RootObj = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> SlotsArray;
	FScopeLock Lock(&SlotsLock);
	for (int32 i = 0; i < MAX_API_KEY_SLOTS; ++i)
	{
		TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
		SlotObj->SetStringField(TEXT("Name"), Slots[i].Name);
		SlotObj->SetStringField(TEXT("Provider"), Slots[i].Provider);
		FString EncryptedKey = ToBlob(Slots[i].ApiKey);
		SlotObj->SetStringField(TEXT("ApiKey"), EncryptedKey);
		SlotObj->SetStringField(TEXT("CustomBaseURL"), Slots[i].CustomBaseURL);
		SlotObj->SetStringField(TEXT("CustomModelName"), Slots[i].CustomModelName);
		SlotObj->SetStringField(TEXT("GeminiModel"),   Slots[i].GeminiModel);
		SlotObj->SetStringField(TEXT("OpenAIModel"),   Slots[i].OpenAIModel);
		SlotObj->SetStringField(TEXT("ClaudeModel"),   Slots[i].ClaudeModel);
		SlotObj->SetStringField(TEXT("DeepSeekModel"), Slots[i].DeepSeekModel);
		SlotObj->SetStringField(TEXT("CustomParams"), Slots[i].CustomParams);
		SlotObj->SetStringField(TEXT("TextureGenApiKey"), ToBlob(Slots[i].TextureGenApiKey));
		SlotObj->SetStringField(TEXT("TextureGenEndpoint"), Slots[i].TextureGenEndpoint);
		SlotObj->SetStringField(TEXT("TextureGenModel"), Slots[i].TextureGenModel);
		SlotObj->SetBoolField(TEXT("TextureGenMode"), Slots[i].bTextureMode);
		SlotObj->SetStringField(TEXT("MeshGenApiKey"), ToBlob(Slots[i].MeshGenApiKey));
		SlotObj->SetStringField(TEXT("MeshyAiModel"), Slots[i].MeshyAiModel);
		SlotObj->SetStringField(TEXT("SoundGenApiKey"), ToBlob(Slots[i].SoundGenApiKey));
		SlotObj->SetStringField(TEXT("SoundGenEndpoint"), Slots[i].SoundGenEndpoint);
		SlotObj->SetStringField(TEXT("SoundGenModel"), Slots[i].SoundGenModel);
		SlotObj->SetStringField(TEXT("SoundGenVoice"), Slots[i].SoundGenVoice);
		SlotObj->SetStringField(TEXT("AgentModel"), Slots[i].AgentModel);
		SlotObj->SetStringField(TEXT("AgentEffort"), Slots[i].AgentEffort);
		SlotsArray.Add(MakeShared<FJsonValueObject>(SlotObj));
	}
	RootObj->SetArrayField(TEXT("Slots"), SlotsArray);
	RootObj->SetNumberField(TEXT("ActiveSlot"), ActiveSlotIndex);
	RootObj->SetNumberField(TEXT("AgentModelVersion"), 1);

	{
		TSharedPtr<FJsonObject> ImgObj = MakeShared<FJsonObject>();
		ImgObj->SetStringField(TEXT("ApiKey"),      ToBlob(ImageGen.ApiKey));
		ImgObj->SetStringField(TEXT("Endpoint"),    ImageGen.Endpoint);
		ImgObj->SetStringField(TEXT("Model"),       ImageGen.Model);
		ImgObj->SetBoolField  (TEXT("TextureMode"), ImageGen.bTextureMode);
		RootObj->SetObjectField(TEXT("imageGen"), ImgObj);
	}
	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(RootObj.ToSharedRef(), Writer);
	FFileHelper::SaveStringToFile(JsonString, *ConfigPath);
}
void FApiKeyManager::SetSlot(int32 SlotIndex, const FApiKeySlot& Slot)
{
	if (SlotIndex < 0 || SlotIndex >= MAX_API_KEY_SLOTS) return;
	FScopeLock Lock(&SlotsLock);
	Slots[SlotIndex] = Slot;
	Slots[SlotIndex].SlotIndex = SlotIndex;
	SaveToConfig();
}
FApiKeySlot FApiKeyManager::GetSlot(int32 SlotIndex) const
{
	if (SlotIndex < 0 || SlotIndex >= MAX_API_KEY_SLOTS) return FApiKeySlot();
	FScopeLock Lock(&SlotsLock);
	return Slots[SlotIndex];
}
void FApiKeyManager::ClearSlot(int32 SlotIndex)
{
	if (SlotIndex < 0 || SlotIndex >= MAX_API_KEY_SLOTS) return;
	FScopeLock Lock(&SlotsLock);
	Slots[SlotIndex].Clear();
	Slots[SlotIndex].SlotIndex = SlotIndex;
	SaveToConfig();
}
TArray<FApiKeySlot> FApiKeyManager::GetAllSlots() const
{
	FScopeLock Lock(&SlotsLock);
	TArray<FApiKeySlot> Result;
	for (int32 i = 0; i < MAX_API_KEY_SLOTS; ++i)
	{
		Result.Add(Slots[i]);
	}
	return Result;
}
int32 FApiKeyManager::GetActiveSlotIndex() const
{
	FScopeLock Lock(&SlotsLock);
	return ActiveSlotIndex;
}
void FApiKeyManager::SetActiveSlot(int32 SlotIndex)
{
	if (SlotIndex < 0 || SlotIndex >= MAX_API_KEY_SLOTS) return;
	FScopeLock Lock(&SlotsLock);
	ActiveSlotIndex = SlotIndex;
	SaveToConfig();
}
FApiKeySlot FApiKeyManager::GetActiveSlot() const
{
	FScopeLock Lock(&SlotsLock);
	return Slots[ActiveSlotIndex];
}
FString FApiKeyManager::GetActiveApiKey() const
{
	return GetActiveSlot().ApiKey;
}
FString FApiKeyManager::GetActiveProvider() const
{
	return GetActiveSlot().Provider;
}
FString FApiKeyManager::GetActiveBaseURL() const
{
	FApiKeySlot Slot = GetActiveSlot();
	if (Slot.IsCustomProvider())
	{
		return Slot.CustomBaseURL;
	}
	FString DynamicURL = FProviderConfigManager::Get().GetEndpointForProvider(Slot.Provider.ToLower());
	if (!DynamicURL.IsEmpty())
	{
		return DynamicURL;
	}
	static const TMap<FString, FString> ProviderURLs = {
		{TEXT("OpenAI"),   TEXT("https://api.openai.com/v1/chat/completions")},
		{TEXT("Claude"),   TEXT("https://api.anthropic.com/v1/messages")},
		{TEXT("Gemini"),   TEXT("https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent")},
		{TEXT("DeepSeek"), TEXT("https://api.deepseek.com/v1/chat/completions")}
	};
	if (const FString* URL = ProviderURLs.Find(Slot.Provider))
	{
		return *URL;
	}
	return TEXT("");
}
FString FApiKeyManager::GetActiveModelName() const
{
	FApiKeySlot Slot = GetActiveSlot();
	if (Slot.IsCustomProvider() && !Slot.CustomModelName.IsEmpty())
	{
		return Slot.CustomModelName;
	}
	if (Slot.Provider.Equals(TEXT("OpenAI"), ESearchCase::IgnoreCase) && !Slot.OpenAIModel.IsEmpty())
	{
		return Slot.OpenAIModel;
	}
	if (Slot.Provider.Equals(TEXT("Claude"), ESearchCase::IgnoreCase) && !Slot.ClaudeModel.IsEmpty())
	{
		return Slot.ClaudeModel;
	}
	if (Slot.Provider.Equals(TEXT("Gemini"), ESearchCase::IgnoreCase) && !Slot.GeminiModel.IsEmpty())
	{
		return Slot.GeminiModel;
	}
	if (Slot.Provider.Equals(TEXT("DeepSeek"), ESearchCase::IgnoreCase) && !Slot.DeepSeekModel.IsEmpty())
	{
		return Slot.DeepSeekModel;
	}
	FString DynamicDefault = FProviderConfigManager::Get().GetDefaultModelForProvider(Slot.Provider.ToLower());
	if (!DynamicDefault.IsEmpty()) return DynamicDefault;
	static const TMap<FString, FString> ProviderModels = {
		{TEXT("OpenAI"),   TEXT("gpt-5-mini-2025-08-07")},
		{TEXT("Claude"),   TEXT("claude-sonnet-4-6")},
		{TEXT("Gemini"),   TEXT("gemini-2.5-flash")},
		{TEXT("DeepSeek"), TEXT("deepseek-v4-flash")}
	};
	if (const FString* Model = ProviderModels.Find(Slot.Provider))
	{
		return *Model;
	}
	return TEXT("gpt-5-mini-2025-08-07");
}
FString FApiKeyManager::GetActiveGeminiModel() const
{
	FApiKeySlot Slot = GetActiveSlot();
	if (!Slot.GeminiModel.IsEmpty())
	{
		return Slot.GeminiModel;
	}
	return TEXT("gemini-2.5-flash");
}
FString FApiKeyManager::GetActiveOpenAIModel() const
{
	FApiKeySlot Slot = GetActiveSlot();
	if (!Slot.OpenAIModel.IsEmpty())
	{
		return Slot.OpenAIModel;
	}
	return TEXT("gpt-5-mini-2025-08-07");
}
FString FApiKeyManager::GetActiveClaudeModel() const
{
	FApiKeySlot Slot = GetActiveSlot();
	if (!Slot.ClaudeModel.IsEmpty())
	{
		return Slot.ClaudeModel;
	}
	return TEXT("claude-sonnet-4-6");
}
FApiKeyManager::FImageGenConfig FApiKeyManager::GetImageGenConfig() const
{
	FScopeLock Lock(&SlotsLock);
	return ImageGen;
}
void FApiKeyManager::SetImageGenConfig(const FImageGenConfig& Cfg)
{
	{ FScopeLock Lock(&SlotsLock); ImageGen = Cfg; }
	SaveToConfig();
}
FString FApiKeyManager::GetActiveTextureGenApiKey() const
{
	{ FScopeLock Lock(&SlotsLock); if (!ImageGen.ApiKey.IsEmpty()) return ImageGen.ApiKey; }
	return GetActiveSlot().ApiKey;
}
FString FApiKeyManager::GetActiveTextureGenEndpoint() const
{
	FScopeLock Lock(&SlotsLock);
	return ImageGen.Endpoint.IsEmpty()
		? TEXT("https://generativelanguage.googleapis.com/v1beta/models/imagen-4.0-generate-001:predict")
		: ImageGen.Endpoint;
}
FString FApiKeyManager::GetActiveTextureGenModel() const
{
	FScopeLock Lock(&SlotsLock);
	return ImageGen.Model.IsEmpty() ? TEXT("imagen-4.0-generate-001") : ImageGen.Model;
}
bool FApiKeyManager::GetActiveTextureGenMode() const
{
	FScopeLock Lock(&SlotsLock);
	return ImageGen.bTextureMode;
}
FString FApiKeyManager::GetActiveMeshGenApiKey() const
{
	FApiKeySlot Slot = GetActiveSlot();
	if (!Slot.MeshGenApiKey.IsEmpty())
	{
		return Slot.MeshGenApiKey;
	}

	FString LegacyMeshKey;
	GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("MeshyApiKey"), LegacyMeshKey, FSettingsManager::GetGlobalConfigPath());
	if (!LegacyMeshKey.IsEmpty())
	{
		return LegacyMeshKey;
	}

	return Slot.ApiKey;
}
FString FApiKeyManager::GetActiveMeshyAiModel() const
{
	const FApiKeySlot Slot = GetActiveSlot();
	return Slot.MeshyAiModel.IsEmpty() ? TEXT("latest") : Slot.MeshyAiModel;
}
FString FApiKeyManager::GetActiveSoundGenApiKey() const
{
	FApiKeySlot Slot = GetActiveSlot();
	return !Slot.SoundGenApiKey.IsEmpty() ? Slot.SoundGenApiKey : Slot.ApiKey;
}
FString FApiKeyManager::GetActiveSoundGenEndpoint() const
{
	FApiKeySlot Slot = GetActiveSlot();
	return Slot.SoundGenEndpoint.IsEmpty() ? TEXT("https://api.openai.com/v1/audio/speech") : Slot.SoundGenEndpoint;
}
FString FApiKeyManager::GetActiveSoundGenModel() const
{
	FApiKeySlot Slot = GetActiveSlot();
	return Slot.SoundGenModel.IsEmpty() ? TEXT("tts-1") : Slot.SoundGenModel;
}
FString FApiKeyManager::GetActiveSoundGenVoice() const
{
	FApiKeySlot Slot = GetActiveSlot();
	return Slot.SoundGenVoice.IsEmpty() ? TEXT("alloy") : Slot.SoundGenVoice;
}
FString FApiKeyManager::FromBlob(const FString& EncryptedKey) const
{
	if (EncryptedKey.IsEmpty())
	{
		return FString();
	}
	TArray<uint8> DecodedBytes;
	FBase64::Decode(EncryptedKey, DecodedBytes);
	if (DecodedBytes.Num() < 2)
	{
		return FString();
	}
	FString Decrypted;
	for (int32 i = 0; i < DecodedBytes.Num(); i++)
	{
		Decrypted.AppendChar(DecodedBytes[i] ^ 0x55);
	}
	return Decrypted;
}
FString FApiKeyManager::ToBlob(const FString& Key) const
{
	if (Key.IsEmpty())
	{
		return FString();
	}
	TArray<uint8> EncodedBytes;
	for (int32 i = 0; i < Key.Len(); i++)
	{
		EncodedBytes.Add(Key[i] ^ 0x55);
	}
	return FBase64::Encode(EncodedBytes);
}
