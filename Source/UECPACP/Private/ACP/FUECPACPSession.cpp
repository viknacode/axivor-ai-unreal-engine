// Copyright 2026, BlueprintsLab, All rights reserved

#include "ACP/FUECPACPSession.h"

#include "ACP/FUECPACPEventMapper.h"
#include "ACP/FUECPACPSchema.h"
#include "UECPACPModule.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Paths.h"

namespace
{
	constexpr const TCHAR* ClientName    = TEXT("uecp-acp-client");
	constexpr const TCHAR* ClientTitle   = TEXT("UECP ACP Client");
	constexpr const TCHAR* ClientVersion = TEXT("0.1.0");

	FString BuildParmsString(const TArray<FString>& Args)
	{
		FString Out;
		for (const FString& A : Args)
		{
			if (!Out.IsEmpty()) Out.AppendChar(TEXT(' '));
			const bool bNeedsQuotes = A.Contains(TEXT(" ")) || A.Contains(TEXT("\t"));
			if (bNeedsQuotes) Out.AppendChar(TEXT('"'));
			Out.Append(A);
			if (bNeedsQuotes) Out.AppendChar(TEXT('"'));
		}
		return Out;
	}

	TSharedPtr<FJsonValue> CloneIdValue(const TSharedPtr<FJsonObject>& Root)
	{
		const TSharedPtr<FJsonValue> Id = Root->TryGetField(TEXT("id"));
		if (!Id.IsValid()) return MakeShared<FJsonValueNull>();
		if (Id->Type == EJson::Number) return MakeShared<FJsonValueNumber>(Id->AsNumber());
		if (Id->Type == EJson::String) return MakeShared<FJsonValueString>(Id->AsString());
		return MakeShared<FJsonValueNull>();
	}

	FString FormatJsonRpcErrorForUser(const TSharedPtr<FJsonObject>& ErrObj)
	{
		if (!ErrObj.IsValid()) return TEXT("Unknown error");

		FString TopMessage;
		double  CodeNum = 0;
		ErrObj->TryGetStringField(TEXT("message"), TopMessage);
		ErrObj->TryGetNumberField(TEXT("code"),    CodeNum);
		const int32 Code = static_cast<int32>(CodeNum);

		const TSharedPtr<FJsonObject>* DataPtr = nullptr;
		ErrObj->TryGetObjectField(TEXT("data"), DataPtr);

		FString DataType, DataMessage;
		if (DataPtr && DataPtr->IsValid())
		{
			const TSharedPtr<FJsonObject>& D = *DataPtr;
			D->TryGetStringField(TEXT("type"), DataType);
			if (DataType.IsEmpty()) D->TryGetStringField(TEXT("error_type"), DataType);
			if (DataType.IsEmpty()) D->TryGetStringField(TEXT("category"),   DataType);

			D->TryGetStringField(TEXT("message"), DataMessage);
			if (DataMessage.IsEmpty()) D->TryGetStringField(TEXT("detail"), DataMessage);
			if (DataMessage.IsEmpty()) D->TryGetStringField(TEXT("description"), DataMessage);

			if (DataMessage.IsEmpty() && DataType.IsEmpty())
			{
				const TSharedPtr<FJsonObject>* NestedErr = nullptr;
				if (D->TryGetObjectField(TEXT("error"), NestedErr) && NestedErr && NestedErr->IsValid())
				{
					(*NestedErr)->TryGetStringField(TEXT("type"),    DataType);
					(*NestedErr)->TryGetStringField(TEXT("message"), DataMessage);
				}
			}
		}

		FString Headline;
		const FString DTypeLower = DataType.ToLower();
		if (DTypeLower.Contains(TEXT("usage_limit")) || DTypeLower.Contains(TEXT("quota")))
			Headline = TEXT("Usage limit reached");
		else if (DTypeLower.Contains(TEXT("rate_limit")) || DTypeLower.Contains(TEXT("rate-limit")))
			Headline = TEXT("Rate limit reached");
		else if (DTypeLower.Contains(TEXT("auth")) || DTypeLower.Contains(TEXT("unauthorized")))
			Headline = TEXT("Authentication failed");
		else if (DTypeLower.Contains(TEXT("insufficient")) || DTypeLower.Contains(TEXT("billing")) || DTypeLower.Contains(TEXT("payment")))
			Headline = TEXT("Billing / credit issue");
		else if (DTypeLower.Contains(TEXT("timeout")))
			Headline = TEXT("Agent timed out");
		else if (DTypeLower.Contains(TEXT("context")) || DTypeLower.Contains(TEXT("token")))
			Headline = TEXT("Context window exceeded");

		FString Out;
		if (!Headline.IsEmpty())
		{
			Out = FString::Printf(TEXT("**%s**"), *Headline);
		}
		else if (!TopMessage.IsEmpty() && !TopMessage.Equals(TEXT("Internal error"), ESearchCase::IgnoreCase))
		{
			Out = FString::Printf(TEXT("**%s**"), *TopMessage);
		}
		else
		{
			Out = TEXT("**Agent error**");
		}

		if (!DataMessage.IsEmpty())
		{
			Out += TEXT("\n\n> ") + DataMessage.Replace(TEXT("\n"), TEXT(" "));
		}
		else if (TopMessage.Equals(TEXT("Internal error"), ESearchCase::IgnoreCase) && Code == -32603)
		{
			Out += TEXT("\n\n> The agent reported an internal error with no further detail. Open the editor's Output Log and filter LogUECPACP for the full error body.");
		}

		if (Code != 0)
		{
			Out += FString::Printf(TEXT("\n\n_(error code %d"), Code);
			if (!DataType.IsEmpty()) Out += FString::Printf(TEXT(" / %s"), *DataType);
			Out += TEXT(")_");
		}
		else if (!DataType.IsEmpty())
		{
			Out += FString::Printf(TEXT("\n\n_(%s)_"), *DataType);
		}

		return Out;
	}
}

