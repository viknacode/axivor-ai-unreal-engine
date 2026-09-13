// Copyright 2026, BlueprintsLab, All rights reserved

#include "TextureGenManager.h"
#include "ApiKeyManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Base64.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "AssetToolsModule.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/TextureFactory.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/Material.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Engine/Texture2D.h"
#include "EditorAssetLibrary.h"

FTextureGenManager& FTextureGenManager::Get()
{
	static FTextureGenManager Instance;
	return Instance;
}

void FTextureGenManager::GenerateTexture(const FTextureGenRequest& Request, const FString& ApiKey, TFunction<void(const FTextureGenResult&)> OnComplete)
{
	FString Endpoint = FApiKeyManager::Get().GetActiveTextureGenEndpoint();
	FString ModelName = FApiKeyManager::Get().GetActiveTextureGenModel();

	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetTimeout(300.0f);
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetURL(Endpoint);
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

	bool bIsOpenAIFormat = Endpoint.Contains(TEXT("together.xyz")) || Endpoint.Contains(TEXT("openrouter.ai")) || Endpoint.Contains(TEXT("chat/completions"));
	bool bIsDALL_EFormat = Endpoint.Contains(TEXT("images/generations")) || Endpoint.Contains(TEXT("api.openai.com/v1/images"));
	bool bIsGoogleNative = Endpoint.Contains(TEXT("generativelanguage.googleapis.com"));
	bool bIsImagenFormat = Endpoint.Contains(TEXT("imagen")) || Endpoint.Contains(TEXT("predict"));

	if (bIsDALL_EFormat || bIsOpenAIFormat)
	{
		HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
	}
	else if (bIsGoogleNative || bIsImagenFormat)
	{
		HttpRequest->SetHeader(TEXT("x-goog-api-key"), ApiKey);
	}

	TSharedPtr<FJsonObject> JsonPayload = MakeShareable(new FJsonObject);

	if (bIsDALL_EFormat)
	{
		JsonPayload->SetStringField(TEXT("model"), ModelName);
		JsonPayload->SetStringField(TEXT("prompt"), Request.Prompt);
		JsonPayload->SetNumberField(TEXT("n"), 1);
		JsonPayload->SetStringField(TEXT("size"), Request.AspectRatio.IsEmpty() ? TEXT("1024x1024") : Request.AspectRatio);
		JsonPayload->SetStringField(TEXT("response_format"), TEXT("b64_json"));
	}
	else if (bIsImagenFormat)
	{
		TArray<TSharedPtr<FJsonValue>> InstancesArray;
		TSharedPtr<FJsonObject> InstanceObj = MakeShareable(new FJsonObject);
		InstanceObj->SetStringField(TEXT("prompt"), Request.Prompt);
		InstancesArray.Add(MakeShared<FJsonValueObject>(InstanceObj));
		JsonPayload->SetArrayField(TEXT("instances"), InstancesArray);

		TSharedPtr<FJsonObject> ParametersObj = MakeShareable(new FJsonObject);
		ParametersObj->SetNumberField(TEXT("sampleCount"), 1);
		FString ConvertedAspectRatio = Request.AspectRatio;
		if (Request.AspectRatio.Contains(TEXT("x")))
		{
			if (Request.AspectRatio == TEXT("1024x1024") || Request.AspectRatio == TEXT("512x512")) ConvertedAspectRatio = TEXT("1:1");
			else if (Request.AspectRatio == TEXT("1792x1024") || Request.AspectRatio == TEXT("1024x576")) ConvertedAspectRatio = TEXT("16:9");
			else if (Request.AspectRatio == TEXT("1024x1792") || Request.AspectRatio == TEXT("576x1024")) ConvertedAspectRatio = TEXT("9:16");
			else if (Request.AspectRatio == TEXT("1024x768")) ConvertedAspectRatio = TEXT("4:3");
			else if (Request.AspectRatio == TEXT("768x1024")) ConvertedAspectRatio = TEXT("3:4");
		}
		ParametersObj->SetStringField(TEXT("aspectRatio"), ConvertedAspectRatio);
		JsonPayload->SetObjectField(TEXT("parameters"), ParametersObj);
	}
	else if (bIsOpenAIFormat)
	{
		JsonPayload->SetStringField(TEXT("model"), ModelName);

		TArray<TSharedPtr<FJsonValue>> MessagesArray;
		TSharedPtr<FJsonObject> MessageObj = MakeShareable(new FJsonObject);
		MessageObj->SetStringField(TEXT("role"), TEXT("user"));

		TArray<TSharedPtr<FJsonValue>> ContentArray;
		TSharedPtr<FJsonObject> TextContent = MakeShareable(new FJsonObject);
		TextContent->SetStringField(TEXT("type"), TEXT("text"));
		TextContent->SetStringField(TEXT("text"), Request.Prompt);
		ContentArray.Add(MakeShared<FJsonValueObject>(TextContent));

		MessageObj->SetArrayField(TEXT("content"), ContentArray);
		MessagesArray.Add(MakeShared<FJsonValueObject>(MessageObj));
		JsonPayload->SetArrayField(TEXT("messages"), MessagesArray);
	}
	else
	{
		TArray<TSharedPtr<FJsonValue>> ContentsArray;
		TSharedPtr<FJsonObject> ContentObj = MakeShareable(new FJsonObject);
		TArray<TSharedPtr<FJsonValue>> PartsArray;
		TSharedPtr<FJsonObject> TextPart = MakeShareable(new FJsonObject);
		TextPart->SetStringField(TEXT("text"), Request.Prompt);
		PartsArray.Add(MakeShared<FJsonValueObject>(TextPart));
		ContentObj->SetArrayField(TEXT("parts"), PartsArray);
		ContentsArray.Add(MakeShared<FJsonValueObject>(ContentObj));
		JsonPayload->SetArrayField(TEXT("contents"), ContentsArray);
	}

	FString RequestBody;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(JsonPayload.ToSharedRef(), Writer);
	HttpRequest->SetContentAsString(RequestBody);

	HttpRequest->OnProcessRequestComplete().BindLambda([this, OnComplete = MoveTemp(OnComplete), bIsImagenFormat, bIsDALL_EFormat, bIsOpenAIFormat, Request](FHttpRequestPtr HttpRequest, FHttpResponsePtr Response, bool bWasSuccessful)
	{
		OnGenerationComplete(HttpRequest, Response, bWasSuccessful, OnComplete, bIsImagenFormat, bIsDALL_EFormat, bIsOpenAIFormat, Request);
	});
	HttpRequest->ProcessRequest();

	ActiveRequest = HttpRequest;
}

