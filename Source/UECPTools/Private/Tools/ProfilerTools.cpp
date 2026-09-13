// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/ProfilerTools.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "TraceServices/ITraceServicesModule.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/Frames.h"
#include "TraceServices/Model/TimingProfiler.h"
#include "TraceServices/Model/Counters.h"
#include "TraceServices/Model/Memory.h"
#include "TraceServices/Containers/Tables.h"
#include "Common/ProviderLock.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "MCPToolsLog.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Event.h"
#include "Async/Async.h"
#include "Misc/Paths.h"

namespace ProfilerTools
{

static double SafeRound(double V, double Scale)
{
	if (!FMath::IsFinite(V)) return 0.0;
	return FMath::RoundToDouble(V * Scale) / Scale;
}

static void AnalyzeTraceInternal(const FString& TracePath, TSharedPtr<FJsonObject>& OutResult, FString& OutError)
{
	if (!FPaths::FileExists(TracePath))
	{
		OutError = FString::Printf(TEXT("Trace file not found: %s"), *TracePath);
		return;
	}

	const int64 FileSize = IFileManager::Get().FileSize(*TracePath);
	if (FileSize <= 0)
	{
		OutError = TEXT("Trace file is empty");
		return;
	}

	ITraceServicesModule* TSModule = FModuleManager::GetModulePtr<ITraceServicesModule>(TEXT("TraceServices"));
	if (!TSModule)
	{
		TSModule = &FModuleManager::LoadModuleChecked<ITraceServicesModule>(TEXT("TraceServices"));
	}
	if (!TSModule)
	{
		OutError = TEXT("TraceServices module not available");
		return;
	}

	TSharedPtr<TraceServices::IAnalysisService> AnalysisService = TSModule->GetAnalysisService();
	if (!AnalysisService.IsValid())
	{
		OutError = TEXT("Failed to get AnalysisService");
		return;
	}

	UE_LOG(LogMCPTool, Log, TEXT("ProfilerTools: Analyzing trace file: %s (%.1f MB)"), *TracePath, FileSize / (1024.0f * 1024.0f));

	TSharedPtr<const TraceServices::IAnalysisSession> Session = AnalysisService->Analyze(*TracePath);
	if (!Session.IsValid())
	{
		OutError = FString::Printf(TEXT("Failed to analyze trace file: %s"), *TracePath);
		return;
	}

	OutResult = MakeShareable(new FJsonObject);
	OutResult->SetBoolField(TEXT("success"), true);
	OutResult->SetStringField(TEXT("trace_path"), TracePath);
	OutResult->SetNumberField(TEXT("trace_size_mb"), FileSize / (1024.0 * 1024.0));

	TraceServices::FAnalysisSessionReadScope ReadScope(*Session);
	double SessionDuration = Session->GetDurationSeconds();
	OutResult->SetNumberField(TEXT("duration_seconds"), SessionDuration);

	{
		const TraceServices::IFrameProvider& FrameProvider = TraceServices::ReadFrameProvider(*Session);
		uint64 GameFrameCount = FrameProvider.GetFrameCount(TraceFrameType_Game);
		uint64 RenderFrameCount = FrameProvider.GetFrameCount(TraceFrameType_Rendering);

		TSharedPtr<FJsonObject> FrameStats = MakeShareable(new FJsonObject);
		FrameStats->SetNumberField(TEXT("game_frame_count"), static_cast<double>(GameFrameCount));
		FrameStats->SetNumberField(TEXT("render_frame_count"), static_cast<double>(RenderFrameCount));

		if (GameFrameCount > 0)
		{
			TArray<double> FrameTimesMs;
			FrameTimesMs.Reserve(GameFrameCount);
			double MinMs = 9999.0, MaxMs = 0.0, SumMs = 0.0;
			int32 FrameIdx = 0;
			const int32 SkipStartupFrames = 5;

			FrameProvider.EnumerateFrames(TraceFrameType_Game, (uint64)0, GameFrameCount,
				[&](const TraceServices::FFrame& Frame)
				{
					FrameIdx++;
					double Ms = (Frame.EndTime - Frame.StartTime) * 1000.0;
					if (Ms > 0.1 && Ms < 10000.0 && FMath::IsFinite(Ms) && FrameIdx > SkipStartupFrames)
					{
						FrameTimesMs.Add(Ms);
						MinMs = FMath::Min(MinMs, Ms);
						MaxMs = FMath::Max(MaxMs, Ms);
						SumMs += Ms;
					}
				});

			double AvgMs = FrameTimesMs.Num() > 0 ? SumMs / FrameTimesMs.Num() : 0.0;
			double AvgFps = AvgMs > 0.001 ? 1000.0 / AvgMs : 0.0;
			if (FrameTimesMs.Num() == 0) { MinMs = 0.0; MaxMs = 0.0; }

			FrameTimesMs.Sort();
			int32 P99Idx = FMath::Clamp(static_cast<int32>(FrameTimesMs.Num() * 0.99), 0, FMath::Max(0, FrameTimesMs.Num() - 1));
			double P99Ms = FrameTimesMs.IsValidIndex(P99Idx) ? FrameTimesMs[P99Idx] : MaxMs;

			FrameStats->SetNumberField(TEXT("avg_ms"), SafeRound(AvgMs, 100.0));
			FrameStats->SetNumberField(TEXT("min_ms"), SafeRound(MinMs, 100.0));
			FrameStats->SetNumberField(TEXT("max_ms"), SafeRound(MaxMs, 100.0));
			FrameStats->SetNumberField(TEXT("p99_ms"), SafeRound(P99Ms, 100.0));
			FrameStats->SetNumberField(TEXT("avg_fps"), SafeRound(AvgFps, 10.0));

			TArray<double> FrameTimesOrdered;
			FrameTimesOrdered.Reserve(GameFrameCount);
			int32 ChartFrameIdx = 0;
			FrameProvider.EnumerateFrames(TraceFrameType_Game, (uint64)0, GameFrameCount,
				[&](const TraceServices::FFrame& Frame)
				{
					ChartFrameIdx++;
					double Ms = (Frame.EndTime - Frame.StartTime) * 1000.0;
					if (Ms > 0.1 && Ms < 10000.0 && FMath::IsFinite(Ms) && ChartFrameIdx > SkipStartupFrames)
						FrameTimesOrdered.Add(Ms);
				});
			TArray<TSharedPtr<FJsonValue>> SampledTimes;
			int32 Step = FMath::Max(1, FrameTimesOrdered.Num() / 200);
			for (int32 i = 0; i < FrameTimesOrdered.Num(); i += Step)
			{
				SampledTimes.Add(MakeShareable(new FJsonValueNumber(SafeRound(FrameTimesOrdered[i], 100.0))));
			}
			FrameStats->SetArrayField(TEXT("frame_times_ms"), SampledTimes);
		}

		OutResult->SetObjectField(TEXT("frame_stats"), FrameStats);
	}

	{
		const TraceServices::ITimingProfilerProvider* TimingProvider = TraceServices::ReadTimingProfilerProvider(*Session);
		if (TimingProvider)
		{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
			TraceServices::FCreateAggregationParams Params;
			Params.bIncludeOldGpu1 = true;
			Params.bIncludeOldGpu2 = true;
#else
			TraceServices::FCreateAggreationParams Params;
			Params.IncludeGpu = true;
#endif
			Params.IntervalStart = 0.0;
			Params.IntervalEnd = SessionDuration;
			Params.CpuThreadFilter = [](uint32) { return true; };

			TraceServices::ITable<TraceServices::FTimingProfilerAggregatedStats>* Table =
				TimingProvider->CreateAggregation(Params);

			if (Table)
			{
				struct FStatEntry
				{
					FString Name;
					double TotalMs;
					double AvgMs;
					uint64 Count;
					bool bIsGpu;
				};
				TArray<FStatEntry> AllStats;

				TraceServices::ITableReader<TraceServices::FTimingProfilerAggregatedStats>* Reader = Table->CreateReader();
				if (Reader)
				{
					while (Reader->IsValid())
					{
						const TraceServices::FTimingProfilerAggregatedStats* Row = Reader->GetCurrentRow();
						if (Row && Row->Timer && Row->Timer->Name && Row->InstanceCount > 0
							&& FMath::IsFinite(Row->TotalInclusiveTime) && Row->TotalInclusiveTime >= 0.0)
						{
							FStatEntry Entry;
							Entry.Name = Row->Timer->Name;
							Entry.TotalMs = FMath::IsFinite(Row->TotalInclusiveTime) ? Row->TotalInclusiveTime * 1000.0 : 0.0;
							Entry.AvgMs = FMath::IsFinite(Row->AverageInclusiveTime) ? Row->AverageInclusiveTime * 1000.0 : 0.0;
							Entry.Count = Row->InstanceCount;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
							Entry.bIsGpu = (Row->Timer->Type == TraceServices::ETimingProfilerTimerType::GpuScope);
#else
							Entry.bIsGpu = Row->Timer->IsGpuTimer;
#endif
							AllStats.Add(MoveTemp(Entry));
						}
						Reader->NextRow();
					}
					delete Reader;
				}
				delete Table;

				AllStats.Sort([](const FStatEntry& A, const FStatEntry& B) { return A.TotalMs > B.TotalMs; });

				TArray<TSharedPtr<FJsonValue>> CpuFunctions;
				TArray<TSharedPtr<FJsonValue>> GpuFunctions;
				for (const FStatEntry& S : AllStats)
				{
					TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
					Obj->SetStringField(TEXT("name"), S.Name);
					Obj->SetNumberField(TEXT("total_ms"), SafeRound(S.TotalMs, 100.0));
					Obj->SetNumberField(TEXT("avg_ms"), SafeRound(S.AvgMs, 100.0));
					Obj->SetNumberField(TEXT("count"), static_cast<double>(S.Count));

					if (S.bIsGpu && GpuFunctions.Num() < 10)
						GpuFunctions.Add(MakeShareable(new FJsonValueObject(Obj)));
					else if (!S.bIsGpu && CpuFunctions.Num() < 20)
						CpuFunctions.Add(MakeShareable(new FJsonValueObject(Obj)));
				}

				OutResult->SetArrayField(TEXT("top_cpu_functions"), CpuFunctions);
				OutResult->SetArrayField(TEXT("top_gpu_functions"), GpuFunctions);
			}
		}
	}

	{
		const TraceServices::ICounterProvider& CounterProvider = TraceServices::ReadCounterProvider(*Session);
		TSharedPtr<FJsonObject> Counters = MakeShareable(new FJsonObject);

		CounterProvider.EnumerateCounters([&](uint32 Id, const TraceServices::ICounter& Counter)
		{
			FString Name = Counter.GetName();
			if (Counter.IsFloatingPoint())
			{
				double LastVal = 0.0;
				Counter.EnumerateFloatValues(0.0, SessionDuration, false,
					[&](double Time, double Value) { LastVal = Value; });
				if (LastVal != 0.0 && FMath::IsFinite(LastVal))
					Counters->SetNumberField(Name, SafeRound(LastVal, 100.0));
			}
			else
			{
				int64 LastVal = 0;
				Counter.EnumerateValues(0.0, SessionDuration, false,
					[&](double Time, int64 Value) { LastVal = Value; });
				if (LastVal != 0)
					Counters->SetNumberField(Name, static_cast<double>(LastVal));
			}
		});

		OutResult->SetObjectField(TEXT("counters"), Counters);
	}

	{
		const TraceServices::IMemoryProvider* MemProvider = TraceServices::ReadMemoryProvider(*Session);
		if (MemProvider)
		{
			TraceServices::FProviderReadScopeLock MemLock(*MemProvider);

			if (MemProvider->GetTagCount() > 0)
			{
			TSharedPtr<FJsonObject> Memory = MakeShareable(new FJsonObject);
			TArray<TSharedPtr<FJsonValue>> Tags;
			double TotalBytes = 0.0;

			MemProvider->EnumerateTags([&](const TraceServices::FMemoryTagInfo& Tag)
			{
				int64 LastValue = 0;
				MemProvider->EnumerateTagSamples(0, Tag.Id, 0.0, SessionDuration, false,
					[&](double Time, double Duration, const TraceServices::FMemoryTagSample& Sample)
					{
						LastValue = Sample.Value;
					});

				if (LastValue > 0 && Tag.ParentId == 0)
				{
					double Mb = LastValue / (1024.0 * 1024.0);
					TSharedPtr<FJsonObject> TagObj = MakeShareable(new FJsonObject);
					TagObj->SetStringField(TEXT("name"), Tag.Name);
					TagObj->SetNumberField(TEXT("mb"), FMath::RoundToDouble(Mb * 10.0) / 10.0);
					Tags.Add(MakeShareable(new FJsonValueObject(TagObj)));
					TotalBytes += LastValue;
				}
			});

			Tags.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
			{
				return A->AsObject()->GetNumberField(TEXT("mb")) > B->AsObject()->GetNumberField(TEXT("mb"));
			});

			Memory->SetNumberField(TEXT("total_mb"), FMath::RoundToDouble(TotalBytes / (1024.0 * 1024.0) * 10.0) / 10.0);
			Memory->SetArrayField(TEXT("tags"), Tags);
			OutResult->SetObjectField(TEXT("memory"), Memory);
			}
		}
	}

