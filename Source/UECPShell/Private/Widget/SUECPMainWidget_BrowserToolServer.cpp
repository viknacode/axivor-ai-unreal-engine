// Copyright 2026, BlueprintsLab, All rights reserved

#include "SUECPMainWidget.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Common/TcpSocketBuilder.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Async/Async.h"

namespace
{
	// Shared state for a game-thread tool dispatch. Kept alive by both the worker
	// (which waits on it) and the dispatched lambda (which fills it). The event is
	// returned to the pool only when the last reference drops, so a lambda that runs
	// after the worker times out never touches a freed event or result.
	struct FBrowserToolCallState
	{
		FToolExecutionResult Result;
		FEvent* Event = nullptr;

		~FBrowserToolCallState()
		{
			if (Event)
			{
				FPlatformProcess::ReturnSynchEventToPool(Event);
				Event = nullptr;
			}
		}
	};

	// Serializes a tool result to the {success, result|error} JSON envelope.
	FString BuildToolResponseJson(const FToolExecutionResult& ToolResult)
	{
		TSharedPtr<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
		ResponseObj->SetBoolField(TEXT("success"), ToolResult.bSuccess);

		if (ToolResult.bSuccess)
		{
			TSharedPtr<FJsonObject> ResultObj;
			TSharedRef<TJsonReader<>> ResultReader = TJsonReaderFactory<>::Create(ToolResult.ResultJson);
			if (FJsonSerializer::Deserialize(ResultReader, ResultObj) && ResultObj.IsValid())
			{
				ResponseObj->SetObjectField(TEXT("result"), ResultObj);
			}
			else
			{
				ResponseObj->SetStringField(TEXT("result"), ToolResult.ResultJson);
			}
		}
		else
		{
			ResponseObj->SetStringField(TEXT("error"), ToolResult.ErrorMessage);
		}

		FString ResponseJson;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseJson);
		FJsonSerializer::Serialize(ResponseObj.ToSharedRef(), Writer);
		return ResponseJson;
	}
}

// Dispatches a tool onto the game thread and waits up to 30s. On timeout it returns a
// structured error instead of reading the (still-pending) result; the shared state keeps
// the result and event alive for the late lambda, so neither is a dangling reference.
FToolExecutionResult FBrowserToolServerWorker::ExecuteBrowserToolCall(SUECPMainWidget* Owner, const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments)
{
	TSharedPtr<FBrowserToolCallState, ESPMode::ThreadSafe> State = MakeShared<FBrowserToolCallState, ESPMode::ThreadSafe>();
	State->Event = FPlatformProcess::GetSynchEventFromPool(true);

	AsyncTask(ENamedThreads::GameThread, [Owner, ToolName, Arguments, State]()
	{
		if (Owner)
		{
			State->Result = Owner->DispatchToolCall(ToolName, Arguments);
		}
		else
		{
			State->Result.bSuccess = false;
			State->Result.ErrorMessage = TEXT("Widget not available");
		}
		State->Event->Trigger();
	});

	if (State->Event->Wait(30000))
	{
		return State->Result;
	}

	FToolExecutionResult TimedOut;
	TimedOut.bSuccess = false;
	TimedOut.ErrorMessage = TEXT("Tool execution timed out (30s)");
	return TimedOut;
}

FBrowserToolServerWorker::FBrowserToolServerWorker(FSocket* InListenSocket, SUECPMainWidget* InOwner)
	: ListenSocket(InListenSocket)
	, bStopping(false)
	, OwnerWidget(InOwner)
{
}

FBrowserToolServerWorker::~FBrowserToolServerWorker()
{
}

bool FBrowserToolServerWorker::Init()
{
	return true;
}

void FBrowserToolServerWorker::Stop()
{
	bStopping = true;
}

