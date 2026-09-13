// Copyright 2026, BlueprintsLab, All rights reserved

#include "Widget/UUECPImageGenBridge.h"
#include "UECPCoreModule.h"
#include "ApiKeyManager.h"
#include "Services/IUECPAssetGenService.h"
#include "Services/UECPAssetGenTypes.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

void UUECPImageGenBridge::GenerateImage(const FString& Json)
{
	if (!IUECPCoreModule::IsAvailable()) return;

	TSharedPtr<FJsonObject> O;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, O) || !O.IsValid()) return;

	FString Prompt;
	O->TryGetStringField(TEXT("prompt"), Prompt);
	if (Prompt.TrimStartAndEnd().IsEmpty())
	{
		PushResult(TEXT("{\"ok\":false,\"error\":\"Enter a prompt first.\"}"));
		return;
	}

	FApiKeyManager::FImageGenConfig Cfg = FApiKeyManager::Get().GetImageGenConfig();
	{
		FString Model;
		if (O->TryGetStringField(TEXT("model"), Model) && !Model.IsEmpty()) Cfg.Model = Model;
		FString Mode;
		if (O->TryGetStringField(TEXT("mode"), Mode) && !Mode.IsEmpty()) Cfg.bTextureMode = (Mode != TEXT("general"));
		FApiKeyManager::Get().SetImageGenConfig(Cfg);
	}

	const FString ApiKey = FApiKeyManager::Get().GetActiveTextureGenApiKey();
	if (ApiKey.IsEmpty())
	{
		PushResult(TEXT("{\"ok\":false,\"error\":\"No image-gen API key set. Add one in Settings -> Image & 3D.\"}"));
		return;
	}

	FTextureGenRequest Req;
	Req.Prompt = Prompt;
	O->TryGetStringField(TEXT("aspect"),    Req.AspectRatio);
	O->TryGetStringField(TEXT("negative"),  Req.NegativePrompt);
	O->TryGetStringField(TEXT("assetName"), Req.CustomAssetName);
	O->TryGetStringField(TEXT("savePath"),  Req.SavePath);
	double SeedD = -1.0;
	if (O->TryGetNumberField(TEXT("seed"), SeedD)) Req.Seed = (int32)SeedD;

	PushResult(TEXT("{\"pending\":true}"));

	TWeakObjectPtr<UUECPImageGenBridge> WeakThis(this);
	IUECPCoreModule::Get().GetAssetGenService().GenerateTexture(Req, ApiKey,
		[WeakThis](const FTextureGenResult& Res)
		{
			if (!WeakThis.IsValid()) return;
			const TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
			J->SetBoolField  (TEXT("ok"),           Res.bSuccess);
			J->SetStringField(TEXT("error"),        Res.ErrorMessage);
			J->SetStringField(TEXT("imagePath"),    Res.ImagePath);
			J->SetStringField(TEXT("assetPath"),    Res.AssetPath);
			J->SetStringField(TEXT("materialPath"), Res.MaterialPath);
			J->SetNumberField(TEXT("width"),        Res.Width);
			J->SetNumberField(TEXT("height"),       Res.Height);
			FString Body;
			const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Body);
			FJsonSerializer::Serialize(J, W);
			WeakThis->PushResult(Body);
		});
}

void UUECPImageGenBridge::RequestImageGenConfig()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	const FApiKeyManager::FImageGenConfig Cfg = FApiKeyManager::Get().GetImageGenConfig();
	const TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetStringField(TEXT("endpoint"),    Cfg.Endpoint);
	J->SetStringField(TEXT("model"),       Cfg.Model);
	J->SetBoolField  (TEXT("textureMode"), Cfg.bTextureMode);
	J->SetBoolField  (TEXT("hasKey"),      !FApiKeyManager::Get().GetActiveTextureGenApiKey().IsEmpty());
	FString Body;
	const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Body);
	FJsonSerializer::Serialize(J, W);
	ExecJs(FString::Printf(TEXT("if(typeof onImageGenConfig==='function')onImageGenConfig(%s)"), *Body));
}

void UUECPImageGenBridge::PushResult(const FString& Json)
{
	ExecJs(FString::Printf(TEXT("if(typeof onImageGenResult==='function')onImageGenResult(%s)"), *Json));
}