	UE_LOG(LogMCPTool, Log, TEXT("ProfilerTools: Analysis complete — %.1fs session, %lld frames"),
		SessionDuration,
		OutResult->HasField(TEXT("frame_stats")) ?
			static_cast<int64>(OutResult->GetObjectField(TEXT("frame_stats"))->GetNumberField(TEXT("game_frame_count"))) : 0LL);
}

void HandleAnalyzeTraceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString TracePath;
	Args->TryGetStringField(TEXT("trace_path"), TracePath);
	HandleAnalyzeTrace(TracePath, OutJsonString, OutError);
}

void HandleProfileProjectFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	float DurationSeconds = 10.0f;
	FString Channels;
	double DurNum = 10.0;
	if (Args->TryGetNumberField(TEXT("duration"), DurNum)) DurationSeconds = (float)DurNum;
	Args->TryGetStringField(TEXT("channels"), Channels);
	HandleProfileProject(DurationSeconds, Channels,  {}, OutJsonString, OutError);
}

void HandleAnalyzeTrace(const FString& TracePath, FString& OutJsonString, FString& OutError)
{
	UE_LOG(LogMCPTool, Log, TEXT("ProfilerTools::HandleAnalyzeTrace — path='%s'"), *TracePath);

	if (TracePath.IsEmpty())
	{
		OutError = TEXT("trace_path is required");
		return;
	}

	TSharedPtr<FJsonObject> Result;
	AnalyzeTraceInternal(TracePath, Result, OutError);

	if (Result.IsValid())
	{
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJsonString);
		FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
	}
}

