// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "AssetReferenceTypes.h"

class SUECPMainWidget;
class UUECPAppBridge;

class UECPCORE_API IUECPAiMemoryService
{
public:
	virtual ~IUECPAiMemoryService() = default;

	virtual void InitializeShellRefs(TWeakPtr<SUECPMainWidget> Shell,
		TWeakObjectPtr<UUECPAppBridge> Bridge) = 0;

	virtual void NotifyPendingMemoriesOnStartup() = 0;

	virtual TArray<TSharedPtr<FAiMemoryEntry>>& GetAiMemories() = 0;

	virtual TArray<TSharedPtr<FAiMemoryEntry>>& GetPendingMemories() = 0;

	virtual void SaveManifest() = 0;

	virtual FString GetContentForAI() const = 0;
	virtual int32   GetTokenCount() const = 0;

	virtual FString GetCategoryDisplayName(EAiMemoryCategory Category) const = 0;

	virtual void AddMemory() = 0;
	virtual void UpdateMemory(const FString& MemoryIdStr, const FString& Content, const FString& CategoryDisplayName) = 0;
	virtual void DeleteMemory(const FString& MemoryIdStr) = 0;
	virtual void ApproveMemory(const FString& MemoryIdStr) = 0;
	virtual void RejectMemory(const FString& MemoryIdStr) = 0;
	virtual void ApproveAllPendingMemories() = 0;
	virtual void RejectAllPendingMemories() = 0;
	virtual void ToggleMemory(const FString& MemoryIdStr, bool bEnabled) = 0;
	virtual void LoadMemoryContent(const FString& MemoryIdStr) = 0;

	virtual void ExtractMemoriesFromConversation() = 0;

	virtual void  SetExtractionThreshold(int32 Threshold) = 0;
	virtual int32 GetExtractionThreshold() const = 0;

	virtual void    RefreshOverlay() = 0;
	virtual FString BuildOverlayHtml() const = 0;
};
