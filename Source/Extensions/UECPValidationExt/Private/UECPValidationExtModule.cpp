// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPValidationExtModule.h"

#include "Tools/ValidationTools.h"

#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"

DEFINE_LOG_CATEGORY(LogUECPValidationExt);

namespace
{
	static const TArray<FName>& OwnedToolNames()
	{
		static const TArray<FName> Names = {
			TEXT("validate_asset"),
			TEXT("validate_assets"),
			TEXT("validate_project"),
		};
		return Names;
	}

	static auto MakeHandler(TFunction<void(const TSharedPtr<FJsonObject>&, FString&, FString&)> Fn)
	{
		return [Fn = MoveTemp(Fn)](const TSharedPtr<FJsonObject>& Args) -> FUECPToolResult
		{
			FUECPToolResult R;
			Fn(Args, R.ResultJson, R.ErrorMessage);
			R.bSuccess = R.ErrorMessage.IsEmpty();
			return R;
		};
	}
}

void FUECPValidationExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	// All validation loads and runs UObject validators on the game thread.
	D.RegisterHandler(TEXT("validate_asset"),   MakeHandler(ValidationTools::HandleValidateAssetFromArgs));
	D.RegisterHandler(TEXT("validate_assets"),  MakeHandler(ValidationTools::HandleValidateAssetsFromArgs));
	D.RegisterHandler(TEXT("validate_project"), MakeHandler(ValidationTools::HandleValidateProjectFromArgs));

	{
		const FName U(TEXT("validation"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("validate_asset"),   TEXT("Run Data Validation on a single asset; returns its errors and warnings."), TEXT("asset_path"));
		Meta(TEXT("validate_assets"),  TEXT("Validate every asset under a content folder (optionally recursive); returns a summary + per-asset issues."), TEXT("folder_path, recursive?, max_reported?"));
		Meta(TEXT("validate_project"), TEXT("Validate all /Game assets; returns totals + the invalid/warning assets (capped). Run before packaging."), TEXT("max_reported?"));
	}

	UE_LOG(LogUECPValidationExt, Log, TEXT("Registered %d validation tools (validation umbrella)"),
		OwnedToolNames().Num());
}

void FUECPValidationExtModule::ShutdownModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const FName& N : OwnedToolNames()) D.UnregisterHandler(N);
}

IMPLEMENT_MODULE(FUECPValidationExtModule, UECPValidationExt)