FUECPACPSession::~FUECPACPSession()
{
	if (State != EState::Closed) Stop();
}

bool FUECPACPSession::Start(const FSpawnArgs& InArgs)
{
	if (State != EState::Uninit)
	{
		UE_LOG(LogUECPACP, Warning, TEXT("Start called in unexpected state (tag=%s)"), *InArgs.LogTag);
		return false;
	}

	Args = InArgs;

	void* ParentWriteToStdin = nullptr; void* ChildReadsStdin   = nullptr;
	void* ParentReadsStdout  = nullptr; void* ChildWritesStdout = nullptr;
	void* ParentReadsStderr  = nullptr; void* ChildWritesStderr = nullptr;

	if (!FPlatformProcess::CreatePipe(ChildReadsStdin,  ParentWriteToStdin,  true))
	{
		UE_LOG(LogUECPACP, Error, TEXT("[%s] CreatePipe stdin failed"), *Args.LogTag);
		return false;
	}
	if (!FPlatformProcess::CreatePipe(ParentReadsStdout, ChildWritesStdout))
	{
		UE_LOG(LogUECPACP, Error, TEXT("[%s] CreatePipe stdout failed"), *Args.LogTag);
		FPlatformProcess::ClosePipe(ChildReadsStdin,  ParentWriteToStdin);
		return false;
	}
	if (!FPlatformProcess::CreatePipe(ParentReadsStderr, ChildWritesStderr))
	{
		UE_LOG(LogUECPACP, Error, TEXT("[%s] CreatePipe stderr failed"), *Args.LogTag);
		FPlatformProcess::ClosePipe(ChildReadsStdin,  ParentWriteToStdin);
		FPlatformProcess::ClosePipe(ParentReadsStdout, ChildWritesStdout);
		return false;
	}

	const FString Parms = BuildParmsString(Args.Args);
	const TCHAR*  Cwd   = Args.Cwd.IsEmpty() ? nullptr : *Args.Cwd;

#if PLATFORM_MAC
	const FString MacEnvArgs = FString::Printf(TEXT("PATH=%s:/usr/local/bin:/usr/bin:/bin \"%s\" %s"),
		*FPaths::GetPath(Args.Command), *Args.Command, *Parms);
	Process = FPlatformProcess::CreateProc(
		TEXT("/usr/bin/env"), *MacEnvArgs,
		       false,
		         true,
		   true,
		&Pid,
		      0,
		Cwd,
		ChildWritesStdout,
		ChildReadsStdin,
		ChildWritesStderr);
#else
	Process = FPlatformProcess::CreateProc(
		*Args.Command, *Parms,
		       false,
		         true,
		   true,
		&Pid,
		      0,
		Cwd,
		ChildWritesStdout,
		ChildReadsStdin,
		ChildWritesStderr);
#endif

	if (!Process.IsValid())
	{
		UE_LOG(LogUECPACP, Error, TEXT("[%s] CreateProc failed: %s %s"), *Args.LogTag, *Args.Command, *Parms);
		FPlatformProcess::ClosePipe(ChildReadsStdin,  ParentWriteToStdin);
		FPlatformProcess::ClosePipe(ParentReadsStdout, ChildWritesStdout);
		FPlatformProcess::ClosePipe(ParentReadsStderr, ChildWritesStderr);
		return false;
	}

	StdinWrite = ParentWriteToStdin;
	StdoutRead = ParentReadsStdout;
	StderrRead = ParentReadsStderr;

	FPlatformProcess::ClosePipe(ChildReadsStdin,    nullptr);
	FPlatformProcess::ClosePipe(nullptr,            ChildWritesStdout);
	FPlatformProcess::ClosePipe(nullptr,            ChildWritesStderr);

	UE_LOG(LogUECPACP, Log, TEXT("[%s] spawned pid=%u: %s %s"),
		*Args.LogTag, Pid, *Args.Command, *Parms);

	State = EState::Initializing;
	SendInitialize();
	return true;
}

