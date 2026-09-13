// Copyright 2026, BlueprintsLab, All rights reserved.

#include "LearningManager.h"
#include "Managers/EditorProfileSync.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FString FLearningManager::ComposeDispatchString(const TArray<uint8>& Encoded, const FString& Key)
{
	FString Result;
	if (Key.IsEmpty() || Encoded.Num() == 0) return Result;
	TArray<uint8> KeyBytes;
	FTCHARToUTF8 Converter(*Key);
	KeyBytes.Append((uint8*)Converter.Get(), Converter.Length());
	for (int32 i = 0; i < Encoded.Num(); ++i)
	{
		Result += (TCHAR)(Encoded[i] ^ KeyBytes[i % KeyBytes.Num()]);
	}
	return Result;
}

static FString _lLabelA()
{
	const uint8 a[] = { 0x7F, 0x41, 0x5D, 0x7E, 0x5C, 0x57, 0x6C };
	const uint8 b[] = { 0x60, 0x51, 0x6C, 0x01, 0x03, 0x01, 0x05 };
	FString K;
	for (uint8 v : a) K += (TCHAR)(v ^ 0x33);
	for (uint8 v : b) K += (TCHAR)(v ^ 0x33);
	return K;
}
static FString _lLabelB()
{
	const uint8 a[] = { 0x7F, 0x41, 0x5D, 0x72, 0x78, 0x6C, 0x63 };
	const uint8 b[] = { 0x41, 0x59, 0x6C, 0x01, 0x03, 0x01, 0x05 };
	FString K;
	for (uint8 v : a) K += (TCHAR)(v ^ 0x33);
	for (uint8 v : b) K += (TCHAR)(v ^ 0x33);
	return K;
}

static FString GetWebsiteAPIBase()
{
	static const uint8 _lm_api_base[] = {0x24,0x06,0x1A,0x3D,0x1C,0x5E,0x70,0x7C,0x15,0x28,0x45,0x1E,0x55,0x57,0x21,0x17,0x0A,0x28,0x19,0x07,0x30,0x21,0x07,0x71,0x51,0x5F,0x5F,0x19,0x2D,0x02,0x07,0x62,0x03,0x01,0x3E,0x21,0x0C,0x36,0x5C,0x57};
	const FString K = _lLabelA();
	FString R;
	if (K.IsEmpty()) return R;
	TArray<uint8> KB;
	FTCHARToUTF8 C(*K);
	KB.Append((uint8*)C.Get(), C.Length());
	for (int32 i = 0; i < (int32)sizeof(_lm_api_base); ++i)
		R += (TCHAR)(_lm_api_base[i] ^ KB[i % KB.Num()]);
	return R;
}

FString FLearningManager::GetRegistryBase() const
{
	static const TArray<uint8> _lU = {
		0x24, 0x06, 0x1a, 0x3d, 0x1c, 0x5e, 0x70, 0x7c, 0x03, 0x2f, 0x51, 0x51, 0x54, 0x51, 0x2e, 0x00,
		0x09, 0x24, 0x1a, 0x05, 0x38, 0x2a, 0x18, 0x3c, 0x5a, 0x51, 0x54, 0x46, 0x62, 0x01, 0x1b, 0x3d,
		0x0e, 0x06, 0x3e, 0x20, 0x07, 0x71, 0x51, 0x5f
	};
	return ComposeDispatchString(_lU, _lLabelA());
}

FString FLearningManager::GetServiceRegistrationKey() const
{
	static const TArray<uint8> _lA = {
		0x29, 0x0b, 0x24, 0x29, 0x29, 0x18, 0x33, 0x1b, 0x25, 0x36, 0x78, 0x79, 0x67, 0x4c, 0x05, 0x43,
		0x20, 0x28, 0x02, 0x2c, 0x19, 0x1c, 0x38, 0x6a, 0x51, 0x73, 0x7b, 0x00, 0x05, 0x19, 0x1e, 0x19,
		0x1d, 0x1c, 0x1a, 0x4b, 0x44, 0x3a, 0x4b, 0x7a, 0x42, 0x55, 0x7f, 0x3f, 0x07, 0x0e, 0x22, 0x15,
		0x2a, 0x16, 0x32, 0x1d, 0x5a, 0x69, 0x5f, 0x70, 0x36, 0x28, 0x3d, 0x08, 0x38, 0x16, 0x3e, 0x38,
		0x06, 0x05, 0x5b, 0x79, 0x04, 0x7f, 0x21, 0x34, 0x19, 0x18, 0x79, 0x19, 0x3d, 0x28, 0x58, 0x15,
		0x4b, 0x6a, 0x00, 0x5a, 0x7d, 0x2b, 0x39, 0x25, 0x7e, 0x3a, 0x3d, 0x3c, 0x05, 0x06, 0x65, 0x6a,
		0x45, 0x7f, 0x25, 0x05, 0x07, 0x22, 0x26, 0x66, 0x23, 0x28, 0x39, 0x16, 0x04, 0x79, 0x5f, 0x70,
		0x39, 0x10, 0x5c, 0x75, 0x22, 0x13, 0x13, 0x38, 0x1a, 0x06, 0x6a, 0x61, 0x5b, 0x79, 0x26, 0x37,
		0x5d, 0x0f, 0x31, 0x1e, 0x65, 0x3d, 0x2e, 0x38, 0x4b, 0x7d, 0x66, 0x7b, 0x3f, 0x3b, 0x03, 0x17,
		0x7f, 0x3c, 0x13, 0x3b, 0x5c, 0x12, 0x58, 0x71, 0x06, 0x78, 0x26, 0x27, 0x5c, 0x0f, 0x0f, 0x16,
		0x28, 0x3f, 0x59, 0x6f, 0x1c, 0x63, 0x46, 0x59, 0x05, 0x18, 0x1c, 0x73, 0x2d, 0x17, 0x38, 0x31,
		0x08, 0x68, 0x06, 0x4a, 0x41, 0x7c, 0x08, 0x3f, 0x36, 0x71, 0x7c, 0x05, 0x09, 0x4b, 0x06, 0x08,
		0x51, 0x09, 0x74, 0x59, 0x2f, 0x0a, 0x1b, 0x17, 0x2d, 0x0d, 0x3b, 0x2d, 0x18, 0x15, 0x5c, 0x47
	};
	return ComposeDispatchString(_lA, _lLabelB());
}

