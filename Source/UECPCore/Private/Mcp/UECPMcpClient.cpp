// Copyright 2026, BlueprintsLab, All rights reserved

#include "Mcp/UECPMcpClient.h"

#include "HAL/PlatformTime.h"
#include "Misc/CString.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "HttpManager.h"
#include "Async/Async.h"

#include "UECPMcpOAuthClient.h"
#include "UECPMcpOAuthTokenStore.h"

DEFINE_LOG_CATEGORY(LogUECPMcpClient);

namespace
{
	const FString& McpProtocolVersion()
	{
		static const FString V = TEXT("2025-06-18");
		return V;
	}

	void ApplyOAuthBearer(const FUECPMcpServerSpec& Spec,
		const TSharedRef<IHttpRequest, ESPMode::ThreadSafe>& Req)
	{
		if (!Spec.bUseOAuth || Spec.OAuthKey.IsEmpty()) return;
		if (!FUECPMcpOAuthClient::EnsureFreshToken(Spec.OAuthKey)) return;
		FUECPMcpOAuthRecord Rec;
		if (FUECPMcpOAuthTokenStore::Get().GetRecord(Spec.OAuthKey, Rec) && Rec.HasAccessToken())
		{
			Req->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Rec.AccessToken);
		}
	}

	FString JsonStr(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj, W);
		W->Close();
		return Out;
	}

	TSharedRef<FJsonObject> BuildInitializeParams()
	{
		const TSharedRef<FJsonObject> ClientInfo = MakeShared<FJsonObject>();
		ClientInfo->SetStringField(TEXT("name"), TEXT("uecp"));
		ClientInfo->SetStringField(TEXT("version"), TEXT("1.0.0"));

		const TSharedRef<FJsonObject> Caps = MakeShared<FJsonObject>();

		const TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("protocolVersion"), McpProtocolVersion());
		P->SetObjectField(TEXT("capabilities"), Caps);
		P->SetObjectField(TEXT("clientInfo"), ClientInfo);
		return P;
	}
}

FUECPMcpClient::~FUECPMcpClient()
{
	if (State != EState::Closed)
	{
		Stop();
	}
}

bool FUECPMcpClient::Start(const FUECPMcpServerSpec& InSpec, FString& OutError, float InitTimeoutSec)
{
	if (State != EState::Uninit)
	{
		OutError = TEXT("Already started");
		return false;
	}
	Spec = InSpec;
	if (Spec.LogTag.IsEmpty()) Spec.LogTag = TEXT("mcp");

	if (Spec.IsHttp())
	{
		if (!Spec.Url.StartsWith(TEXT("http://")) && !Spec.Url.StartsWith(TEXT("https://")))
		{
			OutError = FString::Printf(TEXT("HTTP URL must start with http:// or https:// (got %s)"), *Spec.Url);
			State = EState::Closed;
			return false;
		}
	}
	else
	{
		if (!SpawnProcess(OutError))
		{
			State = EState::Closed;
			return false;
		}
	}

	State = EState::Initializing;
	if (!RoundTripInitialize(InitTimeoutSec, OutError))
	{
		Stop();
		return false;
	}

	State = EState::FetchingTools;
	if (!RoundTripToolsList(InitTimeoutSec, OutError))
	{
		Stop();
		return false;
	}

	State = EState::Ready;
	UE_LOG(LogUECPMcpClient, Log, TEXT("[%s] connected — %s v%s, %d tools"),
		*Spec.LogTag, *ServerName, *ServerVersion, CachedTools.Num());
	return true;
}

void FUECPMcpClient::StartAsync(const FUECPMcpServerSpec& InSpec,
	TFunction<void(bool, FString)> OnComplete, float InitTimeoutSec)
{
	TSharedRef<FUECPMcpClient> Self = AsShared();
	Async(EAsyncExecution::ThreadPool,
		[Self, Spec = InSpec, OnComplete = MoveTemp(OnComplete), InitTimeoutSec]() mutable
	{
		FString Err;
		const bool bOk = Self->Start(Spec, Err, InitTimeoutSec);

		AsyncTask(ENamedThreads::GameThread,
			[bOk, Err = MoveTemp(Err), OnComplete = MoveTemp(OnComplete)]() mutable
		{
			OnComplete(bOk, MoveTemp(Err));
		});
	});
}

