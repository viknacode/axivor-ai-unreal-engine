// Copyright 2026, BlueprintsLab, All rights reserved

#include "SUECPAssetMentionPopup.h"
#include "Utils/MountResolver.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "WidgetBlueprint.h"
#include "Animation/AnimBlueprint.h"
#include "BehaviorTree/BehaviorTree.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Engine/DataTable.h"
#include "Misc/EngineVersionComparison.h"
#if UE_VERSION_OLDER_THAN(5, 5, 0)
#include "Engine/UserDefinedStruct.h"
#else
#include "StructUtils/UserDefinedStruct.h"
#endif
#include "Engine/UserDefinedEnum.h"
#include "NiagaraSystem.h"
#include "LevelSequence.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundCue.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/BlendSpace.h"
#include "PCGGraph.h"
#include "Curves/CurveFloat.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Particles/ParticleSystem.h"
#include "StateTree.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#define LOCTEXT_NAMESPACE "SUECPAssetMentionPopup"

void SUECPAssetMentionPopup::Construct(const FArguments& InArgs)
{
	OnAssetSelected = InArgs._OnAssetSelected;
	OnPickerDismissed = InArgs._OnPickerDismissed;
	CurrentSearchQuery = InArgs._InitialSearchText;

	PopulateAssetInventory();
	ApplySearchFilter();

	const FLinearColor BorderColor = FLinearColor(0.1f, 0.7f, 0.9f);
	const FLinearColor BgColor = FLinearColor(0.08f, 0.08f, 0.1f);
	const FLinearColor TextColor = FLinearColor(0.9f, 0.9f, 0.9f);
	const FLinearColor HoverColor = FLinearColor(0.15f, 0.15f, 0.18f);

	ChildSlot
	[
		SNew(SBorder)
		.BorderBackgroundColor(BorderColor)
		.Padding(0)
		[
			SNew(SBorder)
			.BorderBackgroundColor(BgColor)
			.Padding(8.0f)
			[
				SNew(SBox)
				.MinDesiredWidth(320.0f)
				.MaxDesiredHeight(380.0f)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 0, 0, 6.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("AssetPickerTitle", "REFERENCED ASSETS"))
						.Font(FAppStyle::GetFontStyle("NormalFontBold"))
						.ColorAndOpacity(FSlateColor(TextColor))
					]

					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						SAssignNew(AssetListView, SListView<TSharedPtr<FAssetRefItem>>)
						.ListItemsSource(&DisplayList)
						.OnGenerateRow(this, &SUECPAssetMentionPopup::GenerateRowWidget)
						.OnSelectionChanged(this, &SUECPAssetMentionPopup::OnListSelectionChanged)
						.OnMouseButtonDoubleClick(this, &SUECPAssetMentionPopup::OnItemDoubleClicked)
						.SelectionMode(ESelectionMode::Single)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 6.0f, 0, 0)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("AssetPickerHint", "↑↓ Navigate | Enter Select | Esc Close"))
						.Font(FAppStyle::GetFontStyle("SmallFont"))
						.ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.9f))
					]
				]
			]
		]
	];

	if (DisplayList.Num() > 0)
	{
		AssetListView->SetSelection(DisplayList[0]);
	}
}

bool SUECPAssetMentionPopup::HandleKeyDown(const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		ClosePopup();
		return true;
	}
	else if (InKeyEvent.GetKey() == EKeys::Enter)
	{
		ConfirmCurrentSelection();
		return true;
	}
	else if (InKeyEvent.GetKey() == EKeys::Up)
	{
		MoveSelectionUp();
		return true;
	}
	else if (InKeyEvent.GetKey() == EKeys::Down)
	{
		MoveSelectionDown();
		return true;
	}
	return false;
}

void SUECPAssetMentionPopup::PopulateAssetInventory()
{
	MasterInventory.Empty();
	IndexBlueprintAssets();
	IndexSourceFiles();

	MasterInventory.Sort([](const TSharedPtr<FAssetRefItem>& A, const TSharedPtr<FAssetRefItem>& B)
	{
		if (A->GroupName != B->GroupName)
		{
			return A->GroupName < B->GroupName;
		}
		return A->Label < B->Label;
	});
}