bool FUECPACPSession::Prompt(const FString& UserText)
{
	if (State != EState::Ready)
	{
		UE_LOG(LogUECPACP, Warning, TEXT("[%s] Prompt rejected — state=%d"), *Args.LogTag, (int32)State);
		return false;
	}
	if (SessionId.IsEmpty())
	{
		UE_LOG(LogUECPACP, Warning, TEXT("[%s] Prompt rejected — no sessionId"), *Args.LogTag);
		return false;
	}

	const int64 Id = Codec.NextRequestId();
	CurrentPromptId = Id;
	State = EState::Prompting;
	SilentSinceSec  = FPlatformTime::Seconds();
	bSilenceWarned  = false;
	UE_LOG(LogUECPACP, Verbose, TEXT("[%s] silence detector armed (will warn at +15s of total silence)"), *Args.LogTag);

	const FString Frame = FUECPACPJsonRpc::FormatRequest(
		Id, TEXT("session/prompt"),
		UECPACPSchema::BuildSessionPromptParams(SessionId, UserText));

	Pending.Add(Id, [this](const TSharedPtr<FJsonObject>& Result)
	{
		HandlePromptResult(Result);
	});

	if (PendingPrefsIds.Num() > 0)
	{
		QueuedPromptFrame = Frame;
		QueuedPromptId    = Id;
		UE_LOG(LogUECPACP, Log,
			TEXT("[%s] queueing session/prompt id=%lld frameLen=%d — awaiting %d prefs ack(s)"),
			*Args.LogTag, Id, Frame.Len(), PendingPrefsIds.Num());
		return true;
	}

	UE_LOG(LogUECPACP, Log, TEXT("[%s] dispatching session/prompt id=%lld frameLen=%d"),
		*Args.LogTag, Id, Frame.Len());

	TWeakPtr<FUECPACPSession> WeakSelf = AsWeak();
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
		[WeakSelf, Frame, LogTag = Args.LogTag, Id]()
	{
		TSharedPtr<FUECPACPSession> Self = WeakSelf.Pin();
		if (!Self || !Self->StdinWrite) return;

		const bool bOk = FPlatformProcess::WritePipe(Self->StdinWrite, Frame);
		if (bOk)
		{
			UE_LOG(LogUECPACP, Log, TEXT("[%s] session/prompt written ok id=%lld"), *LogTag, Id);
		}
		else
		{
			UE_LOG(LogUECPACP, Warning, TEXT("[%s] session/prompt WritePipe failed id=%lld"), *LogTag, Id);
		}
	});

	return true;
}

bool FUECPACPSession::PromptRich(const TArray<UECPACPSchema::FPromptContentBlock>& Blocks)
{
	if (State != EState::Ready)
	{
		UE_LOG(LogUECPACP, Warning, TEXT("[%s] PromptRich rejected — state=%d"), *Args.LogTag, (int32)State);
		return false;
	}
	if (SessionId.IsEmpty() || Blocks.Num() == 0) return false;

	const int64 Id = Codec.NextRequestId();
	CurrentPromptId = Id;
	State = EState::Prompting;
	SilentSinceSec  = FPlatformTime::Seconds();
	bSilenceWarned  = false;
	UE_LOG(LogUECPACP, Verbose, TEXT("[%s] silence detector armed (will warn at +15s of total silence)"), *Args.LogTag);

	const FString Frame = FUECPACPJsonRpc::FormatRequest(
		Id, TEXT("session/prompt"),
		UECPACPSchema::BuildSessionPromptParamsRich(SessionId, Blocks));

	Pending.Add(Id, [this](const TSharedPtr<FJsonObject>& Result)
	{
		HandlePromptResult(Result);
	});

	int32 ImageBlocks = 0;
	for (const auto& B : Blocks) if (B.Type == TEXT("image")) ImageBlocks++;

	if (PendingPrefsIds.Num() > 0)
	{
		QueuedPromptFrame = Frame;
		QueuedPromptId    = Id;
		UE_LOG(LogUECPACP, Log,
			TEXT("[%s] queueing session/prompt (rich) id=%lld blocks=%d images=%d — awaiting %d prefs ack(s)"),
			*Args.LogTag, Id, Blocks.Num(), ImageBlocks, PendingPrefsIds.Num());
		return true;
	}

	UE_LOG(LogUECPACP, Log, TEXT("[%s] dispatching session/prompt (rich) id=%lld blocks=%d images=%d frameLen=%d"),
		*Args.LogTag, Id, Blocks.Num(), ImageBlocks, Frame.Len());

	TWeakPtr<FUECPACPSession> WeakSelf = AsWeak();
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
		[WeakSelf, Frame, LogTag = Args.LogTag, Id]()
	{
		TSharedPtr<FUECPACPSession> Self = WeakSelf.Pin();
		if (!Self || !Self->StdinWrite) return;
		const bool bOk = FPlatformProcess::WritePipe(Self->StdinWrite, Frame);
		UE_LOG(LogUECPACP, Log, TEXT("[%s] session/prompt (rich) write %s id=%lld"),
			*LogTag, bOk ? TEXT("ok") : TEXT("FAILED"), Id);
	});
	return true;
}