void FUECPMcpClient::Stop()
{
	if (State == EState::Closed) return;
	State = EState::Closing;

	if (!Spec.IsHttp())
	{
		if (StdinWrite)
		{
			FPlatformProcess::ClosePipe(nullptr, StdinWrite);
			StdinWrite = nullptr;
		}

		const double T0 = FPlatformTime::Seconds();
		while (Process.IsValid() && FPlatformProcess::IsProcRunning(Process)
			&& FPlatformTime::Seconds() - T0 < 1.5)
		{
			FPlatformProcess::Sleep(0.02f);
		}

		if (Process.IsValid())
		{
			if (FPlatformProcess::IsProcRunning(Process))
			{
				UE_LOG(LogUECPMcpClient, Warning, TEXT("[%s] terminating subprocess (graceful exit timed out)"), *Spec.LogTag);
				FPlatformProcess::TerminateProc(Process);
			}
			FPlatformProcess::CloseProc(Process);
			Process.Reset();
		}

		if (StdoutRead)
		{
			FPlatformProcess::ClosePipe(StdoutRead, nullptr);
			StdoutRead = nullptr;
		}
	}

	State = EState::Closed;
}

const FString& FUECPMcpClient::GetServerName() const
{
	return ServerName.IsEmpty() ? Spec.LogTag : ServerName;
}

bool FUECPMcpClient::SpawnProcess(FString& OutError)
{
	void* ParentWriteToStdin = nullptr;
	void* ParentReadsStdout  = nullptr;

	if (!FPlatformProcess::CreatePipe(ChildStdin, ParentWriteToStdin,  true))
	{
		OutError = TEXT("CreatePipe stdin failed");
		return false;
	}
	if (!FPlatformProcess::CreatePipe(ParentReadsStdout, ChildStdout))
	{
		OutError = TEXT("CreatePipe stdout failed");
		FPlatformProcess::ClosePipe(ChildStdin, ParentWriteToStdin);
		ChildStdin = nullptr;
		return false;
	}

	FString Parms;
	for (int32 i = 0; i < Spec.Args.Num(); ++i)
	{
		if (i > 0) Parms += TEXT(' ');
		const bool bNeedsQuotes = Spec.Args[i].Contains(TEXT(" "));
		if (bNeedsQuotes) Parms += TEXT('"');
		Parms += Spec.Args[i];
		if (bNeedsQuotes) Parms += TEXT('"');
	}

	const TCHAR* Cwd = Spec.Cwd.IsEmpty() ? nullptr : *Spec.Cwd;

	Process = FPlatformProcess::CreateProc(
		*Spec.Command, *Parms,
		   false,
		     true,
		 true,
		&Pid,
		  0,
		Cwd,
		ChildStdout,
		ChildStdin);

	if (!Process.IsValid())
	{
		OutError = FString::Printf(TEXT("CreateProc failed: %s %s"), *Spec.Command, *Parms);
		FPlatformProcess::ClosePipe(ChildStdin, ParentWriteToStdin);
		FPlatformProcess::ClosePipe(ParentReadsStdout, ChildStdout);
		ChildStdin = nullptr;
		ChildStdout = nullptr;
		return false;
	}

	StdinWrite = ParentWriteToStdin;
	StdoutRead = ParentReadsStdout;

	FPlatformProcess::ClosePipe(ChildStdin, nullptr);
	ChildStdin = nullptr;
	FPlatformProcess::ClosePipe(nullptr, ChildStdout);
	ChildStdout = nullptr;

	UE_LOG(LogUECPMcpClient, Log, TEXT("[%s] spawned pid=%u: %s %s"),
		*Spec.LogTag, Pid, *Spec.Command, *Parms);
	return true;
}

void FUECPMcpClient::DrainPipe()
{
	if (!StdoutRead) return;
	const FString Chunk = FPlatformProcess::ReadPipe(StdoutRead);
	if (!Chunk.IsEmpty())
	{
		Codec.AppendFString(Chunk);
	}
}

bool FUECPMcpClient::WriteFrame(const FString& Frame)
{
	if (!StdinWrite) return false;
	return FPlatformProcess::WritePipe(StdinWrite, Frame);
}

bool FUECPMcpClient::SendAndAwait(int64 Id, const FString& Frame, float TimeoutSec,
	TSharedPtr<FJsonObject>& OutResult, FString& OutError)
{
	return Spec.IsHttp()
		? SendAndAwaitHttp(Id, Frame, TimeoutSec, OutResult, OutError)
		: SendAndAwaitStdio(Id, Frame, TimeoutSec, OutResult, OutError);
}

