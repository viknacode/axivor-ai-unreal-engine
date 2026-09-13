// Copyright 2026, BlueprintsLab, All rights reserved

#include "Widget/UUECPBridgeBase.h"
#include "SWebBrowser.h"

void UUECPBridgeBase::ExecJs(const FString& Js)
{
	auto Browser = BrowserRef.Pin();
	if (Browser.IsValid())
		Browser->ExecuteJavascript(Js);
}

FString UUECPBridgeBase::EscJs(const FString& In)
{
	FString Out = In;
	Out.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	Out.ReplaceInline(TEXT("\""), TEXT("\\\""));
	Out.ReplaceInline(TEXT("\n"), TEXT("\\n"));
	Out.ReplaceInline(TEXT("\r"), TEXT("\\r"));
	Out.ReplaceInline(TEXT("\t"), TEXT("\\t"));
	return Out;
}

void UUECPBridgeBase::PushToast(const FString& Message, const FString& Type)
{
	ExecJs(FString::Printf(TEXT("if(typeof showToast==='function')showToast(\"%s\",\"%s\")"),
		*EscJs(Message), *EscJs(Type)));
}

void UUECPBridgeBase::PushOverlayHtml(const FString& Html)
{
	ExecJs(FString::Printf(TEXT("if(typeof onOverlayContent==='function')onOverlayContent(\"%s\")"), *EscJs(Html)));
}