void FUECPACPSession::ApplyUserPrefs(const FString& ModeId, const FString& ModelId,
	const TArray<TPair<FString, FString>>& OtherPrefs)
{
	if (SessionId.IsEmpty()) return;
	if (State != EState::Ready && State != EState::Prompting) return;

	if (!ModeId.IsEmpty())
	{
		const int64 Id = Codec.NextRequestId();
		const FString Frame = FUECPACPJsonRpc::FormatRequest(
			Id, TEXT("session/set_mode"),
			UECPACPSchema::BuildSessionSetModeParams(SessionId, ModeId));
		Pending.Add(Id, [this, ModeId](const TSharedPtr<FJsonObject>&)
		{
			UE_LOG(LogUECPACP, Log, TEXT("[%s] session/set_mode applied: %s"), *Args.LogTag, *ModeId);
		});
		PendingPrefsIds.Add(Id);
		WriteFrame(Frame);
	}

	if (!ModelId.IsEmpty())
	{
		const int64 Id = Codec.NextRequestId();
		const FString Frame = FUECPACPJsonRpc::FormatRequest(
			Id, TEXT("session/set_model"),
			UECPACPSchema::BuildSessionSetModelParams(SessionId, ModelId));
		Pending.Add(Id, [this, ModelId](const TSharedPtr<FJsonObject>&)
		{
			UE_LOG(LogUECPACP, Log, TEXT("[%s] session/set_model applied: %s"), *Args.LogTag, *ModelId);
		});
		PendingPrefsIds.Add(Id);
		WriteFrame(Frame);
	}

	for (const TPair<FString, FString>& Pref : OtherPrefs)
	{
		const FString& OptionId = Pref.Key;
		const FString& Value    = Pref.Value;
		if (OptionId.IsEmpty() || Value.IsEmpty()) continue;

		const FString Method = FString::Printf(TEXT("session/set_%s"), *OptionId);
		const int64 Id = Codec.NextRequestId();
		const FString Frame = FUECPACPJsonRpc::FormatRequest(Id, *Method,
			UECPACPSchema::BuildSessionSetOptionParams(SessionId, OptionId, Value));
		Pending.Add(Id, [this, OptionId, Value, Method](const TSharedPtr<FJsonObject>&)
		{
			UE_LOG(LogUECPACP, Log, TEXT("[%s] %s applied: %s=%s"),
				*Args.LogTag, *Method, *OptionId, *Value);
		});
		PendingPrefsIds.Add(Id);
		WriteFrame(Frame);
	}
}

void FUECPACPSession::Cancel()
{
	if (State != EState::Prompting) return;
	if (SessionId.IsEmpty()) return;

	const FString Frame = FUECPACPJsonRpc::FormatNotification(
		TEXT("session/cancel"),
		UECPACPSchema::BuildSessionCancelParams(SessionId));
	WriteFrame(Frame);

	for (auto& Pair : PendingPermissionIds)
	{
		const FString Reply = FUECPACPJsonRpc::FormatResult(Pair.Value,
			UECPACPSchema::BuildPermissionResultCancelled());
		WriteFrame(Reply);
	}
	PendingPermissionIds.Empty();

}

void FUECPACPSession::Stop()
{
	if (State == EState::Closed || State == EState::Closing) return;
	State = EState::Closing;

	if (StdinWrite)
	{
		FPlatformProcess::ClosePipe(nullptr, StdinWrite);
		StdinWrite = nullptr;
	}

	if (Process.IsValid())
	{
		FProcHandle Handed = Process;
		Process.Reset();
		const FString LogTag = Args.LogTag;
		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Handed, LogTag]() mutable
		{
			const double Deadline = FPlatformTime::Seconds() + 3.0;
			while (FPlatformProcess::IsProcRunning(Handed))
			{
				if (FPlatformTime::Seconds() > Deadline) break;
				FPlatformProcess::Sleep(0.05f);
			}
			if (FPlatformProcess::IsProcRunning(Handed))
			{
				FPlatformProcess::TerminateProc(Handed,  true);
			}
			int32 ExitCode = 0;
			FPlatformProcess::GetProcReturnCode(Handed, &ExitCode);
			FPlatformProcess::CloseProc(Handed);
			UE_LOG(LogUECPACP, Log, TEXT("[%s] subprocess closed async (exit=%d)"), *LogTag, ExitCode);
		});
	}

	EnterClosed();
}

