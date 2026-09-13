// Copyright 2026, BlueprintsLab, All rights reserved

#include "Registry/FUECPACPInstaller.h"
#include "UECPACPModule.h"
#include "Managers/SettingsManager.h"
#include "Services/IUECPLicenseService.h"
#include "UECPCoreModule.h"
#include "Utils/EditorRuntime.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "HAL/Event.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString AsIsoUtc()
	{
		return FDateTime::UtcNow().ToIso8601();
	}
}

namespace FUECPACPInstaller
{
	FString MarkerPath(const FString& AgentId)
	{
		return FSettingsManager::GetGlobalDataDir() / TEXT("acp/installs") / (AgentId + TEXT(".json"));
	}

	static FString PackageCacheDir(const FString& AgentId)
	{
		return FSettingsManager::GetGlobalDataDir() / TEXT("acp/packages") / AgentId;
	}

	FString FindNpxOnPath()
	{
#if PLATFORM_WINDOWS
		const TCHAR* Cmd  = TEXT("where.exe");
		const TCHAR* Args = TEXT("npx.cmd");
#else
		const TCHAR* Cmd  = TEXT("/usr/bin/which");
		const TCHAR* Args = TEXT("npx");
#endif
		void* PipeRead = nullptr; void* PipeWrite = nullptr;
		if (!FPlatformProcess::CreatePipe(PipeRead, PipeWrite)) return FString();

		FProcHandle Proc = FPlatformProcess::CreateProc(
			Cmd, Args,  false,  true,  true,
			nullptr, 0, nullptr, PipeWrite, nullptr);
		if (!Proc.IsValid())
		{
			FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
			return FString();
		}

		FString Out;
		const double Start = FPlatformTime::Seconds();
		while (FPlatformProcess::IsProcRunning(Proc) && (FPlatformTime::Seconds() - Start) < 2.0)
		{
			Out += FPlatformProcess::ReadPipe(PipeRead);
			FPlatformProcess::Sleep(0.02f);
		}
		if (FPlatformProcess::IsProcRunning(Proc)) FPlatformProcess::TerminateProc(Proc);
		Out += FPlatformProcess::ReadPipe(PipeRead);
		FPlatformProcess::CloseProc(Proc);
		FPlatformProcess::ClosePipe(PipeRead, PipeWrite);

		Out.TrimStartAndEndInline();
		FString FirstLine;
		if (!Out.Split(TEXT("\n"), &FirstLine, nullptr)) FirstLine = Out;
		FirstLine.TrimStartAndEndInline();
		return (FirstLine.IsEmpty() || !FPaths::FileExists(FirstLine)) ? FString() : FirstLine;
	}

