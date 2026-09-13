// Copyright 2026, BlueprintsLab, All rights reserved

#include "FUECPBugReportCoordinator.h"
#include "SUECPMainWidget.h"
#include "Widget/UUECPAppBridge.h"
#include "Managers/ChatHistoryManager.h"
#include "Managers/EditorProfileSync.h"
#include "ApiKeyManager.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Async/Async.h"

static FString _brAssembleKey()
{
	const uint8 a[] = { 0x71, 0x46, 0x74, 0x61, 0x63, 0x6C };
	const uint8 b[] = { 0x70, 0x74, 0x67, 0x6C, 0x78, 0x02 };
	FString K;
	for (uint8 v : a) K += (TCHAR)(v ^ 0x33);
	for (uint8 v : b) K += (TCHAR)(v ^ 0x33);
	return K;
}
static FString _brDecodeUrl()
{
	static const uint8 _br_url_base[] = {0x2A,0x01,0x33,0x22,0x23,0x65,0x6C,0x68,0x23,0x28,0x3C,0x1F,0x25,0x14,0x2A,0x37,0x34,0x3A,0x35,0x24,0x3B,0x2D,0x2E,0x1F,0x21,0x1A,0x2A};
	const FString K = _brAssembleKey();
	FString R;
	if (K.IsEmpty()) return R;
	TArray<uint8> KB;
	FTCHARToUTF8 C(*K);
	KB.Append((uint8*)C.Get(), C.Length());
	for (int32 i = 0; i < (int32)sizeof(_br_url_base); ++i)
		R += (TCHAR)(_br_url_base[i] ^ KB[i % KB.Num()]);
	return R;
}
static const FString _BugReportBaseUrl = _brDecodeUrl();

FUECPBugReportCoordinator::FUECPBugReportCoordinator() = default;

void FUECPBugReportCoordinator::InitializeShellRefs(TWeakPtr<SUECPMainWidget> InShell,
	TWeakObjectPtr<UUECPAppBridge> InBridge)
{
	Shell  = InShell;
	Bridge = InBridge;
}

void FUECPBugReportCoordinator::ClearAttachedImages()
{
	AttachedImages.Empty();
	PushImagesToJS();
}

void FUECPBugReportCoordinator::AttachImageFromFile()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform) return;

	TArray<FString> Files;
	DesktopPlatform->OpenFileDialog(nullptr, TEXT("Select Screenshot"),
		TEXT(""), TEXT(""), TEXT("Image Files (*.png;*.jpg)|*.png;*.jpg"), 0, Files);

	for (const FString& FilePath : Files)
	{
		if (AttachedImages.Num() >= 5) break;

		TArray<uint8> FileData;
		if (FFileHelper::LoadFileToArray(FileData, *FilePath))
		{
			FAttachedImage Img;
			Img.Base64Data = FBase64::Encode(FileData);
			Img.MimeType = FilePath.EndsWith(TEXT(".png")) ? TEXT("image/png") : TEXT("image/jpeg");
			Img.Name = FPaths::GetCleanFilename(FilePath);
			AttachedImages.Add(MoveTemp(Img));
		}
	}

	PushImagesToJS();
}

void FUECPBugReportCoordinator::AttachImageFromClipboard()
{
	if (AttachedImages.Num() >= 5) return;

	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;

	W->PasteImageFromClipboardForExtraction(AttachedImages);
	PushImagesToJS();
}