bool FUECPACPSession::Tick()
{
	if (State == EState::Uninit || State == EState::Closed) return false;

	if (Process.IsValid() && !FPlatformProcess::IsProcRunning(Process))
	{
		EnterClosed();
		return false;
	}

	const double TickStart = FPlatformTime::Seconds();
	int32 LinesProcessed = 0;
	int32 BytesProcessed = 0;
	const int32 kMaxLinesPerTick = 256;
	const int32 kMaxBytesPerTick = 32 * 1024;

	if (StdoutRead)
	{
		const FString Chunk = FPlatformProcess::ReadPipe(StdoutRead);
		if (!Chunk.IsEmpty())
		{
			Codec.AppendFString(Chunk);
			SilentSinceSec  = -1.0;
			bSilenceWarned  = false;
		}

		FString Line;
		while (Codec.TakeLine(Line))
		{
			if (Line.IsEmpty()) continue;

			TSharedPtr<FJsonObject> Root;
			if (!FUECPACPJsonRpc::ParseFrame(Line, Root))
			{
				const bool bCouldBeJson = !Line.IsEmpty() &&
					(Line[0] == TEXT('{') || Line[0] == TEXT('['));
				if (bCouldBeJson)
				{
					UE_LOG(LogUECPACP, Warning, TEXT("[%s] malformed JSON frame (len=%d): %s"), *Args.LogTag, Line.Len(), *Line.Left(256));
				}
				else
				{
					const ELogVerbosity::Type Sev = (NonJsonStdoutCount < 10)
						? ELogVerbosity::Log : ELogVerbosity::Verbose;
					FMsg::Logf(__FILE__, __LINE__, LogUECPACP.GetCategoryName(), Sev,
						TEXT("[%s] stdout (non-JSON, len=%d): %s"),
						*Args.LogTag, Line.Len(), *Line.Left(200));
					++NonJsonStdoutCount;
				}
			}
			else if (FUECPACPJsonRpc::IsResponse(Root))           HandleResponseFrame(Root);
			else if (FUECPACPJsonRpc::IsRequest(Root))            HandleRequestFrame(Root);
			else if (FUECPACPJsonRpc::IsNotification(Root))       HandleNotificationFrame(Root);
			else
			{
				UE_LOG(LogUECPACP, Warning, TEXT("[%s] frame not request/notif/response: %s"),
					*Args.LogTag, *Line.Left(256));
			}

			++LinesProcessed;
			BytesProcessed += Line.Len();
			if (LinesProcessed >= kMaxLinesPerTick || BytesProcessed >= kMaxBytesPerTick)
			{
				break;
			}
		}
	}

	if (State == EState::Prompting && Codec.PendingBytes() > 0)
	{
		++PartialLineTickCount;
		if (!bPartialLineWarned && PartialLineTickCount >= 50)
		{
			UE_LOG(LogUECPACP, Log,
				TEXT("[%s] stdout: %d bytes buffered without a newline for ~5s — agent likely block-buffering, waiting for LLM response."),
				*Args.LogTag, Codec.PendingBytes());
			bPartialLineWarned = true;
		}
	}
	else if (State == EState::Prompting && SilentSinceSec >= 0.0 && !bSilenceWarned)
	{
		const double SilentFor = FPlatformTime::Seconds() - SilentSinceSec;
		if (SilentFor >= 15.0)
		{
			UE_LOG(LogUECPACP, Warning,
				TEXT("[%s] no output for %.0fs after session/prompt — agent's LLM/gateway call is likely hanging. "
				     "If using Kilo with a `*-auto/*` model, try selecting a specific provider model in Settings. "
				     "If using a custom-provider model, check your API-key quota and provider status."),
				*Args.LogTag, SilentFor);
			bSilenceWarned = true;
		}
	}
	else
	{
		PartialLineTickCount = 0;
	}

	if (StderrRead)
	{
		const FString ErrChunk = FPlatformProcess::ReadPipe(StderrRead);
		if (!ErrChunk.IsEmpty())
		{
			SilentSinceSec  = -1.0;
			bSilenceWarned  = false;
			StderrBuffer += ErrChunk;
			int32 NewlineIdx = INDEX_NONE;
			while (StderrBuffer.FindChar(TEXT('\n'), NewlineIdx))
			{
				FString Line = StderrBuffer.Left(NewlineIdx);
				StderrBuffer.RightChopInline(NewlineIdx + 1, EAllowShrinking::No);
				Line.TrimEndInline();
				if (Line.IsEmpty()) continue;
				UE_LOG(LogUECPACP, Log, TEXT("[%s] stderr: %s"), *Args.LogTag, *Line);
			}
		}
	}

	const double Elapsed = FPlatformTime::Seconds() - TickStart;
	if (Elapsed > 0.050)
	{
		UE_LOG(LogUECPACP, Warning,
			TEXT("[%s] Tick took %.0f ms (lines=%d bytes=%d) — game thread may stutter"),
			*Args.LogTag, Elapsed * 1000.0, LinesProcessed, BytesProcessed);
	}

	return true;
}

void FUECPACPSession::RespondToPermission(const FString& ToolCallId, const FString& OptionId)
{
	const TSharedPtr<FJsonValue>* IdPtr = PendingPermissionIds.Find(ToolCallId);
	if (!IdPtr || !IdPtr->IsValid())
	{
		UE_LOG(LogUECPACP, Warning, TEXT("[%s] RespondToPermission: no pending id for toolCallId='%s'"),
			*Args.LogTag, *ToolCallId);
		return;
	}

	const TSharedRef<FJsonObject> Result = OptionId.IsEmpty()
		? UECPACPSchema::BuildPermissionResultCancelled()
		: UECPACPSchema::BuildPermissionResultSelected(OptionId);

	const FString Frame = FUECPACPJsonRpc::FormatResult(*IdPtr, Result);
	WriteFrame(Frame);
	PendingPermissionIds.Remove(ToolCallId);
}

bool FUECPACPSession::WriteFrame(const FString& Line)
{
	if (!StdinWrite)
	{
		UE_LOG(LogUECPACP, Warning, TEXT("[%s] WriteFrame: stdin closed"), *Args.LogTag);
		return false;
	}
	const bool bOk = FPlatformProcess::WritePipe(StdinWrite, Line);
	if (!bOk) UE_LOG(LogUECPACP, Warning, TEXT("[%s] WritePipe failed"), *Args.LogTag);
	return bOk;
}