bool FUECPMcpClient::SendAndAwaitStdio(int64 Id, const FString& Frame, float TimeoutSec,
	TSharedPtr<FJsonObject>& OutResult, FString& OutError)
{
	if (!WriteFrame(Frame))
	{
		OutError = TEXT("WritePipe failed");
		return false;
	}

	if (TSharedPtr<FJsonObject>* Stashed = PendingResponses.Find(Id))
	{
		OutResult = *Stashed;
		PendingResponses.Remove(Id);
		return true;
	}

	const double Deadline = FPlatformTime::Seconds() + TimeoutSec;
	while (FPlatformTime::Seconds() < Deadline)
	{
		if (Process.IsValid() && !FPlatformProcess::IsProcRunning(Process))
		{
			OutError = TEXT("Subprocess exited before responding");
			return false;
		}

		DrainPipe();

		FString Line;
		while (Codec.TakeLine(Line))
		{
			if (Line.IsEmpty()) continue;
			TSharedPtr<FJsonObject> Root;
			if (!FUECPMcpJsonRpc::ParseFrame(Line, Root) || !Root.IsValid())
			{
				UE_LOG(LogUECPMcpClient, Warning, TEXT("[%s] failed to parse frame: %s"),
					*Spec.LogTag, *Line.Left(200));
				continue;
			}
			if (FUECPMcpJsonRpc::IsResponse(Root))
			{
				double IncomingId = -1.0;
				if (Root->TryGetNumberField(TEXT("id"), IncomingId))
				{
					if (static_cast<int64>(IncomingId) == Id)
					{
						OutResult = Root;
						return true;
					}
					else
					{
						PendingResponses.Add(static_cast<int64>(IncomingId), Root);
					}
				}
			}
		}

		FPlatformProcess::Sleep(0.01f);
	}

	OutError = FString::Printf(TEXT("Timed out after %.1fs"), TimeoutSec);
	return false;
}

bool FUECPMcpClient::SendAndAwaitHttp(int64 Id, const FString& Frame, float TimeoutSec,
	TSharedPtr<FJsonObject>& OutResult, FString& OutError)
{
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetVerb(TEXT("POST"));
	Request->SetURL(Spec.Url);
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json, text/event-stream"));
	Request->SetHeader(TEXT("MCP-Protocol-Version"), McpProtocolVersion());
	if (!HttpSessionId.IsEmpty())
	{
		Request->SetHeader(TEXT("Mcp-Session-Id"), HttpSessionId);
	}
	for (const TPair<FString, FString>& H : Spec.Headers)
	{
		if (!H.Key.IsEmpty()) Request->SetHeader(H.Key, H.Value);
	}
	ApplyOAuthBearer(Spec, Request);
	Request->SetTimeout(TimeoutSec);
	Request->SetContentAsString(Frame);

	bool bDone = false;
	bool bOk = false;
	FString Body;
	int32 StatusCode = 0;
	FString CapturedSession;
	FString FailureReason;

	Request->OnProcessRequestComplete().BindLambda(
		[&](FHttpRequestPtr, FHttpResponsePtr Resp, bool bConnectedSuccessfully)
		{
			bDone = true;
			if (!bConnectedSuccessfully)
			{
				FailureReason = TEXT("connection failed (server not reachable / refused / aborted)");
				return;
			}
			if (!Resp.IsValid())
			{
				FailureReason = TEXT("no HTTP response");
				return;
			}
			StatusCode      = Resp->GetResponseCode();
			Body            = Resp->GetContentAsString();
			CapturedSession = Resp->GetHeader(TEXT("Mcp-Session-Id"));
			bOk             = StatusCode >= 200 && StatusCode < 300;
		});

	if (!Request->ProcessRequest())
	{
		OutError = FString::Printf(TEXT("HTTP request submit failed for %s"), *Spec.Url);
		return false;
	}

	const bool bOnGT = IsInGameThread();
	const double Deadline = FPlatformTime::Seconds() + TimeoutSec + 0.5;
	while (!bDone && FPlatformTime::Seconds() < Deadline)
	{
		if (bOnGT) FHttpModule::Get().GetHttpManager().Tick(0.01f);
		FPlatformProcess::Sleep(0.01f);
	}

	if (!bDone)
	{
		Request->CancelRequest();
		OutError = FString::Printf(TEXT("HTTP timed out after %.1fs talking to %s"), TimeoutSec, *Spec.Url);
		return false;
	}
	if (!bOk)
	{
		if (StatusCode == 0)
		{
			OutError = FString::Printf(TEXT("HTTP request to %s failed: %s"),
				*Spec.Url, FailureReason.IsEmpty() ? TEXT("unknown") : *FailureReason);
		}
		else
		{
			OutError = FString::Printf(TEXT("HTTP %d from %s%s%s"),
				StatusCode, *Spec.Url,
				Body.IsEmpty() ? TEXT("") : TEXT(" — "),
				*Body.Left(400));
		}
		return false;
	}

	if (!CapturedSession.IsEmpty() && HttpSessionId.IsEmpty())
	{
		HttpSessionId = CapturedSession;
	}

	const FString Trimmed = Body.TrimStartAndEnd();
	if (Trimmed.StartsWith(TEXT("{")) || Trimmed.StartsWith(TEXT("[")))
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Trimmed);
		TSharedPtr<FJsonObject> Root;
		if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
		{
			OutResult = Root;
			return true;
		}
	}

	TArray<FString> Lines;
	Body.ParseIntoArrayLines(Lines);
	for (const FString& Raw : Lines)
	{
		FString Line = Raw.TrimStartAndEnd();
		if (!Line.StartsWith(TEXT("data:"))) continue;
		Line.RightChopInline(5);
		Line.TrimStartInline();
		if (Line.IsEmpty() || Line == TEXT("[DONE]")) continue;

		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
		TSharedPtr<FJsonObject> Root;
		if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
		{
			double IncomingId = -1.0;
			if (Root->TryGetNumberField(TEXT("id"), IncomingId)
				&& static_cast<int64>(IncomingId) == Id)
			{
				OutResult = Root;
				return true;
			}
		}
	}

	OutError = FString::Printf(TEXT("HTTP response had no parseable JSON-RPC frame for id=%lld"), Id);
	return false;
}

