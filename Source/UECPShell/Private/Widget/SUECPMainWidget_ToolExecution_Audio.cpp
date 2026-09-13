// Copyright 2026, BlueprintsLab, All rights reserved.

#include "SUECPMainWidget.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "SoundGenManager.h"
#include "ApiKeyManager.h"
#include "Widget/UUECPAppBridge.h"

FToolExecutionResult SUECPMainWidget::ExecuteTool_GenerateSound(const TSharedPtr<FJsonObject>& Args)
{
	FToolExecutionResult Result;

	FString Text;
	Args->TryGetStringField(TEXT("text"), Text);
	if (Text.IsEmpty())
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("'text' is required");
		return Result;
	}

	FString Voice, AssetName, SavePath, Model;
	Args->TryGetStringField(TEXT("voice"),      Voice);
	Args->TryGetStringField(TEXT("asset_name"), AssetName);
	Args->TryGetStringField(TEXT("save_path"),  SavePath);
	Args->TryGetStringField(TEXT("model"),      Model);
	double SpeedD = 1.0;
	Args->TryGetNumberField(TEXT("speed"), SpeedD);

	FSoundGenRequest Req;
	Req.Text            = Text;
	Req.Voice           = Voice.IsEmpty()   ? FApiKeyManager::Get().GetActiveSoundGenVoice()   : Voice;
	Req.Model           = Model.IsEmpty()   ? FApiKeyManager::Get().GetActiveSoundGenModel()   : Model;
	Req.CustomAssetName = AssetName;
	Req.SavePath        = SavePath;
	Req.Speed           = static_cast<float>(SpeedD);

	FString ApiKey   = FApiKeyManager::Get().GetActiveSoundGenApiKey();
	FString Endpoint = FApiKeyManager::Get().GetActiveSoundGenEndpoint();

	if (ApiKey.IsEmpty())
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("No Sound Generation API key configured. Set one in Settings → Voice & Sound.");
		return Result;
	}

	FSoundGenManager::Get().GenerateSound(Req, ApiKey, Endpoint,
		[this, Text](const FSoundGenResult& R)
		{
			if (R.bSuccess)
			{
				UE_LOG(LogTemp, Log, TEXT("Sound generated: %s"), *R.AssetPath);
				if (AppBridgeObject) AppBridgeObject->PushToast(
					FString::Printf(TEXT("Sound generated: %s"), *R.AssetPath), TEXT("success"));
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("Sound generation failed: %s"), *R.ErrorMessage);
				if (AppBridgeObject) AppBridgeObject->PushToast(
					FString::Printf(TEXT("Sound generation failed: %s"), *R.ErrorMessage), TEXT("error"));
			}
		});

	Result.bSuccess = true;
	TSharedPtr<FJsonObject> RObj = MakeShareable(new FJsonObject);
	RObj->SetBoolField(TEXT("success"), true);
	RObj->SetBoolField(TEXT("pending"), true);
	RObj->SetStringField(TEXT("message"), FString::Printf(TEXT("Sound generation started for: '%s'"), *Text.Left(60)));
	FString RStr;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&RStr);
	FJsonSerializer::Serialize(RObj.ToSharedRef(), W);
	Result.ResultJson = RStr;
	return Result;
}

bool SUECPMainWidget::TryDispatchAudioDepthTool(const FString& ToolName,
	const TSharedPtr<FJsonObject>& Arguments, FToolExecutionResult& OutResult)
{
	if (ToolName == TEXT("generate_sound")) { OutResult = ExecuteTool_GenerateSound(Arguments); return true; }
	return false;
}
