// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class SUECPMainWidget;
class UUECPAppBridge;

class UECPCORE_API IUECPGddService
{
public:
	virtual ~IUECPGddService() = default;

	virtual void InitializeShellRefs(TWeakPtr<SUECPMainWidget> Shell,
		TWeakObjectPtr<UUECPAppBridge> Bridge) = 0;

	virtual void ImportFiles() = 0;

	virtual void SaveFile(const FString& FileId, const FString& Content) = 0;

	virtual void DeleteFile(const FString& FileId) = 0;

	virtual void ToggleFile(const FString& FileId, bool bEnabled) = 0;

	virtual void LoadFileContent(const FString& FileId) = 0;

	virtual void RefreshOverlay() = 0;

	virtual FString BuildOverlayHtml() const = 0;

	virtual FString GetContentForAI() const = 0;
	virtual int32   GetTokenCount() const = 0;
};