void FUECPACPSession::SendInitialize()
{
	const int64 Id = Codec.NextRequestId();
	const FString Frame = FUECPACPJsonRpc::FormatRequest(
		Id, TEXT("initialize"),
		UECPACPSchema::BuildInitializeParams(ClientName, ClientTitle, ClientVersion));
	Pending.Add(Id, [this](const TSharedPtr<FJsonObject>& Result)
	{
		HandleInitializeResult(Result);
	});
	WriteFrame(Frame);
}

void FUECPACPSession::SendSessionNew()
{
	const int64 Id = Codec.NextRequestId();
	const FString Cwd = Args.Cwd.IsEmpty() ? FPaths::ProjectDir() : Args.Cwd;
	const FString Frame = FUECPACPJsonRpc::FormatRequest(
		Id, TEXT("session/new"),
		UECPACPSchema::BuildSessionNewParams(Cwd, Args.McpSpecs));
	Pending.Add(Id, [this](const TSharedPtr<FJsonObject>& Result)
	{
		HandleSessionNewResult(Result);
	});
	WriteFrame(Frame);
}

void FUECPACPSession::HandleResponseFrame(const TSharedPtr<FJsonObject>& Root)
{
	double RawId = 0;
	Root->TryGetNumberField(TEXT("id"), RawId);
	const int64 Id = static_cast<int64>(RawId);

	TFunction<void(const TSharedPtr<FJsonObject>&)> Cb;
	Pending.RemoveAndCopyValue(Id, Cb);

	const bool bWasPref = PendingPrefsIds.Remove(Id) > 0;

	if (Root->HasField(TEXT("error")))
	{
		const TSharedPtr<FJsonObject>* ErrPtr = nullptr;
		Root->TryGetObjectField(TEXT("error"), ErrPtr);

		FString RawMessage, FullErr, UserMessage;
		double  ErrCode = 0;
		if (ErrPtr && ErrPtr->IsValid())
		{
			(*ErrPtr)->TryGetStringField(TEXT("message"), RawMessage);
			(*ErrPtr)->TryGetNumberField(TEXT("code"),    ErrCode);

			FString ErrBody;
			const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&ErrBody);
			FJsonSerializer::Serialize(ErrPtr->ToSharedRef(), W);
			W->Close();
			FullErr = MoveTemp(ErrBody);

			UserMessage = FormatJsonRpcErrorForUser(*ErrPtr);
		}

		const bool bMethodNotFound = (static_cast<int32>(ErrCode) == -32601);

		const bool bAuthRequired =
			(static_cast<int32>(ErrCode) == -32000) &&
			(RawMessage.Contains(TEXT("Authentication"), ESearchCase::IgnoreCase) ||
			 RawMessage.Contains(TEXT("auth required"),  ESearchCase::IgnoreCase));

		if (bMethodNotFound)
		{
			UE_LOG(LogUECPACP, Verbose, TEXT("[%s] JSON-RPC Method not found id=%lld msg='%s' (agent advertised but did not implement this session method)"),
				*Args.LogTag, Id, *RawMessage);
		}
		else
		{
			UE_LOG(LogUECPACP, Warning, TEXT("[%s] JSON-RPC error id=%lld msg='%s' body=%s"),
				*Args.LogTag, Id, *RawMessage, *FullErr);
		}

		if (bAuthRequired)
		{
			FString MethodId;
			if (ErrPtr && ErrPtr->IsValid())
			{
				const TSharedPtr<FJsonObject>* DataPtr = nullptr;
				if ((*ErrPtr)->TryGetObjectField(TEXT("data"), DataPtr) && DataPtr)
				{
					FString DataMsg;
					(*DataPtr)->TryGetStringField(TEXT("message"), DataMsg);
					const int32 AnchorPos = DataMsg.ToLower().Find(TEXT("methodid"));
					if (AnchorPos != INDEX_NONE)
					{
						const int32 OpenQuote = DataMsg.Find(TEXT("'"),
							ESearchCase::CaseSensitive, ESearchDir::FromStart, AnchorPos);
						if (OpenQuote != INDEX_NONE)
						{
							const int32 CloseQuote = DataMsg.Find(TEXT("'"),
								ESearchCase::CaseSensitive, ESearchDir::FromStart, OpenQuote + 1);
							if (CloseQuote != INDEX_NONE)
							{
								MethodId = DataMsg.Mid(OpenQuote + 1, CloseQuote - OpenQuote - 1);
							}
						}
					}
				}
			}
			if (OnAuthRequired.IsBound()) OnAuthRequired.Execute(MethodId);
		}
		if (Id == CurrentPromptId)
		{
			State = EState::Ready;
			CurrentPromptId = -1;
			EnterError(UserMessage.IsEmpty() ? RawMessage : UserMessage);
		}
		if (bWasPref) MaybeFlushQueuedPrompt();
		return;
	}

	const TSharedPtr<FJsonObject> Result = Root->GetObjectField(TEXT("result"));
	if (Cb) Cb(Result);
	if (bWasPref) MaybeFlushQueuedPrompt();
}

void FUECPACPSession::HandleRequestFrame(const TSharedPtr<FJsonObject>& Root)
{
	FString Method;
	Root->TryGetStringField(TEXT("method"), Method);
	const TSharedPtr<FJsonValue> Id = CloneIdValue(Root);
	const TSharedPtr<FJsonObject> Params = Root->GetObjectField(TEXT("params"));

	if (Method == TEXT("session/request_permission"))
	{
		HandlePermissionRequest(Id, Params);
	}
	else
	{
		HandleReverseRpcUnsupported(Id, Method);
	}
}