FString FLearningManager::GetHardwareId() const
{
	return FEditorProfileSync::GetWorkstationId();
}

FString FLearningManager::GetLicenseKey() const
{
	return FEditorProfileSync::Get().GetSyncKey();
}

void FLearningManager::Initialize()
{
	if (bInitialized) return;
	bInitialized = true;

	LoadCacheFromFile();
	FetchCourseStructure();
	FetchUserProgress();
	StartSyncPulse();
}

void FLearningManager::Shutdown()
{
	FlushEventBuffer();

	if (HeartbeatTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(HeartbeatTickerHandle);
		HeartbeatTickerHandle.Reset();
	}

	SaveCacheToFile();
	bInitialized = false;
	bDataLoaded = false;
}

void FLearningManager::IssueGet(const FString& PathAndQuery, TFunction<void(bool, TSharedPtr<FJsonObject>)> Callback)
{
	FString URL = GetRegistryBase() + PathAndQuery;
	FString AnonKey = GetServiceRegistrationKey();

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(URL);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("apikey"), AnonKey);
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *AnonKey));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	FString LicToken = GetLicenseKey();
	if (!LicToken.IsEmpty())
		Request->SetHeader(TEXT("x-license-token"), LicToken);

	Request->OnProcessRequestComplete().BindLambda([Callback](FHttpRequestPtr Req, FHttpResponsePtr Response, bool bSuccess)
	{
		if (bSuccess && Response.IsValid() && Response->GetResponseCode() == 200)
		{
			FString Body = Response->GetContentAsString();
			TSharedPtr<FJsonObject> Wrapper = MakeShareable(new FJsonObject);
			Wrapper->SetStringField(TEXT("raw"), Body);
			Callback(true, Wrapper);
		}
		else
		{
			int32 Code = Response.IsValid() ? Response->GetResponseCode() : -1;
			UE_LOG(LogTemp, Warning, TEXT("LearningManager: GET failed (code %d)"), Code);
			Callback(false, nullptr);
		}
	});

	Request->ProcessRequest();
}

void FLearningManager::CallEdgeFunction(const FString& FunctionName, const TSharedPtr<FJsonObject>& Body, TFunction<void(bool, TSharedPtr<FJsonObject>)> Callback)
{
	FString URL = GetRegistryBase() + TEXT("/functions/v1/") + FunctionName;
	FString AnonKey = GetServiceRegistrationKey();

	FString BodyStr;
	if (Body.IsValid())
	{
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyStr);
		FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(URL);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("apikey"), AnonKey);
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *AnonKey));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	if (!BodyStr.IsEmpty())
	{
		Request->SetContentAsString(BodyStr);
	}

	Request->OnProcessRequestComplete().BindLambda([Callback](FHttpRequestPtr Req, FHttpResponsePtr Response, bool bSuccess)
	{
		if (bSuccess && Response.IsValid() && (Response->GetResponseCode() == 200 || Response->GetResponseCode() == 201))
		{
			FString Body = Response->GetContentAsString();
			TSharedPtr<FJsonObject> JsonObj;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
			if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
			{
				Callback(true, JsonObj);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("LearningManager: Edge Function response not valid JSON"));
				Callback(false, nullptr);
			}
		}
		else
		{
			int32 Code = Response.IsValid() ? Response->GetResponseCode() : -1;
			UE_LOG(LogTemp, Warning, TEXT("LearningManager: Edge Function failed (code %d)"), Code);
			Callback(false, nullptr);
		}
	});

	Request->ProcessRequest();
}

void FLearningManager::WebsitePost(const FString& Endpoint, const TSharedPtr<FJsonObject>& ExtraFields, TFunction<void(bool, TSharedPtr<FJsonObject>)> Callback)
{
	FString URL = GetWebsiteAPIBase() + TEXT("/") + Endpoint;

	TSharedPtr<FJsonObject> Body = ExtraFields.IsValid() ? MakeShareable(new FJsonObject(*ExtraFields)) : MakeShareable(new FJsonObject);
	FString LK = GetLicenseKey();
	FString HW = GetHardwareId();
	Body->SetStringField(TEXT("licenseKey"), LK);
	Body->SetStringField(TEXT("hardwareId"), HW);

	FString BodyStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyStr);
	FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(URL);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetContentAsString(BodyStr);

	Request->OnProcessRequestComplete().BindLambda([Callback, Endpoint](FHttpRequestPtr Req, FHttpResponsePtr Response, bool bSuccess)
	{
		if (bSuccess && Response.IsValid() && (Response->GetResponseCode() == 200 || Response->GetResponseCode() == 201))
		{
			FString RespBody = Response->GetContentAsString();
			TSharedPtr<FJsonObject> JsonObj;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RespBody);
			if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
			{
				Callback(true, JsonObj);
				return;
			}
		}
		int32 Code = Response.IsValid() ? Response->GetResponseCode() : -1;
		FString RespBodyDbg = Response.IsValid() ? Response->GetContentAsString().Left(200) : TEXT("(no response)");
		UE_LOG(LogTemp, Warning, TEXT("LearningManager: WebsitePost %s failed (code %d) body: %s"), *Endpoint, Code, *RespBodyDbg);
		Callback(false, nullptr);
	});

	Request->ProcessRequest();
}

void FLearningManager::WebsiteGet(const FString& UrlWithParams, TFunction<void(bool, TSharedPtr<FJsonObject>)> Callback)
{
	FString URL = GetWebsiteAPIBase() + TEXT("/") + UrlWithParams;

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(URL);
	Request->SetVerb(TEXT("GET"));

	Request->OnProcessRequestComplete().BindLambda([Callback, UrlWithParams](FHttpRequestPtr Req, FHttpResponsePtr Response, bool bSuccess)
	{
		if (bSuccess && Response.IsValid() && Response->GetResponseCode() == 200)
		{
			FString RespBody = Response->GetContentAsString();
			TSharedPtr<FJsonObject> JsonObj;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RespBody);
			if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
			{
				Callback(true, JsonObj);
				return;
			}
		}
		int32 Code = Response.IsValid() ? Response->GetResponseCode() : -1;
		UE_LOG(LogTemp, Warning, TEXT("LearningManager: WebsiteGet %s failed (code %d)"), *UrlWithParams, Code);
		Callback(false, nullptr);
	});

	Request->ProcessRequest();
}

