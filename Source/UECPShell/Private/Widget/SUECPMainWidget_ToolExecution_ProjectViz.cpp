// Copyright 2026, BlueprintsLab, All rights reserved

#include "SUECPMainWidget.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Dom/JsonObject.h"
#include "Tools/DependencyGraphTools.h"
#include "Utils/DiagramUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

#define LOCTEXT_NAMESPACE "SUECPMainWidget"

FToolExecutionResult SUECPMainWidget::ExecuteTool_OpenProjectDashboard(const TSharedPtr<FJsonObject>& Args)
{
	FToolExecutionResult Result;

	FString DepGraphJson, DepError;
	DependencyGraphTools::HandleGetDependencyGraph(TEXT(""), 3, false, DepGraphJson, DepError);

	FString InheritanceNomnoml, InhError;
	DependencyGraphTools::HandleGetInheritanceTree(TEXT(""), TEXT(""), InheritanceNomnoml, InhError);

	FString HeatmapData;
	{
		TSharedPtr<FJsonObject> DepObj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(DepGraphJson);
		if (FJsonSerializer::Deserialize(Reader, DepObj) && DepObj.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* NodesArr;
			if (DepObj->TryGetArrayField(TEXT("nodes"), NodesArr))
			{
				for (auto& NodeVal : *NodesArr)
				{
					auto NodeObj = NodeVal->AsObject();
					if (!NodeObj.IsValid()) continue;
					FString Label = NodeObj->GetStringField(TEXT("label"));
					FString Type = NodeObj->GetStringField(TEXT("type"));
					int32 Score = 1;
					if (NodeObj->HasField(TEXT("connections")))
						Score = FMath::Max(1, static_cast<int32>(NodeObj->GetNumberField(TEXT("connections"))));
					if (Type != TEXT("external"))
						HeatmapData += FString::Printf(TEXT("%s %d %s\n"), *Label, Score, *Type);
				}
			}
		}
	}

	FString PieChartData;
	{
		TMap<FString, int32> TypeCounts;
		TSharedPtr<FJsonObject> DepObj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(DepGraphJson);
		if (FJsonSerializer::Deserialize(Reader, DepObj) && DepObj.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* NodesArr;
			if (DepObj->TryGetArrayField(TEXT("nodes"), NodesArr))
			{
				for (auto& NodeVal : *NodesArr)
				{
					auto NodeObj = NodeVal->AsObject();
					if (!NodeObj.IsValid()) continue;
					FString Type = NodeObj->GetStringField(TEXT("type"));
					TypeCounts.FindOrAdd(Type)++;
				}
			}
		}
		bool bFirst = true;
		for (auto& [Type, Count] : TypeCounts)
		{
			if (!bFirst) PieChartData += TEXT(", ");
			PieChartData += FString::Printf(TEXT("%s %d"), *Type, Count);
			bFirst = false;
		}
	}

	FString VisNetworkJs, NomnomlJs, GraphreJs;
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BpGeneratorUltimate"));
	if (Plugin.IsValid())
	{
		FString ResDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("UI"));
		FFileHelper::LoadFileToString(VisNetworkJs, *FPaths::Combine(ResDir, TEXT("vis-network.min.js")));
		FFileHelper::LoadFileToString(NomnomlJs, *FPaths::Combine(ResDir, TEXT("nomnoml.min.js")));
		FFileHelper::LoadFileToString(GraphreJs, *FPaths::Combine(ResDir, TEXT("graphre.min.js")));
	}

	FString PastTracesJson = TEXT("[]");
	{
		TArray<FString> TraceFiles;
		IFileManager::Get().FindFiles(TraceFiles, *FPaths::Combine(FPaths::ProfilingDir(), TEXT("BpGen_Profile_*.utrace")), true, false);
		if (TraceFiles.Num() > 0)
		{
			TraceFiles.Sort([](const FString& A, const FString& B) { return A > B; });
			TArray<TSharedPtr<FJsonValue>> TraceArray;
			for (int32 i = 0; i < FMath::Min(TraceFiles.Num(), 20); ++i)
			{
				FString FullPath = FPaths::Combine(FPaths::ProfilingDir(), TraceFiles[i]);
				int64 FileSize = IFileManager::Get().FileSize(*FullPath);
				TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
				Obj->SetStringField(TEXT("path"), FullPath);
				Obj->SetStringField(TEXT("name"), TraceFiles[i]);
				Obj->SetNumberField(TEXT("size_mb"), FileSize / (1024.0 * 1024.0));
				TraceArray.Add(MakeShareable(new FJsonValueObject(Obj)));
			}
			TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&PastTracesJson);
			FJsonSerializer::Serialize(TraceArray, Writer);
		}
	}

	if (ActiveDashboardWindow.IsValid())
	{
		if (TSharedPtr<SWindow> OldWindow = ActiveDashboardWindow.Pin())
			OldWindow->RequestDestroyWindow();
	}

	DiagramUtils::FOnDashboardLoadUrl UrlDelegate;
	UrlDelegate.BindSP(this, &SUECPMainWidget::OnBrowserLoadUrl);

	TSharedPtr<SWindow> DashWindow = DiagramUtils::CreateProjectDashboardWindow(
		DepGraphJson, HeatmapData, PieChartData, InheritanceNomnoml, LastProfileResultJson, PastTracesJson,
		VisNetworkJs, NomnomlJs, GraphreJs, UrlDelegate);

	if (DashWindow.IsValid())
	{
		ActiveDashboardWindow = DashWindow;
		FSlateApplication::Get().AddWindow(DashWindow.ToSharedRef());
		Result.bSuccess = true;
		Result.ResultJson = TEXT("{\"success\":true,\"message\":\"Project dashboard opened\"}");
	}
	else
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("Failed to create dashboard window");
	}

	return Result;
}

bool SUECPMainWidget::TryDispatchProjectVizTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, FToolExecutionResult& OutResult)
{
	if (ToolName == TEXT("open_project_dashboard")) { OutResult = ExecuteTool_OpenProjectDashboard(Arguments); return true; }
	return false;
}

#undef LOCTEXT_NAMESPACE
