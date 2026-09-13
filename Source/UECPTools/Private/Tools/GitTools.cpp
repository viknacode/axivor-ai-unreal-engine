// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/GitTools.h"
#include "MCPToolsLog.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Char.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>

static bool RunProcessHidden(const FString& Exe, const FString& Args, const FString& WorkDir,
    FString& OutStdOut, FString& OutStdErr, int32& OutRetCode)
{
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;

    HANDLE hOutR = nullptr, hOutW = nullptr, hErrR = nullptr, hErrW = nullptr;
    if (!::CreatePipe(&hOutR, &hOutW, &sa, 0)) return false;
    ::SetHandleInformation(hOutR, HANDLE_FLAG_INHERIT, 0);
    if (!::CreatePipe(&hErrR, &hErrW, &sa, 0))
    {
        ::CloseHandle(hOutR); ::CloseHandle(hOutW);
        return false;
    }
    ::SetHandleInformation(hErrR, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput  = ::GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = hOutW;
    si.hStdError  = hErrW;

    FString CmdLine = FString::Printf(TEXT("\"%s\" %s"), *Exe, *Args);

    PROCESS_INFORMATION pi = {};
    BOOL bCreated = ::CreateProcessW(
        nullptr, CmdLine.GetCharArray().GetData(),
        nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | NORMAL_PRIORITY_CLASS,
        nullptr,
        WorkDir.IsEmpty() ? nullptr : *WorkDir,
        &si, &pi);

    ::CloseHandle(hOutW);
    ::CloseHandle(hErrW);

    if (!bCreated)
    {
        ::CloseHandle(hOutR); ::CloseHandle(hErrR);
        OutRetCode = -1;
        return false;
    }

    TArray<uint8> OutBuf, ErrBuf;
    for (;;)
    {
        bool bAny = false;
        DWORD Avail = 0;
        if (::PeekNamedPipe(hOutR, nullptr, 0, nullptr, &Avail, nullptr) && Avail > 0)
        {
            char Tmp[4096]; DWORD Read = 0;
            if (::ReadFile(hOutR, Tmp, FMath::Min(Avail, (DWORD)sizeof(Tmp)), &Read, nullptr) && Read > 0)
                { OutBuf.Append((uint8*)Tmp, Read); bAny = true; }
        }
        if (::PeekNamedPipe(hErrR, nullptr, 0, nullptr, &Avail, nullptr) && Avail > 0)
        {
            char Tmp[4096]; DWORD Read = 0;
            if (::ReadFile(hErrR, Tmp, FMath::Min(Avail, (DWORD)sizeof(Tmp)), &Read, nullptr) && Read > 0)
                { ErrBuf.Append((uint8*)Tmp, Read); bAny = true; }
        }
        if (!bAny)
        {
            if (::WaitForSingleObject(pi.hProcess, 10) == WAIT_OBJECT_0) break;
        }
    }
    { char Tmp[4096]; DWORD Read;
      while (::ReadFile(hOutR, Tmp, sizeof(Tmp), &Read, nullptr) && Read > 0) OutBuf.Append((uint8*)Tmp, Read); }
    { char Tmp[4096]; DWORD Read;
      while (::ReadFile(hErrR, Tmp, sizeof(Tmp), &Read, nullptr) && Read > 0) ErrBuf.Append((uint8*)Tmp, Read); }

    DWORD ExitCode = 0;
    ::GetExitCodeProcess(pi.hProcess, &ExitCode);
    OutRetCode = (int32)ExitCode;

    OutBuf.Add(0); ErrBuf.Add(0);
    OutStdOut = UTF8_TO_TCHAR((const ANSICHAR*)OutBuf.GetData());
    OutStdErr  = UTF8_TO_TCHAR((const ANSICHAR*)ErrBuf.GetData());

    ::CloseHandle(pi.hProcess); ::CloseHandle(pi.hThread);
    ::CloseHandle(hOutR); ::CloseHandle(hErrR);
    return true;
}
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace GitTools
{

static FString CachedGitPath;

FString FindGitBinary()
{
	if (!CachedGitPath.IsEmpty()) return CachedGitPath;

#if PLATFORM_WINDOWS
	FString PathEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("PATH"));
	TArray<FString> PathDirs;
	PathEnv.ParseIntoArray(PathDirs, TEXT(";"), true);
	for (const FString& Dir : PathDirs)
	{
		FString Candidate = FPaths::Combine(Dir.TrimStartAndEnd(), TEXT("git.exe"));
		if (FPaths::FileExists(Candidate))
		{
			CachedGitPath = Candidate;
			return CachedGitPath;
		}
	}

	TArray<FString> CommonPaths = {
		TEXT("C:\\Program Files\\Git\\bin\\git.exe"),
		TEXT("C:\\Program Files (x86)\\Git\\bin\\git.exe"),
	};
	CommonPaths.Add(FString::Printf(TEXT("C:\\Users\\%s\\AppData\\Local\\Programs\\Git\\bin\\git.exe"), FPlatformProcess::UserName()));
	for (const FString& Path : CommonPaths)
	{
		if (FPaths::FileExists(Path))
		{
			CachedGitPath = Path;
			return CachedGitPath;
		}
	}
#elif PLATFORM_MAC || PLATFORM_LINUX
	FString WhichOut, WhichErr;
	int32 WhichRet;
	FPlatformProcess::ExecProcess(TEXT("/usr/bin/which"), TEXT("git"), &WhichRet, &WhichOut, &WhichErr);
	if (WhichRet == 0 && !WhichOut.IsEmpty())
	{
		CachedGitPath = WhichOut.TrimStartAndEnd();
		return CachedGitPath;
	}
	TArray<FString> CommonPaths = { TEXT("/usr/bin/git"), TEXT("/usr/local/bin/git"), TEXT("/opt/homebrew/bin/git") };
	for (const FString& Path : CommonPaths)
	{
		if (FPaths::FileExists(Path))
		{
			CachedGitPath = Path;
			return CachedGitPath;
		}
	}
#endif

	return FString();
}

bool IsGitRepository(const FString& ProjectDir)
{
	FString GitDir = FPaths::Combine(ProjectDir, TEXT(".git"));
	return FPaths::DirectoryExists(GitDir);
}

bool RunGitCommand(const FString& Args, const FString& WorkingDir,
	FString& OutStdOut, FString& OutStdErr, int32& OutReturnCode)
{
	static const bool bGitPromptsDisabled = []()
	{
		FPlatformMisc::SetEnvironmentVar(TEXT("GIT_TERMINAL_PROMPT"), TEXT("0"));
		FPlatformMisc::SetEnvironmentVar(TEXT("GCM_INTERACTIVE"), TEXT("Never"));
		return true;
	}();
	(void)bGitPromptsDisabled;

	FString GitBin = FindGitBinary();
	if (GitBin.IsEmpty())
	{
		OutStdErr = TEXT("Git binary not found. Install Git from https://git-scm.com/download");
		OutReturnCode = -1;
		return false;
	}

	FString FullArgs = FString::Printf(TEXT("-C \"%s\" %s"), *WorkingDir, *Args);

#if PLATFORM_WINDOWS
	RunProcessHidden(GitBin, FullArgs, WorkingDir, OutStdOut, OutStdErr, OutReturnCode);
#else
	FPlatformProcess::ExecProcess(*GitBin, *FullArgs, &OutReturnCode, &OutStdOut, &OutStdErr);
#endif

	return OutReturnCode == 0;
}

void HandleGitStatus(const FString& Path, FString& OutJson, FString& OutError)
{
	FString WorkDir = Path.IsEmpty() ? FPaths::ProjectDir() : Path;

	if (!IsGitRepository(WorkDir))
	{
		TSharedPtr<FJsonObject> R = MakeShareable(new FJsonObject);
		R->SetBoolField(TEXT("success"), true);
		R->SetBoolField(TEXT("is_repo"), false);
		R->SetStringField(TEXT("message"), TEXT("No git repository found. Use git_init to create one."));
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJson);
		FJsonSerializer::Serialize(R.ToSharedRef(), W);
		return;
	}

	FString BranchOut, BranchErr, StatusOut, StatusErr;
	int32 BranchRet, StatusRet;
	RunGitCommand(TEXT("rev-parse --abbrev-ref HEAD"), WorkDir, BranchOut, BranchErr, BranchRet);
	if (BranchRet != 0)
	{
		RunGitCommand(TEXT("symbolic-ref --short HEAD"), WorkDir, BranchOut, BranchErr, BranchRet);
		if (BranchRet != 0) BranchOut = TEXT("(no commits)");
	}
	RunGitCommand(TEXT("status --porcelain"), WorkDir, StatusOut, StatusErr, StatusRet);

	FString RemoteOut, RemoteErr;
	int32 RemoteRet;
	RunGitCommand(TEXT("remote get-url origin"), WorkDir, RemoteOut, RemoteErr, RemoteRet);

	TArray<FString> StatusLines;
	StatusOut.ParseIntoArrayLines(StatusLines);

	int32 Modified = 0, Added = 0, Deleted = 0, Untracked = 0;
	TArray<TSharedPtr<FJsonValue>> FileArr;
	for (const FString& Line : StatusLines)
	{
		if (Line.Len() < 3) continue;
		FString Code = Line.Left(2);
		FString File = Line.Mid(3).TrimStartAndEnd();

		TSharedPtr<FJsonObject> FileObj = MakeShareable(new FJsonObject);
		FileObj->SetStringField(TEXT("file"), File);
		FileObj->SetStringField(TEXT("status"), Code.TrimStartAndEnd());
		FileArr.Add(MakeShareable(new FJsonValueObject(FileObj)));

		if (Code.Contains(TEXT("M"))) Modified++;
		else if (Code.Contains(TEXT("A"))) Added++;
		else if (Code.Contains(TEXT("D"))) Deleted++;
		else if (Code.Contains(TEXT("?"))) Untracked++;
	}

	TSharedPtr<FJsonObject> R = MakeShareable(new FJsonObject);
	R->SetBoolField(TEXT("success"), true);
	R->SetBoolField(TEXT("is_repo"), true);
	R->SetStringField(TEXT("branch"), BranchOut.TrimStartAndEnd());
	R->SetStringField(TEXT("remote"), RemoteOut.TrimStartAndEnd());
	R->SetNumberField(TEXT("modified"), Modified);
	R->SetNumberField(TEXT("added"), Added);
	R->SetNumberField(TEXT("deleted"), Deleted);
	R->SetNumberField(TEXT("untracked"), Untracked);
	R->SetArrayField(TEXT("files"), FileArr);

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(R.ToSharedRef(), W);
}