bool FUECPMcpClient::RoundTripInitialize(float TimeoutSec, FString& OutError)
{
	const int64 Id = Codec.NextRequestId();
	const FString Frame = FUECPMcpJsonRpc::FormatRequest(
		Id, TEXT("initialize"), BuildInitializeParams());

	TSharedPtr<FJsonObject> Response;
	if (!SendAndAwait(Id, Frame, TimeoutSec, Response, OutError))
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* ErrObj = nullptr;
	if (Response->TryGetObjectField(TEXT("error"), ErrObj) && ErrObj && ErrObj->IsValid())
	{
		FString Msg;
		(*ErrObj)->TryGetStringField(TEXT("message"), Msg);
		OutError = FString::Printf(TEXT("initialize error: %s"), *Msg);
		return false;
	}

	const TSharedPtr<FJsonObject>* Result = nullptr;
	if (Response->TryGetObjectField(TEXT("result"), Result) && Result && Result->IsValid())
	{
		const TSharedPtr<FJsonObject>* ServerInfo = nullptr;
		if ((*Result)->TryGetObjectField(TEXT("serverInfo"), ServerInfo) && ServerInfo && ServerInfo->IsValid())
		{
			(*ServerInfo)->TryGetStringField(TEXT("name"),    ServerName);
			(*ServerInfo)->TryGetStringField(TEXT("version"), ServerVersion);
		}
	}

	const FString InitNotif = FUECPMcpJsonRpc::FormatNotification(
		TEXT("notifications/initialized"), nullptr);
	if (Spec.IsHttp())
	{
		const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> NotifReq = FHttpModule::Get().CreateRequest();
		NotifReq->SetVerb(TEXT("POST"));
		NotifReq->SetURL(Spec.Url);
		NotifReq->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		NotifReq->SetHeader(TEXT("Accept"), TEXT("application/json, text/event-stream"));
		NotifReq->SetHeader(TEXT("MCP-Protocol-Version"), McpProtocolVersion());
		if (!HttpSessionId.IsEmpty()) NotifReq->SetHeader(TEXT("Mcp-Session-Id"), HttpSessionId);
		for (const TPair<FString, FString>& H : Spec.Headers)
		{
			if (!H.Key.IsEmpty()) NotifReq->SetHeader(H.Key, H.Value);
		}
		ApplyOAuthBearer(Spec, NotifReq);
		NotifReq->SetContentAsString(InitNotif);
		NotifReq->ProcessRequest();
	}
	else
	{
		WriteFrame(InitNotif);
	}

	return true;
}

