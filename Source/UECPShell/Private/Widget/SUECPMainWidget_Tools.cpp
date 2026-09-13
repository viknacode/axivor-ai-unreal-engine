// Copyright 2026, BlueprintsLab, All rights reserved

#include "SUECPMainWidget.h"
#include "Viewports/SBodyZoneSelector.h"
#include "Tools/AssetPropertyTools.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void SUECPMainWidget::OnMeshSplitterLoadBones()
{
	FString MeshPath = LoadedMeshPath;
	if (MeshPath.IsEmpty()) return;

	if (MeshPath.Contains(TEXT("'")))
	{
		int32 Start = MeshPath.Find(TEXT("'"));
		int32 End = MeshPath.Find(TEXT("'"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (Start != End && Start != INDEX_NONE)
			MeshPath = MeshPath.Mid(Start + 1, End - Start - 1);
	}

	FString OutJson, OutError;
	AssetPropertyTools::HandleListSkeletonBones(MeshPath, OutJson, OutError);
	if (!OutError.IsEmpty()) return;

	TSharedPtr<FJsonObject> JsonObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(OutJson);
	if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid()) return;

	const TArray<TSharedPtr<FJsonValue>>* BonesArr = nullptr;
	if (!JsonObj->TryGetArrayField(TEXT("bones"), BonesArr)) return;

	LoadedMeshPath = MeshPath;
	BoneList.Empty();
	SelectedBones.Empty();

	for (const auto& BoneVal : *BonesArr)
	{
		const TSharedPtr<FJsonObject>* BoneObj = nullptr;
		if (BoneVal->TryGetObject(BoneObj))
		{
			FString BoneName;
			(*BoneObj)->TryGetStringField(TEXT("name"), BoneName);
			BoneList.Add(MakeShared<FString>(BoneName));
		}
	}

	ZoneToBoneMap.Empty();
	for (int32 z = 0; z < (int32)EBodyZone::COUNT; z++)
	{
		const EBodyZone Zone = (EBodyZone)z;
		const TArray<FString> Patterns = SBodyZoneSelector::GetBonePatterns(Zone);
		for (const FString& Pattern : Patterns)
		{
			bool bFound = false;
			for (const auto& BonePtr : BoneList)
			{
				if (BonePtr.IsValid() && BonePtr->Contains(Pattern, ESearchCase::IgnoreCase))
				{
					ZoneToBoneMap.Add(Zone, *BonePtr);
					bFound = true;
					break;
				}
			}
			if (bFound) break;
		}
	}
}