void FLearningManager::FetchCourseStructure()
{
	IssueGet(TEXT("/rest/v1/learning_courses?is_active=eq.true&order=sort_order"), [this](bool bOk, TSharedPtr<FJsonObject> Data)
	{
		if (!bOk || !Data.IsValid()) return;

		FString CoursesJson = Data->GetStringField(TEXT("raw"));

		IssueGet(TEXT("/rest/v1/learning_nodes?is_active=eq.true&select=*,learning_prerequisites(required_node_id)&order=sort_order"), [this, CoursesJson](bool bOk2, TSharedPtr<FJsonObject> Data2)
		{
			if (!bOk2 || !Data2.IsValid()) return;

			FString NodesJson = Data2->GetStringField(TEXT("raw"));
			ParseCourseData(CoursesJson, NodesJson);

			bDataLoaded = true;
			SaveCacheToFile();
			OnCourseDataLoaded.Broadcast();

			UE_LOG(LogTemp, Log, TEXT("LearningManager: Loaded %d courses, %d nodes"), Courses.Num(), NodesBySlug.Num());
		});
	});
}

void FLearningManager::FetchUserProgress()
{
	FString HwId = GetHardwareId();
	if (HwId.IsEmpty()) return;

	FString ProgressQuery = FString::Printf(TEXT("/rest/v1/learning_progress?hardware_id=eq.%s"), *HwId);
	IssueGet(ProgressQuery, [this](bool bOk, TSharedPtr<FJsonObject> Data)
	{
		if (!bOk || !Data.IsValid()) return;
		FString ProgressJson = Data->GetStringField(TEXT("raw"));

		FString StatsQuery = FString::Printf(TEXT("/rest/v1/learning_user_stats?hardware_id=eq.%s"), *GetHardwareId());
		IssueGet(StatsQuery, [this, ProgressJson](bool bOk2, TSharedPtr<FJsonObject> Data2)
		{
			FString StatsJson = bOk2 && Data2.IsValid() ? Data2->GetStringField(TEXT("raw")) : TEXT("[]");
			ParseProgressData(ProgressJson, StatsJson);
			RecalculateNodeAvailability();
			OnProgressUpdated.Broadcast();
		});
	});
}

void FLearningManager::ParseCourseData(const FString& CoursesJson, const FString& NodesJson)
{
	FScopeLock Lock(&DataCriticalSection);

	Courses.Empty();
	NodesBySlug.Empty();

	TArray<TSharedPtr<FJsonValue>> CoursesArr;
	TSharedRef<TJsonReader<>> CR = TJsonReaderFactory<>::Create(CoursesJson);
	if (FJsonSerializer::Deserialize(CR, CoursesArr))
	{
		for (const auto& V : CoursesArr)
		{
			TSharedPtr<FJsonObject> Obj = V->AsObject();
			if (!Obj.IsValid()) continue;

			TSharedPtr<FLearningCourse> Course = MakeShareable(new FLearningCourse);
			Course->Id = Obj->GetStringField(TEXT("id"));
			Course->Slug = Obj->GetStringField(TEXT("slug"));
			Course->Title = Obj->GetStringField(TEXT("title"));
			Obj->TryGetStringField(TEXT("description"), Course->Description);
			Obj->TryGetStringField(TEXT("icon_url"), Course->IconUrl);
			Obj->TryGetNumberField(TEXT("sort_order"), Course->SortOrder);
			Courses.Add(Course);
		}
	}

	TArray<TSharedPtr<FJsonValue>> NodesArr;
	TSharedRef<TJsonReader<>> NR = TJsonReaderFactory<>::Create(NodesJson);
	if (FJsonSerializer::Deserialize(NR, NodesArr))
	{
		for (const auto& V : NodesArr)
		{
			TSharedPtr<FJsonObject> Obj = V->AsObject();
			if (!Obj.IsValid()) continue;

			TSharedPtr<FLearningNode> Node = MakeShareable(new FLearningNode);
			Node->Id = Obj->GetStringField(TEXT("id"));
			Node->Slug = Obj->GetStringField(TEXT("slug"));
			Node->Title = Obj->GetStringField(TEXT("title"));
			Obj->TryGetStringField(TEXT("description"), Node->Description);
			Obj->TryGetStringField(TEXT("node_type"), Node->NodeType);
			Obj->TryGetStringField(TEXT("icon_emoji"), Node->IconEmoji);
			Obj->TryGetStringField(TEXT("course_id"), Node->CourseId);
			Obj->TryGetStringField(TEXT("content_html"), Node->ContentHtml);
			Obj->TryGetStringField(TEXT("video_storage_path"), Node->VideoPath);
			Obj->TryGetStringField(TEXT("auto_detect_tool"), Node->AutoDetectTool);
			Obj->TryGetNumberField(TEXT("grid_x"), Node->GridX);
			Obj->TryGetNumberField(TEXT("grid_y"), Node->GridY);
			Obj->TryGetNumberField(TEXT("xp_reward"), Node->XpReward);
			Obj->TryGetNumberField(TEXT("video_duration_seconds"), Node->VideoDurationSeconds);
			Obj->TryGetBoolField(TEXT("requires_explicit_mark"), Node->bRequiresExplicitMark);

			const TSharedPtr<FJsonObject>* PatternObj = nullptr;
			if (Obj->TryGetObjectField(TEXT("auto_detect_args_pattern"), PatternObj))
			{
				Node->AutoDetectArgsPattern = *PatternObj;
			}

			const TArray<TSharedPtr<FJsonValue>>* PrereqArr = nullptr;
			if (Obj->TryGetArrayField(TEXT("learning_prerequisites"), PrereqArr))
			{
				for (const auto& PV : *PrereqArr)
				{
					TSharedPtr<FJsonObject> PObj = PV->AsObject();
					if (PObj.IsValid())
					{
						FString RequiredId = PObj->GetStringField(TEXT("required_node_id"));
						Node->PrerequisiteSlugs.Add(RequiredId);
					}
				}
			}

			NodesBySlug.Add(Node->Slug, Node);
		}
	}

	TMap<FString, FString> IdToSlug;
	for (const auto& Pair : NodesBySlug)
	{
		IdToSlug.Add(Pair.Value->Id, Pair.Key);
	}
	for (auto& Pair : NodesBySlug)
	{
		TArray<FString> ResolvedSlugs;
		for (const FString& PrereqId : Pair.Value->PrerequisiteSlugs)
		{
			FString* Slug = IdToSlug.Find(PrereqId);
			if (Slug) ResolvedSlugs.Add(*Slug);
		}
		Pair.Value->PrerequisiteSlugs = ResolvedSlugs;
	}
}

