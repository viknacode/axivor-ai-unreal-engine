// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "AssetReferenceTypes.h"

class FAiMemoryManager
{
public:
	static UECPAIMEMORY_API FAiMemoryManager& Get();

	TArray<TSharedPtr<FAiMemoryEntry>>& GetAiMemories() { return AiMemories; }
	const TArray<TSharedPtr<FAiMemoryEntry>>& GetAiMemories() const { return AiMemories; }

	TArray<TSharedPtr<FAiMemoryEntry>>& GetPendingMemories() { return PendingMemories; }
	const TArray<TSharedPtr<FAiMemoryEntry>>& GetPendingMemories() const { return PendingMemories; }

	UECPAIMEMORY_API void SaveManifest();
	UECPAIMEMORY_API void LoadManifest();

	UECPAIMEMORY_API FString GetContentForAI() const;
	UECPAIMEMORY_API int32 GetTokenCount() const;

	static UECPAIMEMORY_API FString GetCategoryDisplayName(EAiMemoryCategory Category);
	static UECPAIMEMORY_API FLinearColor GetCategoryColor(EAiMemoryCategory Category);

private:
	FAiMemoryManager() = default;
	FAiMemoryManager(const FAiMemoryManager&) = delete;
	FAiMemoryManager& operator=(const FAiMemoryManager&) = delete;

	TArray<TSharedPtr<FAiMemoryEntry>> AiMemories;
	TArray<TSharedPtr<FAiMemoryEntry>> PendingMemories;
};