	FString FindNpxPath()
	{
		IFileManager& FM = IFileManager::Get();

		const FString UserHome     = FPlatformProcess::UserHomeDir();
		const FString AppDataRoam  = FPaths::Combine(FPlatformProcess::UserDir(), TEXT("AppData/Roaming"));
		const FString LocalAppData = FPaths::Combine(FPlatformProcess::UserDir(), TEXT("AppData/Local"));

		TArray<FString> Candidates;
#if PLATFORM_WINDOWS
		Candidates = {
			TEXT("C:/Program Files/nodejs/npx.cmd"),
			TEXT("C:/Program Files (x86)/nodejs/npx.cmd"),
			AppDataRoam  / TEXT("npm/npx.cmd"),
			LocalAppData / TEXT("Programs/nodejs/npx.cmd"),
			LocalAppData / TEXT("Volta/bin/npx.cmd"),
			FPaths::Combine(FPlatformProcess::UserDir(), TEXT("scoop/apps/nodejs/current/npx.cmd")),
			FPaths::Combine(FPlatformProcess::UserDir(), TEXT("scoop/shims/npx.cmd")),
			TEXT("C:/ProgramData/chocolatey/lib/nodejs/tools/npx.cmd"),
			TEXT("C:/ProgramData/chocolatey/bin/npx.cmd"),
		};
		{
			const FString NvmRoot = AppDataRoam / TEXT("nvm");
			if (FM.DirectoryExists(*NvmRoot))
			{
				TArray<FString> VersionDirs;
				IFileManager::Get().FindFiles(VersionDirs, *(NvmRoot / TEXT("v*")), false, true);
				for (const FString& Vd : VersionDirs)
					Candidates.Add(NvmRoot / Vd / TEXT("npx.cmd"));
			}
		}
#else
		Candidates = {
			TEXT("/usr/local/bin/npx"),
			TEXT("/usr/bin/npx"),
			TEXT("/opt/homebrew/bin/npx"),
			TEXT("/snap/bin/npx"),
			UserHome / TEXT(".volta/bin/npx"),
			UserHome / TEXT(".fnm/aliases/default/bin/npx"),
			UserHome / TEXT(".nodebrew/current/bin/npx"),
		};
		{
			const FString NvmVersionsRoot = UserHome / TEXT(".nvm/versions/node");
			if (FM.DirectoryExists(*NvmVersionsRoot))
			{
				TArray<FString> VersionDirs;
				IFileManager::Get().FindFiles(VersionDirs, *(NvmVersionsRoot / TEXT("v*")), false, true);
				for (const FString& Vd : VersionDirs)
					Candidates.Add(NvmVersionsRoot / Vd / TEXT("bin/npx"));
			}
		}
#endif

		for (const FString& P : Candidates)
		{
			if (FM.FileExists(*P)) return P;
		}

		const FString FromPath = FindNpxOnPath();
		if (!FromPath.IsEmpty())
		{
			UE_LOG(LogUECPACP, Log, TEXT("npx resolved via PATH lookup: %s"), *FromPath);
			return FromPath;
		}

		UE_LOG(LogUECPACP, Warning,
			TEXT("npx not found via static probes or PATH lookup. Install Node.js (https://nodejs.org) or set npx on PATH."));
		return FString();
	}

	bool WriteMarker(const FUECPACPInstallMarker& Marker)
	{
		const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("agentId"),            Marker.AgentId);
		O->SetStringField(TEXT("version"),            Marker.Version);
		O->SetNumberField(TEXT("method"),             static_cast<int32>(Marker.Method));
		O->SetStringField(TEXT("entrypointCommand"),  Marker.EntrypointCommand);
		TArray<TSharedPtr<FJsonValue>> Args;
		for (const FString& A : Marker.EntrypointArgs) Args.Add(MakeShared<FJsonValueString>(A));
		O->SetArrayField(TEXT("entrypointArgs"),      Args);
		O->SetStringField(TEXT("installedAtUtc"),     Marker.InstalledAtUtc);