void HandleGitLog(const FString& FilePath, int32 MaxCount, FString& OutJson, FString& OutError)
{
	FString WorkDir = FPaths::ProjectDir();
	if (MaxCount <= 0) MaxCount = 10;
	if (MaxCount > 50) MaxCount = 50;

	FString FormatStr = TEXT("log --format=%H|%an|%ai|%s -n ");
	FormatStr += FString::FromInt(MaxCount);
	FString Args = FormatStr;
	if (!FilePath.IsEmpty())
	{
		Args += FString::Printf(TEXT(" -- \"%s\""), *FilePath);
	}

	FString StdOut, StdErr;
	int32 RetCode;
	RunGitCommand(Args, WorkDir, StdOut, StdErr, RetCode);

	TArray<TSharedPtr<FJsonValue>> Commits;
	TArray<FString> Lines;
	StdOut.ParseIntoArrayLines(Lines);

	for (const FString& Line : Lines)
	{
		TArray<FString> Parts;
		Line.ParseIntoArray(Parts, TEXT("|"), false);
		if (Parts.Num() >= 4)
		{
			TSharedPtr<FJsonObject> C = MakeShareable(new FJsonObject);
			C->SetStringField(TEXT("hash"), Parts[0]);
			C->SetStringField(TEXT("author"), Parts[1]);
			C->SetStringField(TEXT("date"), Parts[2]);
			C->SetStringField(TEXT("message"), Parts[3]);
			Commits.Add(MakeShareable(new FJsonValueObject(C)));
		}
	}

	TSharedPtr<FJsonObject> R = MakeShareable(new FJsonObject);
	R->SetBoolField(TEXT("success"), true);
	R->SetNumberField(TEXT("count"), Commits.Num());
	R->SetArrayField(TEXT("commits"), Commits);

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(R.ToSharedRef(), W);
}

