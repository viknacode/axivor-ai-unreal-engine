// Copyright 2026, BlueprintsLab, All rights reserved

#include "SoundGenManager.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Base64.h"
#include "HAL/PlatformFileManager.h"
#include "Tools/ImportTools.h"
#include "Async/Async.h"

FSoundGenManager& FSoundGenManager::Get()
{
	static FSoundGenManager Instance;
	return Instance;
}

FString FSoundGenManager::GetSoundGenDir()
{
	FString Dir = FPaths::ProjectSavedDir() / TEXT("BpGeneratorUltimate") / TEXT("SoundGen");
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	if (!PF.DirectoryExists(*Dir)) PF.CreateDirectoryTree(*Dir);
	return Dir;
}

FString FSoundGenManager::GenerateAssetName(const FString& Text)
{
	FString Base = Text.Left(30).Replace(TEXT(" "), TEXT("_"));
	FString Clean;
	for (TCHAR C : Base)
	{
		if (FChar::IsAlnum(C) || C == '_') Clean += C;
	}
	if (Clean.IsEmpty()) Clean = TEXT("GeneratedSound");
	FString Timestamp = FDateTime::Now().ToString(TEXT("%Y_%m_%d-%H_%M_%S"));
	return FString::Printf(TEXT("SFX_%s_%s"), *Clean, *Timestamp);
}

FSoundGenManager::EProviderType FSoundGenManager::DetectProvider(const FString& Endpoint)
{
	if (Endpoint.Contains(TEXT("openai.com")))       return EProviderType::OpenAI;
	if (Endpoint.Contains(TEXT("sound-generation")))  return EProviderType::ElevenLabsSFX;
	if (Endpoint.Contains(TEXT("elevenlabs")))        return EProviderType::ElevenLabs;
	if (Endpoint.Contains(TEXT("googleapis.com")) &&
	    Endpoint.Contains(TEXT("text:synthesize")))    return EProviderType::GoogleTTS;
	if (Endpoint.Contains(TEXT("stability.ai")))      return EProviderType::StabilityAI;
	if (Endpoint.Contains(TEXT("huggingface.co")) ||
	    Endpoint.Contains(TEXT("api-inference")))      return EProviderType::HuggingFace;
	return EProviderType::Generic;
}

void FSoundGenManager::CancelGeneration()
{
	if (ActiveRequest.IsValid())
	{
		ActiveRequest->CancelRequest();
		ActiveRequest.Reset();
	}
}

