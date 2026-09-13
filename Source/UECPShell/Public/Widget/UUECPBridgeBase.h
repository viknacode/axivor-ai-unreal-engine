// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UUECPBridgeBase.generated.h"

class SUECPMainWidget;
class SWebBrowser;

UCLASS()
class UUECPBridgeBase : public UObject
{
	GENERATED_BODY()

public:

	TWeakPtr<SUECPMainWidget> OwnerWidget;
	TWeakPtr<SWebBrowser>     BrowserRef;

protected:
	void ExecJs(const FString& Js);
	static FString EscJs(const FString& In);
	void PushToast(const FString& Message, const FString& Type);
	void PushOverlayHtml(const FString& Html);
};
