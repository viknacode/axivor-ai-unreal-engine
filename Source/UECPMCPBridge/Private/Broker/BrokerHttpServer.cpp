// Copyright 2026, BlueprintsLab, All rights reserved

#include "BrokerHttpServer.h"
#include "InstanceRegistryWatcher.h"

#include "MCPToolsLog.h"

#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"

namespace
{

	FString GetHeaderFirst(const FHttpServerRequest& Req, const FString& Name)
	{
		const FString Key = Name.ToLower();
		if (const TArray<FString>* Vals = Req.Headers.Find(Key))
		{
			if (Vals->Num() > 0) return (*Vals)[0];
		}
		return FString();
	}

	const TCHAR* VerbToString(EHttpServerRequestVerbs V)
	{
		switch (V)
		{
			case EHttpServerRequestVerbs::VERB_GET:    return TEXT("GET");
			case EHttpServerRequestVerbs::VERB_POST:   return TEXT("POST");
			case EHttpServerRequestVerbs::VERB_PUT:    return TEXT("PUT");
			case EHttpServerRequestVerbs::VERB_PATCH:  return TEXT("PATCH");
			case EHttpServerRequestVerbs::VERB_DELETE: return TEXT("DELETE");
			default:                                   return TEXT("GET");
		}
	}

	TUniquePtr<FHttpServerResponse> MakeJsonResponse(int32 Code, const FString& Body)
	{
		TUniquePtr<FHttpServerResponse> R = FHttpServerResponse::Create(Body, TEXT("application/json"));
		R->Code = (EHttpServerResponseCodes)Code;
		R->Headers.Add(TEXT("Access-Control-Allow-Origin"),  { TEXT("*") });
		R->Headers.Add(TEXT("Access-Control-Allow-Headers"), { TEXT("Authorization, Content-Type") });
		R->Headers.Add(TEXT("Access-Control-Allow-Methods"), { TEXT("POST, GET, OPTIONS") });
		return R;
	}
}

FBrokerHttpServer::FBrokerHttpServer(int32 InPort, const FInstanceRegistryWatcher* InRegistry)
	: Registry(InRegistry)
	, Port(InPort)
{}

bool FBrokerHttpServer::Start()
{
	FHttpServerModule& Server = FHttpServerModule::Get();
	Router = Server.GetHttpRouter((uint32)Port,  true);
	if (!Router.IsValid())
	{
		UE_LOG(LogMCPTool, Verbose, TEXT("Broker: port %d unavailable; another editor holds it"), Port);
		return false;
	}
	BoundPort = Port;

	const auto Preprocessor = FHttpRequestHandler::CreateLambda(
		[this](const FHttpServerRequest& Req, const FHttpResultCallback& OnComplete)
		{
			return this->HandleRequest(Req, OnComplete);
		});
	PreprocessorHandle = Router->RegisterRequestPreprocessor(Preprocessor);

	Server.StartAllListeners();
	UE_LOG(LogMCPTool, Log, TEXT("Broker: bound :%d  routing /uecp/<project>/..."), BoundPort);
	return true;
}

void FBrokerHttpServer::Stop()
{
	if (Router.IsValid())
	{
		if (PreprocessorHandle.IsValid())
		{
			Router->UnregisterRequestPreprocessor(PreprocessorHandle);
			PreprocessorHandle.Reset();
		}
		Router.Reset();
	}
	BoundPort = 0;
}

bool FBrokerHttpServer::HandleRequest(const FHttpServerRequest& Req, const FHttpResultCallback& OnComplete)
{
	const FString PathStr = Req.RelativePath.GetPath();

	if (Req.Verb == EHttpServerRequestVerbs::VERB_OPTIONS)
	{
		OnComplete(MakeJsonResponse(204, TEXT("")));
		return true;
	}

	if (PathStr == TEXT("/") || PathStr == TEXT("/health"))
	{
		HandleDiscovery(Req, OnComplete);
		return true;
	}

	const FString Prefix = TEXT("/uecp/");
	if (!PathStr.StartsWith(Prefix))
	{
		return false;
	}

	const FString After = PathStr.RightChop(Prefix.Len());
	int32 SlashIdx = INDEX_NONE;
	After.FindChar(TEXT('/'), SlashIdx);
	const FString ProjectName = (SlashIdx == INDEX_NONE) ? After : After.Left(SlashIdx);
	const FString RemainingPath = (SlashIdx == INDEX_NONE) ? FString() : After.RightChop(SlashIdx);

	if (ProjectName.IsEmpty() || !Registry)
	{
		OnComplete(MakeJsonResponse(404, TEXT("{\"error\":\"Project name missing or registry unavailable\"}")));
		return true;
	}

	const FBrokerInstanceEntry* Entry = Registry->FindByProject(ProjectName);
	if (!Entry)
	{
		OnComplete(MakeJsonResponse(404, FString::Printf(
			TEXT("{\"error\":\"No live UECP editor for project '%s'\"}"),
			*ProjectName)));
		return true;
	}

	const FString WorkerPath = RemainingPath.IsEmpty() ? TEXT("/mcp") : RemainingPath;
	const FString WorkerUrl  = FString::Printf(TEXT("http://localhost:%d%s"),
		Entry->McpHttpPort, *WorkerPath);

	ForwardToWorker(Req, OnComplete, WorkerUrl);
	return true;
}

