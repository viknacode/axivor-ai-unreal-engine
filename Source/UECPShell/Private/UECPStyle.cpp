// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPStyle.h"
#include "UECPShellModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/SlateStyleRegistry.h"
#include "Slate/SlateGameResources.h"
const TCHAR* GpluginWindow = TEXT("uep_B81");
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleMacros.h"

#define RootToContentDir Style->RootToContentDir

TSharedPtr<FSlateStyleSet> FUECPStyle::StyleInstance = nullptr;

void FUECPStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FUECPStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FUECPStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("BpGeneratorUltimateStyle"));
	return StyleSetName;
}

const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon20x20(20.0f, 20.0f);

TSharedRef< FSlateStyleSet > FUECPStyle::Create()
{
	TSharedRef< FSlateStyleSet > Style = MakeShareable(new FSlateStyleSet("BpGeneratorUltimateStyle"));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("BpGeneratorUltimate")->GetBaseDir() / TEXT("Resources"));

	Style->Set("BpGeneratorUltimate.PluginAction", new IMAGE_BRUSH(TEXT("Icon128"), Icon20x20));

    const FVector2D BrandingLogoSize(128.0f, 45.0f);
    Style->Set("BpGeneratorUltimate.BrandingLogo", new IMAGE_BRUSH(TEXT("bplabs1"), BrandingLogoSize));

	return Style;
}

void FUECPStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FUECPStyle::Get()
{
	return *StyleInstance;
}
