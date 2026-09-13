// Copyright 2026, BlueprintsLab, All rights reserved

#include "HttpSseServer.h"

#include "MCPToolsLog.h"
#include "../Auth/SessionTokenStore.h"
#include "../Protocol/McpProtocol.h"
#include "../Protocol/McpToolsCatalog.h"
#include "../Util/PortProbe.h"

#include "Async/Async.h"
#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"

namespace
{
	constexpr const TCHAR* kAuthScheme = TEXT("Bearer ");

	TUniquePtr<FHttpServerResponse> MakeJsonResponse(int32 Code, const FString& Body)
	{
		TUniquePtr<FHttpServerResponse> R = FHttpServerResponse::Create(Body, TEXT("application/json"));
		R->Code = (EHttpServerResponseCodes)Code;
		R->Headers.Add(TEXT("Access-Control-Allow-Origin"),  { TEXT("*") });
		R->Headers.Add(TEXT("Access-Control-Allow-Headers"), { TEXT("Authorization, Content-Type") });
		R->Headers.Add(TEXT("Access-Control-Allow-Methods"), { TEXT("POST, GET, OPTIONS") });
		return R;
	}

	FString GetHeader(const FHttpServerRequest& Req, const FString& Name)
	{
		const FString Key = Name.ToLower();
		if (const TArray<FString>* Values = Req.Headers.Find(Key))
		{
			if (Values->Num() > 0) return (*Values)[0];
		}
		return FString();
	}
}

FHttpSseServer::FHttpSseServer(int32 InBasePort, int32 InMaxAttempts)
	: BasePort(InBasePort)
	, MaxAttempts(InMaxAttempts)
{}

bool FHttpSseServer::Start()
{
	FHttpServerModule& Server = FHttpServerModule::Get();

	for (int32 Attempt = 0; Attempt < MaxAttempts; ++Attempt)
	{
		const int32 Port = BasePort + Attempt;

		if (UECP::IsLocalPortHeld(Port))
		{
			continue;
		}

		Router = Server.GetHttpRouter((uint32)Port,  true);
		if (Router.IsValid())
		{
			BoundPort = Port;
			break;
		}
	}

	if (!Router.IsValid())
	{
		UE_LOG(LogMCPTool, Warning,
			TEXT("HttpSseServer: failed to bind any port in %d-%d"),
			BasePort, BasePort + MaxAttempts - 1);
		return false;
	}

	McpRoute = Router->BindRoute(
		FHttpPath(TEXT("/mcp")),
		EHttpServerRequestVerbs::VERB_POST | EHttpServerRequestVerbs::VERB_OPTIONS,
		FHttpRequestHandler::CreateRaw(this, &FHttpSseServer::HandleMcpRequest));

	SseRoute = Router->BindRoute(
		FHttpPath(TEXT("/mcp/sse")),
		EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateRaw(this, &FHttpSseServer::HandleSseRequest));

	HealthRoute = Router->BindRoute(
		FHttpPath(TEXT("/mcp/health")),
		EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateRaw(this, &FHttpSseServer::HandleHealthRequest));

	Server.StartAllListeners();
	UE_LOG(LogMCPTool, Log,
		TEXT("HttpSseServer: bound :%d  endpoints=/mcp,/mcp/sse,/mcp/health"),
		BoundPort);
	return true;
}

void FHttpSseServer::Stop()
{
	if (Router.IsValid())
	{
		if (McpRoute.IsValid())    Router->UnbindRoute(McpRoute);
		if (SseRoute.IsValid())    Router->UnbindRoute(SseRoute);
		if (HealthRoute.IsValid()) Router->UnbindRoute(HealthRoute);
		McpRoute.Reset();
		SseRoute.Reset();
		HealthRoute.Reset();
		Router.Reset();
	}
	BoundPort = 0;
}