uint32 FBrowserToolServerWorker::Run()
{
	while (!bStopping)
	{
		bool bHasPendingConnection;
		if (ListenSocket->HasPendingConnection(bHasPendingConnection) && bHasPendingConnection)
		{
			FSocket* ClientSocket = ListenSocket->Accept(TEXT("BrowserToolClient"));
			if (ClientSocket)
			{
				TArray<uint8> RecvBuffer;
				RecvBuffer.SetNumZeroed(65536);
				FString FullRequestString;

				ClientSocket->SetNonBlocking(false);
				ClientSocket->SetRecvErr();

				int32 TotalBytesRead = 0;
				bool bDoneReading = false;
				double StartTime = FPlatformTime::Seconds();
				const double TimeoutSeconds = 10.0;

				while (!bDoneReading && !bStopping)
				{
					int32 BytesRead = 0;
					if (ClientSocket->Recv(RecvBuffer.GetData() + TotalBytesRead, RecvBuffer.Num() - TotalBytesRead, BytesRead))
					{
						if (BytesRead > 0)
						{
							TotalBytesRead += BytesRead;
							FString PartialRequest = FString(TotalBytesRead, UTF8_TO_TCHAR((char*)RecvBuffer.GetData()));

							int32 HeaderEndIdx = PartialRequest.Find(TEXT("\r\n\r\n"));
							if (HeaderEndIdx != INDEX_NONE)
							{
								FString ContentLengthStr;
								FRegexPattern ContentLengthPattern(TEXT("Content-Length:\\s*(\\d+)"));
								FRegexMatcher ContentLengthMatcher(ContentLengthPattern, PartialRequest);
								int32 ContentLength = 0;
								if (ContentLengthMatcher.FindNext())
								{
									ContentLength = FCString::Atoi(*ContentLengthMatcher.GetCaptureGroup(1));
								}

								int32 BodyStart = HeaderEndIdx + 4;
								int32 BodyReceived = TotalBytesRead - BodyStart;

								if (BodyReceived >= ContentLength || ContentLength == 0)
								{
									FullRequestString = PartialRequest;
									bDoneReading = true;
								}
							}

							if (PartialRequest.StartsWith(TEXT("OPTIONS")) && HeaderEndIdx != INDEX_NONE)
							{
								FullRequestString = PartialRequest;
								bDoneReading = true;
							}
						}
						else
						{
							bDoneReading = true;
						}
					}
					else
					{
						bDoneReading = true;
					}

					if (FPlatformTime::Seconds() - StartTime > TimeoutSeconds)
					{
						bDoneReading = true;
					}
				}

				if (!FullRequestString.IsEmpty())
				{
					FString HttpMethod;
					FString JsonBody;
					FString ResponseString;

					if (ParseHttpRequest(FullRequestString, HttpMethod, JsonBody))
					{
						if (HttpMethod == TEXT("OPTIONS"))
						{
							ResponseString = BuildHttpResponse(200, TEXT(""));
						}
						else if (HttpMethod == TEXT("POST"))
						{
							TSharedPtr<FJsonObject> JsonObject;
							TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonBody);

							if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
							{
								FString ToolName;
								if (JsonObject->TryGetStringField(TEXT("tool_name"), ToolName))
								{
									TSharedPtr<FJsonObject> Arguments = JsonObject->GetObjectField(TEXT("arguments"));
									if (!Arguments.IsValid())
									{
										Arguments = MakeShared<FJsonObject>();
									}

									{
									FToolExecutionResult ToolResult = ExecuteBrowserToolCall(OwnerWidget, ToolName, Arguments);
									ResponseString = BuildHttpResponse(200, BuildToolResponseJson(ToolResult));
									}
								}
								else
								{
									ResponseString = BuildHttpResponse(400,
										TEXT("{\"success\":false,\"error\":\"Missing tool_name field\"}"));
								}
							}
							else
							{
								ResponseString = BuildHttpResponse(400,
									TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}"));
							}
						}
						else
						{
							ResponseString = BuildHttpResponse(405,
								TEXT("{\"success\":false,\"error\":\"Method not allowed\"}"));
						}
					}
					else
					{
						TSharedPtr<FJsonObject> JsonObject;
						TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FullRequestString);

						if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
						{
							FString ToolName;
							if (JsonObject->TryGetStringField(TEXT("tool_name"), ToolName))
							{
								TSharedPtr<FJsonObject> Arguments = JsonObject->GetObjectField(TEXT("arguments"));
								if (!Arguments.IsValid())
								{
									Arguments = MakeShared<FJsonObject>();
								}

								{
								FToolExecutionResult ToolResult = ExecuteBrowserToolCall(OwnerWidget, ToolName, Arguments);
								ResponseString = BuildToolResponseJson(ToolResult);
								}
							}
							else
							{
								ResponseString = TEXT("{\"success\":false,\"error\":\"Missing tool_name field\"}");
							}
						}
						else
						{
							ResponseString = TEXT("{\"success\":false,\"error\":\"Invalid request\"}");
						}
					}

					FTCHARToUTF8 Converter(*ResponseString);
					int32 BytesSent = 0;
					ClientSocket->Send((const uint8*)Converter.Get(), Converter.Length(), BytesSent);
				}

				ClientSocket->Close();
				ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ClientSocket);
			}
		}
		else
		{
			FPlatformProcess::Sleep(0.01f);
		}
	}

	return 0;
}