void HandleGitCommit(const FString& Message, const TArray<FString>& Files, bool bStageAll, FString& OutJson, FString& OutError)
{
	FString WorkDir = FPaths::ProjectDir();

	if (Message.IsEmpty())
	{
		OutError = TEXT("Commit message cannot be empty");
		return;
	}

	FString StdOut, StdErr;
	int32 RetCode;

	if (bStageAll)
	{
		RunGitCommand(TEXT("add -A"), WorkDir, StdOut, StdErr, RetCode);
	}
	else if (Files.Num() > 0)
	{
		for (const FString& File : Files)
		{
			RunGitCommand(FString::Printf(TEXT("add \"%s\""), *File), WorkDir, StdOut, StdErr, RetCode);
		}
	}
	else
	{
		RunGitCommand(TEXT("add -u"), WorkDir, StdOut, StdErr, RetCode);
	}

	FString CommitArgs = FString::Printf(TEXT("commit -m \"%s\""), *Message.Replace(TEXT("\""), TEXT("\\\"")).Replace(TEXT("\n"), TEXT(" ")));
	bool bOk = RunGitCommand(CommitArgs, WorkDir, StdOut, StdErr, RetCode);

	TSharedPtr<FJsonObject> R = MakeShareable(new FJsonObject);
	R->SetBoolField(TEXT("success"), bOk);
	R->SetStringField(TEXT("message"), bOk ? TEXT("Committed successfully") : StdErr.TrimStartAndEnd());
	R->SetStringField(TEXT("output"), StdOut.TrimStartAndEnd());

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(R.ToSharedRef(), W);
}