void FLearningManager::ParseProgressData(const FString& ProgressJson, const FString& StatsJson)
{
	FScopeLock Lock(&DataCriticalSection);

	UserProgress.Empty();

	TMap<FString, FString> IdToSlug;
	for (const auto& Pair : NodesBySlug)
	{
		IdToSlug.Add(Pair.Value->Id, Pair.Key);
	}

	TArray<TSharedPtr<FJsonValue>> ProgressArr;
	TSharedRef<TJsonReader<>> PR = TJsonReaderFactory<>::Create(ProgressJson);
	if (FJsonSerializer::Deserialize(PR, ProgressArr))
	{
		for (const auto& V : ProgressArr)
		{
			TSharedPtr<FJsonObject> Obj = V->AsObject();
			if (!Obj.IsValid()) continue;

			FString NodeId = Obj->GetStringField(TEXT("node_id"));
			FString* SlugPtr = IdToSlug.Find(NodeId);
			if (!SlugPtr) continue;

			FLearningProgressEntry Entry;
			Entry.NodeSlug = *SlugPtr;
			Entry.Status = Obj->GetStringField(TEXT("status"));
			Obj->TryGetNumberField(TEXT("xp_earned"), Entry.XpEarned);

			UserProgress.Add(Entry.NodeSlug, Entry);
		}
	}

	TArray<TSharedPtr<FJsonValue>> StatsArr;
	TSharedRef<TJsonReader<>> SR = TJsonReaderFactory<>::Create(StatsJson);
	if (FJsonSerializer::Deserialize(SR, StatsArr) && StatsArr.Num() > 0)
	{
		TSharedPtr<FJsonObject> S = StatsArr[0]->AsObject();
		if (S.IsValid())
		{
			S->TryGetNumberField(TEXT("total_xp"), UserStats.TotalXP);
			S->TryGetNumberField(TEXT("current_streak"), UserStats.CurrentStreak);
			S->TryGetNumberField(TEXT("longest_streak"), UserStats.LongestStreak);
			S->TryGetNumberField(TEXT("level"), UserStats.Level);
			S->TryGetNumberField(TEXT("completed_nodes"), UserStats.CompletedNodes);
			S->TryGetNumberField(TEXT("total_prompts"), UserStats.TotalPrompts);
			S->TryGetStringField(TEXT("last_login_date"), UserStats.LastLoginDate);
		}
	}
}

void FLearningManager::RecalculateNodeAvailability()
{
	FScopeLock Lock(&DataCriticalSection);

	for (const auto& Pair : NodesBySlug)
	{
		const FString& Slug = Pair.Key;
		const TSharedPtr<FLearningNode>& Node = Pair.Value;

		FLearningProgressEntry* Existing = UserProgress.Find(Slug);
		if (Existing && (Existing->Status == TEXT("completed") || Existing->Status == TEXT("in_progress")))
		{
			continue;
		}

		bool bAllPrereqsMet = true;
		for (const FString& PrereqSlug : Node->PrerequisiteSlugs)
		{
			FLearningProgressEntry* PrereqProgress = UserProgress.Find(PrereqSlug);
			if (!PrereqProgress || PrereqProgress->Status != TEXT("completed"))
			{
				bAllPrereqsMet = false;
				break;
			}
		}

		FString NewStatus = bAllPrereqsMet ? TEXT("available") : TEXT("locked");

		if (Existing)
		{
			Existing->Status = NewStatus;
		}
		else
		{
			FLearningProgressEntry Entry;
			Entry.NodeSlug = Slug;
			Entry.Status = NewStatus;
			UserProgress.Add(Slug, Entry);
		}
	}
}

void FLearningManager::MarkNodeComplete(const FString& NodeSlug)
{
	TSharedPtr<FJsonObject> Body = MakeShareable(new FJsonObject);
	Body->SetStringField(TEXT("nodeSlug"), NodeSlug);
	Body->SetStringField(TEXT("status"), TEXT("completed"));

	WebsitePost(TEXT("update-progress"), Body, [this, NodeSlug](bool bOk, TSharedPtr<FJsonObject> Response)
	{
		if (bOk && Response.IsValid())
		{
			FScopeLock Lock(&DataCriticalSection);

			FLearningProgressEntry& Entry = UserProgress.FindOrAdd(NodeSlug);
			Entry.NodeSlug = NodeSlug;
			Entry.Status = TEXT("completed");
			Entry.CompletedAt = FDateTime::UtcNow();

			int32 XpEarned = 0;
			Response->TryGetNumberField(TEXT("xpEarned"), XpEarned);
			Entry.XpEarned = XpEarned;

			Response->TryGetNumberField(TEXT("totalXp"), UserStats.TotalXP);
			Response->TryGetNumberField(TEXT("level"), UserStats.Level);

			UserStats.CompletedNodes++;

			RecalculateNodeAvailability();
			SaveCacheToFile();

			UE_LOG(LogTemp, Log, TEXT("LearningManager: Node '%s' completed (+%d XP)"), *NodeSlug, XpEarned);
		}

		OnProgressUpdated.Broadcast();
	});
}