void FSoundGenManager::GenerateSound(const FSoundGenRequest& Request, const FString& ApiKey,
                                      const FString& Endpoint, TFunction<void(const FSoundGenResult&)> OnComplete)
{
	if (Request.Text.IsEmpty())
	{
		FSoundGenResult R;
		R.ErrorMessage = TEXT("Text is required for sound generation");
		if (OnComplete) OnComplete(R);
		return;
	}

	if (ApiKey.IsEmpty() || Endpoint.IsEmpty())
	{
		FSoundGenResult R;
		R.ErrorMessage = TEXT("Sound generation API key and endpoint are required. Configure them in Settings → Voice & Generation.");
		if (OnComplete) OnComplete(R);
		return;
	}

	EProviderType Provider = DetectProvider(Endpoint);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpReq = FHttpModule::Get().CreateRequest();
	HttpReq->SetVerb(TEXT("POST"));
	HttpReq->SetTimeout(120.0f);

	FString Voice = Request.Voice.IsEmpty() ? TEXT("alloy") : Request.Voice;
	FString Model = Request.Model.IsEmpty() ? TEXT("tts-1") : Request.Model;

	FString Url = Endpoint;

	if (Provider == EProviderType::OpenAI || Provider == EProviderType::Generic)
	{
		HttpReq->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
		HttpReq->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

		TSharedPtr<FJsonObject> Body = MakeShareable(new FJsonObject);
		Body->SetStringField(TEXT("model"), Model);
		Body->SetStringField(TEXT("input"), Request.Text);
		Body->SetStringField(TEXT("voice"), Voice);
		Body->SetStringField(TEXT("response_format"), TEXT("wav"));
		if (FMath::Abs(Request.Speed - 1.0f) > 0.01f)
			Body->SetNumberField(TEXT("speed"), static_cast<double>(Request.Speed));

		FString BodyStr;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&BodyStr);
		FJsonSerializer::Serialize(Body.ToSharedRef(), W);
		HttpReq->SetContentAsString(BodyStr);
	}
	else if (Provider == EProviderType::ElevenLabsSFX)
	{
		HttpReq->SetHeader(TEXT("xi-api-key"), ApiKey);
		HttpReq->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

		TSharedPtr<FJsonObject> Body = MakeShareable(new FJsonObject);
		Body->SetStringField(TEXT("text"), Request.Text);
		Body->SetNumberField(TEXT("duration_seconds"), 5.0);

		FString BodyStr;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&BodyStr);
		FJsonSerializer::Serialize(Body.ToSharedRef(), W);
		HttpReq->SetContentAsString(BodyStr);
	}
	else if (Provider == EProviderType::ElevenLabs)
	{
		Url = Endpoint;
		if (!Url.Contains(Voice))
		{
			if (!Url.EndsWith(TEXT("/"))) Url += TEXT("/");
			Url += Voice;
		}
		HttpReq->SetHeader(TEXT("xi-api-key"), ApiKey);
		HttpReq->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		HttpReq->SetHeader(TEXT("Accept"), TEXT("audio/mpeg"));

		TSharedPtr<FJsonObject> Body = MakeShareable(new FJsonObject);
		Body->SetStringField(TEXT("text"), Request.Text);
		if (!Model.IsEmpty())
			Body->SetStringField(TEXT("model_id"), Model);

		FString BodyStr;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&BodyStr);
		FJsonSerializer::Serialize(Body.ToSharedRef(), W);
		HttpReq->SetContentAsString(BodyStr);
	}
	else if (Provider == EProviderType::GoogleTTS)
	{
		HttpReq->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
		HttpReq->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

		TSharedPtr<FJsonObject> Input = MakeShareable(new FJsonObject);
		Input->SetStringField(TEXT("text"), Request.Text);

		TSharedPtr<FJsonObject> VoiceObj = MakeShareable(new FJsonObject);
		VoiceObj->SetStringField(TEXT("languageCode"), TEXT("en-US"));
		if (!Voice.IsEmpty())
			VoiceObj->SetStringField(TEXT("name"), Voice);

		TSharedPtr<FJsonObject> AudioConfig = MakeShareable(new FJsonObject);
		AudioConfig->SetStringField(TEXT("audioEncoding"), TEXT("LINEAR16"));

		TSharedPtr<FJsonObject> Body = MakeShareable(new FJsonObject);
		Body->SetObjectField(TEXT("input"), Input);
		Body->SetObjectField(TEXT("voice"), VoiceObj);
		Body->SetObjectField(TEXT("audioConfig"), AudioConfig);

		FString BodyStr;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&BodyStr);
		FJsonSerializer::Serialize(Body.ToSharedRef(), W);
		HttpReq->SetContentAsString(BodyStr);
	}

	else if (Provider == EProviderType::StabilityAI)
	{
		HttpReq->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
		HttpReq->SetHeader(TEXT("Accept"), TEXT("audio/*"));

		FString Boundary = TEXT("----BpGenBoundary") + FGuid::NewGuid().ToString();
		HttpReq->SetHeader(TEXT("Content-Type"), FString::Printf(TEXT("multipart/form-data; boundary=%s"), *Boundary));

		FString MultipartBody;
		auto AddField = [&](const FString& Name, const FString& Value) {
			MultipartBody += TEXT("--") + Boundary + TEXT("\r\n");
			MultipartBody += FString::Printf(TEXT("Content-Disposition: form-data; name=\"%s\"\r\n\r\n%s\r\n"), *Name, *Value);
		};
		AddField(TEXT("prompt"), Request.Text);
		AddField(TEXT("duration"), TEXT("10"));
		AddField(TEXT("output_format"), TEXT("wav"));
		MultipartBody += TEXT("--") + Boundary + TEXT("--\r\n");

		FTCHARToUTF8 Conv(*MultipartBody);
		TArray<uint8> Payload;
		Payload.Append((const uint8*)Conv.Get(), Conv.Length());
		HttpReq->SetContent(Payload);
	}
	else if (Provider == EProviderType::HuggingFace)
	{
		HttpReq->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
		HttpReq->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

		FString BodyStr = FString::Printf(TEXT("{\"inputs\":\"%s\"}"),
			*Request.Text.Replace(TEXT("\""), TEXT("\\\"")).Replace(TEXT("\n"), TEXT(" ")));
		HttpReq->SetContentAsString(BodyStr);
	}

	HttpReq->SetURL(Url);

	ActiveRequest = HttpReq;
	HttpReq->OnProcessRequestComplete().BindRaw(this, &FSoundGenManager::OnRequestComplete,
		Request, OnComplete);
	HttpReq->ProcessRequest();

	UE_LOG(LogTemp, Log, TEXT("SoundGenManager: Generating sound — Provider=%d, Model=%s, Voice=%s, Text='%s'"),
		static_cast<int32>(Provider), *Model, *Voice, *Request.Text.Left(60));
}

