// Copyright 2026, BlueprintsLab, All rights reserved

#include "McpResourcesProvider.h"

#include "MCPToolsLog.h"
#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Utils/MountResolver.h"

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"

namespace McpResourcesProvider
{

namespace
{

	constexpr int32 kDefaultPageSize = 200;

	constexpr const TCHAR* kUriPrefix = TEXT("uecp://asset/");

	const TCHAR* const kExposedClassPaths[] = {
		TEXT("/Script/Engine.Blueprint"),
		TEXT("/Script/Engine.Material"),
		TEXT("/Script/Engine.MaterialFunction"),
		TEXT("/Script/Engine.Texture"),
		TEXT("/Script/Engine.StaticMesh"),
		TEXT("/Script/Engine.SkeletalMesh"),
		TEXT("/Script/Engine.SoundCue"),
		TEXT("/Script/Engine.SoundClass"),
		TEXT("/Script/Engine.DataTable"),
		TEXT("/Script/Engine.DataAsset"),
		TEXT("/Script/Engine.UserDefinedStruct"),
		TEXT("/Script/Engine.UserDefinedEnum"),
		TEXT("/Script/Engine.CurveFloat"),
		TEXT("/Script/Engine.CurveLinearColor"),
		TEXT("/Script/Engine.CurveVector"),
		TEXT("/Script/Engine.AnimMontage"),
		TEXT("/Script/Engine.AnimSequence"),
		TEXT("/Script/Engine.BlendSpace"),
		TEXT("/Script/Engine.LevelSequence"),
		TEXT("/Script/AIModule.BehaviorTree"),
		TEXT("/Script/AIModule.BlackboardData"),
		TEXT("/Script/AIModule.EnvQuery"),
		TEXT("/Script/Niagara.NiagaraSystem"),
		TEXT("/Script/Niagara.NiagaraEmitter"),
		TEXT("/Script/UMG.WidgetBlueprint"),
		TEXT("/Script/EnhancedInput.InputAction"),
		TEXT("/Script/EnhancedInput.InputMappingContext"),
		TEXT("/Script/GameplayAbilities.GameplayEffect"),
		TEXT("/Script/MetasoundEngine.MetaSoundSource"),
	};

	bool DecodeAssetUri(const FString& Uri, FString& OutPackagePath)
	{
		if (!Uri.StartsWith(kUriPrefix)) return false;
		OutPackagePath = TEXT("/") + Uri.RightChop(FCString::Strlen(kUriPrefix));
		return !OutPackagePath.IsEmpty();
	}

	FString EncodeAssetUri(const FString& PackagePath)
	{
		FString Trimmed = PackagePath;
		if (Trimmed.StartsWith(TEXT("/"))) Trimmed = Trimmed.RightChop(1);
		return FString(kUriPrefix) + Trimmed;
	}

	FString MimeForAsset(const FAssetData& )
	{
		return TEXT("application/json");
	}

	TSharedRef<FJsonObject> BuildResourceDescriptor(const FAssetData& Asset)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("uri"),         EncodeAssetUri(Asset.PackageName.ToString()));
		Obj->SetStringField(TEXT("name"),        Asset.AssetName.ToString());
		Obj->SetStringField(TEXT("description"), FString::Printf(
			TEXT("%s at %s"),
			*Asset.AssetClassPath.GetAssetName().ToString(),
			*Asset.PackageName.ToString()));
		Obj->SetStringField(TEXT("mimeType"),    MimeForAsset(Asset));
		return Obj;
	}

	FString FetchAssetSummary(const FString& PackagePath)
	{
		if (!IUECPCoreModule::IsAvailable())
		{
			return TEXT("{\"error\":\"Core unavailable\"}");
		}

		IUECPToolDispatcher& Dispatcher = IUECPCoreModule::Get().GetToolDispatcher();
		const FName ToolName(TEXT("get_asset_summary"));
		if (!Dispatcher.IsRegistered(ToolName))
		{
			return TEXT("{\"error\":\"get_asset_summary not registered\"}");
		}

		TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), PackagePath);

		FUECPToolResult Result;
		FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady(
			[&Dispatcher, ToolName, Args, &Result]()
			{ Result = Dispatcher.ExecuteFromArgs(ToolName, Args); },
			TStatId(), nullptr, ENamedThreads::GameThread);
		Task->Wait();

		if (!Result.bSuccess && Result.ResultJson.IsEmpty())
		{
			return FString::Printf(TEXT("{\"error\":\"%s\"}"),
				*Result.ErrorMessage.ReplaceCharWithEscapedChar());
		}
		return Result.ResultJson;
	}
}