		FString Body;
		const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Body);
		FJsonSerializer::Serialize(O, W);
		W->Close();

		const FString Path = MarkerPath(Marker.AgentId);
		const FString Dir  = FPaths::GetPath(Path);
		IFileManager::Get().MakeDirectory(*Dir,  true);
		if (!FFileHelper::SaveStringToFile(Body, *Path))
		{
			UE_LOG(LogUECPACP, Error, TEXT("Failed to write install marker: %s"), *Path);
			return false;
		}
		return true;
	}

	bool Uninstall(const FString& AgentId)
	{
		const FString Path = MarkerPath(AgentId);
		const bool bMarkerRemoved = FPaths::FileExists(Path) && IFileManager::Get().Delete(*Path);

		const FString CacheDir = PackageCacheDir(AgentId);
		if (IFileManager::Get().DirectoryExists(*CacheDir))
		{
			IFileManager::Get().DeleteDirectory(*CacheDir,  false,  true);
		}

		UE_LOG(LogUECPACP, Log, TEXT("Uninstalled '%s' — marker=%d cache=%s"),
			*AgentId, bMarkerRemoved ? 1 : 0, *CacheDir);
		return bMarkerRemoved;
	}

	static bool RunAndCapture(const FString& Exe, const FString& Args, int32 TimeoutSec,
		FString& OutStdout, int32& OutExitCode)
	{
		void* PipeRead = nullptr; void* PipeWrite = nullptr;
		if (!FPlatformProcess::CreatePipe(PipeRead, PipeWrite)) return false;

#if PLATFORM_MAC
		const FString EnvArgs = FString::Printf(TEXT("PATH=%s:/usr/local/bin:/usr/bin:/bin %s %s"),
			*FPaths::GetPath(Exe), *Exe, *Args);
		FProcHandle Proc = FPlatformProcess::CreateProc(
			TEXT("/usr/bin/env"), *EnvArgs, false, true, true, nullptr, 0, nullptr, PipeWrite, nullptr);
#else
		FProcHandle Proc = FPlatformProcess::CreateProc(
			*Exe, *Args, false, true, true, nullptr, 0, nullptr, PipeWrite, nullptr);
#endif
		if (!Proc.IsValid())
		{
			FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
			return false;
		}

		const double Deadline = FPlatformTime::Seconds() + TimeoutSec;
		while (FPlatformProcess::IsProcRunning(Proc))
		{
			OutStdout.Append(FPlatformProcess::ReadPipe(PipeRead));
			if (FPlatformTime::Seconds() > Deadline)
			{
				FPlatformProcess::TerminateProc(Proc,  true);
				break;
			}
			FPlatformProcess::Sleep(0.05f);
		}
		OutStdout.Append(FPlatformProcess::ReadPipe(PipeRead));
		FPlatformProcess::GetProcReturnCode(Proc, &OutExitCode);
		FPlatformProcess::CloseProc(Proc);
		FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
		return true;
	}

	static FString FindNodeExe(const FString& NpxPath)
	{
		if (NpxPath.IsEmpty()) return FString();
		const FString Dir = FPaths::GetPath(NpxPath);
#if PLATFORM_WINDOWS
		const FString NodeExe = Dir / TEXT("node.exe");
#else
		const FString NodeExe = Dir / TEXT("node");
#endif
		return IFileManager::Get().FileExists(*NodeExe) ? NodeExe : FString();
	}

	static FString ReadInstalledBinScript(const FString& Prefix, const FString& NpxPackage)
	{
		FString PkgName = NpxPackage;
		for (int32 i = PkgName.Len() - 1; i > 0; --i)
		{
			if (PkgName[i] == TEXT('@')) { PkgName = PkgName.Left(i); break; }
		}

		const FString PkgDir      = Prefix / TEXT("node_modules") / PkgName;
		const FString PkgJsonPath = PkgDir  / TEXT("package.json");

		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *PkgJsonPath)) return FString();

		TSharedPtr<FJsonObject> Obj;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Content), Obj) || !Obj.IsValid())
			return FString();

		FString BinRel;

		const TSharedPtr<FJsonValue>* BinPtr = Obj->Values.Find(TEXT("bin"));
		if (BinPtr && BinPtr->IsValid())
		{
			if ((*BinPtr)->Type == EJson::String)
			{
				BinRel = (*BinPtr)->AsString();
			}
			else if ((*BinPtr)->Type == EJson::Object)
			{
				const TSharedPtr<FJsonObject> BinObj = (*BinPtr)->AsObject();
				if (BinObj.IsValid())
				{
					for (const auto& KV : BinObj->Values)
					{
						if (KV.Value.IsValid() && KV.Value->Type == EJson::String)
						{
							BinRel = KV.Value->AsString();
							break;
						}
					}
				}
			}
		}

		if (BinRel.IsEmpty()) Obj->TryGetStringField(TEXT("main"), BinRel);
		if (BinRel.IsEmpty()) return FString();

		while (BinRel.StartsWith(TEXT("./"))) BinRel.RemoveFromStart(TEXT("./"));

		const FString Abs = FPaths::Combine(PkgDir, BinRel);
		return IFileManager::Get().FileExists(*Abs) ? Abs : FString();
	}

	static FString FindNpmPath()
	{
		const FString NpxPath = FindNpxPath();
		if (NpxPath.IsEmpty()) return FString();
		const FString Dir = FPaths::GetPath(NpxPath);
#if PLATFORM_WINDOWS
		const FString NpmCmd = Dir / TEXT("npm.cmd");
#else
		const FString NpmCmd = Dir / TEXT("npm");
#endif
		return IFileManager::Get().FileExists(*NpmCmd) ? NpmCmd : FString();
	}

	static bool DownloadNpmPackage(const FString& Package, const FString& AgentId,
		FUECPACPInstallProgress OnProgress, FString& OutError)
	{
		const FString NpmPath = FindNpmPath();
		if (NpmPath.IsEmpty())
		{
			OutError = TEXT("npm not found");
			return false;
		}

		const FString Prefix = PackageCacheDir(AgentId);
		IFileManager::Get().MakeDirectory(*Prefix,  true);

		const FString PackageJsonPath = Prefix / TEXT("package.json");
		if (!FPaths::FileExists(PackageJsonPath))
		{
			const FString MinimalPkg = FString::Printf(
				TEXT("{\"name\":\"uecp-acp-%s\",\"version\":\"0.0.0\",\"private\":true}"),
				*AgentId);
			FFileHelper::SaveStringToFile(MinimalPkg, *PackageJsonPath);
		}

		if (OnProgress.IsBound())
		{
			AsyncTask(ENamedThreads::GameThread, [OnProgress]()
			{
				OnProgress.Execute(TEXT("Downloading"), 0.4f);
			});
		}

		const FString Args = FString::Printf(
			TEXT("install --prefix \"%s\" --no-save --no-fund --no-audit --loglevel=error %s"),
			*Prefix, *Package);

		FString Stdout; int32 ExitCode = -1;
		if (!RunAndCapture(NpmPath, Args,  180, Stdout, ExitCode))
		{
			OutError = TEXT("npm install spawn failed");
			return false;
		}
		if (ExitCode != 0)
		{
			OutError = FString::Printf(TEXT("npm install failed (exit %d): %s"),
				ExitCode, *Stdout.Left(512));
			return false;
		}
		UE_LOG(LogUECPACP, Log, TEXT("npm install ok for '%s' → %s"), *Package, *Prefix);
		return true;
	}

	static FString GetCurrentPlatformString()
	{
#if PLATFORM_WINDOWS
	#if PLATFORM_CPU_ARM_FAMILY
		return TEXT("windows-aarch64");
	#else
		return TEXT("windows-x86_64");
	#endif
#elif PLATFORM_MAC
	#if PLATFORM_CPU_ARM_FAMILY
		return TEXT("darwin-aarch64");
	#else
		return TEXT("darwin-x86_64");
	#endif
#elif PLATFORM_LINUX
	#if PLATFORM_CPU_ARM_FAMILY
		return TEXT("linux-aarch64");
	#else
		return TEXT("linux-x86_64");
	#endif
#else
		return FString();
#endif
	}

	static const FUECPACPBinaryDistribution* FindBinaryForCurrentPlatform(const FUECPACPAgentEntry& Entry)
	{
		const FString Plat = GetCurrentPlatformString();
		if (Plat.IsEmpty()) return nullptr;
		return Entry.Binaries.FindByPredicate(
			[&Plat](const FUECPACPBinaryDistribution& B) { return B.Platform == Plat; });
	}

	static bool DownloadFileHttp(const FString& Url, const FString& OutPath,
		FUECPACPInstallProgress OnProgress, FString& OutError)
	{
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
		Req->SetURL(Url);
		Req->SetVerb(TEXT("GET"));
		Req->SetTimeout(600.0f);

		FEvent* Done = FPlatformProcess::GetSynchEventFromPool(false);
		bool bSuccess = false;
		FString ErrMsg;
		TArray<uint8> Body;
		int32 RespCode = 0;

		Req->OnProcessRequestComplete().BindLambda(
			[&bSuccess, &ErrMsg, &Body, &RespCode, Done]
			(FHttpRequestPtr R, FHttpResponsePtr Resp, bool bOk)
			{
				RespCode = Resp.IsValid() ? Resp->GetResponseCode() : 0;
				if (bOk && Resp.IsValid() && RespCode >= 200 && RespCode < 300)
				{
					Body = Resp->GetContent();
					bSuccess = true;
				}
				else
				{
					ErrMsg = FString::Printf(TEXT("HTTP %d (ok=%d)"), RespCode, bOk ? 1 : 0);
				}
				Done->Trigger();
			});

		Req->OnRequestProgress64().BindLambda(
			[OnProgress] (FHttpRequestPtr R, uint64 BytesSent, uint64 BytesReceived)
			{
				if (!OnProgress.IsBound()) return;
				const double MB = static_cast<double>(BytesReceived) / (1024.0 * 1024.0);
				const FString Msg = FString::Printf(TEXT("Downloading %.1f MB"), MB);
				AsyncTask(ENamedThreads::GameThread, [OnProgress, Msg]()
				{
					if (OnProgress.IsBound()) OnProgress.Execute(Msg, 0.4f);
				});
			});

		if (!Req->ProcessRequest())
		{
			OutError = TEXT("Failed to start HTTP request");
			FPlatformProcess::ReturnSynchEventToPool(Done);
			return false;
		}

		Done->Wait();
		FPlatformProcess::ReturnSynchEventToPool(Done);

		if (!bSuccess)
		{
			OutError = FString::Printf(TEXT("Download failed: %s"), *ErrMsg);
			return false;
		}
		if (!FFileHelper::SaveArrayToFile(Body, *OutPath))
		{
			OutError = FString::Printf(TEXT("Failed to write archive to %s"), *OutPath);
			return false;
		}
		return true;
	}

	static bool ExtractArchive(const FString& ArchivePath, const FString& OutDir, FString& OutError)
	{
		IFileManager::Get().MakeDirectory(*OutDir,  true);

		const FString Lower = ArchivePath.ToLower();
		const bool bIsZip = Lower.EndsWith(TEXT(".zip"));

		FString Cmd;
		FString Args;

#if PLATFORM_WINDOWS
		const FString SystemRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot"));
		Cmd = (SystemRoot.IsEmpty() ? FString(TEXT("C:\\Windows")) : SystemRoot) / TEXT("System32") / TEXT("tar.exe");
		if (!IFileManager::Get().FileExists(*Cmd))
		{
			Cmd = TEXT("C:\\Windows\\System32\\tar.exe");
		}
		Args = FString::Printf(TEXT("-xf \"%s\" -C \"%s\""), *ArchivePath, *OutDir);
#elif PLATFORM_LINUX
		if (bIsZip)
		{
			Cmd  = TEXT("unzip");
			Args = FString::Printf(TEXT("-q -o \"%s\" -d \"%s\""), *ArchivePath, *OutDir);
		}
		else
		{
			Cmd  = TEXT("tar");
			Args = FString::Printf(TEXT("-xf \"%s\" -C \"%s\""), *ArchivePath, *OutDir);
		}
#else
		Cmd  = TEXT("/usr/bin/tar");
		Args = FString::Printf(TEXT("-xf \"%s\" -C \"%s\""), *ArchivePath, *OutDir);
#endif

		FString Stdout; int32 ExitCode = -1;
		if (!RunAndCapture(Cmd, Args,  180, Stdout, ExitCode))
		{
			OutError = FString::Printf(TEXT("Extraction spawn failed (cmd=%s)"), *Cmd);
			return false;
		}
		if (ExitCode != 0)
		{
			OutError = FString::Printf(TEXT("Extraction failed (exit %d): %s"),
				ExitCode, *Stdout.Left(512));
			return false;
		}
		(void)bIsZip;
		return true;
	}

	static void MakeExecutable(const FString& Path)
	{
#if !PLATFORM_WINDOWS
		FString Stdout; int32 Exit = -1;
		RunAndCapture(TEXT("/bin/chmod"),
			FString::Printf(TEXT("+x \"%s\""), *Path),
			 5, Stdout, Exit);
#else
		(void)Path;
#endif
	}

	static FString ResolveCmdAbsPath(const FString& CacheDir, const FString& CmdRelRaw)
	{
		FString CmdRel = CmdRelRaw;
		while (CmdRel.StartsWith(TEXT("./"))) CmdRel.RemoveFromStart(TEXT("./"));
		CmdRel.ReplaceInline(TEXT("\\"), TEXT("/"));
		return FPaths::Combine(CacheDir, CmdRel);
	}

	static bool InstallBinary(const FUECPACPAgentEntry& Entry,
		FUECPACPInstallProgress OnProgress,
		FUECPACPInstallMarker& OutMarker, FString& OutError)
	{
		const FUECPACPBinaryDistribution* Bin = FindBinaryForCurrentPlatform(Entry);
		if (!Bin)
		{
			OutError = FString::Printf(TEXT("No binary distribution for platform '%s'"),
				*GetCurrentPlatformString());
			return false;
		}

		const FString CacheDir = PackageCacheDir(Entry.Id);
		IFileManager::Get().MakeDirectory(*CacheDir,  true);

		IFileManager::Get().DeleteDirectory(*CacheDir,  false,  true);
		IFileManager::Get().MakeDirectory(*CacheDir,  true);

		FString ArchiveName = FPaths::GetCleanFilename(Bin->ArchiveUrl);
		int32 QueryAt = INDEX_NONE;
		if (ArchiveName.FindChar(TEXT('?'), QueryAt)) ArchiveName.LeftInline(QueryAt);
		if (ArchiveName.FindChar(TEXT('#'), QueryAt)) ArchiveName.LeftInline(QueryAt);
		if (ArchiveName.IsEmpty()) ArchiveName = TEXT("archive.bin");

		const FString ArchivePath = CacheDir / ArchiveName;

		if (OnProgress.IsBound())
		{
			AsyncTask(ENamedThreads::GameThread, [OnProgress]()
			{
				if (OnProgress.IsBound()) OnProgress.Execute(TEXT("Downloading"), 0.2f);
			});
		}

		if (!DownloadFileHttp(Bin->ArchiveUrl, ArchivePath, OnProgress, OutError))
		{
			return false;
		}

		if (OnProgress.IsBound())
		{
			AsyncTask(ENamedThreads::GameThread, [OnProgress]()
			{
				if (OnProgress.IsBound()) OnProgress.Execute(TEXT("Extracting"), 0.7f);
			});
		}

		if (!ExtractArchive(ArchivePath, CacheDir, OutError))
		{
			return false;
		}

		const FString CmdAbs = ResolveCmdAbsPath(CacheDir, Bin->Cmd);
		if (!IFileManager::Get().FileExists(*CmdAbs))
		{
			OutError = FString::Printf(
				TEXT("Extracted archive is missing the registered entrypoint: %s"), *CmdAbs);
			return false;
		}

		MakeExecutable(CmdAbs);

		IFileManager::Get().Delete(*ArchivePath,  false);

		OutMarker.AgentId           = Entry.Id;
		OutMarker.Version           = Entry.Version;
		OutMarker.Method            = EUECPACPDistributionKind::Binary;
		OutMarker.EntrypointCommand = CmdAbs;
		OutMarker.EntrypointArgs    = Bin->Args;
		OutMarker.InstalledAtUtc    = AsIsoUtc();

		UE_LOG(LogUECPACP, Log, TEXT("Binary install ok for '%s' on %s → %s"),
			*Entry.Id, *GetCurrentPlatformString(), *CmdAbs);
		return true;
	}

	void Install(const FUECPACPAgentEntry& Entry,
		FUECPACPInstallProgress OnProgress,
		FUECPACPInstallComplete OnComplete)
	{
		if (!EditorReadiness::IsSessionActive())
		{
			UE_LOG(LogUECPACP, Warning, TEXT("Install '%s' refused — session inactive"), *Entry.Id);
			AsyncTask(ENamedThreads::GameThread, [OnComplete]()
			{
				if (OnComplete.IsBound()) OnComplete.Execute(false, FUECPACPInstallMarker{});
			});
			return;
		}

		const bool bHasNpx = !Entry.NpxPackage.IsEmpty();
		const bool bHasBin = Entry.Binaries.Num() > 0;

		if (!bHasNpx && !bHasBin)
		{
			UE_LOG(LogUECPACP, Warning,
				TEXT("Install '%s' refused — registry entry has no installable distribution (no npx, no binary)"),
				*Entry.Id);
			AsyncTask(ENamedThreads::GameThread, [OnComplete]()
			{
				if (OnComplete.IsBound()) OnComplete.Execute(false, FUECPACPInstallMarker{});
			});
			return;
		}

		if (!bHasNpx && bHasBin)
		{
			const FUECPACPAgentEntry EntryCopy = Entry;
			Async(EAsyncExecution::Thread, [EntryCopy, OnProgress, OnComplete]()
			{
				FUECPACPInstallMarker Marker;
				FString ErrorMsg;
				const bool bOk = InstallBinary(EntryCopy, OnProgress, Marker, ErrorMsg);
				if (!bOk)
				{
					UE_LOG(LogUECPACP, Error, TEXT("Binary install failed for '%s': %s"),
						*EntryCopy.Id, *ErrorMsg);
					AsyncTask(ENamedThreads::GameThread, [OnComplete]()
					{
						if (OnComplete.IsBound()) OnComplete.Execute(false, FUECPACPInstallMarker{});
					});
					return;
				}
				const bool bWrote = WriteMarker(Marker);
				AsyncTask(ENamedThreads::GameThread, [OnComplete, Marker, bWrote]()
				{
					if (OnComplete.IsBound()) OnComplete.Execute(bWrote, Marker);
				});
			});
			if (OnProgress.IsBound()) OnProgress.Execute(TEXT("Starting"), 0.05f);
			return;
		}

		const FString NpxPath = FindNpxPath();
		if (NpxPath.IsEmpty())
		{
			AsyncTask(ENamedThreads::GameThread, [OnComplete]()
			{
				if (OnComplete.IsBound()) OnComplete.Execute(false, FUECPACPInstallMarker{});
			});
			return;
		}

		const FUECPACPAgentEntry EntryCopy = Entry;
		Async(EAsyncExecution::Thread, [NpxPath, EntryCopy, OnProgress, OnComplete]()
		{
			FString ErrorMsg;
			const bool bDownloaded = DownloadNpmPackage(EntryCopy.NpxPackage, EntryCopy.Id, OnProgress, ErrorMsg);
			if (!bDownloaded)
			{
				UE_LOG(LogUECPACP, Error, TEXT("Install failed for '%s': %s"), *EntryCopy.Id, *ErrorMsg);
				AsyncTask(ENamedThreads::GameThread, [OnComplete]()
				{
					if (OnComplete.IsBound()) OnComplete.Execute(false, FUECPACPInstallMarker{});
				});
				return;
			}

			FUECPACPInstallMarker Marker;
			Marker.AgentId  = EntryCopy.Id;
			Marker.Version  = EntryCopy.Version;
			Marker.Method   = EUECPACPDistributionKind::Npx;

			const FString NodeExe   = FindNodeExe(NpxPath);
			const FString BinScript = ReadInstalledBinScript(PackageCacheDir(EntryCopy.Id), EntryCopy.NpxPackage);

			if (!NodeExe.IsEmpty() && !BinScript.IsEmpty())
			{
				UE_LOG(LogUECPACP, Log, TEXT("Install '%s': entrypoint resolved → node %s"),
					*EntryCopy.Id, *BinScript);
				Marker.EntrypointCommand = NodeExe;
				Marker.EntrypointArgs    = { BinScript };
				Marker.EntrypointArgs.Append(EntryCopy.NpxArgs);
			}
			else
			{
				UE_LOG(LogUECPACP, Log, TEXT("Install '%s': node/script not found, falling back to npx"), *EntryCopy.Id);
				Marker.EntrypointCommand = NpxPath;
				Marker.EntrypointArgs    = { TEXT("-y"), TEXT("--prefer-offline"), EntryCopy.NpxPackage };
				Marker.EntrypointArgs.Append(EntryCopy.NpxArgs);
			}
			Marker.InstalledAtUtc    = AsIsoUtc();

			const bool bWrote = WriteMarker(Marker);
			UE_LOG(LogUECPACP, Log, TEXT("Install complete for '%s'"), *EntryCopy.Id);

			AsyncTask(ENamedThreads::GameThread, [OnComplete, Marker, bWrote]()
			{
				if (OnComplete.IsBound()) OnComplete.Execute(bWrote, Marker);
			});
		});

		if (OnProgress.IsBound()) OnProgress.Execute(TEXT("Starting"), 0.05f);
	}
}