void FUECPACPSession::HandleNotificationFrame(const TSharedPtr<FJsonObject>& Root)
{
	FString Method;
	Root->TryGetStringField(TEXT("method"), Method);

	if (Method == TEXT("session/update"))
	{
		HandleSessionUpdate(Root->GetObjectField(TEXT("params")));
	}
	else
	{
		UE_LOG(LogUECPACP, Log, TEXT("[%s] unhandled notification method='%s'"), *Args.LogTag, *Method);
	}
}

void FUECPACPSession::HandleInitializeResult(const TSharedPtr<FJsonObject>& Result)
{
	if (!UECPACPSchema::ReadInitializeResult(Result, NegotiatedVersion, AgentName, AgentVersion))
	{
		EnterError(TEXT("initialize: agent did not accept protocolVersion 1"));
		return;
	}
	UE_LOG(LogUECPACP, Log, TEXT("[%s] initialize ok — agent=%s v=%s"),
		*Args.LogTag, *AgentName, *AgentVersion);

	if (Result.IsValid())
	{
		FString Body;
		const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Body);
		FJsonSerializer::Serialize(Result.ToSharedRef(), W);
		W->Close();
		UE_LOG(LogUECPACP, Verbose, TEXT("[%s] initialize result: %s"), *Args.LogTag, *Body.Left(2000));
	}

	SendSessionNew();
}

void FUECPACPSession::HandleSessionNewResult(const TSharedPtr<FJsonObject>& Result)
{
	FString NewSessionId;
	if (!UECPACPSchema::ReadSessionNewResult(Result, NewSessionId) || NewSessionId.IsEmpty())
	{
		EnterError(TEXT("session/new: no sessionId in result"));
		return;
	}
	SessionId = MoveTemp(NewSessionId);
	State = EState::Ready;
	UE_LOG(LogUECPACP, Log, TEXT("[%s] ready — sessionId=%s"), *Args.LogTag, *SessionId);

	if (Result.IsValid())
	{
		FString Body;
		const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Body);
		FJsonSerializer::Serialize(Result.ToSharedRef(), W);
		W->Close();
		UE_LOG(LogUECPACP, Verbose, TEXT("[%s] session/new result: %s"), *Args.LogTag, *Body.Left(4000));
	}

	UECPACPSchema::FConfigSnapshot Cfg;
	UECPACPSchema::ReadSessionNewConfig(Result, Cfg);
	UE_LOG(LogUECPACP, Log, TEXT("[%s] config captured — models=%d modes=%d"),
		*Args.LogTag, Cfg.Models.Num(), Cfg.Modes.Num());

	for (const UECPACPSchema::FConfigOption& M : Cfg.Modes)
	{
		UE_LOG(LogUECPACP, Verbose, TEXT("[%s]   mode: id=%s name=%s"),
			*Args.LogTag, *M.Id, *M.Name);
	}
	if (OnAgentConfig.IsBound()) OnAgentConfig.Execute(Cfg);
}

void FUECPACPSession::HandlePromptResult(const TSharedPtr<FJsonObject>& Result)
{
	FString StopReason;
	if (Result.IsValid()) Result->TryGetStringField(TEXT("stopReason"), StopReason);
	State = EState::Ready;
	CurrentPromptId = -1;
	SilentSinceSec  = -1.0;
	bSilenceWarned  = false;
	if (OnTurnComplete.IsBound()) OnTurnComplete.Execute(StopReason);
}

void FUECPACPSession::HandlePermissionRequest(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Params)
{
	FString ToolCallId, ToolTitle, ToolKind;
	TArray<TPair<FString, FString>> Options;
	if (!UECPACPSchema::ReadPermissionRequestParams(Params, ToolCallId, ToolTitle, ToolKind, Options))
	{
		UE_LOG(LogUECPACP, Warning, TEXT("[%s] permission request with no valid params; auto-cancelling"), *Args.LogTag);
		WriteFrame(FUECPACPJsonRpc::FormatResult(Id, UECPACPSchema::BuildPermissionResultCancelled()));
		return;
	}

	PendingPermissionIds.Add(ToolCallId, Id);
	if (OnPermissionRequest.IsBound())
	{
		OnPermissionRequest.Execute(ToolCallId, ToolTitle, ToolKind, Options);
	}
	else
	{
		FString Chosen;
		for (const TPair<FString, FString>& O : Options)
		{
			if (O.Value == TEXT("allow_once")) { Chosen = O.Key; break; }
		}
		if (Chosen.IsEmpty())
		{
			for (const TPair<FString, FString>& O : Options)
			{
				if (O.Value == TEXT("allow_always")) { Chosen = O.Key; break; }
			}
		}
		RespondToPermission(ToolCallId, Chosen);
	}
}

void FUECPACPSession::HandleReverseRpcUnsupported(const TSharedPtr<FJsonValue>& Id, const FString& Method)
{
	UE_LOG(LogUECPACP, Verbose, TEXT("[%s] reverse RPC '%s' not implemented; replying method-not-found"),
		*Args.LogTag, *Method);
	const FString Frame = FUECPACPJsonRpc::FormatError(Id, -32601, TEXT("method not implemented"));
	WriteFrame(Frame);
}

