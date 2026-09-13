// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/CppCompileTools.h"
#include "UECPCppExtModule.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Async/TaskGraphInterfaces.h"
#include "Async/Async.h"

#if PLATFORM_WINDOWS
#include "ILiveCodingModule.h"
#endif

namespace
{
	void WriteJsonErrorPair(const FString& ErrorMessage, FString& OutJsonString, FString& OutError)
	{
		OutError = ErrorMessage;
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), ErrorMessage);
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
	}
}

namespace CppCompileTools
{

void HandleCheckProjectSourceFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJsonString, FString& OutError)
{
	const FString ProjectDir = FPaths::ProjectDir();
	const FString SourceDir  = FPaths::Combine(*ProjectDir, TEXT("Source"));

	const bool bHasSource = FPaths::DirectoryExists(*SourceDir) && IFileManager::Get().DirectoryExists(*SourceDir);

	TArray<FString> UProjectFiles;
	IFileManager::Get().FindFiles(UProjectFiles, *FPaths::Combine(*ProjectDir, TEXT("*.uproject")), true, false);
	const bool bHasProjectFile = UProjectFiles.Num() > 0;

	bool bHasSourceControl = false;
	if (FPaths::DirectoryExists(*FPaths::Combine(*ProjectDir, TEXT("Config"))))
	{
		bHasSourceControl =
			FPaths::DirectoryExists(*FPaths::Combine(*ProjectDir, TEXT(".git"))) ||
			FPaths::DirectoryExists(*FPaths::Combine(*ProjectDir, TEXT(".svn"))) ||
			FPaths::DirectoryExists(*FPaths::Combine(*ProjectDir, TEXT(".p4")));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField  (TEXT("success"),            true);
	Result->SetBoolField  (TEXT("has_source_code"),    bHasSource);
	Result->SetBoolField  (TEXT("has_project_file"),   bHasProjectFile);
	Result->SetBoolField  (TEXT("has_source_control"), bHasSourceControl);
	Result->SetStringField(TEXT("project_directory"),  ProjectDir);
	Result->SetStringField(TEXT("source_directory"),   SourceDir);

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

static void DoUbtCompile(FString& OutJsonString, FString& OutError);

#if PLATFORM_WINDOWS
static bool TryLiveCodingCompile(bool bForce, FString& OutJsonString, FString& OutError);
#endif

void HandleCompileProjectFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString Mode = TEXT("auto");
	if (Args.IsValid()) Args->TryGetStringField(TEXT("mode"), Mode);
	Mode = Mode.ToLower().TrimStartAndEnd();

	const bool bWantsLive = (Mode == TEXT("live"));
	const bool bWantsFull = (Mode == TEXT("full"));

#if PLATFORM_WINDOWS
	if (!bWantsFull)
	{
		const bool bEditorRunning = GIsEditor && !IsRunningCommandlet() && !IsRunningGame();
		if (bEditorRunning || bWantsLive)
		{
			if (TryLiveCodingCompile(bWantsLive, OutJsonString, OutError)) return;
		}
	}
#else
	if (bWantsLive)
	{
		WriteJsonErrorPair(TEXT("Live Coding is Windows-only. Use mode=\"full\" or mode=\"auto\" on this platform."), OutJsonString, OutError);
		return;
	}
#endif

	DoUbtCompile(OutJsonString, OutError);
}

static void DoUbtCompile(FString& OutJsonString, FString& OutError)
{
	const FString UProjectPath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	const FString ProjectName  = FPaths::GetBaseFilename(UProjectPath);
	const FString TargetName   = ProjectName + TEXT("Editor");

	const FString EngineDir = FPaths::ConvertRelativePathToFull(FPaths::EngineDir());
#if PLATFORM_WINDOWS
	const FString BuildScript = FPaths::Combine(EngineDir, TEXT("Build/BatchFiles/Build.bat"));
#elif PLATFORM_MAC
	const FString BuildScript = FPaths::Combine(EngineDir, TEXT("Build/BatchFiles/Mac/Build.sh"));
#else
	const FString BuildScript = FPaths::Combine(EngineDir, TEXT("Build/BatchFiles/Linux/Build.sh"));
#endif

	if (!FPaths::FileExists(BuildScript))
	{
		WriteJsonErrorPair(FString::Printf(TEXT("Build script not found at: %s"), *BuildScript), OutJsonString, OutError);
		return;
	}

#if PLATFORM_WINDOWS
	const TCHAR* TargetPlatform = TEXT("Win64");
#elif PLATFORM_MAC
	const TCHAR* TargetPlatform = TEXT("Mac");
#else
	const TCHAR* TargetPlatform = TEXT("Linux");
#endif

#if PLATFORM_WINDOWS
	const FString BuildArgs = FString::Printf(
		TEXT("%s %s Development \"%s\" -waitmutex -FromMsBuild"),
		*TargetName, TargetPlatform, *UProjectPath);
#else
	const FString BuildArgs = FString::Printf(
		TEXT("%s %s Development \"%s\" -waitmutex"),
		*TargetName, TargetPlatform, *UProjectPath);
#endif

	UE_LOG(LogUECPCppExt, Log, TEXT("CompileProject: starting build - %s %s"), *BuildScript, *BuildArgs);

	int32 ReturnCode = -1;
	FString StdOut, StdErr;

#if PLATFORM_WINDOWS
	const FString CmdArgs = FString::Printf(TEXT("/c \"\"%s\" %s\""), *BuildScript, *BuildArgs);
	const bool bLaunched = FPlatformProcess::ExecProcess(TEXT("cmd.exe"), *CmdArgs, &ReturnCode, &StdOut, &StdErr);
#else
	const bool bLaunched = FPlatformProcess::ExecProcess(*BuildScript, *BuildArgs, &ReturnCode, &StdOut, &StdErr);
#endif

	if (!bLaunched)
	{
		WriteJsonErrorPair(TEXT("Failed to launch build process"), OutJsonString, OutError);
		return;
	}

	TArray<FString> ErrorLines;
	TArray<FString> WarningLines;
	TArray<FString> OutputLines;
	StdOut.ParseIntoArrayLines(OutputLines);

	for (const FString& Line : OutputLines)
	{
		if (Line.Contains(TEXT(": error ")) || Line.Contains(TEXT(": fatal error")))
		{
			ErrorLines.Add(Line.TrimStartAndEnd());
		}
		else if (Line.Contains(TEXT(": warning ")) && !Line.Contains(TEXT("warning treated as error")))
		{
			WarningLines.Add(Line.TrimStartAndEnd());
		}
	}

	if (!StdErr.IsEmpty())
	{
		TArray<FString> ErrLines;
		StdErr.ParseIntoArrayLines(ErrLines);
		for (const FString& Line : ErrLines)
		{
			if (Line.Contains(TEXT("error")) || Line.Contains(TEXT("fatal")))
			{
				ErrorLines.Add(Line.TrimStartAndEnd());
			}
		}
	}

	const bool bBuildSuccess = (ReturnCode == 0);

	UE_LOG(LogUECPCppExt, Log, TEXT("CompileProject: build %s (return code %d, %d errors, %d warnings)"),
		bBuildSuccess ? TEXT("SUCCEEDED") : TEXT("FAILED"), ReturnCode, ErrorLines.Num(), WarningLines.Num());

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField  (TEXT("success"),      bBuildSuccess);
	Result->SetStringField(TEXT("compile_path"), TEXT("ubt"));
	Result->SetNumberField(TEXT("return_code"),  ReturnCode);
	Result->SetStringField(TEXT("target"),       TargetName);

	if (bBuildSuccess)
	{
		Result->SetStringField(TEXT("message"), FString::Printf(
			TEXT("Build succeeded for %s. New C++ classes are now available in the editor."), *TargetName));
	}
	else
	{
		FString ReturnCodeHint;
		if      (ReturnCode == 6) ReturnCodeHint = TEXT(" Return code 6 = editor is locking the DLLs. Close the editor, build from IDE/command line, then reopen.");
		else if (ReturnCode == 2) ReturnCodeHint = TEXT(" Return code 2 = build tool error (missing SDK or toolchain).");
		else if (ReturnCode == 1) ReturnCodeHint = TEXT(" Return code 1 = compilation errors (see error list).");

		if (ErrorLines.Num() > 0)
		{
			Result->SetStringField(TEXT("message"), FString::Printf(
				TEXT("Build failed with %d compilation error(s).%s"), ErrorLines.Num(), *ReturnCodeHint));
		}
		else
		{
			Result->SetStringField(TEXT("message"), FString::Printf(
				TEXT("Build failed (return code %d, no compilation errors detected).%s The code may be correct but the build environment has an issue."),
				ReturnCode, *ReturnCodeHint));
		}

		TArray<TSharedPtr<FJsonValue>> ErrArr;
		for (int32 i = 0; i < FMath::Min(ErrorLines.Num(), 20); i++)
		{
			ErrArr.Add(MakeShared<FJsonValueString>(ErrorLines[i]));
		}
		Result->SetArrayField(TEXT("errors"), ErrArr);

		OutError = ErrorLines.Num() > 0
			? FString::Printf(TEXT("Build failed with %d error(s)"), ErrorLines.Num())
			: FString::Printf(TEXT("Build failed (return code %d)"), ReturnCode);

		if (OutputLines.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> TailArr;
			const int32 Start = FMath::Max(0, OutputLines.Num() - 10);
			for (int32 i = Start; i < OutputLines.Num(); i++)
			{
				const FString Line = OutputLines[i].TrimStartAndEnd();
				if (!Line.IsEmpty()) TailArr.Add(MakeShared<FJsonValueString>(Line));
			}
			Result->SetArrayField(TEXT("build_output_tail"), TailArr);
		}
	}

	if (WarningLines.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> WarnArr;
		for (int32 i = 0; i < FMath::Min(WarningLines.Num(), 10); i++)
		{
			WarnArr.Add(MakeShared<FJsonValueString>(WarningLines[i]));
		}
		Result->SetArrayField(TEXT("warnings"), WarnArr);
	}

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

#if PLATFORM_WINDOWS

static ILiveCodingModule* GetLiveCodingModule()
{
	return FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
}

static const TCHAR* LcResultToString(ELiveCodingCompileResult R)
{
	switch (R)
	{
		case ELiveCodingCompileResult::Success:            return TEXT("Success");
		case ELiveCodingCompileResult::NoChanges:          return TEXT("NoChanges");
		case ELiveCodingCompileResult::InProgress:         return TEXT("InProgress");
		case ELiveCodingCompileResult::CompileStillActive: return TEXT("CompileStillActive");
		case ELiveCodingCompileResult::NotStarted:         return TEXT("NotStarted");
		case ELiveCodingCompileResult::Failure:            return TEXT("Failure");
		case ELiveCodingCompileResult::Cancelled:          return TEXT("Cancelled");
		default:                                            return TEXT("Unknown");
	}
}

static bool TryLiveCodingCompile(bool bForce, FString& OutJsonString, FString& OutError)
{
	ILiveCodingModule* LC = GetLiveCodingModule();
	if (!LC)
	{
		if (bForce)
		{
			WriteJsonErrorPair(TEXT("Live Coding module is not loaded. Either enable it in Project Settings -> Live Coding, or use mode=\"full\" to fall back to UBT."), OutJsonString, OutError);
			return true;
		}
		return false;
	}

	if (!LC->IsEnabledForSession())
	{
		if (bForce)
		{
			const FText& ErrText = LC->GetEnableErrorText();
			const FString Hint = ErrText.IsEmpty()
				? FString(TEXT("Enable Live Coding via Edit -> Editor Preferences -> General/Live Coding, or via the LC console toggle (Ctrl+Alt+F11)."))
				: ErrText.ToString();
			WriteJsonErrorPair(FString::Printf(TEXT("Live Coding is not enabled for this session. %s"), *Hint), OutJsonString, OutError);
			return true;
		}
		return false;
	}

	if (LC->IsCompiling())
	{
		WriteJsonErrorPair(TEXT("A Live Coding compile is already in progress. Wait for it to finish (watch the LC console window) and retry."), OutJsonString, OutError);
		return true;
	}

	UE_LOG(LogUECPCppExt, Log, TEXT("compile_project: routing through Live Coding"));

	ELiveCodingCompileResult Result = ELiveCodingCompileResult::NotStarted;
	bool bStarted = false;

	if (IsInGameThread())
	{
		bStarted = LC->Compile(ELiveCodingCompileFlags::WaitForCompletion, &Result);
	}
	else
	{
		FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady(
			[LC, &bStarted, &Result]()
			{
				bStarted = LC->Compile(ELiveCodingCompileFlags::WaitForCompletion, &Result);
			},
			TStatId(), nullptr, ENamedThreads::GameThread);
		Task->Wait();
	}

	UE_LOG(LogUECPCppExt, Log, TEXT("compile_project: Live Coding result = %s (started=%d)"),
		LcResultToString(Result), bStarted ? 1 : 0);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetStringField(TEXT("compile_path"), TEXT("live_coding"));
	R->SetStringField(TEXT("lc_result"),    LcResultToString(Result));

	switch (Result)
	{
		case ELiveCodingCompileResult::Success:
			R->SetBoolField  (TEXT("success"),         true);
			R->SetStringField(TEXT("message"),
				TEXT("Live Coding patched the running editor. New / changed C++ types are now available — you can use them in Blueprint immediately."));
			break;

		case ELiveCodingCompileResult::NoChanges:
			R->SetBoolField  (TEXT("success"),         true);
			R->SetStringField(TEXT("message"),
				TEXT("No source changes detected since the last compile. Editor is already up to date."));
			break;

		case ELiveCodingCompileResult::Failure:
		{
			R->SetBoolField  (TEXT("success"),          false);
			R->SetBoolField  (TEXT("requires_restart"), true);
			R->SetStringField(TEXT("message"),
				TEXT("Live Coding could not patch the editor. Two common causes: (1) compile errors in the C++ source, or "
				     "(2) the change requires a full reload (e.g., a new UPROPERTY changing class layout, a parent class "
				     "swap, struct member additions). Open the Live Coding console (Window -> Developer Tools -> Live "
				     "Coding) for details. To proceed: fix any compile errors, then either retry, or close the editor + "
				     "call compile_project mode=\"full\" + reopen."));
			OutError = TEXT("Live Coding compile failed - see message for next steps");
			break;
		}

		case ELiveCodingCompileResult::Cancelled:
			R->SetBoolField  (TEXT("success"), false);
			R->SetStringField(TEXT("message"), TEXT("Live Coding compile was cancelled. Retry when ready."));
			OutError = TEXT("Live Coding compile cancelled");
			break;

		case ELiveCodingCompileResult::CompileStillActive:
			R->SetBoolField  (TEXT("success"), false);
			R->SetStringField(TEXT("message"), TEXT("A previous Live Coding compile is still active. Wait for it to finish and retry."));
			OutError = TEXT("Live Coding busy");
			break;

		case ELiveCodingCompileResult::NotStarted:
		case ELiveCodingCompileResult::InProgress:
		default:
			R->SetBoolField  (TEXT("success"),         false);
			R->SetBoolField  (TEXT("requires_restart"), true);
			R->SetStringField(TEXT("message"), FString::Printf(
				TEXT("Live Coding returned %s; the patch was not applied. Try mode=\"full\" to do a full UBT relink "
				     "(close the editor first - the locked DLL would otherwise return code 6)."),
				LcResultToString(Result)));
			OutError = FString::Printf(TEXT("Live Coding result: %s"), LcResultToString(Result));
			break;
	}

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(R.ToSharedRef(), Writer);
	return true;
}

#endif

}
