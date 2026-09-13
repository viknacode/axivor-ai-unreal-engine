// Copyright 2026, BlueprintsLab, All rights reserved

#include "SUECPMainWidget.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"

FToolExecutionResult SUECPMainWidget::ExecuteTool_SelectFolder(const TSharedPtr<FJsonObject>& Args)
{
	FToolExecutionResult Result;
	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform != nullptr)
	{
		FString SelectedPath;
		void* ParentWindowWindowHandle = nullptr;
		TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
		if (ParentWindow.IsValid() && ParentWindow->GetNativeWindow().IsValid())
		{
			ParentWindowWindowHandle = ParentWindow->GetNativeWindow()->GetOSWindowHandle();
		}

		const FString DefaultPath = FPaths::EnginePluginsDir();

		const bool bFolderSelected = DesktopPlatform->OpenDirectoryDialog(
			ParentWindowWindowHandle,
			TEXT("Select Plugin Source Folder"),
			DefaultPath,
			SelectedPath);

		if (bFolderSelected && !SelectedPath.IsEmpty())
		{
			ResultObject->SetStringField(TEXT("selected_path"), SelectedPath);
			ResultObject->SetBoolField(TEXT("success"), true);
			ResultObject->SetStringField(TEXT("message"), FString::Printf(TEXT("User selected folder: %s"), *SelectedPath));
			Result.bSuccess = true;
			Result.ErrorMessage = TEXT("Folder selected");
		}
		else
		{
			ResultObject->SetBoolField(TEXT("success"), false);
			ResultObject->SetStringField(TEXT("error"), TEXT("User cancelled folder selection"));
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("Folder selection cancelled");
		}
	}
	else
	{
		UE_LOG(LogUECPShell, Error, TEXT("SelectFolder tool: DesktopPlatform module not available"));
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), TEXT("Folder dialog not available on this platform"));
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("Folder dialog not available");
	}

	FString ResultString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
	FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
	Result.ResultJson = ResultString;
	return Result;
}

bool SUECPMainWidget::TryDispatchAssetCoreTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, FToolExecutionResult& OutResult)
{
	if (ToolName == TEXT("select_folder")) { OutResult = ExecuteTool_SelectFolder(Arguments); return true; }
	return false;
}