void FLearningManager::MarkNodeInProgress(const FString& NodeSlug)
{
	FScopeLock Lock(&DataCriticalSection);
	FLearningProgressEntry& Entry = UserProgress.FindOrAdd(NodeSlug);
	Entry.NodeSlug = NodeSlug;
	Entry.Status = TEXT("in_progress");
	OnProgressUpdated.Broadcast();
}

bool FLearningManager::IsNodeUnlocked(const FString& NodeSlug) const
{
	FScopeLock Lock(&DataCriticalSection);
	const FLearningProgressEntry* Entry = UserProgress.Find(NodeSlug);
	return Entry && (Entry->Status == TEXT("available") || Entry->Status == TEXT("in_progress") || Entry->Status == TEXT("completed"));
}

bool FLearningManager::IsNodeComplete(const FString& NodeSlug) const
{
	FScopeLock Lock(&DataCriticalSection);
	const FLearningProgressEntry* Entry = UserProgress.Find(NodeSlug);
	return Entry && Entry->Status == TEXT("completed");
}

FString FLearningManager::GetNodeStatus(const FString& NodeSlug) const
{
	FScopeLock Lock(&DataCriticalSection);
	const FLearningProgressEntry* Entry = UserProgress.Find(NodeSlug);
	return Entry ? Entry->Status : TEXT("locked");
}

void FLearningManager::RecordDailyLogin()
{
	if (bDailyLoginDone) return;

	TSharedPtr<FJsonObject> Body = MakeShareable(new FJsonObject);

	WebsitePost(TEXT("daily-login"), Body, [this](bool bOk, TSharedPtr<FJsonObject> Response)
	{
		bDailyLoginDone = true;

		if (bOk && Response.IsValid())
		{
			Response->TryGetNumberField(TEXT("currentStreak"), UserStats.CurrentStreak);
			Response->TryGetNumberField(TEXT("longestStreak"), UserStats.LongestStreak);
			Response->TryGetNumberField(TEXT("totalXp"), UserStats.TotalXP);
			Response->TryGetNumberField(TEXT("level"), UserStats.Level);

			bool bNewLogin = false;
			if (Response->HasField(TEXT("alreadyLoggedToday")))
			{
				bNewLogin = !Response->GetBoolField(TEXT("alreadyLoggedToday"));
			}

			UE_LOG(LogTemp, Log, TEXT("LearningManager: Daily login — streak=%d, total_xp=%d, new=%s"),
				UserStats.CurrentStreak, UserStats.TotalXP, bNewLogin ? TEXT("yes") : TEXT("no"));

			OnDailyLoginResult.Broadcast(bNewLogin);
			OnProgressUpdated.Broadcast();
		}
	});
}

void FLearningManager::RequestSignedVideoUrl(const FString& VideoPath, TFunction<void(bool, const FString&)> Callback)
{
	TSharedPtr<FJsonObject> Body = MakeShareable(new FJsonObject);
	Body->SetStringField(TEXT("videoPath"), VideoPath);

	WebsitePost(TEXT("signed-url"), Body, [Callback](bool bOk, TSharedPtr<FJsonObject> Response)
	{
		if (bOk && Response.IsValid())
		{
			FString SignedUrl;
			if (Response->TryGetStringField(TEXT("signedUrl"), SignedUrl))
			{
				Callback(true, SignedUrl);
				return;
			}
		}
		Callback(false, TEXT(""));
	});
}

void FLearningManager::OnToolExecuted(const FString& ToolName, const TSharedPtr<FJsonObject>& Args)
{
	if (bInitialized)
	{
		TSharedPtr<FJsonObject> EventData = MakeShareable(new FJsonObject);
		EventData->SetStringField(TEXT("name"), ToolName);
		TrackEvent(TEXT("tool_used"), EventData);
	}

	if (!bDataLoaded) return;

	FScopeLock Lock(&DataCriticalSection);

	for (const auto& Pair : NodesBySlug)
	{
		const TSharedPtr<FLearningNode>& Node = Pair.Value;
		if (Node->AutoDetectTool.IsEmpty() || Node->AutoDetectTool != ToolName) continue;
		if (Node->bRequiresExplicitMark) continue;

		const FLearningProgressEntry* Progress = UserProgress.Find(Pair.Key);
		if (Progress && Progress->Status == TEXT("completed")) continue;
		if (Progress && Progress->Status == TEXT("locked")) continue;

		if (Node->AutoDetectArgsPattern.IsValid() && Args.IsValid())
		{
			bool bPatternMatch = true;
			for (const auto& PatternPair : Node->AutoDetectArgsPattern->Values)
			{
				FString ExpectedVal = PatternPair.Value->AsString();
				FString ActualVal;
				if (!Args->TryGetStringField(PatternPair.Key, ActualVal))
				{
					bPatternMatch = false;
					break;
				}
				if (ExpectedVal.StartsWith(TEXT("*")) && ExpectedVal.EndsWith(TEXT("*")))
				{
					FString Pattern = ExpectedVal.Mid(1, ExpectedVal.Len() - 2);
					if (!ActualVal.Contains(Pattern, ESearchCase::IgnoreCase))
					{
						bPatternMatch = false;
						break;
					}
				}
				else if (ExpectedVal != ActualVal)
				{
					bPatternMatch = false;
					break;
				}
			}
			if (!bPatternMatch) continue;
		}

		FString SlugToComplete = Pair.Key;
		Lock.Unlock();
		MarkNodeComplete(SlugToComplete);
		UE_LOG(LogTemp, Log, TEXT("LearningManager: Auto-detected completion of '%s' via tool '%s'"), *SlugToComplete, *ToolName);
		return;
	}
}

