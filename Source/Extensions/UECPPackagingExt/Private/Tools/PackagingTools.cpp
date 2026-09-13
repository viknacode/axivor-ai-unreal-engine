// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/PackagingTools.h"
#include "UECPPackagingExtModule.h"

#include "Settings/ProjectPackagingSettings.h"
#include "GameMapsSettings.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Interfaces/ITargetPlatform.h"

#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Async/Async.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace PackagingTools
{

// ============================================================
// Job registry — one entry per launched BuildCookRun, kept in
// module memory so get_packaging_status can poll across calls.
// ============================================================
namespace
{
	struct FPackagingJob
	{
		FString            JobId;
		FString            LogPath;
		FString            Command;
		FString            Platform;
		FString            Configuration;
		FDateTime          StartTime;
		uint32             Pid = 0;
		TAtomic<bool>      bFinished{ false };
		TAtomic<int32>     ExitCode{ 0 };
	};

	FCriticalSection& JobsLock()
	{
		static FCriticalSection L;
		return L;
	}

	TMap<FString, TSharedPtr<FPackagingJob>>& Jobs()
	{
		static TMap<FString, TSharedPtr<FPackagingJob>> M;
		return M;
	}

	FString& LastJobId()
	{
		static FString Id;
		return Id;
	}

	FString BuildConfigToString(EProjectPackagingBuildConfigurations Cfg)
	{
		switch (Cfg)
		{
			case EProjectPackagingBuildConfigurations::PPBC_Debug:       return TEXT("Debug");
			case EProjectPackagingBuildConfigurations::PPBC_DebugGame:   return TEXT("DebugGame");
			case EProjectPackagingBuildConfigurations::PPBC_Development:  return TEXT("Development");
			case EProjectPackagingBuildConfigurations::PPBC_Test:        return TEXT("Test");
			case EProjectPackagingBuildConfigurations::PPBC_Shipping:    return TEXT("Shipping");
			default:                                                     return TEXT("Development");
		}
	}

	FString RunUatPath()
	{
#if PLATFORM_WINDOWS
		return FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Build/BatchFiles/RunUAT.bat"));
#else
		return FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Build/BatchFiles/RunUAT.sh"));
#endif
	}

	FString SerializeObject(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj, Writer);
		return Out;
	}
}

// ============================================================
// get_packaging_settings
// ============================================================
void HandleGetPackagingSettingsFromArgs(const TSharedPtr<FJsonObject>& /*Args*/, FString& OutJsonString, FString& OutError)
{
	const UProjectPackagingSettings* Settings = GetDefault<UProjectPackagingSettings>();
	if (!Settings)
	{
		OutError = TEXT("Could not resolve UProjectPackagingSettings.");
		return;
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("build_configuration"), BuildConfigToString(Settings->BuildConfiguration));
	Root->SetStringField(TEXT("build_target"), Settings->BuildTarget.IsEmpty() ? TEXT("(default)") : Settings->BuildTarget);
	Root->SetBoolField(TEXT("full_rebuild"), Settings->FullRebuild);
	Root->SetBoolField(TEXT("for_distribution"), Settings->ForDistribution);
	Root->SetBoolField(TEXT("include_debug_files"), Settings->IncludeDebugFiles);
	Root->SetBoolField(TEXT("use_pak_file"), Settings->UsePakFile);
	Root->SetBoolField(TEXT("compressed"), Settings->bCompressed);

	TArray<TSharedPtr<FJsonValue>> Maps;
	for (const FFilePath& Map : Settings->MapsToCook)
	{
		if (!Map.FilePath.IsEmpty()) Maps.Add(MakeShared<FJsonValueString>(Map.FilePath));
	}
	Root->SetArrayField(TEXT("maps_to_cook"), Maps);

	TArray<TSharedPtr<FJsonValue>> AlwaysCook;
	for (const FDirectoryPath& Dir : Settings->DirectoriesToAlwaysCook)
	{
		if (!Dir.Path.IsEmpty()) AlwaysCook.Add(MakeShared<FJsonValueString>(Dir.Path));
	}
	Root->SetArrayField(TEXT("directories_to_always_cook"), AlwaysCook);

	Root->SetStringField(TEXT("default_game_map"), UGameMapsSettings::GetGameDefaultMap());
	Root->SetStringField(TEXT("project_file"), FPaths::GetProjectFilePath());

	OutJsonString = SerializeObject(Root);
}