void FUECPBugReportCoordinator::Submit(const FString& UserMessage, const FString& ReportType,
	const FString& ConversationChatId)
{
	TArray<TSharedPtr<FJsonValue>> ConvHistory;
	FString ActiveView;

	if (!ConversationChatId.IsEmpty())
	{
		ConvHistory = FChatHistoryManager::Get().LoadChatHistory(
			EConversationViewType::Architect, ConversationChatId);
		ActiveView = TEXT("architect");
	}

	TSharedPtr<FJsonObject> ReportBody = MakeShared<FJsonObject>();
	ReportBody->SetStringField(TEXT("hardwareId"),  FEditorProfileSync::GetWorkstationId());
	ReportBody->SetStringField(TEXT("pcUsername"),  FEditorProfileSync::GetProcessUser());
	ReportBody->SetStringField(TEXT("machineName"), FEditorProfileSync::GetMachineName());
	ReportBody->SetStringField(TEXT("fabOrderId"),  FEditorProfileSync::Get().GetPendingBundleCode());
	ReportBody->SetStringField(TEXT("reportType"),  ReportType);
	ReportBody->SetStringField(TEXT("userMessage"), UserMessage);
	ReportBody->SetStringField(TEXT("activeView"),  ActiveView);

	FString PluginVersion = TEXT("unknown");
	if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BpGeneratorUltimate")))
	{
		PluginVersion = Plugin->GetDescriptor().VersionName;
	}
	ReportBody->SetStringField(TEXT("pluginVersion"), PluginVersion);
	ReportBody->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString(EVersionComponent::Minor));

	FApiKeySlot Slot = FApiKeyManager::Get().GetActiveSlot();
	ReportBody->SetStringField(TEXT("apiProvider"), Slot.Provider);
	ReportBody->SetStringField(TEXT("apiModel"),    FApiKeyManager::Get().GetActiveModelName());

	if (ConvHistory.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ConvCopy;
		for (const TSharedPtr<FJsonValue>& V : ConvHistory)
		{
			TSharedPtr<FJsonObject> Obj = V.IsValid() ? V->AsObject() : nullptr;
			if (!Obj.IsValid()) continue;
			FString R; Obj->TryGetStringField(TEXT("role"), R);
			if (R == TEXT("agent_thinking") || R == TEXT("context") || R == TEXT("tool_bubble")) continue;
			ConvCopy.Add(V);
		}
		ReportBody->SetArrayField(TEXT("conversationJson"), ConvCopy);
		int32 CharCount = 0;
		for (const auto& V : ConvCopy)
		{
			if (V.IsValid() && V->AsObject())
			{
				FString Content;
				V->AsObject()->TryGetStringField(TEXT("content"), Content);
				CharCount += Content.Len();
			}
		}
		ReportBody->SetNumberField(TEXT("conversationCharCount"), CharCount);
	}

	if (AttachedImages.Num() == 0)
	{
		ReportBody->SetArrayField(TEXT("imageUrls"), TArray<TSharedPtr<FJsonValue>>());
		FinalizeSubmit(ReportBody);
		return;
	}

	TSharedPtr<TArray<FString>> UploadedUrls = MakeShared<TArray<FString>>();
	TSharedPtr<int32>           PendingCount = MakeShared<int32>(AttachedImages.Num());

	for (int32 i = 0; i < AttachedImages.Num(); i++)
	{
		const FAttachedImage& Img = AttachedImages[i];
		TArray<uint8> ImageData;
		FBase64::Decode(Img.Base64Data, ImageData);
		FString FileName = FString::Printf(TEXT("report_%s_%d.%s"),
			*FGuid::NewGuid().ToString().Left(8),
			i,
			Img.MimeType.Contains(TEXT("png")) ? TEXT("png") : TEXT("jpg"));

		UploadImage(ImageData, FileName,
			[this, ReportBody, UploadedUrls, PendingCount](bool bSuccess, const FString& Url)
			{
				if (bSuccess && !Url.IsEmpty())
					UploadedUrls->Add(Url);

				(*PendingCount)--;
				if (*PendingCount <= 0)
				{
					TArray<TSharedPtr<FJsonValue>> UrlArray;
					for (const FString& U : *UploadedUrls)
						UrlArray.Add(MakeShared<FJsonValueString>(U));
					ReportBody->SetArrayField(TEXT("imageUrls"), UrlArray);
					FinalizeSubmit(ReportBody);
				}
			});
	}
}

