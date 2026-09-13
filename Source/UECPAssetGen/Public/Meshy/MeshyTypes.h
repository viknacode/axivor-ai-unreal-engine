// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct FMeshyError
{
	FString Type;
	FString Message;
	int32   Code = 0;
	FString DocUrl;

	bool IsEmpty() const { return Type.IsEmpty() && Message.IsEmpty() && Code == 0; }
	FString ToDisplayString() const
	{
		if (!Message.IsEmpty()) return Type.IsEmpty() ? Message : FString::Printf(TEXT("%s: %s"), *Type, *Message);
		if (!Type.IsEmpty())    return Type;
		if (Code != 0)          return FString::Printf(TEXT("Meshy error code %d"), Code);
		return TEXT("Unknown Meshy error");
	}
};

struct FMeshyHttpResult
{
	bool    bSuccess = false;
	int32   HttpCode = 0;
	FString ResponseBody;
	TSharedPtr<FJsonObject> ResponseJson;
	FMeshyError Error;
};

enum class EMeshyTaskKind : uint8
{
	TextTo3DPreview,
	TextTo3DRefine,
	ImageTo3D,
	MultiImageTo3D,
	Remesh,
	Retexture,
	Rigging,
	Animation,
	TextToImage,
};

struct FMeshyTaskInfo
{
	FString          TaskId;
	EMeshyTaskKind   Kind = EMeshyTaskKind::TextTo3DPreview;
	FString          PollUrl;
	FString          ApiKey;
	FString          Label;
	int32            Progress = 0;
	FString          Status;
	FDateTime        CreatedAt;
};

struct FMeshyTaskResult
{
	bool                     bSuccess = false;
	FString                  TaskId;
	FString                  Status;
	FString                  ErrorMessage;
	int32                    Progress = 0;
	TSharedPtr<FJsonObject>  ResponseJson;
};