void FTextureGenManager::CancelGeneration()
{
	if (ActiveRequest.IsValid())
	{
		ActiveRequest->CancelRequest();
		ActiveRequest.Reset();
	}
}

void FTextureGenManager::OnGenerationComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, TFunction<void(const FTextureGenResult&)> Callback, bool bIsImagenFormat, bool bIsDALL_EFormat, bool bIsOpenAIFormat, const FTextureGenRequest& OriginalRequest)
{
	FTextureGenResult Result;
	Result.bSuccess = false;

	if (bWasSuccessful && Response.IsValid() && Response->GetResponseCode() == 200)
	{
		FString ResponseString = Response->GetContentAsString();
		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(*ResponseString);

		if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
		{
			FString Base64Data;

			if (bIsImagenFormat)
			{
				TArray<TSharedPtr<FJsonValue>> PredictionsArray = JsonObject->GetArrayField(TEXT("predictions"));
				if (PredictionsArray.Num() > 0)
				{
					TSharedPtr<FJsonObject> FirstPrediction = PredictionsArray[0]->AsObject();
					if (FirstPrediction.IsValid())
					{
						TSharedPtr<FJsonObject> ImageObj = FirstPrediction->GetObjectField(TEXT("image"));
						if (ImageObj.IsValid())
						{
							if (ImageObj->HasField(TEXT("imageBytes")))
							{
								Base64Data = ImageObj->GetStringField(TEXT("imageBytes"));
							}
							else if (ImageObj->HasField(TEXT("bytesBase64Encoded")))
							{
								Base64Data = ImageObj->GetStringField(TEXT("bytesBase64Encoded"));
							}
							else if (ImageObj->HasField(TEXT("bytesBase64")))
							{
								Base64Data = ImageObj->GetStringField(TEXT("bytesBase64"));
							}
							else if (ImageObj->HasField(TEXT("data")))
							{
								Base64Data = ImageObj->GetStringField(TEXT("data"));
							}
						}

						if (Base64Data.IsEmpty())
						{
							FString TestString = FirstPrediction->HasField(TEXT("bytesBase64Encoded")) ? FirstPrediction->GetStringField(TEXT("bytesBase64Encoded")) : TEXT("");
							if (!TestString.IsEmpty())
							{
								Base64Data = TestString;
							}
							else
							{
								TestString = FirstPrediction->HasField(TEXT("bytesBase64")) ? FirstPrediction->GetStringField(TEXT("bytesBase64")) : TEXT("");
								if (!TestString.IsEmpty())
								{
									Base64Data = TestString;
								}
								else
								{
									TestString = FirstPrediction->HasField(TEXT("imageBytes")) ? FirstPrediction->GetStringField(TEXT("imageBytes")) : TEXT("");
									if (!TestString.IsEmpty())
									{
										Base64Data = TestString;
									}
									else
									{
										TestString = FirstPrediction->HasField(TEXT("imageBase64")) ? FirstPrediction->GetStringField(TEXT("imageBase64")) : TEXT("");
										if (!TestString.IsEmpty())
										{
											Base64Data = TestString;
										}
										else
										{
											TestString = FirstPrediction->HasField(TEXT("data")) ? FirstPrediction->GetStringField(TEXT("data")) : TEXT("");
											if (!TestString.IsEmpty())
											{
												Base64Data = TestString;
											}
											else
											{
												for (const auto& FieldPair : FirstPrediction->Values)
												{
													FString ValueType;
													if (FieldPair.Value->Type == EJson::String)
													{
														ValueType = TEXT("String");
													}
													else if (FieldPair.Value->Type == EJson::Object)
													{
														ValueType = TEXT("Object");
													}
													else if (FieldPair.Value->Type == EJson::Array)
													{
														ValueType = TEXT("Array");
													}
													else if (FieldPair.Value->Type == EJson::Null)
													{
														ValueType = TEXT("Null");
													}
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}
			else if (bIsDALL_EFormat)
			{
				const TArray<TSharedPtr<FJsonValue>>& DataArray = JsonObject->GetArrayField(TEXT("data"));
				if (DataArray.Num() > 0)
				{
					TSharedPtr<FJsonObject> FirstImage = DataArray[0]->AsObject();
					if (FirstImage.IsValid())
					{
						Base64Data = FirstImage->GetStringField(TEXT("b64_json"));
					}
				}
			}
			else if (bIsOpenAIFormat)
			{
				TArray<TSharedPtr<FJsonValue>> ChoicesArray = JsonObject->GetArrayField(TEXT("choices"));
				if (ChoicesArray.Num() > 0)
				{
					TSharedPtr<FJsonObject> FirstChoice = ChoicesArray[0]->AsObject();
					if (FirstChoice.IsValid())
					{
						TSharedPtr<FJsonObject> MessageObj = FirstChoice->GetObjectField(TEXT("message"));
						if (MessageObj.IsValid())
						{
							TArray<TSharedPtr<FJsonValue>> ContentArray = MessageObj->GetArrayField(TEXT("content"));
							if (ContentArray.Num() > 0)
							{
								for (const TSharedPtr<FJsonValue>& ContentItem : ContentArray)
								{
									TSharedPtr<FJsonObject> ContentObjItem = ContentItem->AsObject();
									if (ContentObjItem.IsValid())
									{
										FString Type = ContentObjItem->GetStringField(TEXT("type"));
										if (Type == TEXT("image_url"))
										{
											TSharedPtr<FJsonObject> ImageURLObj = ContentObjItem->GetObjectField(TEXT("image_url"));
											if (ImageURLObj.IsValid())
											{
												FString URL = ImageURLObj->GetStringField(TEXT("url"));
												if (!URL.IsEmpty())
												{
													if (URL.StartsWith(TEXT("data:")))
													{
														int32 CommaPos = URL.Find(TEXT(","));
														if (CommaPos != INDEX_NONE)
														{
															Base64Data = URL.RightChop(CommaPos + 1);
														}
														break;
													}
												}
											}
										}
										else if (Type == TEXT("text"))
										{
											FString TextContent = ContentObjItem->GetStringField(TEXT("text"));
											if (!TextContent.IsEmpty())
											{
												Base64Data = TextContent;
												break;
											}
										}
									}
								}
							}
						}
					}
				}
			}
			else
			{
				TArray<TSharedPtr<FJsonValue>> CandidatesArray = JsonObject->GetArrayField(TEXT("candidates"));
				if (CandidatesArray.Num() > 0)
				{
					TSharedPtr<FJsonObject> FirstCandidate = CandidatesArray[0]->AsObject();
					if (FirstCandidate.IsValid())
					{
						TSharedPtr<FJsonObject> ContentObj = FirstCandidate->GetObjectField(TEXT("content"));
						if (ContentObj.IsValid())
						{
							TArray<TSharedPtr<FJsonValue>> PartsArray = ContentObj->GetArrayField(TEXT("parts"));
							if (PartsArray.Num() > 0)
							{
								TSharedPtr<FJsonObject> FirstPart = PartsArray[0]->AsObject();
								if (FirstPart.IsValid())
								{
									TSharedPtr<FJsonObject> InlineDataObj = FirstPart->GetObjectField(TEXT("inlineData"));
									if (InlineDataObj.IsValid())
									{
										Base64Data = InlineDataObj->GetStringField(TEXT("data"));
									}
								}
							}
						}
					}
				}
			}

			if (!Base64Data.IsEmpty())
			{
				TArray<uint8> ImageData;
				FBase64::Decode(*Base64Data, ImageData);

				if (ImageData.Num() > 0)
				{
					FString AssetName;
					if (!OriginalRequest.CustomAssetName.IsEmpty())
					{
						AssetName = OriginalRequest.CustomAssetName;
					}
					else
					{
						FString Timestamp = FDateTime::Now().ToString();
						AssetName = FString::Printf(TEXT("Gen_Tex_%s"), *Timestamp.Replace(TEXT(":"), TEXT("_")).Replace(TEXT("."), TEXT("_")));
					}
					FString SavePath = OriginalRequest.SavePath.IsEmpty() ? TEXT("/Game/GeneratedTextures") : OriginalRequest.SavePath;
					FString Filename = FString::Printf(TEXT("GeneratedTextures/%s.png"), *AssetName);

					SaveTextureToFile(ImageData, Filename);
					Result.ImagePath = FPaths::ProjectSavedDir() / Filename;

					ImportTextureAsAsset(Result.ImagePath, AssetName, SavePath, Result);

				}
			}
			else if (Result.ErrorMessage.IsEmpty())
			{
				Result.ErrorMessage = TEXT("No image data found in response");
			}
		}
		else if (Result.ErrorMessage.IsEmpty())
		{
			Result.ErrorMessage = TEXT("Failed to parse response JSON");
		}
	}
	else if (!Result.bSuccess)
	{
		if (Response.IsValid())
		{
			FString ResponseContent = Response->GetContentAsString();
			Result.ErrorMessage = FString::Printf(TEXT("HTTP %d: %s"), Response->GetResponseCode(), *ResponseContent.Left(500));
		}
		else
		{
			Result.ErrorMessage = TEXT("Request failed - no response");
		}
	}

	if (!Result.bSuccess && Result.ErrorMessage.IsEmpty())
	{
		Result.ErrorMessage = TEXT("Failed to generate texture - unknown error");
	}

	Callback(Result);
	ActiveRequest.Reset();
}

void FTextureGenManager::SaveTextureToFile(const TArray<uint8>& ImageData, const FString& Filename)
{
	FString FullPath = FPaths::ProjectSavedDir() / Filename;
	FString Directory = FPaths::GetPath(FullPath);

	if (!IFileManager::Get().DirectoryExists(*Directory))
	{
		IFileManager::Get().MakeDirectory(*Directory);
	}

	FFileHelper::SaveArrayToFile(ImageData, *FullPath);
}

void FTextureGenManager::ImportTextureAsAsset(const FString& FilePath, const FString& AssetName, const FString&PackagePath, FTextureGenResult& Result)
{
	FString FullPackagePath = PackagePath;
	while (FullPackagePath.EndsWith(TEXT("/"))) FullPackagePath.LeftChopInline(1, EAllowShrinking::No);
	FString AssetPath = FString::Printf(TEXT("%s/%s"), *FullPackagePath, *AssetName);

	if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
	{
		Result.ErrorMessage = FString::Printf(TEXT("Asset already exists: %s"), *AssetPath);
		return;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UTextureFactory* TextureFactory = NewObject<UTextureFactory>();
	TextureFactory->SuppressImportOverwriteDialog();

	TArray<FString> FilesToImport;
	FilesToImport.Add(FilePath);

	TArray<UObject*> ImportedAssets = AssetToolsModule.Get().ImportAssets(FilesToImport, FullPackagePath, TextureFactory, false);

	if (ImportedAssets.Num() > 0)
	{
		UTexture2D* Texture = Cast<UTexture2D>(ImportedAssets[0]);
		if (Texture)
		{
			Texture->CompressionSettings = TC_Default;
			Texture->SRGB = true;
			Texture->MarkPackageDirty();
			Texture->UpdateResource();

			Result.AssetPath = AssetPath;
			Result.bSuccess = true;
		}
	}

	if (!Result.bSuccess && Result.ErrorMessage.IsEmpty())
	{
		Result.ErrorMessage = TEXT("Failed to import texture");
	}
}

void FTextureGenManager::CreateMaterialFromTexture(const FString& TexturePath, const FString& MaterialName, const FString& PackagePath, FTextureGenResult& Result)
{

	FString MaterialPath = PackagePath;
	if (!MaterialPath.EndsWith(TEXT("/")))
	{
		MaterialPath += TEXT("/");
	}
	MaterialPath += MaterialName;

	if (UEditorAssetLibrary::DoesAssetExist(MaterialPath))
	{
		Result.MaterialPath = MaterialPath;
		return;
	}

	UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *TexturePath);
	if (!Texture)
	{
		Result.ErrorMessage = TEXT("Failed to load texture for material creation");
		return;
	}

	UPackage* Package = CreatePackage(*MaterialPath);
	if (!Package)
	{
		Result.ErrorMessage = TEXT("Failed to create package");
		return;
	}

	UMaterialFactoryNew* MaterialFactory = NewObject<UMaterialFactoryNew>();
	UMaterial* NewMaterial = Cast<UMaterial>(MaterialFactory->FactoryCreateNew(
		UMaterial::StaticClass(),
		Package,
		FName(*MaterialName),
		RF_Public | RF_Standalone,
		nullptr,
		GWarn
	));

	if (!NewMaterial)
	{
		Result.ErrorMessage = TEXT("Failed to create material");
		return;
	}

	UMaterialExpressionTextureSample* TextureExpression = NewObject<UMaterialExpressionTextureSample>(NewMaterial);
	TextureExpression->Texture = Texture;
	TextureExpression->SamplerType = SAMPLERTYPE_Color;
	NewMaterial->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(TextureExpression);

	NewMaterial->GetEditorOnlyData()->BaseColor.Expression = TextureExpression;

	NewMaterial->PreEditChange(nullptr);
	NewMaterial->PostEditChange();
	Package->MarkPackageDirty();

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	AssetRegistryModule.AssetCreated(NewMaterial);

	Result.MaterialPath = MaterialPath;
	Result.bSuccess = true;
}
