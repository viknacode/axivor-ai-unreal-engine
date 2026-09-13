// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Managers/ThemeManager.h"

class SWidgetSwitcher;
class SEditableTextBox;
class STextBlock;
class SButton;
class STextComboBox;

class SEditorSyncPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEditorSyncPanel) {}
		SLATE_EVENT(FSimpleDelegate, OnActivationSuccess)
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs);
private:
	int32 ActiveTab = 0;
	TSharedPtr<SEditableTextBox> ProfileCodeInput;
	TSharedPtr<SEditableTextBox> EmailInput;
	TSharedPtr<SEditableTextBox> BundleCodeInput;
	TSharedPtr<STextBlock> StatusText;
	TSharedPtr<SWidgetSwitcher> TabSwitcher;
	TSharedPtr<SButton> SubmitButton;
	TSharedPtr<SButton> WebsiteTabButton;
	TSharedPtr<SButton> BundleTabButton;
	TSharedPtr<SButton> DiscordLinkButton;
	TSharedPtr<SButton> WebsiteLinkButton;
	TArray<TSharedPtr<FString>> LanguageOptions;
	void OnLanguageChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
	static FString LangNameToCode(const FString& Name);
	bool bIsSubmitting = false;
	bool bIsBundlePending = false;
	FSimpleDelegate OnActivationSuccessDelegate;
	FBpGeneratorTheme CachedTheme;
	FReply OnWebsiteTabClicked();
	FReply OnBundleTabClicked();
	FReply OnSubmitClicked();
	void UpdateStatus(const FString& Message, bool bIsError = false);
	void SetSubmitting(bool bSubmitting);
	bool ValidateProfileCodeFormat(const FString& Key) const;
	bool ValidateEmailFormat(const FString& Email) const;
	bool ValidateBundleCodeFormat(const FString& OrderId) const;
	void OnBundleClaimResumed();
};