void SUECPAssetMentionPopup::IndexBlueprintAssets()
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	struct FAssetClassEntry
	{
		FTopLevelAssetPath ClassPath;
		FString GroupLabel;
		EAssetRefType RefType;
	};

	TArray<FAssetClassEntry> AssetClasses = {
		{ UBlueprint::StaticClass()->GetClassPathName(),              TEXT("Blueprints"),            EAssetRefType::Blueprint },
		{ UWidgetBlueprint::StaticClass()->GetClassPathName(),        TEXT("Widget Blueprints"),     EAssetRefType::WidgetBlueprint },
		{ UAnimBlueprint::StaticClass()->GetClassPathName(),          TEXT("Animation Blueprints"),  EAssetRefType::AnimBlueprint },
		{ UBehaviorTree::StaticClass()->GetClassPathName(),           TEXT("Behavior Trees"),        EAssetRefType::BehaviorTree },
		{ UMaterial::StaticClass()->GetClassPathName(),               TEXT("Materials"),             EAssetRefType::Material },
		{ UMaterialInstanceConstant::StaticClass()->GetClassPathName(), TEXT("Material Instances"),  EAssetRefType::MaterialInstance },
		{ UDataTable::StaticClass()->GetClassPathName(),              TEXT("Data Tables"),           EAssetRefType::DataTable },
		{ FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("UserDefinedStruct")),   TEXT("Structs"), EAssetRefType::Struct },
		{ FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("UserDefinedEnum")),     TEXT("Enums"),   EAssetRefType::Enum },
		{ UStaticMesh::StaticClass()->GetClassPathName(),             TEXT("Static Meshes"),        EAssetRefType::StaticMesh },
		{ USkeletalMesh::StaticClass()->GetClassPathName(),           TEXT("Skeletal Meshes"),      EAssetRefType::SkeletalMesh },
		{ UTexture2D::StaticClass()->GetClassPathName(),              TEXT("Textures"),             EAssetRefType::Texture },
		{ USoundWave::StaticClass()->GetClassPathName(),              TEXT("Sounds"),               EAssetRefType::Sound },
		{ USoundCue::StaticClass()->GetClassPathName(),               TEXT("Sound Cues"),           EAssetRefType::Sound },
		{ UAnimSequence::StaticClass()->GetClassPathName(),           TEXT("Animations"),           EAssetRefType::Animation },
		{ UAnimMontage::StaticClass()->GetClassPathName(),            TEXT("Anim Montages"),        EAssetRefType::Animation },
		{ UBlendSpace::StaticClass()->GetClassPathName(),             TEXT("Blend Spaces"),         EAssetRefType::Animation },
		{ UCurveFloat::StaticClass()->GetClassPathName(),             TEXT("Curves"),               EAssetRefType::Curve },
		{ UPhysicsAsset::StaticClass()->GetClassPathName(),           TEXT("Physics Assets"),       EAssetRefType::PhysicsAsset },
		{ UParticleSystem::StaticClass()->GetClassPathName(),         TEXT("Particle Systems"),     EAssetRefType::ParticleSystem },
		{ FTopLevelAssetPath(TEXT("/Script/Niagara"), TEXT("NiagaraSystem")),       TEXT("Niagara Systems"),  EAssetRefType::NiagaraSystem },
		{ FTopLevelAssetPath(TEXT("/Script/PCG"), TEXT("PCGGraph")),                TEXT("PCG Graphs"),       EAssetRefType::PCGGraph },
		{ FTopLevelAssetPath(TEXT("/Script/LevelSequence"), TEXT("LevelSequence")), TEXT("Level Sequences"),  EAssetRefType::LevelSequence },
		{ FTopLevelAssetPath(TEXT("/Script/StateTreeModule"), TEXT("StateTree")),   TEXT("State Trees"),      EAssetRefType::StateTree },
	};

	const TArray<FString> ContentMounts = UECPMountResolver::GetUserContentMounts();

	for (const FAssetClassEntry& Entry : AssetClasses)
	{
		TArray<FAssetData> Assets;
		AssetRegistry.GetAssetsByClass(Entry.ClassPath, Assets, true);

		for (const FAssetData& Asset : Assets)
		{
			if (!UECPMountResolver::IsPathUnderMount(Asset.PackagePath.ToString(), ContentMounts))
			{
				continue;
			}

			MasterInventory.Add(MakeShared<FAssetRefItem>(
				Asset.AssetName.ToString(),
				Asset.PackageName.ToString(),
				Entry.GroupLabel,
				Entry.RefType
			));
		}
	}
}

void SUECPAssetMentionPopup::IndexSourceFiles()
{
	FString SourceDir = FPaths::ProjectDir() / TEXT("Source");

	if (!FPaths::DirectoryExists(SourceDir))
	{
		return;
	}

	TArray<FString> FoundFiles;
	IFileManager::Get().FindFilesRecursive(FoundFiles, *SourceDir, TEXT("*.h"), true, false);
	IFileManager::Get().FindFilesRecursive(FoundFiles, *SourceDir, TEXT("*.cpp"), true, false);

	for (const FString& FilePath : FoundFiles)
	{
		FString FileName = FPaths::GetCleanFilename(FilePath);
		FString Extension = FPaths::GetExtension(FilePath).ToLower();

		EAssetRefType RefType = Extension == TEXT("h") ? EAssetRefType::CppHeader : EAssetRefType::CppSource;
		FString GroupLabel = Extension == TEXT("h") ? TEXT("C++ Headers") : TEXT("C++ Sources");

		FString RelativePath = FilePath;
		FPaths::MakePathRelativeTo(RelativePath, *FPaths::ProjectDir());

		MasterInventory.Add(MakeShared<FAssetRefItem>(
			FileName,
			RelativePath,
			GroupLabel,
			RefType
		));
	}
}