// ============================================================
// list_target_platforms
// ============================================================
void HandleListTargetPlatformsFromArgs(const TSharedPtr<FJsonObject>& /*Args*/, FString& OutJsonString, FString& OutError)
{
	ITargetPlatformManagerModule* TPM = FModuleManager::LoadModulePtr<ITargetPlatformManagerModule>(TEXT("TargetPlatform"));
	if (!TPM)
	{
		OutError = TEXT("TargetPlatform module is not available.");
		return;
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const ITargetPlatform* Platform : TPM->GetTargetPlatforms())
	{
		if (!Platform) continue;
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("name"), Platform->PlatformName());
		P->SetBoolField(TEXT("is_server_only"), Platform->IsServerOnly());
		P->SetBoolField(TEXT("is_client_only"), Platform->IsClientOnly());
		Arr.Add(MakeShared<FJsonValueObject>(P));
	}
	Root->SetArrayField(TEXT("platforms"), Arr);
	Root->SetNumberField(TEXT("count"), Arr.Num());

	OutJsonString = SerializeObject(Root);
}

// ============================================================
// validate_packaging_setup
// ============================================================
void HandleValidatePackagingSetupFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const UProjectPackagingSettings* Settings = GetDefault<UProjectPackagingSettings>();

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);

	TArray<TSharedPtr<FJsonValue>> Issues;
	const auto AddIssue = [&Issues](const TCHAR* Severity, const FString& Message, const FString& Suggestion)
	{
		TSharedRef<FJsonObject> I = MakeShared<FJsonObject>();
		I->SetStringField(TEXT("severity"), Severity);
		I->SetStringField(TEXT("message"), Message);
		if (!Suggestion.IsEmpty()) I->SetStringField(TEXT("suggestion"), Suggestion);
		Issues.Add(MakeShared<FJsonValueObject>(I));
	};

	// RunUAT present?
	const FString Uat = RunUatPath();
	if (!IFileManager::Get().FileExists(*Uat))
	{
		AddIssue(TEXT("error"),
			FString::Printf(TEXT("RunUAT not found at '%s'."), *Uat),
			TEXT("Verify the engine installation; packaging cannot run without UnrealAutomationTool."));
	}

	// Build target set?
	if (Settings && Settings->BuildTarget.IsEmpty())
	{
		AddIssue(TEXT("info"),
			TEXT("No explicit BuildTarget selected in Packaging settings."),
			TEXT("UAT will pick the default target. Set one in Project Settings > Packaging if the project has multiple game targets."));
	}

	// Something to cook?
	const bool bHasMaps = Settings && Settings->MapsToCook.Num() > 0;
	const bool bHasDefaultMap = !UGameMapsSettings::GetGameDefaultMap().IsEmpty();
	if (!bHasMaps && !bHasDefaultMap)
	{
		AddIssue(TEXT("warning"),
			TEXT("No maps in MapsToCook and no default game map is set."),
			TEXT("Set a default map (Project Settings > Maps & Modes) or add entries to MapsToCook, otherwise the cook may stage no content."));
	}

	// Platform SDK sanity for a requested platform.
	FString Platform;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("platform"), Platform);
	if (!Platform.IsEmpty())
	{
		ITargetPlatformManagerModule* TPM = FModuleManager::LoadModulePtr<ITargetPlatformManagerModule>(TEXT("TargetPlatform"));
		bool bFound = false;
		if (TPM)
		{
			for (const ITargetPlatform* P : TPM->GetTargetPlatforms())
			{
				if (P && P->PlatformName() == Platform) { bFound = true; break; }
			}
		}
		if (!bFound)
		{
			AddIssue(TEXT("error"),
				FString::Printf(TEXT("Requested platform '%s' is not among this editor's target platforms."), *Platform),
				TEXT("Call list_target_platforms to see valid names, or install the platform's SDK/support files."));
		}
	}

	Root->SetArrayField(TEXT("issues"), Issues);
	const bool bBlocking = Issues.ContainsByPredicate([](const TSharedPtr<FJsonValue>& V)
	{
		const TSharedPtr<FJsonObject>& O = V->AsObject();
		return O.IsValid() && O->GetStringField(TEXT("severity")) == TEXT("error");
	});
	Root->SetBoolField(TEXT("ready_to_package"), !bBlocking);

	OutJsonString = SerializeObject(Root);
}