void HandleGitDiff(const FString& FilePath, const FString& Revision, FString& OutJson, FString& OutError)
{
	FString WorkDir = FPaths::ProjectDir();
	FString Args = TEXT("diff --stat");
	if (!Revision.IsEmpty()) Args += FString::Printf(TEXT(" %s"), *Revision);
	if (!FilePath.IsEmpty()) Args += FString::Printf(TEXT(" -- \"%s\""), *FilePath);

	FString StdOut, StdErr;
	int32 RetCode;
	RunGitCommand(Args, WorkDir, StdOut, StdErr, RetCode);

	TSharedPtr<FJsonObject> R = MakeShareable(new FJsonObject);
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("diff"), StdOut.TrimStartAndEnd());

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(R.ToSharedRef(), W);
}

void HandleGitRevert(const TArray<FString>& Files, FString& OutJson, FString& OutError)
{
	FString WorkDir = FPaths::ProjectDir();
	FString StdOut, StdErr;
	int32 RetCode;
	bool bOk = true;

	if (Files.Num() == 0)
	{
		OutError = TEXT("No files specified to revert");
		return;
	}

	for (const FString& File : Files)
	{
		FString Args = FString::Printf(TEXT("checkout -- \"%s\""), *File);
		if (!RunGitCommand(Args, WorkDir, StdOut, StdErr, RetCode))
		{
			bOk = false;
		}
	}

	TSharedPtr<FJsonObject> R = MakeShareable(new FJsonObject);
	R->SetBoolField(TEXT("success"), bOk);
	R->SetStringField(TEXT("message"), bOk ? FString::Printf(TEXT("Reverted %d file(s)"), Files.Num()) : StdErr.TrimStartAndEnd());

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(R.ToSharedRef(), W);
}