bool FUECPMcpClient::RoundTripToolsList(float TimeoutSec, FString& OutError)
{
	const int64 Id = Codec.NextRequestId();
	const FString Frame = FUECPMcpJsonRpc::FormatRequest(
		Id, TEXT("tools/list"), MakeShared<FJsonObject>());

	TSharedPtr<FJsonObject> Response;
	if (!SendAndAwait(Id, Frame, TimeoutSec, Response, OutError))
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* ErrObj = nullptr;
	if (Response->TryGetObjectField(TEXT("error"), ErrObj) && ErrObj && ErrObj->IsValid())
	{
		FString Msg;
		(*ErrObj)->TryGetStringField(TEXT("message"), Msg);
		OutError = FString::Printf(TEXT("tools/list error: %s"), *Msg);
		return false;
	}

	const TSharedPtr<FJsonObject>* Result = nullptr;
	if (!Response->TryGetObjectField(TEXT("result"), Result) || !Result || !Result->IsValid())
	{
		OutError = TEXT("tools/list response had no result");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* ToolsArr = nullptr;
	if (!(*Result)->TryGetArrayField(TEXT("tools"), ToolsArr) || !ToolsArr)
	{
		CachedTools.Reset();
		return true;
	}

	CachedTools.Reset(ToolsArr->Num());
	for (const TSharedPtr<FJsonValue>& V : *ToolsArr)
	{
		const TSharedPtr<FJsonObject> ToolObj = V.IsValid() ? V->AsObject() : nullptr;
		if (!ToolObj.IsValid()) continue;

		FUECPMcpToolDescriptor Desc;
		ToolObj->TryGetStringField(TEXT("name"), Desc.Name);
		ToolObj->TryGetStringField(TEXT("description"), Desc.Description);

		const TSharedPtr<FJsonObject>* Schema = nullptr;
		if (ToolObj->TryGetObjectField(TEXT("inputSchema"), Schema) && Schema && Schema->IsValid())
		{
			Desc.InputSchema = *Schema;
		}

		const TSharedPtr<FJsonObject>* AnnObj = nullptr;
		if (ToolObj->TryGetObjectField(TEXT("annotations"), AnnObj) && AnnObj && AnnObj->IsValid())
		{
			(*AnnObj)->TryGetBoolField(TEXT("readOnlyHint"),    Desc.bReadOnlyHint);
			(*AnnObj)->TryGetBoolField(TEXT("destructiveHint"), Desc.bDestructiveHint);
		}

		if (!Desc.Name.IsEmpty())
		{
			CachedTools.Add(MoveTemp(Desc));
		}
	}
	return true;
}

FUECPToolResult FUECPMcpClient::CallTool(const FString& ToolName,
	const TSharedPtr<FJsonObject>& Args, float CallTimeoutSec)
{
	FUECPToolResult R;
	if (State != EState::Ready)
	{
		R.bSuccess = false;
		R.ErrorMessage = TEXT("MCP client not Ready");
		return R;
	}

	const TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("name"), ToolName);
	Params->SetObjectField(TEXT("arguments"),
		Args.IsValid() ? Args : MakeShared<FJsonObject>());

	const int64 Id = Codec.NextRequestId();
	const FString Frame = FUECPMcpJsonRpc::FormatRequest(Id, TEXT("tools/call"), Params);

	TSharedPtr<FJsonObject> Response;
	FString WaitErr;
	if (!SendAndAwait(Id, Frame, CallTimeoutSec, Response, WaitErr))
	{
		R.bSuccess = false;
		R.ErrorMessage = FString::Printf(TEXT("tools/call '%s' failed: %s"), *ToolName, *WaitErr);
		return R;
	}

	const TSharedPtr<FJsonObject>* ErrObj = nullptr;
	if (Response->TryGetObjectField(TEXT("error"), ErrObj) && ErrObj && ErrObj->IsValid())
	{
		FString Msg;
		(*ErrObj)->TryGetStringField(TEXT("message"), Msg);
		R.bSuccess = false;
		R.ErrorMessage = FString::Printf(TEXT("MCP error: %s"), *Msg);
		return R;
	}

	const TSharedPtr<FJsonObject>* Result = nullptr;
	if (Response->TryGetObjectField(TEXT("result"), Result) && Result && Result->IsValid())
	{
		bool bIsError = false;
		if ((*Result)->TryGetBoolField(TEXT("isError"), bIsError) && bIsError)
		{
			R.bSuccess = false;
			FString ErrText;
			const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
			if ((*Result)->TryGetArrayField(TEXT("content"), Content) && Content)
			{
				for (const TSharedPtr<FJsonValue>& CV : *Content)
				{
					const TSharedPtr<FJsonObject> CO = CV.IsValid() ? CV->AsObject() : nullptr;
					if (!CO.IsValid()) continue;
					FString T;
					if (CO->TryGetStringField(TEXT("text"), T))
					{
						if (!ErrText.IsEmpty()) ErrText += TEXT(" ");
						ErrText += T;
					}
				}
			}
			R.ErrorMessage = ErrText.IsEmpty() ? TEXT("(MCP isError with no text)") : ErrText;
			return R;
		}

		R.bSuccess = true;
		R.ResultJson = JsonStr(Result->ToSharedRef());
		return R;
	}

	R.bSuccess = false;
	R.ErrorMessage = TEXT("MCP response had neither result nor error");
	return R;
}
