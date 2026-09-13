// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "Services/IUECPExtensionService.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/UECPToolSafety.h"
#include "Tools/BatchToolHelper.h"
#include "Tools/ExtensionToolUtils.h"
#include "UECPCoreModule.h"

#define UECP_EXTENSION_API_VERSION 1

#define UECP_REGISTER_TOOL(Name, Fn) \
	Dispatcher.RegisterHandler(TEXT(Name), \
		[](const TSharedPtr<FJsonObject>& Args) -> FUECPToolResult { return Fn(Args); })

#define UECP_REGISTER_TOOL_BG(Name, Fn) \
	Dispatcher.RegisterHandler(TEXT(Name), \
		[](const TSharedPtr<FJsonObject>& Args) -> FUECPToolResult { return Fn(Args); }, \
		EUECPToolThreadAffinity::AnyThread)

#define UECP_REGISTER_TOOL_LEGACY(Name, Fn) \
	Dispatcher.RegisterHandler(TEXT(Name), \
		[](const TSharedPtr<FJsonObject>& Args) -> FUECPToolResult { \
			FUECPToolResult R; \
			Fn(Args, R.ResultJson, R.ErrorMessage); \
			R.bSuccess = R.ErrorMessage.IsEmpty(); \
			return R; \
		})

#define UECP_REGISTER_TOOL_DESTRUCTIVE(Name, Fn) \
	Dispatcher.RegisterHandler(TEXT(Name), \
		[](const TSharedPtr<FJsonObject>& Args) -> FUECPToolResult { return Fn(Args); }); \
	UECPToolSafety::RegisterToolSafety(FName(TEXT(Name)), EUECPToolSafety::Destructive)
