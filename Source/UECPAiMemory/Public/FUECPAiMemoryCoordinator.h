// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Services/IUECPAiMemoryService.h"

class UUECPAppBridge;
class SUECPMainWidget;

class UECPAIMEMORY_API FUECPAiMemoryCoordinator final : public IUECPAiMemoryService
{
public:
	FUECPAiMemoryCoordinator();

	virtual void InitializeShellRefs(TWeakPtr<SUECPMainWidget> InShell,
		TWeakObjectPtr<UUECPAppBridge> InBridge) override;
	virtual void NotifyPendingMemoriesOnStartup() override;

	virtual TArray<TSharedPtr<FAiMemoryEntry>>& GetAiMemories() override;
	virtual TArray<TSharedPtr<FAiMemoryEntry>>& GetPendingMemories() override;
	virtual void    SaveManifest() override;
	virtual FString GetContentForAI() const override;
	virtual int32   GetTokenCount() const override;
	virtual FString GetCategoryDisplayName(EAiMemoryCategory Category) const override;

	virtual void AddMemory() override;
	virtual void UpdateMemory(const FString& MemoryIdStr, const FString& Content, const FString& CategoryDisplayName) override;
	virtual void DeleteMemory(const FString& MemoryIdStr) override;
	virtual void ApproveMemory(const FString& MemoryIdStr) override;
	virtual void RejectMemory(const FString& MemoryIdStr) override;
	virtual void ApproveAllPendingMemories() override;
	virtual void RejectAllPendingMemories() override;
	virtual void ToggleMemory(const FString& MemoryIdStr, bool bEnabled) override;
	virtual void LoadMemoryContent(const FString& MemoryIdStr) override;
	virtual void ExtractMemoriesFromConversation() override;

	virtual void  SetExtractionThreshold(int32 Threshold) override;
	virtual int32 GetExtractionThreshold() const override { return ExtractionThreshold; }

	virtual void    RefreshOverlay() override;
	virtual FString BuildOverlayHtml() const override;

private:
	TWeakPtr<SUECPMainWidget>      Shell;
	TWeakObjectPtr<UUECPAppBridge> Bridge;

	int32 ExtractionThreshold = 7;

	void LoadThresholdFromConfig();
};