void FUECPBugReportCoordinator::UploadImage(const TArray<uint8>& ImageData, const FString& FileName,
	TFunction<void(bool, const FString&)> Callback)
{
	FString UploadUrl = _BugReportBaseUrl + TEXT("/api/feedback/upload-image");

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(UploadUrl);
	Request->SetVerb(TEXT("POST"));

	FString Boundary = FString::Printf(TEXT("----BpGen%s"),
		*FGuid::NewGuid().ToString().Replace(TEXT("-"), TEXT("")));
	Request->SetHeader(TEXT("Content-Type"),
		FString::Printf(TEXT("multipart/form-data; boundary=%s"), *Boundary));

	TArray<uint8> Payload;
	FString Header = FString::Printf(
		TEXT("--%s\r\nContent-Disposition: form-data; name=\"image\"; filename=\"%s\"\r\nContent-Type: image/png\r\n\r\n"),
		*Boundary, *FileName);
	FString Footer = FString::Printf(TEXT("\r\n--%s--\r\n"), *Boundary);

	auto AppendStr = [&Payload](const FString& Str) {
		FTCHARToUTF8 Utf8(*Str);
		Payload.Append((const uint8*)Utf8.Get(), Utf8.Length());
	};
	AppendStr(Header);
	Payload.Append(ImageData);
	AppendStr(Footer);
	Request->SetContent(Payload);

	TSharedPtr<TFunction<void(bool, const FString&)>> CB =
		MakeShared<TFunction<void(bool, const FString&)>>(MoveTemp(Callback));

	Request->OnProcessRequestComplete().BindLambda(
		[CB](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bConnectedSuccessfully)
		{
			if (!bConnectedSuccessfully || !Resp.IsValid() || Resp->GetResponseCode() != 200)
			{
				(*CB)(false, TEXT(""));
				return;
			}
			TSharedPtr<FJsonObject> Json;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
			if (FJsonSerializer::Deserialize(Reader, Json) && Json.IsValid())
			{
				FString Url;
				Json->TryGetStringField(TEXT("url"), Url);
				(*CB)(true, Url);
			}
			else
			{
				(*CB)(false, TEXT(""));
			}
		});

	Request->ProcessRequest();
}

void FUECPBugReportCoordinator::FinalizeSubmit(TSharedPtr<FJsonObject> ReportBody)
{
	FString SubmitUrl = _BugReportBaseUrl + TEXT("/api/feedback/submit");

	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(ReportBody.ToSharedRef(), Writer);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(SubmitUrl);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetContentAsString(JsonString);

	TWeakObjectPtr<UUECPAppBridge> BridgeWeak = Bridge;
	Request->OnProcessRequestComplete().BindLambda(
		[this, BridgeWeak](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bConnectedSuccessfully)
		{
			AsyncTask(ENamedThreads::GameThread, [this, BridgeWeak, bConnectedSuccessfully, Resp]()
			{
				if (bConnectedSuccessfully && Resp.IsValid() && Resp->GetResponseCode() == 200)
				{
					AttachedImages.Empty();
					if (UUECPAppBridge* B = BridgeWeak.Get())
						B->PushToast(TEXT("Report submitted - thank you!"), TEXT("success"));
				}
				else if (UUECPAppBridge* B = BridgeWeak.Get())
				{
					B->PushToast(TEXT("Failed to submit report. Check your connection."), TEXT("error"));
				}
			});
		});

	Request->ProcessRequest();
}

void FUECPBugReportCoordinator::PushImagesToJS()
{
	if (UUECPAppBridge* B = Bridge.Get())
	{
		B->ExecJs(FString::Printf(TEXT("if(typeof onBugImages==='function')onBugImages(%s)"),
			*UUECPAppBridge::BuildImagesJson(AttachedImages)));
	}
}