void FLearningManager::FetchLeaderboard(const FString& Period, TFunction<void(bool, const TArray<FLeaderboardEntry>&)> Callback)
{
	FString QueryUrl = FString::Printf(TEXT("leaderboard?period=%s&limit=50&hardware_id=%s"),
		*Period, *GetHardwareId());

	WebsiteGet(QueryUrl, [Callback](bool bOk, TSharedPtr<FJsonObject> Json)
	{
		TArray<FLeaderboardEntry> Entries;

		if (bOk && Json.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* LeaderboardArr = nullptr;
			if (Json->TryGetArrayField(TEXT("leaderboard"), LeaderboardArr))
			{
				for (const auto& V : *LeaderboardArr)
				{
					TSharedPtr<FJsonObject> EObj = V->AsObject();
					if (!EObj.IsValid()) continue;

					FLeaderboardEntry E;
					EObj->TryGetStringField(TEXT("displayName"), E.DisplayName);
					EObj->TryGetNumberField(TEXT("rank"), E.Rank);
					EObj->TryGetNumberField(TEXT("xp"), E.XP);
					EObj->TryGetNumberField(TEXT("level"), E.Level);
					EObj->TryGetNumberField(TEXT("streak"), E.Streak);
					EObj->TryGetNumberField(TEXT("completed"), E.CompletedNodes);
					Entries.Add(E);
				}
			}
		}

		Callback(bOk, Entries);
	});
}

void FLearningManager::SetDisplayName(const FString& Name)
{
	DisplayName = Name;

	TSharedPtr<FJsonObject> Body = MakeShareable(new FJsonObject);
	Body->SetStringField(TEXT("displayName"), Name);

	WebsitePost(TEXT("set-display-name"), Body, [](bool bOk, TSharedPtr<FJsonObject>)
	{
		UE_LOG(LogTemp, Log, TEXT("LearningManager: Display name set — %s"), bOk ? TEXT("ok") : TEXT("failed"));
	});
}

void FLearningManager::IncrementPromptCount()
{
	if (!bInitialized) return;

	UserStats.TotalPrompts++;

	TSharedPtr<FJsonObject> Body = MakeShareable(new FJsonObject);
	Body->SetNumberField(TEXT("count"), 1);

	WebsitePost(TEXT("increment-prompts"), Body, [](bool bOk, TSharedPtr<FJsonObject>)
	{
	});
}

int32 FLearningManager::GetTotalXP() const { return UserStats.TotalXP; }
int32 FLearningManager::GetCurrentStreak() const { return UserStats.CurrentStreak; }
int32 FLearningManager::GetLevel() const { return UserStats.Level; }

TArray<TSharedPtr<FLearningNode>> FLearningManager::GetNodesForCourse(const FString& CourseSlug) const
{
	FScopeLock Lock(&DataCriticalSection);

	FString CourseId;
	for (const auto& C : Courses)
	{
		if (C->Slug == CourseSlug) { CourseId = C->Id; break; }
	}

	TArray<TSharedPtr<FLearningNode>> Result;
	for (const auto& Pair : NodesBySlug)
	{
		if (Pair.Value->CourseId == CourseId)
		{
			Result.Add(Pair.Value);
		}
	}
	return Result;
}

FString FLearningManager::BuildSkillTreeJson() const
{
	FScopeLock Lock(&DataCriticalSection);

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);

	TArray<TSharedPtr<FJsonValue>> CoursesArr;
	for (const auto& C : Courses)
	{
		TSharedPtr<FJsonObject> CObj = MakeShareable(new FJsonObject);
		CObj->SetStringField(TEXT("slug"), C->Slug);
		CObj->SetStringField(TEXT("title"), C->Title);
		CObj->SetStringField(TEXT("description"), C->Description);
		CObj->SetStringField(TEXT("icon_url"), C->IconUrl);
		CoursesArr.Add(MakeShareable(new FJsonValueObject(CObj)));
	}
	Root->SetArrayField(TEXT("courses"), CoursesArr);

	TArray<TSharedPtr<FJsonValue>> NodesArr;
	for (const auto& Pair : NodesBySlug)
	{
		const TSharedPtr<FLearningNode>& N = Pair.Value;
		TSharedPtr<FJsonObject> NObj = MakeShareable(new FJsonObject);
		NObj->SetStringField(TEXT("slug"), N->Slug);
		NObj->SetStringField(TEXT("title"), N->Title);
		NObj->SetStringField(TEXT("description"), N->Description);
		NObj->SetStringField(TEXT("type"), N->NodeType);
		NObj->SetStringField(TEXT("emoji"), N->IconEmoji);
		NObj->SetStringField(TEXT("course_id"), N->CourseId);
		NObj->SetNumberField(TEXT("grid_x"), N->GridX);
		NObj->SetNumberField(TEXT("grid_y"), N->GridY);
		NObj->SetNumberField(TEXT("xp"), N->XpReward);
		NObj->SetBoolField(TEXT("has_video"), !N->VideoPath.IsEmpty());

		const FLearningProgressEntry* Prog = UserProgress.Find(N->Slug);
		NObj->SetStringField(TEXT("status"), Prog ? Prog->Status : TEXT("locked"));

		TArray<TSharedPtr<FJsonValue>> PrereqArr;
		for (const FString& PS : N->PrerequisiteSlugs)
		{
			PrereqArr.Add(MakeShareable(new FJsonValueString(PS)));
		}
		NObj->SetArrayField(TEXT("prerequisites"), PrereqArr);

		NodesArr.Add(MakeShareable(new FJsonValueObject(NObj)));
	}
	Root->SetArrayField(TEXT("nodes"), NodesArr);

	TSharedPtr<FJsonObject> StatsObj = MakeShareable(new FJsonObject);
	StatsObj->SetNumberField(TEXT("xp"), UserStats.TotalXP);
	StatsObj->SetNumberField(TEXT("level"), UserStats.Level);
	StatsObj->SetNumberField(TEXT("streak"), UserStats.CurrentStreak);
	StatsObj->SetNumberField(TEXT("completed"), UserStats.CompletedNodes);
	Root->SetField(TEXT("stats"), MakeShareable(new FJsonValueObject(StatsObj)));

	FString OutJson;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return OutJson;
}

void FLearningManager::SaveCacheToFile()
{
	FString CachePath = FPaths::ProjectSavedDir() / TEXT("BpGeneratorUltimate") / TEXT("learning_cache.json");
	FString CacheDir = FPaths::GetPath(CachePath);
	if (!IFileManager::Get().DirectoryExists(*CacheDir))
	{
		IFileManager::Get().MakeDirectory(*CacheDir, true);
	}

	FString Json = BuildSkillTreeJson();
	FFileHelper::SaveStringToFile(Json, *CachePath);
}

