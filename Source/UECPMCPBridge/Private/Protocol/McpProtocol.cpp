// Copyright 2026, BlueprintsLab, All rights reserved

#include "McpProtocol.h"

#include "McpJsonRpc.h"
#include "McpPromptsProvider.h"
#include "McpResourcesProvider.h"
#include "McpToolsCatalog.h"
#include "MCPToolsLog.h"

#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Serialization/JsonSerializer.h"

namespace
{

TSharedRef<FJsonObject> HandleInitialize(const TSharedPtr<FJsonObject>& )
{
	TSharedRef<FJsonObject> Capabilities = MakeShared<FJsonObject>();
	{
		TSharedRef<FJsonObject> ToolsCap = MakeShared<FJsonObject>();
		ToolsCap->SetBoolField(TEXT("listChanged"), false);
		Capabilities->SetObjectField(TEXT("tools"), ToolsCap);
	}
	{
		TSharedRef<FJsonObject> ResourcesCap = MakeShared<FJsonObject>();
		ResourcesCap->SetBoolField(TEXT("listChanged"), false);
		ResourcesCap->SetBoolField(TEXT("subscribe"),   false);
		Capabilities->SetObjectField(TEXT("resources"), ResourcesCap);
	}
	{
		TSharedRef<FJsonObject> PromptsCap = MakeShared<FJsonObject>();
		PromptsCap->SetBoolField(TEXT("listChanged"), false);
		Capabilities->SetObjectField(TEXT("prompts"), PromptsCap);
	}

	TSharedRef<FJsonObject> ServerInfo = MakeShared<FJsonObject>();
	ServerInfo->SetStringField(TEXT("name"),    FMcpProtocol::ServerName);
	ServerInfo->SetStringField(TEXT("title"),   TEXT("Ultimate Engine Co-Pilot"));
	ServerInfo->SetStringField(TEXT("version"), FMcpProtocol::ServerVersion);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("protocolVersion"), FMcpProtocol::ProtocolVersion);
	Result->SetObjectField(TEXT("capabilities"),    Capabilities);
	Result->SetObjectField(TEXT("serverInfo"),      ServerInfo);

	{
		FString Instructions = TEXT(
			"This server drives a LIVE Unreal Engine 5 editor. Project state lives in binary .uasset files reachable ONLY through these tools — not the filesystem or shell. Tools are grouped into umbrella tools that take an `action` field, e.g. blueprint(action='add_function', ...).\n\n"
			"Finding actions: call search_tools(query='<english intent>') to find the right action (returns action + params, usually enough to call directly). For one action's full params use get_tool_docs(category='X', action='Y'); for an umbrella's recipes/gotchas use get_tool_docs(category='X'). Never guess action names.");

		if (IUECPCoreModule::IsAvailable())
		{
			TArray<FUECPToolMeta> AlwaysShow;
			IUECPCoreModule::Get().GetToolDispatcher().GetAllToolMetadata(AlwaysShow);
			AlwaysShow.RemoveAll([](const FUECPToolMeta& M) { return !M.bAlwaysShow; });
			if (AlwaysShow.Num() > 0)
			{
				AlwaysShow.Sort([](const FUECPToolMeta& A, const FUECPToolMeta& B)
				{ return A.Action.ToString() < B.Action.ToString(); });
				Instructions += TEXT("\n\n=== ALWAYS AVAILABLE (no get_tool_docs needed — invoke exactly as shown) ===\n");
				for (const FUECPToolMeta& M : AlwaysShow)
				{
					const FString Act = M.Action.ToString();
					const FString Umb = M.Umbrella.ToString();
					FString Call;
					if (Umb == TEXT("discovery"))
						Call = M.Params.IsEmpty() ? FString::Printf(TEXT("%s(...)"), *Act)
						                          : FString::Printf(TEXT("%s(%s)"), *Act, *M.Params);
					else
						Call = M.Params.IsEmpty() ? FString::Printf(TEXT("%s(action='%s')"), *Umb, *Act)
						                          : FString::Printf(TEXT("%s(action='%s', %s)"), *Umb, *Act, *M.Params);
					Instructions += FString::Printf(TEXT("- %s — %s\n"), *Call, *M.Summary);
				}
			}
		}
		Result->SetStringField(TEXT("instructions"), Instructions);
	}
	return Result;
}

}

FString FMcpProtocol::HandleMessage(const FString& JsonRpcMessage,
                                    const FMcpInvocationContext& Context)
{
	McpJsonRpc::FRequest Req;
	FString ParseError;
	if (!McpJsonRpc::ParseRequest(JsonRpcMessage, Req, ParseError))
	{
		UE_LOG(LogMCPTool, Warning, TEXT("MCP: parse failure on incoming message"));
		return ParseError;
	}

	if (Req.bIsNotification)
	{
		return FString();
	}

	if (Req.Method == TEXT("initialize"))
	{
		return McpJsonRpc::MakeSuccess(Req.Id, HandleInitialize(Req.Params));
	}
	if (Req.Method == TEXT("tools/list"))
	{
		return McpJsonRpc::MakeSuccess(Req.Id, McpToolsCatalog::BuildToolsListResult());
	}
	if (Req.Method == TEXT("tools/call"))
	{
		return McpJsonRpc::MakeSuccess(Req.Id,
			McpToolsCatalog::HandleToolsCall(Req.Params, Context));
	}
	if (Req.Method == TEXT("resources/list"))
	{
		return McpJsonRpc::MakeSuccess(Req.Id,
			McpResourcesProvider::BuildResourcesListResult(Req.Params));
	}
	if (Req.Method == TEXT("resources/read"))
	{
		return McpJsonRpc::MakeSuccess(Req.Id,
			McpResourcesProvider::HandleResourcesRead(Req.Params));
	}
	if (Req.Method == TEXT("prompts/list"))
	{
		return McpJsonRpc::MakeSuccess(Req.Id,
			McpPromptsProvider::BuildPromptsListResult());
	}
	if (Req.Method == TEXT("prompts/get"))
	{
		return McpJsonRpc::MakeSuccess(Req.Id,
			McpPromptsProvider::HandlePromptsGet(Req.Params));
	}
	if (Req.Method == TEXT("ping"))
	{
		return McpJsonRpc::MakeSuccess(Req.Id, MakeShared<FJsonObject>());
	}

	UE_LOG(LogMCPTool, Verbose, TEXT("MCP: method not found: %s"), *Req.Method);
	return McpJsonRpc::MakeError(Req.Id, McpJsonRpc::EError::MethodNotFound,
		FString::Printf(TEXT("Method not found: %s"), *Req.Method));
}
