// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Input/Reply.h"
#include "AssetReferenceTypes.h"

DECLARE_DELEGATE_OneParam(FOnAssetRefSelected, const FAssetRefItem&);

class UECPSHELL_API SUECPAssetMentionPopup : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUECPAssetMentionPopup)
	{}
		SLATE_ARGUMENT(FString, InitialSearchText)
		SLATE_ARGUMENT(FOnAssetRefSelected, OnAssetSelected)
		SLATE_ARGUMENT(FSimpleDelegate, OnPickerDismissed)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	bool HandleKeyDown(const FKeyEvent& InKeyEvent);
	void UpdateSearchFilter(const FString& SearchText);
	void RebuildAssetList();
	void MoveSelectionUp();
	void MoveSelectionDown();
	void ConfirmCurrentSelection();
	void ClosePopup();

private:
	void PopulateAssetInventory();
	void IndexBlueprintAssets();
	void IndexSourceFiles();
	void ApplySearchFilter();

	TSharedRef<ITableRow> GenerateRowWidget(TSharedPtr<FAssetRefItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnListSelectionChanged(TSharedPtr<FAssetRefItem> Item, ESelectInfo::Type SelectInfo);
	void OnItemDoubleClicked(TSharedPtr<FAssetRefItem> Item);
	FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent);

private:
	TArray<TSharedPtr<FAssetRefItem>> MasterInventory;
	TArray<TSharedPtr<FAssetRefItem>> DisplayList;
	TSharedPtr<SListView<TSharedPtr<FAssetRefItem>>> AssetListView;
	FString CurrentSearchQuery;

	FOnAssetRefSelected OnAssetSelected;
	FSimpleDelegate OnPickerDismissed;
};