void FLearningManager::LoadCacheFromFile()
{
	FString CachePath = FPaths::ProjectSavedDir() / TEXT("BpGeneratorUltimate") / TEXT("learning_cache.json");
	FString FileContent;
	if (!FFileHelper::LoadFileToString(FileContent, *CachePath)) return;

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContent);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return;

	const TSharedPtr<FJsonObject>* StatsObj = nullptr;
	if (Root->TryGetObjectField(TEXT("stats"), StatsObj))
	{
		(*StatsObj)->TryGetNumberField(TEXT("xp"), UserStats.TotalXP);
		(*StatsObj)->TryGetNumberField(TEXT("level"), UserStats.Level);
		(*StatsObj)->TryGetNumberField(TEXT("streak"), UserStats.CurrentStreak);
		(*StatsObj)->TryGetNumberField(TEXT("completed"), UserStats.CompletedNodes);
	}

	UE_LOG(LogTemp, Log, TEXT("LearningManager: Loaded cache (XP=%d, Level=%d)"), UserStats.TotalXP, UserStats.Level);
}

void FLearningManager::TrackToolCompletion(const FString& ToolName, const TSharedPtr<FJsonObject>& Args,
	bool bSuccess, const FString& ResultJson, const FString& ErrorMessage, const FString& Source)
{
	if (!bInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT("LearningManager: TrackToolCompletion called but NOT initialized — skipping '%s'"), *ToolName);
		return;
	}

	{
		TSharedPtr<FJsonObject> D = MakeShareable(new FJsonObject);
		D->SetStringField(TEXT("name"), ToolName);
		D->SetStringField(TEXT("source"), Source);
		TrackEvent(TEXT("tool_used"), D);
	}

	if (bSuccess)
	{
		TSharedPtr<FJsonObject> ResJson;
		if (!ResultJson.IsEmpty())
		{
			TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(ResultJson);
			FJsonSerializer::Deserialize(R, ResJson);
		}

		FString CreateAssetType;
		if (ToolName == TEXT("create_asset") && Args.IsValid())
		{
			Args->TryGetStringField(TEXT("asset_type"), CreateAssetType);
		}

		static const TSet<FString> GraphTools = {
			TEXT("build_blueprint_graph"), TEXT("place_node"), TEXT("build_pcg_graph"),
			TEXT("build_behavior_tree"), TEXT("build_state_tree")
		};
		if (GraphTools.Contains(ToolName) && ResJson.IsValid())
		{
			int32 NodeCount = 0;
			ResJson->TryGetNumberField(TEXT("node_count"), NodeCount);
			if (NodeCount == 0) ResJson->TryGetNumberField(TEXT("total_node_count"), NodeCount);
			TSharedPtr<FJsonObject> D = MakeShareable(new FJsonObject);
			D->SetStringField(TEXT("tool"), ToolName);
			D->SetNumberField(TEXT("nodes"), NodeCount);
			TrackEvent(TEXT("blueprint_generated"), D);
		}

		if (ToolName.StartsWith(TEXT("create_")) || ToolName == TEXT("add_component") ||
			ToolName == TEXT("add_variable") || ToolName == TEXT("add_function") ||
			ToolName == TEXT("spawn_actors") || ToolName == TEXT("add_widget_to_user_widget"))
		{
			TSharedPtr<FJsonObject> D = MakeShareable(new FJsonObject);
			D->SetStringField(TEXT("tool"), ToolName);
			if (ResJson.IsValid())
			{
				FString AssetPath;
				if (ResJson->TryGetStringField(TEXT("asset_path"), AssetPath) ||
					ResJson->TryGetStringField(TEXT("path"), AssetPath))
					D->SetStringField(TEXT("asset_path"), AssetPath);
			}
			TrackEvent(TEXT("asset_created"), D);
		}

		if (ToolName == TEXT("create_landscape"))
		{
			TSharedPtr<FJsonObject> D = MakeShareable(new FJsonObject);
			FString Preset; if (Args.IsValid()) Args->TryGetStringField(TEXT("preset"), Preset);
			D->SetStringField(TEXT("preset"), Preset);
			TrackEvent(TEXT("landscape_created"), D);
		}

		if (ToolName == TEXT("compile_project"))
		{
			TSharedPtr<FJsonObject> D = MakeShareable(new FJsonObject);
			int32 RetCode = -1; if (ResJson.IsValid()) ResJson->TryGetNumberField(TEXT("return_code"), RetCode);
			D->SetNumberField(TEXT("return_code"), RetCode);
			D->SetBoolField(TEXT("success"), RetCode == 0);
			TrackEvent(TEXT("cpp_compiled"), D);
		}

		if (ToolName == TEXT("generate_pbr_material") ||
			CreateAssetType == TEXT("Material") ||
			CreateAssetType == TEXT("MaterialInstance"))
		{
			TSharedPtr<FJsonObject> D = MakeShareable(new FJsonObject);
			D->SetStringField(TEXT("tool"), ToolName);
			if (!CreateAssetType.IsEmpty()) D->SetStringField(TEXT("asset_type"), CreateAssetType);
			TrackEvent(TEXT("material_created"), D);
		}

		if (ToolName == TEXT("spawn_environment_actor") || ToolName == TEXT("create_water_body"))
		{
			TSharedPtr<FJsonObject> D = MakeShareable(new FJsonObject);
			D->SetStringField(TEXT("tool"), ToolName);
			TrackEvent(TEXT("environment_spawned"), D);
		}

		if (ToolName == TEXT("add_anim_state") || ToolName == TEXT("add_anim_notify") ||
			CreateAssetType == TEXT("BlendSpace") || CreateAssetType == TEXT("AnimMontage") ||
			CreateAssetType == TEXT("AnimComposite") || CreateAssetType == TEXT("AnimBlueprint"))
		{
			TSharedPtr<FJsonObject> D = MakeShareable(new FJsonObject);
			D->SetStringField(TEXT("tool"), ToolName);
			if (!CreateAssetType.IsEmpty()) D->SetStringField(TEXT("asset_type"), CreateAssetType);
			TrackEvent(TEXT("animation_created"), D);
		}

		if (ToolName == TEXT("add_emitter_to_system") || ToolName == TEXT("add_niagara_module") ||
			CreateAssetType == TEXT("NiagaraSystem") || CreateAssetType == TEXT("NiagaraEmitter"))
		{
			TSharedPtr<FJsonObject> D = MakeShareable(new FJsonObject);
			D->SetStringField(TEXT("tool"), ToolName);
			if (!CreateAssetType.IsEmpty()) D->SetStringField(TEXT("asset_type"), CreateAssetType);
			TrackEvent(TEXT("niagara_created"), D);
		}

		if (ToolName == TEXT("add_sequence_track") || ToolName == TEXT("add_sequence_keyframe") ||
			CreateAssetType == TEXT("LevelSequence"))
		{
			TSharedPtr<FJsonObject> D = MakeShareable(new FJsonObject);
			D->SetStringField(TEXT("tool"), ToolName);
			if (!CreateAssetType.IsEmpty()) D->SetStringField(TEXT("asset_type"), CreateAssetType);
			TrackEvent(TEXT("sequencer_created"), D);
		}

		if (ToolName == TEXT("create_pcg_graph") || ToolName == TEXT("spawn_pcg_actor") ||
			ToolName == TEXT("populate_landscape"))
		{
			TSharedPtr<FJsonObject> D = MakeShareable(new FJsonObject);
			D->SetStringField(TEXT("tool"), ToolName);
			TrackEvent(TEXT("pcg_created"), D);
		}

		if (ToolName == TEXT("create_widget_blueprint") || ToolName == TEXT("add_widget_to_user_widget") ||
			ToolName == TEXT("create_widget_from_layout"))
		{
			TSharedPtr<FJsonObject> D = MakeShareable(new FJsonObject);
			D->SetStringField(TEXT("tool"), ToolName);
			TrackEvent(TEXT("widget_created"), D);
		}

		if (ToolName == TEXT("write_cpp_file") || ToolName == TEXT("edit_cpp_file"))
		{
			TSharedPtr<FJsonObject> D = MakeShareable(new FJsonObject);
			D->SetStringField(TEXT("tool"), ToolName);
			FString FilePath; if (Args.IsValid()) Args->TryGetStringField(TEXT("file_path"), FilePath);
			D->SetStringField(TEXT("file_path"), FilePath);
			TrackEvent(TEXT("cpp_file_written"), D);
		}
	}
	else if (!ErrorMessage.IsEmpty())
	{
		TSharedPtr<FJsonObject> D = MakeShareable(new FJsonObject);
		D->SetStringField(TEXT("tool"), ToolName);
		D->SetStringField(TEXT("message"), ErrorMessage);
		TrackEvent(TEXT("error"), D);
	}
}