// ============================================================
// package_project — launch UAT BuildCookRun as a background job
// ============================================================
void HandlePackageProjectFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString ProjectFile = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	if (ProjectFile.IsEmpty())
	{
		OutError = TEXT("Could not resolve the project file path.");
		return;
	}

	const FString Uat = RunUatPath();
	if (!IFileManager::Get().FileExists(*Uat))
	{
		OutError = FString::Printf(TEXT("RunUAT not found at '%s'. Cannot package."), *Uat);
		return;
	}

	const UProjectPackagingSettings* Settings = GetDefault<UProjectPackagingSettings>();

	FString Platform = TEXT("Win64");
	FString Configuration = Settings ? BuildConfigToString(Settings->BuildConfiguration) : TEXT("Development");
	FString ArchiveDir;
	FString Maps;
	FString ExtraArgs;
	bool bNoPak = false;
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("platform"), Platform);
		Args->TryGetStringField(TEXT("configuration"), Configuration);
		Args->TryGetStringField(TEXT("archive_directory"), ArchiveDir);
		Args->TryGetStringField(TEXT("maps"), Maps);
		Args->TryGetStringField(TEXT("extra_args"), ExtraArgs);
		Args->TryGetBoolField(TEXT("no_pak"), bNoPak);
	}
	if (ArchiveDir.IsEmpty())
	{
		ArchiveDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("StagedBuilds"));
	}

	// Compose the BuildCookRun argument string.
	FString UatArgs = FString::Printf(
		TEXT("BuildCookRun -project=\"%s\" -noP4 -nocompileeditor -utf8output -platform=%s -clientconfig=%s -cook -build -stage -archive -archivedirectory=\"%s\""),
		*ProjectFile, *Platform, *Configuration, *ArchiveDir);
	if (!bNoPak) UatArgs += TEXT(" -pak");
	if (!Maps.IsEmpty()) UatArgs += FString::Printf(TEXT(" -map=%s"), *Maps);
	if (!ExtraArgs.IsEmpty()) UatArgs += TEXT(" ") + ExtraArgs;

	// Log file for this job.
	const FString JobId = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12);
	const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	const FString JobLogPath = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("Logs") / FString::Printf(TEXT("UECP_Packaging_%s.log"), *Stamp));

	// Platform-specific launcher: on Windows a .bat must go through cmd.exe.
	FString Executable;
	FString FullParams;
#if PLATFORM_WINDOWS
	Executable = TEXT("cmd.exe");
	FullParams = FString::Printf(TEXT("/c \"\"%s\" %s\""), *Uat, *UatArgs);
#else
	Executable = TEXT("/bin/sh");
	FullParams = FString::Printf(TEXT("\"%s\" %s"), *Uat, *UatArgs);
#endif

	const FString FullCommand = Executable + TEXT(" ") + FullParams;

	// Write a header line so the log exists immediately.
	FFileHelper::SaveStringToFile(
		FString::Printf(TEXT("[UECP] Packaging started %s\n[UECP] %s\n\n"), *Stamp, *FullCommand),
		*JobLogPath, FFileHelper::EEncodingOptions::ForceUTF8);

	void* ReadPipe = nullptr;
	void* WritePipe = nullptr;
	if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
	{
		OutError = TEXT("Failed to create an output pipe for the packaging process.");
		return;
	}

	uint32 Pid = 0;
	FProcHandle Handle = FPlatformProcess::CreateProc(
		*Executable, *FullParams,
		/*bLaunchDetached*/ false, /*bLaunchHidden*/ true, /*bLaunchReallyHidden*/ true,
		&Pid, /*PriorityModifier*/ 0, /*WorkingDir*/ nullptr, WritePipe);

	if (!Handle.IsValid())
	{
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
		OutError = TEXT("Failed to launch UnrealAutomationTool. Check that the engine path is valid.");
		return;
	}

	TSharedPtr<FPackagingJob> Job = MakeShared<FPackagingJob>();
	Job->JobId         = JobId;
	Job->LogPath       = JobLogPath;
	Job->Command       = FullCommand;
	Job->Platform      = Platform;
	Job->Configuration = Configuration;
	Job->StartTime     = FDateTime::Now();
	Job->Pid           = Pid;

	{
		FScopeLock Lock(&JobsLock());
		Jobs().Add(JobId, Job);
		LastJobId() = JobId;
	}

	// Drain the child's output into the log until it exits, off the game thread.
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Job, Handle, ReadPipe, WritePipe]() mutable
	{
		while (FPlatformProcess::IsProcRunning(Handle))
		{
			const FString Chunk = FPlatformProcess::ReadPipe(ReadPipe);
			if (!Chunk.IsEmpty())
			{
				FFileHelper::SaveStringToFile(Chunk, *Job->LogPath,
					FFileHelper::EEncodingOptions::ForceUTF8, &IFileManager::Get(), FILEWRITE_Append);
			}
			else
			{
				FPlatformProcess::Sleep(0.1f);
			}
		}

		const FString Rest = FPlatformProcess::ReadPipe(ReadPipe);
		if (!Rest.IsEmpty())
		{
			FFileHelper::SaveStringToFile(Rest, *Job->LogPath,
				FFileHelper::EEncodingOptions::ForceUTF8, &IFileManager::Get(), FILEWRITE_Append);
		}

		int32 Code = 0;
		FPlatformProcess::GetProcReturnCode(Handle, &Code);
		Job->ExitCode.Store(Code);
		Job->bFinished.Store(true);

		FPlatformProcess::CloseProc(Handle);
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
	});

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("job_id"), JobId);
	Root->SetNumberField(TEXT("pid"), (double)Pid);
	Root->SetStringField(TEXT("log_path"), JobLogPath);
	Root->SetStringField(TEXT("platform"), Platform);
	Root->SetStringField(TEXT("configuration"), Configuration);
	Root->SetStringField(TEXT("command"), FullCommand);
	Root->SetStringField(TEXT("message"),
		TEXT("Packaging started in the background. Poll get_packaging_status with this job_id; it will run for several minutes."));

	OutJsonString = SerializeObject(Root);
}