bool FHttpSseServer::ValidateBearer(const FHttpServerRequest& Req, const FHttpResultCallback& OnComplete) const
{
	const FString Header = GetHeader(Req, TEXT("Authorization"));
	if (!Header.StartsWith(kAuthScheme))
	{
		OnComplete(MakeJsonResponse(401,
			TEXT("{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32001,\"message\":\"Missing Authorization: Bearer header\"}}")));
		return false;
	}

	const FString Token = Header.RightChop(FCString::Strlen(kAuthScheme)).TrimStartAndEnd();
	if (!FSessionTokenStore::Get().ValidateBearer(Token))
	{
		OnComplete(MakeJsonResponse(401,
			TEXT("{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32001,\"message\":\"Invalid bearer token. Copy a fresh one from Settings → MCP after editor restart.\"}}")));
		return false;
	}

	return true;
}

bool FHttpSseServer::HandleMcpRequest(const FHttpServerRequest& Req, const FHttpResultCallback& OnComplete)
{
	if (Req.Verb == EHttpServerRequestVerbs::VERB_OPTIONS)
	{
		OnComplete(MakeJsonResponse(204, TEXT("")));
		return true;
	}

	if (!ValidateBearer(Req, OnComplete))
	{
		return true;
	}

	FString JsonBody;
	if (Req.Body.Num() > 0)
	{
		FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(Req.Body.GetData()), Req.Body.Num());
		JsonBody = FString(Conv.Length(), Conv.Get());
	}

	{
		static int32 LoggedCount = 0;
		const ELogVerbosity::Type Sev = (LoggedCount < 50)
			? ELogVerbosity::Log : ELogVerbosity::Verbose;
		FMsg::Logf(__FILE__, __LINE__, LogMCPTool.GetCategoryName(), Sev,
			TEXT("MCP HTTP request: bytes=%d body[0..200]=%s"),
			Req.Body.Num(), *JsonBody.Left(200));
		++LoggedCount;
	}

	const FString CrewChatId    = GetHeader(Req, TEXT("X-Crew-Chat-Id"));
	const FString CrewChatToken = GetHeader(Req, TEXT("X-Crew-Chat-Token"));

	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
		[Body = MoveTemp(JsonBody), OnComplete, CrewChatId, CrewChatToken]() mutable
		{
			FMcpInvocationContext Ctx;
			Ctx.CallerChatId    = CrewChatId;
			Ctx.CallerChatToken = CrewChatToken;
			FString ResponseBody = FMcpProtocol::HandleMessage(Body, Ctx);

			AsyncTask(ENamedThreads::GameThread,
				[OnComplete, ResponseBody = MoveTemp(ResponseBody)]() mutable
				{
					if (ResponseBody.IsEmpty())
					{
						OnComplete(MakeJsonResponse(204, TEXT("")));
						return;
					}
					OnComplete(MakeJsonResponse(200, ResponseBody));
				});
		});

	return true;
}

bool FHttpSseServer::HandleSseRequest(const FHttpServerRequest& Req, const FHttpResultCallback& OnComplete)
{
	if (!ValidateBearer(Req, OnComplete))
	{
		return true;
	}

	TUniquePtr<FHttpServerResponse> R = MakeJsonResponse(501,
		TEXT("{\"error\":\"SSE not implemented; this server pushes no notifications today.\"}"));
	R->Headers.Add(TEXT("Content-Type"), { TEXT("application/json") });
	OnComplete(MoveTemp(R));
	return true;
}

bool FHttpSseServer::HandleHealthRequest(const FHttpServerRequest& , const FHttpResultCallback& OnComplete)
{
	const FString Body = FString::Printf(
		TEXT("{\"server\":\"%s\",\"version\":\"%s\",\"protocolVersion\":\"%s\",\"port\":%d}"),
		FMcpProtocol::ServerName,
		FMcpProtocol::ServerVersion,
		FMcpProtocol::ProtocolVersion,
		BoundPort);

	OnComplete(MakeJsonResponse(200, Body));
	return true;
}
