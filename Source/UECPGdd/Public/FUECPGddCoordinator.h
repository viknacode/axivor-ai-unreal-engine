// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Services/IUECPGddService.h"

class UUECPAppBridge;
class SUECPMainWidget;

class UECPGDD_API FUECPGddCoordinator final : public IUECPGddService
{
public:
	FUECPGddCoordinator();

	virtual void InitializeShellRefs(TWeakPtr<SUECPMainWidget> InShell,
		TWeakObjectPtr<UUECPAppBridge> InBridge) override;
	virtual void ImportFiles() override;
	virtual void SaveFile(const FString& FileId, const FString& Content) override;
	virtual void DeleteFile(const FString& FileId) override;
	virtual void ToggleFile(const FString& FileId, bool bEnabled) override;
	virtual void LoadFileContent(const FString& FileId) override;
	virtual void RefreshOverlay() override;
	virtual FString BuildOverlayHtml() const override;

	virtual FString GetContentForAI() const override;
	virtual int32   GetTokenCount() const override;

private:
	TWeakPtr<SUECPMainWidget>      Shell;
	TWeakObjectPtr<UUECPAppBridge> Bridge;
};