void FSoundGenManager::OnRequestComplete(FHttpRequestPtr Req, FHttpResponsePtr Response, bool bWasSuccessful,
                                          FSoundGenRequest OrigRequest, TFunction<void(const FSoundGenResult&)> Callback)
{
	ActiveRequest.Reset();
	FSoundGenResult Result;

	if (!bWasSuccessful || !Response.IsValid())
	{
		Result.ErrorMessage = TEXT("Sound generation HTTP request failed");
		if (Callback) AsyncTask(ENamedThreads::GameThread, [Callback, Result]() { Callback(Result); });
		return;
	}

	int32 Code = Response->GetResponseCode();
	if (Code != 200)
	{
		Result.ErrorMessage = FString::Printf(TEXT("Sound generation failed (HTTP %d): %s"),
			Code, *Response->GetContentAsString().Left(500));
		if (Callback) AsyncTask(ENamedThreads::GameThread, [Callback, Result]() { Callback(Result); });
		return;
	}

	TArray<uint8> AudioData;
	EProviderType Provider = DetectProvider(Req->GetURL());

	if (Provider == EProviderType::GoogleTTS)
	{
		TSharedPtr<FJsonObject> JsonResp;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
		if (FJsonSerializer::Deserialize(Reader, JsonResp) && JsonResp.IsValid())
		{
			FString B64;
			if (JsonResp->TryGetStringField(TEXT("audioContent"), B64))
				FBase64::Decode(B64, AudioData);
		}
		if (AudioData.Num() == 0)
		{
			Result.ErrorMessage = TEXT("Failed to decode Google TTS audio response");
			if (Callback) AsyncTask(ENamedThreads::GameThread, [Callback, Result]() { Callback(Result); });
			return;
		}
	}
	else
	{
		AudioData = Response->GetContent();
	}

	if (AudioData.Num() == 0)
	{
		Result.ErrorMessage = TEXT("Empty audio response from provider");
		if (Callback) AsyncTask(ENamedThreads::GameThread, [Callback, Result]() { Callback(Result); });
		return;
	}

	FString AssetName = OrigRequest.CustomAssetName.IsEmpty()
		? GenerateAssetName(OrigRequest.Text) : OrigRequest.CustomAssetName;
	FString SaveDir = GetSoundGenDir();

	FString Ext = TEXT("wav");
	FString ContentType = Response->GetHeader(TEXT("Content-Type")).ToLower();
	if (ContentType.Contains(TEXT("mpeg")) || ContentType.Contains(TEXT("mp3")))
		Ext = TEXT("mp3");
	else if (ContentType.Contains(TEXT("ogg")))
		Ext = TEXT("ogg");
	else if (ContentType.Contains(TEXT("flac")))
		Ext = TEXT("flac");

	FString FilePath = SaveDir / FString::Printf(TEXT("%s.%s"), *AssetName, *Ext);
	if (!FFileHelper::SaveArrayToFile(AudioData, *FilePath))
	{
		Result.ErrorMessage = FString::Printf(TEXT("Failed to save audio to %s"), *FilePath);
		if (Callback) AsyncTask(ENamedThreads::GameThread, [Callback, Result]() { Callback(Result); });
		return;
	}

	Result.FilePath = FilePath;
	UE_LOG(LogTemp, Log, TEXT("SoundGenManager: Audio saved (%d bytes) → %s"), AudioData.Num(), *FilePath);

	FString DestPath = OrigRequest.SavePath.IsEmpty() ? TEXT("/Game/Audio") : OrigRequest.SavePath;
	AsyncTask(ENamedThreads::GameThread, [FilePath, DestPath, AssetName, Callback, Result]() mutable
	{
		FString OutJson, OutError;
		ImportTools::HandleImportSoundWave(FilePath, DestPath, OutJson, OutError);

		if (!OutError.IsEmpty())
		{
			Result.ErrorMessage = FString::Printf(TEXT("Import failed: %s"), *OutError);
		}
		else
		{
			Result.bSuccess  = true;
			Result.AssetName = AssetName;
			TSharedPtr<FJsonObject> J;
			TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(OutJson);
			if (FJsonSerializer::Deserialize(R, J) && J.IsValid())
				J->TryGetStringField(TEXT("asset_path"), Result.AssetPath);
		}

		UE_LOG(LogTemp, Log, TEXT("SoundGenManager: Import %s → %s"),
			Result.bSuccess ? TEXT("OK") : TEXT("FAILED"), *Result.AssetPath);

		if (Callback) Callback(Result);
	});
}