TSharedRef<FJsonObject> BuildResourcesListResult(const TSharedPtr<FJsonObject>& Params)
{
	int32 Offset = 0;
	int32 PageSize = kDefaultPageSize;
	if (Params.IsValid())
	{
		FString CursorStr;
		if (Params->TryGetStringField(TEXT("cursor"), CursorStr))
		{
			Offset = FMath::Max(0, FCString::Atoi(*CursorStr));
		}
		double PageSizeDouble;
		if (Params->TryGetNumberField(TEXT("pageSize"), PageSizeDouble))
		{
			PageSize = FMath::Clamp((int32)PageSizeDouble, 1, 1000);
		}
	}

	FARFilter Filter;
	for (const TCHAR* ClassPath : kExposedClassPaths)
	{
		Filter.ClassPaths.Add(FTopLevelAssetPath(ClassPath));
	}
	for (const FString& Mount : UECPMountResolver::GetUserContentMounts())
	{
		Filter.PackagePaths.Add(FName(*Mount));
	}
	Filter.bRecursivePaths   = true;
	Filter.bRecursiveClasses = true;

	TArray<FAssetData> Assets;
	FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady(
		[&Filter, &Assets]()
		{
			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			ARM.Get().GetAssets(Filter, Assets);
		},
		TStatId(), nullptr, ENamedThreads::GameThread);
	Task->Wait();

	Assets.Sort([](const FAssetData& A, const FAssetData& B)
	{
		return A.PackageName.LexicalLess(B.PackageName);
	});

	const int32 PageEnd = FMath::Min(Offset + PageSize, Assets.Num());
	TArray<TSharedPtr<FJsonValue>> ResourceArray;
	ResourceArray.Reserve(PageEnd - Offset);
	for (int32 i = Offset; i < PageEnd; ++i)
	{
		ResourceArray.Add(MakeShared<FJsonValueObject>(BuildResourceDescriptor(Assets[i])));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("resources"), ResourceArray);
	if (PageEnd < Assets.Num())
	{
		Result->SetStringField(TEXT("nextCursor"), FString::FromInt(PageEnd));
	}
	return Result;
}

TSharedRef<FJsonObject> HandleResourcesRead(const TSharedPtr<FJsonObject>& Params)
{
	const auto MakeError = [](const FString& Uri, const FString& Msg) -> TSharedRef<FJsonObject>
	{
		TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Contents;
		TSharedRef<FJsonObject> Block = MakeShared<FJsonObject>();
		Block->SetStringField(TEXT("uri"),      Uri);
		Block->SetStringField(TEXT("mimeType"), TEXT("application/json"));
		Block->SetStringField(TEXT("text"),
			FString::Printf(TEXT("{\"error\":\"%s\"}"), *Msg.ReplaceCharWithEscapedChar()));
		Contents.Add(MakeShared<FJsonValueObject>(Block));
		R->SetArrayField(TEXT("contents"), Contents);
		return R;
	};

	if (!Params.IsValid())
	{
		return MakeError(TEXT(""), TEXT("missing params"));
	}

	FString Uri;
	if (!Params->TryGetStringField(TEXT("uri"), Uri) || Uri.IsEmpty())
	{
		return MakeError(TEXT(""), TEXT("params.uri is required"));
	}

	FString PackagePath;
	if (!DecodeAssetUri(Uri, PackagePath))
	{
		return MakeError(Uri, FString::Printf(TEXT("Unrecognised URI scheme — expected %s<package-path>"), kUriPrefix));
	}

	const FString SummaryJson = FetchAssetSummary(PackagePath);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Contents;
	TSharedRef<FJsonObject> Block = MakeShared<FJsonObject>();
	Block->SetStringField(TEXT("uri"),      Uri);
	Block->SetStringField(TEXT("mimeType"), TEXT("application/json"));
	Block->SetStringField(TEXT("text"),     SummaryJson);
	Contents.Add(MakeShared<FJsonValueObject>(Block));
	Result->SetArrayField(TEXT("contents"), Contents);
	return Result;
}

}
