// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once
#include "CoreMinimal.h"
#include "AssetReferenceTypes.h"
class UECPCORE_API FApiKeyManager
{
public:
	static FApiKeyManager& Get();
	void SetSlot(int32 SlotIndex, const FApiKeySlot& Slot);
	FApiKeySlot GetSlot(int32 SlotIndex) const;
	void ClearSlot(int32 SlotIndex);
	TArray<FApiKeySlot> GetAllSlots() const;
	int32 GetActiveSlotIndex() const;
	void SetActiveSlot(int32 SlotIndex);
	FApiKeySlot GetActiveSlot() const;
	FString GetActiveApiKey() const;
	FString GetActiveProvider() const;
	FString GetActiveBaseURL() const;
	FString GetActiveModelName() const;
	FString GetActiveGeminiModel() const;
	FString GetActiveOpenAIModel() const;
	FString GetActiveClaudeModel() const;

	struct FImageGenConfig
	{
		FString ApiKey;
		FString Endpoint  = TEXT("https://generativelanguage.googleapis.com/v1beta/models/imagen-4.0-generate-001:predict");
		FString Model     = TEXT("imagen-4.0-generate-001");
		bool    bTextureMode = true;
	};
	FImageGenConfig GetImageGenConfig() const;
	void SetImageGenConfig(const FImageGenConfig& Cfg);

	FString GetActiveTextureGenApiKey() const;
	FString GetActiveTextureGenEndpoint() const;
	FString GetActiveTextureGenModel() const;
	bool GetActiveTextureGenMode() const;

	FString GetActiveMeshGenApiKey() const;
	FString GetActiveMeshyAiModel() const;

	FString GetActiveSoundGenApiKey() const;
	FString GetActiveSoundGenEndpoint() const;
	FString GetActiveSoundGenModel() const;
	FString GetActiveSoundGenVoice() const;
private:
	FApiKeyManager();
	~FApiKeyManager();
	FApiKeySlot Slots[MAX_API_KEY_SLOTS];
	int32 ActiveSlotIndex;
	FImageGenConfig ImageGen;
	mutable FCriticalSection SlotsLock;
	void LoadFromConfig();
	void SaveToConfig();
	static FString GetConfigFilePath();
	FString FromBlob(const FString& EncryptedKey) const;
	FString ToBlob(const FString& Key) const;
};