void SUECPAssetMentionPopup::ApplySearchFilter()
{
	DisplayList.Empty();

	if (CurrentSearchQuery.IsEmpty())
	{
		const int32 MaxItems = 500;
		for (int32 i = 0; i < FMath::Min(MasterInventory.Num(), MaxItems); ++i)
		{
			DisplayList.Add(MasterInventory[i]);
		}
	}
	else
	{
		FString SearchLower = CurrentSearchQuery.ToLower();

		TArray<TSharedPtr<FAssetRefItem>> ContainsMatches;
		for (const TSharedPtr<FAssetRefItem>& Item : MasterInventory)
		{
			FString LabelLower = Item->Label.ToLower();
			if (LabelLower.StartsWith(SearchLower))
			{
				DisplayList.Add(Item);
			}
			else if (LabelLower.Contains(SearchLower))
			{
				ContainsMatches.Add(Item);
			}
		}
		DisplayList.Append(ContainsMatches);
	}

	if (AssetListView.IsValid())
	{
		AssetListView->RequestListRefresh();

		if (DisplayList.Num() > 0)
		{
			AssetListView->SetSelection(DisplayList[0]);
		}
	}
}

void SUECPAssetMentionPopup::UpdateSearchFilter(const FString& SearchText)
{
	CurrentSearchQuery = SearchText;
	ApplySearchFilter();
}

void SUECPAssetMentionPopup::RebuildAssetList()
{
	PopulateAssetInventory();
	ApplySearchFilter();
}

void SUECPAssetMentionPopup::MoveSelectionDown()
{
	if (DisplayList.Num() == 0)
	{
		return;
	}

	TArray<TSharedPtr<FAssetRefItem>> Selected;
	AssetListView->GetSelectedItems(Selected);

	int32 CurrentIdx = 0;
	if (Selected.Num() > 0)
	{
		CurrentIdx = DisplayList.IndexOfByKey(Selected[0]);
	}

	int32 NextIdx = FMath::Min(CurrentIdx + 1, DisplayList.Num() - 1);
	AssetListView->SetSelection(DisplayList[NextIdx]);
	AssetListView->RequestScrollIntoView(DisplayList[NextIdx]);
}

void SUECPAssetMentionPopup::MoveSelectionUp()
{
	if (DisplayList.Num() == 0)
	{
		return;
	}

	TArray<TSharedPtr<FAssetRefItem>> Selected;
	AssetListView->GetSelectedItems(Selected);

	int32 CurrentIdx = 0;
	if (Selected.Num() > 0)
	{
		CurrentIdx = DisplayList.IndexOfByKey(Selected[0]);
	}

	int32 PrevIdx = FMath::Max(CurrentIdx - 1, 0);
	AssetListView->SetSelection(DisplayList[PrevIdx]);
	AssetListView->RequestScrollIntoView(DisplayList[PrevIdx]);
}

void SUECPAssetMentionPopup::ConfirmCurrentSelection()
{
	TArray<TSharedPtr<FAssetRefItem>> Selected;
	AssetListView->GetSelectedItems(Selected);

	if (Selected.Num() > 0 && Selected[0].IsValid())
	{
		OnAssetSelected.ExecuteIfBound(*Selected[0]);
	}
}

TSharedRef<ITableRow> SUECPAssetMentionPopup::GenerateRowWidget(TSharedPtr<FAssetRefItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FAssetRefItem>>, OwnerTable)
		.Padding(FMargin(8.0f, 4.0f))
		.ToolTipText(FText::FromString(Item->AssetPath))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Item->Label))
				.Font(FAppStyle::GetFontStyle("NormalFont"))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Item->GroupName))
				.Font(FAppStyle::GetFontStyle("SmallFont"))
				.ColorAndOpacity(FSlateColor(FStyleColors::AccentBlue))
			]
		];
}

void SUECPAssetMentionPopup::OnListSelectionChanged(TSharedPtr<FAssetRefItem> Item, ESelectInfo::Type SelectInfo)
{
}

void SUECPAssetMentionPopup::OnItemDoubleClicked(TSharedPtr<FAssetRefItem> Item)
{
	if (Item.IsValid())
	{
		OnAssetSelected.ExecuteIfBound(*Item);
	}
}

FReply SUECPAssetMentionPopup::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		ClosePopup();
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::Enter)
	{
		ConfirmCurrentSelection();
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::Up)
	{
		MoveSelectionUp();
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::Down)
	{
		MoveSelectionDown();
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SUECPAssetMentionPopup::ClosePopup()
{
	OnPickerDismissed.ExecuteIfBound();
}

#undef LOCTEXT_NAMESPACE