void HandleGitInit(const FString& Path, FString& OutJson, FString& OutError)
{
	FString WorkDir = Path.IsEmpty() ? FPaths::ProjectDir() : Path;

	if (IsGitRepository(WorkDir))
	{
		OutError = TEXT("Git repository already exists");
		return;
	}

	FString StdOut, StdErr;
	int32 RetCode;

	FString GitBin = FindGitBinary();
	if (GitBin.IsEmpty())
	{
		OutError = TEXT("Git not found. Install from https://git-scm.com/download");
		return;
	}

	FString InitArgs = FString::Printf(TEXT("init \"%s\""), *WorkDir);
	FPlatformProcess::ExecProcess(*GitBin, *InitArgs, &RetCode, &StdOut, &StdErr);

	if (RetCode != 0)
	{
		OutError = FString::Printf(TEXT("git init failed: %s"), *StdErr);
		return;
	}

	FString GitIgnore =
		TEXT("# UE5 Generated\n")
		TEXT("Binaries/\n")
		TEXT("DerivedDataCache/\n")
		TEXT("Intermediate/\n")
		TEXT("Saved/\n")
		TEXT(".vs/\n")
		TEXT("*.sln\n")
		TEXT("*.suo\n")
		TEXT("*.opensdf\n")
		TEXT("*.sdf\n")
		TEXT("*.VC.db\n")
		TEXT("*.VC.opendb\n")
		TEXT("\n")
		TEXT("# OS\n")
		TEXT(".DS_Store\n")
		TEXT("Thumbs.db\n");

	FString GitIgnorePath = FPaths::Combine(WorkDir, TEXT(".gitignore"));
	FFileHelper::SaveStringToFile(GitIgnore, *GitIgnorePath);

	TSharedPtr<FJsonObject> R = MakeShareable(new FJsonObject);
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("message"), TEXT("Git repository initialized with .gitignore"));
	R->SetStringField(TEXT("gitignore_path"), GitIgnorePath);
	R->SetStringField(TEXT("gitignore_content"), GitIgnore);

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(R.ToSharedRef(), W);
}

void HandleGitBranch(const FString& SubAction, const FString& BranchName, FString& OutJson, FString& OutError)
{
	FString WorkDir = FPaths::ProjectDir();
	FString StdOut, StdErr;
	int32 RetCode;

	if (SubAction == TEXT("list") || SubAction.IsEmpty())
	{
		RunGitCommand(TEXT("branch -a"), WorkDir, StdOut, StdErr, RetCode);
	}
	else if (SubAction == TEXT("create") && !BranchName.IsEmpty())
	{
		RunGitCommand(FString::Printf(TEXT("branch \"%s\""), *BranchName), WorkDir, StdOut, StdErr, RetCode);
	}
	else if (SubAction == TEXT("switch") && !BranchName.IsEmpty())
	{
		RunGitCommand(FString::Printf(TEXT("checkout \"%s\""), *BranchName), WorkDir, StdOut, StdErr, RetCode);
	}
	else if (SubAction == TEXT("delete") && !BranchName.IsEmpty())
	{
		RunGitCommand(FString::Printf(TEXT("branch -d \"%s\""), *BranchName), WorkDir, StdOut, StdErr, RetCode);
	}
	else
	{
		OutError = TEXT("Invalid branch action. Use: list, create, switch, delete");
		return;
	}

	TSharedPtr<FJsonObject> R = MakeShareable(new FJsonObject);
	R->SetBoolField(TEXT("success"), RetCode == 0);
	R->SetStringField(TEXT("output"), StdOut.TrimStartAndEnd());
	if (RetCode != 0) R->SetStringField(TEXT("error"), StdErr.TrimStartAndEnd());

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(R.ToSharedRef(), W);
}

void HandleGitStash(const FString& SubAction, FString& OutJson, FString& OutError)
{
	FString WorkDir = FPaths::ProjectDir();
	FString StdOut, StdErr;
	int32 RetCode;

	FString Action = SubAction.IsEmpty() ? TEXT("push") : SubAction;
	RunGitCommand(FString::Printf(TEXT("stash %s"), *Action), WorkDir, StdOut, StdErr, RetCode);

	TSharedPtr<FJsonObject> R = MakeShareable(new FJsonObject);
	R->SetBoolField(TEXT("success"), RetCode == 0);
	R->SetStringField(TEXT("output"), StdOut.TrimStartAndEnd());
	if (RetCode != 0) R->SetStringField(TEXT("error"), StdErr.TrimStartAndEnd());

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(R.ToSharedRef(), W);
}

void HandleGitFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	FString Action;
	Args->TryGetStringField(TEXT("action"), Action);

	if (Action == TEXT("git_status"))
	{
		FString Path; Args->TryGetStringField(TEXT("path"), Path);
		HandleGitStatus(Path, OutJson, OutError);
	}
	else if (Action == TEXT("git_log"))
	{
		FString FilePath; Args->TryGetStringField(TEXT("file_path"), FilePath);
		int32 MaxCount = 10; Args->TryGetNumberField(TEXT("max_count"), MaxCount);
		HandleGitLog(FilePath, MaxCount, OutJson, OutError);
	}
	else if (Action == TEXT("git_commit"))
	{
		FString Message; Args->TryGetStringField(TEXT("message"), Message);
		bool bStageAll = false; Args->TryGetBoolField(TEXT("stage_all"), bStageAll);
		TArray<FString> Files;
		const TArray<TSharedPtr<FJsonValue>>* FilesArr;
		if (Args->TryGetArrayField(TEXT("files"), FilesArr))
		{
			for (const auto& V : *FilesArr)
			{
				FString F; if (V->TryGetString(F)) Files.Add(F);
			}
		}
		HandleGitCommit(Message, Files, bStageAll, OutJson, OutError);
	}
	else if (Action == TEXT("git_diff"))
	{
		FString FilePath, Revision;
		Args->TryGetStringField(TEXT("file_path"), FilePath);
		Args->TryGetStringField(TEXT("revision"), Revision);
		HandleGitDiff(FilePath, Revision, OutJson, OutError);
	}
	else if (Action == TEXT("git_revert"))
	{
		TArray<FString> Files;
		const TArray<TSharedPtr<FJsonValue>>* FilesArr;
		if (Args->TryGetArrayField(TEXT("files"), FilesArr))
		{
			for (const auto& V : *FilesArr)
			{
				FString F; if (V->TryGetString(F)) Files.Add(F);
			}
		}
		FString SingleFile; if (Args->TryGetStringField(TEXT("file_path"), SingleFile) && !SingleFile.IsEmpty())
			Files.AddUnique(SingleFile);
		HandleGitRevert(Files, OutJson, OutError);
	}
	else if (Action == TEXT("git_init"))
	{
		FString Path; Args->TryGetStringField(TEXT("path"), Path);
		HandleGitInit(Path, OutJson, OutError);
	}
	else if (Action == TEXT("git_branch"))
	{
		FString SubAction, BranchName;
		Args->TryGetStringField(TEXT("sub_action"), SubAction);
		Args->TryGetStringField(TEXT("branch_name"), BranchName);
		HandleGitBranch(SubAction, BranchName, OutJson, OutError);
	}
	else if (Action == TEXT("git_stash"))
	{
		FString SubAction; Args->TryGetStringField(TEXT("sub_action"), SubAction);
		HandleGitStash(SubAction, OutJson, OutError);
	}
	else if (Action == TEXT("run_command"))
	{
		FString Command, WorkDir;
		Args->TryGetStringField(TEXT("command"), Command);
		Args->TryGetStringField(TEXT("working_dir"), WorkDir);
		HandleRunCommand(Command, WorkDir, OutJson, OutError);
	}
	else
	{
		OutError = FString::Printf(TEXT("Unknown git action: %s. Available: git_status, git_log, git_commit, git_diff, git_revert, git_init, git_branch, git_stash, run_command"), *Action);
	}
}

void HandleRunCommand(const FString& Command, const FString& WorkDir, FString& OutJson, FString& OutError)
{
	if (Command.IsEmpty())
	{
		OutError = TEXT("run_command: 'command' is required");
		return;
	}

	FString WorkingDir = WorkDir.IsEmpty() ? FPaths::ProjectDir() : WorkDir;
	FPaths::NormalizeDirectoryName(WorkingDir);

	FString StdOut, StdErr;
	int32 RetCode = -1;

#if PLATFORM_WINDOWS
	RunProcessHidden(TEXT("C:\\Windows\\System32\\cmd.exe"),
		FString::Printf(TEXT("/c \"%s\""), *Command),
		WorkingDir, StdOut, StdErr, RetCode);
#else
	FPlatformProcess::ExecProcess(TEXT("/bin/sh"), *FString::Printf(TEXT("-c \"%s\""), *Command),
		&RetCode, &StdOut, &StdErr);
#endif

	StdOut.TrimEndInline();
	StdErr.TrimEndInline();

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), RetCode == 0);
	ResultObj->SetNumberField(TEXT("exit_code"), RetCode);
	ResultObj->SetStringField(TEXT("stdout"), StdOut);
	if (!StdErr.IsEmpty())
		ResultObj->SetStringField(TEXT("stderr"), StdErr);

	FString ResultString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
	FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
	OutJson = ResultString;

	UE_LOG(LogMCPTool, Log, TEXT("run_command: [%s] exit=%d"), *Command, RetCode);
}

}