void HandleProfileProject(
	float DurationSeconds,
	const FString& Channels,
	TFunction<void(const FString&)> ProgressCallback,
	FString& OutJsonString,
	FString& OutError)
{
	UE_LOG(LogMCPTool, Log, TEXT("ProfilerTools::HandleProfileProject — duration=%.1fs channels='%s'"), DurationSeconds, *Channels);

	if (IsInGameThread())
	{
		OutError = TEXT("profile_project blocks the calling thread for the full trace duration and cannot run from the in-editor agent path. ")
			TEXT("Drive it manually instead: begin_play_in_editor → wait the desired number of seconds across turns ")
			TEXT("(call get_pie_performance for live metrics) → stop_play_in_editor. ")
			TEXT("For one-shot capture use the Profile button in the plugin UI, or call profile_project from an external MCP client.");
		return;
	}

	auto Progress = [&](const FString& Msg)
	{
		UE_LOG(LogMCPTool, Log, TEXT("ProfilerTools: %s"), *Msg);
		if (ProgressCallback) ProgressCallback(Msg);
	};

	if (DurationSeconds <= 0.0f) DurationSeconds = 10.0f;
	if (DurationSeconds > 300.0f) DurationSeconds = 300.0f;

	{
		FEvent* CheckEvent = FPlatformProcess::GetSynchEventFromPool(false);
		bool bPIERunning = false;
		AsyncTask(ENamedThreads::GameThread, [&]()
		{
			bPIERunning = (GEditor && GEditor->PlayWorld != nullptr);
			CheckEvent->Trigger();
		});
		CheckEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(CheckEvent);

		if (bPIERunning)
		{
			OutError = TEXT("PIE is already running. Stop it first.");
			return;
		}
	}

	{
		FEvent* PreStopEvent = FPlatformProcess::GetSynchEventFromPool(false);
		AsyncTask(ENamedThreads::GameThread, [&]()
		{
			if (FTraceAuxiliary::IsConnected())
			{
				FTraceAuxiliary::Stop();
			}
			PreStopEvent->Trigger();
		});
		PreStopEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(PreStopEvent);
		FPlatformProcess::Sleep(0.5f);
	}

	FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	FString TraceFileName = FString::Printf(TEXT("BpGen_Profile_%s"), *Timestamp);
	FString TracePath = FPaths::Combine(FPaths::ProfilingDir(), TraceFileName);

	Progress(TEXT("Starting trace..."));
	FString ActiveChannels = Channels.IsEmpty() ? TEXT("cpu,gpu,frame,bookmark,counters") : Channels;
	bool bTraceStarted = false;
	FString TraceDestination;
	{
		FEvent* TraceStartEvent = FPlatformProcess::GetSynchEventFromPool(false);
		AsyncTask(ENamedThreads::GameThread, [&]()
		{
			bTraceStarted = FTraceAuxiliary::Start(
				FTraceAuxiliary::EConnectionType::File,
				*TraceFileName,
				*ActiveChannels);
			if (bTraceStarted)
				TraceDestination = FTraceAuxiliary::GetTraceDestinationString();
			TraceStartEvent->Trigger();
		});
		TraceStartEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(TraceStartEvent);
	}

	if (!bTraceStarted)
	{
		OutError = TEXT("Failed to start trace recording");
		return;
	}

	UE_LOG(LogMCPTool, Log, TEXT("ProfilerTools: Trace started → %s"), *TraceDestination);

	Progress(TEXT("Launching PIE..."));
	{
		FEvent* PIEStartEvent = FPlatformProcess::GetSynchEventFromPool(false);
		AsyncTask(ENamedThreads::GameThread, [&]()
		{
			if (GEditor)
			{
				FRequestPlaySessionParams Params;
				Params.WorldType = EPlaySessionWorldType::PlayInEditor;
				GEditor->RequestPlaySession(Params);
			}
			PIEStartEvent->Trigger();
		});
		PIEStartEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(PIEStartEvent);
	}

	Progress(TEXT("Waiting for PIE to initialize..."));
	bool bPIEStarted = false;
	for (int32 i = 0; i < 150; ++i)
	{
		FPlatformProcess::Sleep(0.1f);
		FEvent* CheckEvent = FPlatformProcess::GetSynchEventFromPool(false);
		bool bReady = false;
		AsyncTask(ENamedThreads::GameThread, [&]()
		{
			bReady = (GEditor && GEditor->PlayWorld != nullptr);
			CheckEvent->Trigger();
		});
		CheckEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(CheckEvent);

		if (bReady)
		{
			bPIEStarted = true;
			break;
		}
	}

	if (!bPIEStarted)
	{
		Progress(TEXT("PIE failed to start. Stopping trace..."));
		FEvent* StopEvent = FPlatformProcess::GetSynchEventFromPool(false);
		AsyncTask(ENamedThreads::GameThread, [&]()
		{
			if (FTraceAuxiliary::IsConnected())
			{
				FTraceAuxiliary::Stop();
			}
			StopEvent->Trigger();
		});
		StopEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(StopEvent);
		OutError = TEXT("PIE failed to start within 15 seconds");
		return;
	}

	double PIEStartTime = FPlatformTime::Seconds();

	Progress(FString::Printf(TEXT("PIE running (0/%.0fs)..."), DurationSeconds));
	bool bPIECrashed = false;
	float Elapsed = 0.0f;
	const float PollInterval = 0.5f;
	while (Elapsed < DurationSeconds)
	{
		FPlatformProcess::Sleep(PollInterval);
		Elapsed += PollInterval;

		FEvent* CheckEvent = FPlatformProcess::GetSynchEventFromPool(false);
		bool bStillRunning = false;
		AsyncTask(ENamedThreads::GameThread, [&]()
		{
			bStillRunning = (GEditor && GEditor->PlayWorld != nullptr);
			CheckEvent->Trigger();
		});
		CheckEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(CheckEvent);

		if (!bStillRunning)
		{
			bPIECrashed = true;
			Progress(TEXT("PIE stopped unexpectedly. Capturing partial data..."));
			break;
		}

		if (FMath::FloorToInt(Elapsed) % 2 == 0 && FMath::Abs(Elapsed - FMath::FloorToFloat(Elapsed)) < PollInterval)
		{
			Progress(FString::Printf(TEXT("PIE running (%.0f/%.0fs)..."), Elapsed, DurationSeconds));
		}
	}

	double PIEEndTime = FPlatformTime::Seconds();
	double PIEGameplaySeconds = PIEEndTime - PIEStartTime;

	if (!bPIECrashed)
	{
		Progress(TEXT("Stopping PIE..."));
		FEvent* StopEvent = FPlatformProcess::GetSynchEventFromPool(false);
		AsyncTask(ENamedThreads::GameThread, [&]()
		{
			if (GEditor && GEditor->PlayWorld)
			{
				GEditor->RequestEndPlayMap();
			}
			StopEvent->Trigger();
		});
		StopEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(StopEvent);

		for (int32 i = 0; i < 50; ++i)
		{
			FPlatformProcess::Sleep(0.1f);
			FEvent* CheckEvent = FPlatformProcess::GetSynchEventFromPool(false);
			bool bStopped = false;
			AsyncTask(ENamedThreads::GameThread, [&]()
			{
				bStopped = (GEditor == nullptr || GEditor->PlayWorld == nullptr);
				CheckEvent->Trigger();
			});
			CheckEvent->Wait();
			FPlatformProcess::ReturnSynchEventToPool(CheckEvent);
			if (bStopped) break;
		}
	}

	Progress(TEXT("Stopping trace..."));
	{
		FEvent* TraceStopEvent = FPlatformProcess::GetSynchEventFromPool(false);
		AsyncTask(ENamedThreads::GameThread, [&]()
		{
			FTraceAuxiliary::Stop();
			TraceStopEvent->Trigger();
		});
		TraceStopEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(TraceStopEvent);
	}
	FPlatformProcess::Sleep(1.0f);

	FString ActualTracePath = TraceDestination;
	if (!FPaths::FileExists(ActualTracePath))
	{
		TArray<FString> SearchPaths = {
			TraceDestination,
			FPaths::Combine(FPaths::ProfilingDir(), TraceFileName + TEXT(".utrace")),
			FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TraceSessions"), TraceFileName + TEXT(".utrace")),
		};
		for (const FString& P : SearchPaths)
		{
			if (FPaths::FileExists(P))
			{
				ActualTracePath = P;
				break;
			}
		}
	}

	if (!FPaths::FileExists(ActualTracePath))
	{
		OutError = FString::Printf(TEXT("Trace file not found after recording. Expected at: %s"), *TraceDestination);
		return;
	}

	Progress(TEXT("Analyzing trace data..."));
	TSharedPtr<FJsonObject> Result;
	FString AnalyzeError;
	AnalyzeTraceInternal(ActualTracePath, Result, AnalyzeError);

	if (!AnalyzeError.IsEmpty())
	{
		OutError = AnalyzeError;
		return;
	}

	if (Result.IsValid())
	{
		Result->SetNumberField(TEXT("gameplay_seconds"), SafeRound(PIEGameplaySeconds, 10.0));
		Result->SetNumberField(TEXT("requested_duration"), static_cast<double>(DurationSeconds));
		if (bPIECrashed)
			Result->SetBoolField(TEXT("pie_crashed"), true);

		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJsonString);
		FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
	}

	Progress(TEXT("Profile complete!"));
}

}