bool FBrowserToolServerWorker::ParseHttpRequest(const FString& RawRequest, FString& OutMethod, FString& OutBody)
{
	if (!RawRequest.StartsWith(TEXT("GET ")) &&
		!RawRequest.StartsWith(TEXT("POST ")) &&
		!RawRequest.StartsWith(TEXT("OPTIONS ")) &&
		!RawRequest.StartsWith(TEXT("PUT ")) &&
		!RawRequest.StartsWith(TEXT("DELETE ")))
	{
		return false;
	}

	int32 SpaceIdx;
	if (RawRequest.FindChar(TEXT(' '), SpaceIdx))
	{
		OutMethod = RawRequest.Left(SpaceIdx).ToUpper();
	}

	int32 BodyStart = RawRequest.Find(TEXT("\r\n\r\n"));
	if (BodyStart != INDEX_NONE)
	{
		OutBody = RawRequest.Mid(BodyStart + 4);
	}
	else
	{
		OutBody = TEXT("");
	}

	return true;
}

FString FBrowserToolServerWorker::BuildHttpResponse(int32 StatusCode, const FString& Body)
{
	FString StatusText;
	switch (StatusCode)
	{
		case 200: StatusText = TEXT("OK"); break;
		case 400: StatusText = TEXT("Bad Request"); break;
		case 405: StatusText = TEXT("Method Not Allowed"); break;
		case 500: StatusText = TEXT("Internal Server Error"); break;
		default: StatusText = TEXT("Unknown"); break;
	}

	FString Response = FString::Printf(
		TEXT("HTTP/1.1 %d %s\r\n")
		TEXT("Content-Type: application/json\r\n")
		TEXT("Access-Control-Allow-Origin: *\r\n")
		TEXT("Access-Control-Allow-Methods: POST, OPTIONS\r\n")
		TEXT("Access-Control-Allow-Headers: Content-Type\r\n")
		TEXT("Connection: close\r\n")
		TEXT("Content-Length: %d\r\n")
		TEXT("\r\n")
		TEXT("%s"),
		StatusCode, *StatusText,
		FTCHARToUTF8(*Body).Length(),
		*Body
	);

	return Response;
}

void SUECPMainWidget::StartBrowserToolServer()
{
	const FIPv4Address Address = FIPv4Address::Any;
	const int32 BasePort = 8765;
	const int32 MaxPortAttempts = 5;

	for (int32 PortOffset = 0; PortOffset < MaxPortAttempts; PortOffset++)
	{
		int32 Port = BasePort + PortOffset;
		FIPv4Endpoint Endpoint(Address, Port);

		BrowserToolListenerSocket = FTcpSocketBuilder(TEXT("BrowserToolListenerSocket"))
			.AsReusable()
			.BoundToEndpoint(Endpoint)
			.Listening(8);

		if (BrowserToolListenerSocket)
		{
			BrowserToolServerPort = Port;
			UE_LOG(LogUECPShell, Log, TEXT("Browser Tool Server: Listening on port %d (HTTP, no MCP)"), Port);

			BrowserToolServerWorker = new FBrowserToolServerWorker(BrowserToolListenerSocket, this);
			BrowserToolServerThread = FRunnableThread::Create(BrowserToolServerWorker, TEXT("BrowserToolServerThread"));
			return;
		}

		UE_LOG(LogUECPShell, Warning, TEXT("Browser Tool Server: Port %d is busy, trying next..."), Port);
	}

	UE_LOG(LogUECPShell, Warning, TEXT("Browser Tool Server: Failed to bind to any port from %d to %d. Browser extension tool calls will not work."), BasePort, BasePort + MaxPortAttempts - 1);
}

void SUECPMainWidget::StopBrowserToolServer()
{
	if (BrowserToolServerThread)
	{
		BrowserToolServerWorker->Stop();
		BrowserToolServerThread->WaitForCompletion();

		delete BrowserToolServerThread;
		BrowserToolServerThread = nullptr;

		delete BrowserToolServerWorker;
		BrowserToolServerWorker = nullptr;
	}

	if (BrowserToolListenerSocket)
	{
		BrowserToolListenerSocket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(BrowserToolListenerSocket);
		BrowserToolListenerSocket = nullptr;
	}

	BrowserToolServerPort = 0;
	UE_LOG(LogUECPShell, Log, TEXT("Browser Tool Server: Stopped"));
}
