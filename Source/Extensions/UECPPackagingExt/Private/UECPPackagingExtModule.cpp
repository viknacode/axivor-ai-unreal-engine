// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPPackagingExtModule.h"

#include "Tools/PackagingTools.h"

#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"

DEFINE_LOG_CATEGORY(LogUECPPackagingExt);

namespace
{
	static const TArray<FName>& OwnedToolNames()
	{
		static const TArray<FName> Names = {
			TEXT("get_packaging_settings"),
			TEXT("list_target_platforms"),
			TEXT("validate_packaging_setup"),
			TEXT("package_project"),
			TEXT("get_packaging_status"),
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

void FUECPPackagingExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	// Reads of engine settings / UObjects run on the game thread.
	D.RegisterHandler(TEXT("get_packaging_settings"),   MakeHandler(PackagingTools::HandleGetPackagingSettingsFromArgs));
	D.RegisterHandler(TEXT("list_target_platforms"),    MakeHandler(PackagingTools::HandleListTargetPlatformsFromArgs));
	D.RegisterHandler(TEXT("validate_packaging_setup"), MakeHandler(PackagingTools::HandleValidatePackagingSetupFromArgs));
	D.RegisterHandler(TEXT("package_project"),          MakeHandler(PackagingTools::HandlePackageProjectFromArgs));

	// Status polling only touches the job registry + a log file — safe off the game
	// thread, so polling never blocks the editor.
	D.RegisterHandler(TEXT("get_packaging_status"),     MakeHandler(PackagingTools::HandleGetPackagingStatusFromArgs),
		EUECPToolThreadAffinity::AnyThread);

	{
		const FName U(TEXT("packaging"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("get_packaging_settings"),   TEXT("Read the project's packaging config (build config, target, pak/compress flags, maps to cook)."), TEXT(""));
		Meta(TEXT("list_target_platforms"),    TEXT("List target platforms known to this editor install."), TEXT(""));
		Meta(TEXT("validate_packaging_setup"), TEXT("Check for common packaging blockers (missing build target, no cook maps, missing RunUAT) before a build."), TEXT("platform?"));
		Meta(TEXT("package_project"),          TEXT("Launch UAT BuildCookRun as a background job and return a job_id + log path. Poll with get_packaging_status."), TEXT("platform?, configuration?, archive_directory?, maps?, extra_args?, no_pak?"));
		Meta(TEXT("get_packaging_status"),     TEXT("Poll a package_project job: running/succeeded/failed, exit code, elapsed, error/warning counts, log tail."), TEXT("job_id?, log_lines?"));
	}

	UE_LOG(LogUECPPackagingExt, Log, TEXT("Registered %d packaging tools (packaging umbrella)"),
		OwnedToolNames().Num());
}

void FUECPPackagingExtModule::ShutdownModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const FName& N : OwnedToolNames()) D.UnregisterHandler(N);
}

IMPLEMENT_MODULE(FUECPPackagingExtModule, UECPPackagingExt)
