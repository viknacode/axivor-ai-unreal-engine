// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPCreateAssetRegistry.h"
#include "UECPCoreModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/PackageName.h"

FName FUECPCreateAssetRegistryImpl::Normalise(FName In)
{
	FString S = In.ToString();
	S.ToLowerInline();
	return FName(*S);
}

void FUECPCreateAssetRegistryImpl::RegisterType(FName AssetType, FFactoryFn Factory, FName OwningExtension)
{
	if (AssetType.IsNone())
	{
		UE_LOG(LogUECPCore, Warning, TEXT("CreateAssetRegistry: RegisterType called with empty AssetType — ignored."));
		return;
	}
	if (!Factory)
	{
		UE_LOG(LogUECPCore, Warning, TEXT("CreateAssetRegistry: RegisterType(%s) called with empty factory — ignored."),
			*AssetType.ToString());
		return;
	}

	FEntry Entry;
	Entry.Factory         = MoveTemp(Factory);
	Entry.OwningExtension = OwningExtension;
	Types.Add(Normalise(AssetType), MoveTemp(Entry));
}

void FUECPCreateAssetRegistryImpl::UnregisterType(FName AssetType)
{
	Types.Remove(Normalise(AssetType));
}

FUECPCreateAssetResult FUECPCreateAssetRegistryImpl::Create(
	const FString& AssetType,
	const FString& Name,
	const FString& SavePath,
	const TSharedPtr<FJsonObject>& Options,
	FString& OutError)
{
	FUECPCreateAssetResult Result;

	if (AssetType.IsEmpty())
	{
		OutError = TEXT("create_asset: asset_type is required.");
		Result.Error = OutError;
		return Result;
	}

	const FName Key = Normalise(FName(*AssetType));
	if (const FEntry* Entry = Types.Find(Key))
	{
		FString CleanName = Name;
		if (!CleanName.IsEmpty())
		{
			int32 DotIdx = INDEX_NONE;
			if (CleanName.FindChar(TEXT('.'), DotIdx) && DotIdx > 0)
			{
				const FString LeftPart = CleanName.Left(DotIdx);
				const FString RightPart = CleanName.RightChop(DotIdx + 1);
				if (RightPart.Equals(LeftPart) || RightPart.Equals(LeftPart + TEXT("_C")))
				{
					CleanName = LeftPart;
				}
			}
			static const TCHAR* IllegalChars = TEXT(". /\\:*?<>|\"");
			bool bHasIllegal = false;
			FString IllegalFound;
			for (TCHAR C : CleanName)
			{
				if (FCString::Strchr(IllegalChars, C))
				{
					bHasIllegal = true;
					IllegalFound = FString::Printf(TEXT("%c"), C);
					break;
				}
			}
			if (bHasIllegal)
			{
				OutError = FString::Printf(
					TEXT("create_asset: name '%s' contains illegal character '%s' — asset names allow letters, digits and underscores only. "
					     "If you meant to pass the full path '/Game/Folder/AssetName', set save_path='/Game/Folder' and name='AssetName' separately."),
					*Name, *IllegalFound);
				Result.Error = OutError;
				return Result;
			}
		}

		if (!CleanName.IsEmpty() && !SavePath.IsEmpty())
		{
			FString CleanSavePath = SavePath;
			while (CleanSavePath.EndsWith(TEXT("/"))) CleanSavePath = CleanSavePath.LeftChop(1);
			if (CleanSavePath.EndsWith(TEXT("/") + CleanName, ESearchCase::IgnoreCase))
			{
				CleanSavePath = CleanSavePath.LeftChop(CleanName.Len() + 1);
				while (CleanSavePath.EndsWith(TEXT("/"))) CleanSavePath = CleanSavePath.LeftChop(1);
			}
			const FString FullAssetPath = CleanSavePath + TEXT("/") + CleanName;
			if (FPackageName::IsValidLongPackageName(FullAssetPath))
			{
				bool bExists = FPackageName::DoesPackageExist(FullAssetPath);
				if (!bExists)
				{
					IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
						TEXT("AssetRegistry")).Get();
					const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *FullAssetPath, *CleanName);
					const FAssetData AD = AR.GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
					if (AD.IsValid()) bExists = true;
				}
				if (bExists)
				{
					OutError = FString::Printf(
						TEXT("create_asset: an asset already exists at '%s' (asset_type='%s'). Refusing to overwrite — the editor would pop a modal dialog that blocks unattended runs. Pick a different name OR delete the existing asset first via asset_management(action='delete_asset', asset_path='%s')."),
						*FullAssetPath, *AssetType, *FullAssetPath);
					Result.Error = OutError;
					return Result;
				}
			}
		}

		Result = Entry->Factory(CleanName, SavePath, Options, OutError);
		if (!Result.Error.IsEmpty() && OutError.IsEmpty())
		{
			OutError = Result.Error;
		}
		return Result;
	}

	FString Closest;
	int32   ClosestDist = MAX_int32;
	const FString LowerIn = AssetType.ToLower();
	for (const auto& Pair : Types)
	{
		const FString Candidate = Pair.Key.ToString();
		int32 Dist = FMath::Abs(LowerIn.Len() - Candidate.Len());
		for (TCHAR C : LowerIn)
		{
			if (!Candidate.Contains(FString(1, &C))) ++Dist;
		}
		for (TCHAR C : Candidate)
		{
			if (!LowerIn.Contains(FString(1, &C))) ++Dist;
		}
		if (Dist < ClosestDist)
		{
			ClosestDist = Dist;
			Closest = Candidate;
		}
	}

	TArray<FString> CoreTypes;
	for (const auto& Pair : Types)
	{
		if (Pair.Value.OwningExtension.IsNone()) CoreTypes.Add(Pair.Key.ToString());
	}
	CoreTypes.Sort();
	const FString CoreList = FString::Join(CoreTypes, TEXT(", "));

	const int32 SuggestThreshold = FMath::Max(2, LowerIn.Len() / 5);

	static const TSet<FString> ParentClassConfusion = {
		TEXT("actor"), TEXT("character"), TEXT("pawn"), TEXT("playercontroller"),
		TEXT("gamemode"), TEXT("gameinstance"), TEXT("savegame"),
		TEXT("actorcomponent"), TEXT("scenecomponent"), TEXT("camerashakebase")
	};
	if (ParentClassConfusion.Contains(LowerIn))
	{
		OutError = FString::Printf(
			TEXT("create_asset: '%s' is a parent_class, not an asset_type. Did you mean ")
			TEXT("`create_asset(asset_type='Blueprint', options={parent_class:'%s'}, name=..., save_path=...)`? "
			     "Core asset_types: %s"),
			*AssetType, *AssetType, *CoreList);
	}
	else if (!Closest.IsEmpty() && ClosestDist <= SuggestThreshold)
	{
		OutError = FString::Printf(
			TEXT("create_asset: unknown asset_type '%s'. Did you mean '%s'? Core asset_types: %s. ")
			TEXT("Call list_create_asset_types for opt-in extension types (animation/AI/audio/...)."),
			*AssetType, *Closest, *CoreList);
	}
	else
	{
		OutError = FString::Printf(
			TEXT("create_asset: unknown asset_type '%s'. Core asset_types: %s. ")
			TEXT("If your type is in an opt-in extension (LiveLink, MVVM, Vehicle, GameFeatures, Media), "
			     "enable it in Settings → Extensions, then `list_create_asset_types` to confirm."),
			*AssetType, *CoreList);
	}

	Result.Error = OutError;
	return Result;
}

TArray<FName> FUECPCreateAssetRegistryImpl::ListTypes() const
{
	TArray<FName> Out;
	Out.Reserve(Types.Num());
	for (const auto& Pair : Types) Out.Add(Pair.Key);
	Out.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
	return Out;
}

FName FUECPCreateAssetRegistryImpl::GetOwningExtension(FName AssetType) const
{
	if (const FEntry* Entry = Types.Find(Normalise(AssetType)))
	{
		return Entry->OwningExtension;
	}
	return NAME_None;
}