void FLearningManager::TrackEvent(const FString& EventType, const TSharedPtr<FJsonObject>& Data)
{
	if (!bInitialized) return;

	TSharedPtr<FJsonObject> Event = MakeShareable(new FJsonObject);
	Event->SetStringField(TEXT("type"), EventType);
	if (Data.IsValid())
	{
		Event->SetObjectField(TEXT("data"), Data);
	}
	else
	{
		Event->SetObjectField(TEXT("data"), MakeShareable(new FJsonObject));
	}

	{
		FScopeLock Lock(&EventBufferLock);
		EventBuffer.Add(Event);
	}

	int32 BufferSize;
	{
		FScopeLock Lock(&EventBufferLock);
		BufferSize = EventBuffer.Num();
	}
	if (BufferSize >= FlushThreshold)
	{
		FlushEventBuffer();
	}
}

void FLearningManager::FlushEventBuffer()
{
	TArray<TSharedPtr<FJsonObject>> EventsToSend;
	{
		FScopeLock Lock(&EventBufferLock);
		if (EventBuffer.Num() == 0) return;
		UE_LOG(LogTemp, Warning, TEXT("LearningManager: Flushing %d events to server"), EventBuffer.Num());

		int32 Count = FMath::Min(EventBuffer.Num(), MaxEventsPerFlush);
		for (int32 i = 0; i < Count; i++)
		{
			EventsToSend.Add(EventBuffer[i]);
		}
		EventBuffer.RemoveAt(0, Count);
	}

	TArray<TSharedPtr<FJsonValue>> EventsArr;
	for (const auto& Evt : EventsToSend)
	{
		EventsArr.Add(MakeShareable(new FJsonValueObject(Evt)));
	}

	TSharedPtr<FJsonObject> Body = MakeShareable(new FJsonObject);
	Body->SetArrayField(TEXT("events"), EventsArr);

	WebsitePost(TEXT("track"), Body, [EventsToSend](bool bOk, TSharedPtr<FJsonObject> Response)
	{
		if (bOk)
		{
			UE_LOG(LogTemp, Log, TEXT("LearningManager: Tracked %d events"), EventsToSend.Num());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("LearningManager: Failed to flush %d tracking events"), EventsToSend.Num());
		}
	});
}

void FLearningManager::SendHeartbeatEvent()
{
	double Now = FPlatformTime::Seconds();
	double Duration = (LastHeartbeatTime > 0.0) ? (Now - LastHeartbeatTime) : SyncPulseIntervalSeconds;
	LastHeartbeatTime = Now;

	TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject);
	Data->SetNumberField(TEXT("duration"), static_cast<int32>(Duration));
	Data->SetStringField(TEXT("engine"), TEXT("5.5"));
	Data->SetStringField(TEXT("project"), FApp::GetProjectName());

	TrackEvent(TEXT("session_heartbeat"), Data);

	FlushEventBuffer();
}

void FLearningManager::StartSyncPulse()
{
	LastHeartbeatTime = FPlatformTime::Seconds();

	HeartbeatTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([this](float DeltaTime) -> bool
		{
			SendHeartbeatEvent();
			return true;
		}),
		static_cast<float>(SyncPulseIntervalSeconds)
	);
}