void FUECPACPSession::HandleSessionUpdate(const TSharedPtr<FJsonObject>& NotifParams)
{
	FString Disc;
	TSharedPtr<FJsonObject> UpdateObj;
	if (!UECPACPSchema::ReadSessionUpdateDiscriminator(NotifParams, Disc, UpdateObj)) return;

	if (Disc == TEXT("tool_call") || Disc == TEXT("tool_call_update"))
	{
		FString FrameJson;
		const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&FrameJson);
		FJsonSerializer::Serialize(UpdateObj.ToSharedRef(), W);
		W->Close();
		UE_LOG(LogUECPACP, Log, TEXT("[%s] %s frame: %s"),
			*Args.LogTag, *Disc, *FrameJson.Left(800));
	}

	TArray<FUECPACPStreamEvent> Events;
	UECPACPEventMapper::Map(UpdateObj, Events);

	for (FUECPACPStreamEvent& E : Events)
	{
		if (E.ToolCallId.IsEmpty()) continue;
		const bool bIsToolEvent =
			E.Kind == EUECPACPStreamEventKind::ToolStart  ||
			E.Kind == EUECPACPStreamEventKind::ToolUpdate ||
			E.Kind == EUECPACPStreamEventKind::ToolResult;
		if (!bIsToolEvent) continue;

		if (!E.ToolRawInputJson.IsEmpty())
		{
			CapturedToolArgs.Add(E.ToolCallId, { E.ToolAction, E.ToolRawInputJson });
		}

		if (E.Kind == EUECPACPStreamEventKind::ToolResult && E.ToolRawInputJson.IsEmpty())
		{
			if (const TPair<FString, FString>* Cached = CapturedToolArgs.Find(E.ToolCallId))
			{
				if (E.ToolAction.IsEmpty())     E.ToolAction       = Cached->Key;
				if (E.ToolRawInputJson.IsEmpty()) E.ToolRawInputJson = Cached->Value;
			}
		}

		if (E.Kind == EUECPACPStreamEventKind::ToolResult)
		{
			CapturedToolArgs.Remove(E.ToolCallId);
		}
	}

	for (const FUECPACPStreamEvent& E : Events)
	{
		if (OnStreamEvent.IsBound()) OnStreamEvent.Execute(E);
	}
}

void FUECPACPSession::MaybeFlushQueuedPrompt()
{
	if (PendingPrefsIds.Num() > 0) return;
	if (QueuedPromptFrame.IsEmpty()) return;

	const FString Frame = MoveTemp(QueuedPromptFrame);
	const int64   Id    = QueuedPromptId;
	QueuedPromptFrame.Reset();
	QueuedPromptId = -1;

	SilentSinceSec = FPlatformTime::Seconds();
	bSilenceWarned = false;

	UE_LOG(LogUECPACP, Log,
		TEXT("[%s] flushing queued session/prompt id=%lld frameLen=%d (all prefs acked)"),
		*Args.LogTag, Id, Frame.Len());

	TWeakPtr<FUECPACPSession> WeakSelf = AsWeak();
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
		[WeakSelf, Frame, LogTag = Args.LogTag, Id]()
	{
		TSharedPtr<FUECPACPSession> Self = WeakSelf.Pin();
		if (!Self || !Self->StdinWrite) return;
		const bool bOk = FPlatformProcess::WritePipe(Self->StdinWrite, Frame);
		if (bOk)
		{
			UE_LOG(LogUECPACP, Log, TEXT("[%s] session/prompt written ok id=%lld"), *LogTag, Id);
		}
		else
		{
			UE_LOG(LogUECPACP, Warning, TEXT("[%s] session/prompt WritePipe failed id=%lld"), *LogTag, Id);
		}
	});
}

void FUECPACPSession::EnterError(const FString& Message)
{
	UE_LOG(LogUECPACP, Error, TEXT("[%s] %s"), *Args.LogTag, *Message);
	if (OnError.IsBound()) OnError.Execute(Message);
}

void FUECPACPSession::EnterClosed()
{
	if (State == EState::Closed) return;

	int32 ExitCode = 0;
	if (Process.IsValid())
	{
		FPlatformProcess::GetProcReturnCode(Process, &ExitCode);
		FPlatformProcess::CloseProc(Process);
		Process.Reset();
	}

	if (StdinWrite) { FPlatformProcess::ClosePipe(nullptr, StdinWrite); StdinWrite = nullptr; }
	if (StdoutRead) { FPlatformProcess::ClosePipe(StdoutRead, nullptr); StdoutRead = nullptr; }
	if (StderrRead) { FPlatformProcess::ClosePipe(StderrRead, nullptr); StderrRead = nullptr; }

	Pending.Empty();
	PendingPermissionIds.Empty();
	PendingPrefsIds.Empty();
	QueuedPromptFrame.Reset();
	QueuedPromptId = -1;

	UE_LOG(LogUECPACP, Log, TEXT("[%s] session closed (exit=%d)"), *Args.LogTag, ExitCode);
	State = EState::Closed;
	if (OnClosed.IsBound()) OnClosed.Execute(ExitCode);
}