void FBrokerHttpServer::HandleDiscovery(const FHttpServerRequest& , const FHttpResultCallback& OnComplete) const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("server"),     TEXT("uecp-broker"));
	Root->SetNumberField(TEXT("port"),       BoundPort);

	TArray<TSharedPtr<FJsonValue>> InstanceArr;
	if (Registry)
	{
		for (const TPair<FString, FBrokerInstanceEntry>& KV : Registry->GetEntries())
		{
			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("project_name"),  KV.Value.ProjectName);
			Obj->SetStringField(TEXT("project_path"),  KV.Value.ProjectPath);
			Obj->SetNumberField(TEXT("mcp_http_port"), KV.Value.McpHttpPort);
			Obj->SetNumberField(TEXT("pid"),           KV.Value.ProcessId);
			Obj->SetStringField(TEXT("started_at"),    KV.Value.StartedAt);
			Obj->SetStringField(TEXT("broker_url"),    FString::Printf(
				TEXT("http://localhost:%d/uecp/%s/mcp"), BoundPort, *KV.Value.ProjectName));
			InstanceArr.Add(MakeShared<FJsonValueObject>(Obj));
		}
	}
	Root->SetArrayField(TEXT("instances"), InstanceArr);

	FString Body;
	const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Body);
	FJsonSerializer::Serialize(Root, W);
	OnComplete(MakeJsonResponse(200, Body));
}

void FBrokerHttpServer::ForwardToWorker(const FHttpServerRequest& Req, const FHttpResultCallback& OnComplete,
	const FString& WorkerUrl)
{
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpReq = FHttpModule::Get().CreateRequest();
	HttpReq->SetURL(WorkerUrl);
	HttpReq->SetVerb(VerbToString(Req.Verb));

	for (const TPair<FString, TArray<FString>>& KV : Req.Headers)
	{
		if (KV.Key == TEXT("host") || KV.Key == TEXT("content-length")) continue;
		if (KV.Value.Num() == 0) continue;
		HttpReq->SetHeader(KV.Key, KV.Value[0]);
	}

	if (Req.Body.Num() > 0)
	{
		HttpReq->SetContent(Req.Body);
	}

	HttpReq->OnProcessRequestComplete().BindLambda(
		[OnComplete](FHttpRequestPtr OutReq, FHttpResponsePtr Resp, bool bOk)
		{
			if (!bOk || !Resp.IsValid())
			{
				OnComplete(MakeJsonResponse(502,
					TEXT("{\"error\":\"Broker forwarding failed — destination editor did not respond\"}")));
				return;
			}

			const int32 Code = Resp->GetResponseCode();
			const FString CType = Resp->GetContentType();
			const TArray<uint8>& Bytes = Resp->GetContent();

			TUniquePtr<FHttpServerResponse> Out = FHttpServerResponse::Create(
				TArray<uint8>(Bytes), CType.IsEmpty() ? TEXT("application/json") : CType);
			Out->Code = (EHttpServerResponseCodes)Code;
			Out->Headers.Add(TEXT("Access-Control-Allow-Origin"),  { TEXT("*") });
			Out->Headers.Add(TEXT("Access-Control-Allow-Headers"), { TEXT("Authorization, Content-Type") });
			Out->Headers.Add(TEXT("Access-Control-Allow-Methods"), { TEXT("POST, GET, OPTIONS") });
			OnComplete(MoveTemp(Out));
		});

	if (!HttpReq->ProcessRequest())
	{
		OnComplete(MakeJsonResponse(502,
			TEXT("{\"error\":\"Broker forwarding failed — could not initiate outbound request\"}")));
	}
}