// ============================================================
// get_packaging_status
// ============================================================
void HandleGetPackagingStatusFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString JobId;
	int32 LogLines = 40;
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("job_id"), JobId);
		double N = 0;
		if (Args->TryGetNumberField(TEXT("log_lines"), N)) LogLines = FMath::Clamp((int32)N, 1, 500);
	}

	TSharedPtr<FPackagingJob> Job;
	{
		FScopeLock Lock(&JobsLock());
		if (JobId.IsEmpty()) JobId = LastJobId();
		if (const TSharedPtr<FPackagingJob>* Found = Jobs().Find(JobId)) Job = *Found;
	}

	if (!Job.IsValid())
	{
		OutError = JobId.IsEmpty()
			? TEXT("No packaging job has been started this session.")
			: FString::Printf(TEXT("No packaging job with id '%s'."), *JobId);
		return;
	}

	const bool bFinished = Job->bFinished.Load();
	const int32 ExitCode = Job->ExitCode.Load();

	FString State;
	if (!bFinished)        State = TEXT("running");
	else if (ExitCode == 0) State = TEXT("succeeded");
	else                   State = TEXT("failed");

	// Read the log tail + count error/warning markers.
	FString LogContent;
	FFileHelper::LoadFileToString(LogContent, *Job->LogPath);

	int32 ErrorCount = 0;
	int32 WarningCount = 0;
	FString Tail;
	if (!LogContent.IsEmpty())
	{
		TArray<FString> AllLines;
		LogContent.ParseIntoArrayLines(AllLines, /*bCullEmpty*/ false);
		for (const FString& Line : AllLines)
		{
			if (Line.Contains(TEXT("Error:")) || Line.Contains(TEXT("error:")) || Line.Contains(TEXT("BUILD FAILED"))) ++ErrorCount;
			if (Line.Contains(TEXT("Warning:")) || Line.Contains(TEXT("warning:"))) ++WarningCount;
		}
		const int32 Start = FMath::Max(0, AllLines.Num() - LogLines);
		TArray<FString> TailLines;
		for (int32 i = Start; i < AllLines.Num(); ++i) TailLines.Add(AllLines[i]);
		Tail = FString::Join(TailLines, TEXT("\n"));
	}

	const FTimespan Elapsed = FDateTime::Now() - Job->StartTime;

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("job_id"), Job->JobId);
	Root->SetStringField(TEXT("state"), State);
	Root->SetBoolField(TEXT("finished"), bFinished);
	if (bFinished) Root->SetNumberField(TEXT("exit_code"), ExitCode);
	Root->SetStringField(TEXT("platform"), Job->Platform);
	Root->SetStringField(TEXT("configuration"), Job->Configuration);
	Root->SetNumberField(TEXT("elapsed_seconds"), Elapsed.GetTotalSeconds());
	Root->SetNumberField(TEXT("error_count"), ErrorCount);
	Root->SetNumberField(TEXT("warning_count"), WarningCount);
	Root->SetStringField(TEXT("log_path"), Job->LogPath);
	Root->SetStringField(TEXT("log_tail"), Tail);

	OutJsonString = SerializeObject(Root);
}

}
