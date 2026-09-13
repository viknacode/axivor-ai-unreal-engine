// Copyright 2026, BlueprintsLab, All rights reserved

#include "FUECPGddCoordinator.h"
#include "GddManager.h"
#include "SUECPMainWidget.h"
#include "Widget/UUECPAppBridge.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Framework/Application/SlateApplication.h"

FUECPGddCoordinator::FUECPGddCoordinator() = default;

void FUECPGddCoordinator::InitializeShellRefs(TWeakPtr<SUECPMainWidget> InShell,
	TWeakObjectPtr<UUECPAppBridge> InBridge)
{
	Shell  = InShell;
	Bridge = InBridge;
}

void FUECPGddCoordinator::ImportFiles()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform) return;

	TArray<FString> OutFiles;
	const FString FileTypes = TEXT("GDD Files (*.txt;*.md)|*.txt;*.md|Text Files (*.txt)|*.txt|Markdown Files (*.md)|*.md|All Files (*.*)|*.*");

	void* ParentHandle = nullptr;
	if (FSlateApplication::IsInitialized())
	{
		if (TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow())
			if (ActiveWindow->GetNativeWindow().IsValid())
				ParentHandle = ActiveWindow->GetNativeWindow()->GetOSWindowHandle();
	}

	const bool bOpened = DesktopPlatform->OpenFileDialog(
		ParentHandle,
		TEXT("Import GDD Files"),
		FPaths::ProjectDir(),
		TEXT(""),
		FileTypes,
		EFileDialogFlags::Multiple,
		OutFiles);

	if (!bOpened || OutFiles.Num() == 0) return;

	int32 ImportedCount = 0;
	int32 TotalTokens = 0;
	FString LastImportedFileName;

	TArray<TSharedPtr<FGddFileEntry>>& Files = FGddManager::Get().GetGddFiles();

	for (const FString& SourceFilePath : OutFiles)
	{
		FString FileContent;
		if (!FFileHelper::LoadFileToString(FileContent, *SourceFilePath)) continue;

		TSharedPtr<FGddFileEntry> NewFile = MakeShareable(new FGddFileEntry);
		NewFile->FileName        = FPaths::GetCleanFilename(SourceFilePath);
		NewFile->FilePath        = FString::Printf(TEXT("gdd_%s.txt"), *NewFile->FileId.ToString(EGuidFormats::Digits));
		NewFile->CharCount       = FileContent.Len();
		NewFile->EstimatedTokens = FileContent.Len() / 4;
		NewFile->bEnabled        = true;
		NewFile->LastModified    = FDateTime::Now();

		const FString GddDir  = FPaths::ProjectSavedDir() / TEXT("BpGeneratorUltimate") / TEXT("gdd");
		const FString SavePath = GddDir / NewFile->FilePath;
		FFileHelper::SaveStringToFile(FileContent, *SavePath);

		Files.Add(NewFile);
		ImportedCount++;
		TotalTokens += NewFile->EstimatedTokens;
		LastImportedFileName = NewFile->FileName;
	}

	if (ImportedCount == 0) return;

	FGddManager::Get().SaveManifest();

	if (UUECPAppBridge* B = Bridge.Get())
	{
		const FString Msg = (ImportedCount == 1)
			? FString::Printf(TEXT("GDD imported: %s (~%d tokens)"), *LastImportedFileName, TotalTokens)
			: FString::Printf(TEXT("%d GDD files imported (~%d tokens total)"), ImportedCount, TotalTokens);
		B->PushToast(Msg, TEXT("success"));
	}

	RefreshOverlay();
}

void FUECPGddCoordinator::SaveFile(const FString& FileIdStr, const FString& Content)
{
	FGuid Guid;
	if (!FGuid::Parse(FileIdStr, Guid)) return;

	TArray<TSharedPtr<FGddFileEntry>>& Files = FGddManager::Get().GetGddFiles();
	for (TSharedPtr<FGddFileEntry>& File : Files)
	{
		if (File.IsValid() && File->FileId == Guid)
		{
			File->CharCount       = Content.Len();
			File->EstimatedTokens = Content.Len() / 4;
			File->LastModified    = FDateTime::Now();

			const FString GddDir   = FPaths::ProjectSavedDir() / TEXT("BpGeneratorUltimate") / TEXT("gdd");
			const FString SavePath = GddDir / File->FilePath;
			FFileHelper::SaveStringToFile(Content, *SavePath);

			FGddManager::Get().SaveManifest();

			if (UUECPAppBridge* B = Bridge.Get())
			{
				B->PushToast(FString::Printf(TEXT("GDD saved: %s (~%d tokens)"),
					*File->FileName, File->EstimatedTokens), TEXT("success"));
			}
			break;
		}
	}

	RefreshOverlay();
}

void FUECPGddCoordinator::DeleteFile(const FString& FileIdStr)
{
	FGuid Guid;
	if (!FGuid::Parse(FileIdStr, Guid)) return;

	TArray<TSharedPtr<FGddFileEntry>>& Files = FGddManager::Get().GetGddFiles();
	FString DeletedName;

	for (int32 i = 0; i < Files.Num(); i++)
	{
		if (Files[i].IsValid() && Files[i]->FileId == Guid)
		{
			DeletedName = Files[i]->FileName;
			const FString GddDir   = FPaths::ProjectSavedDir() / TEXT("BpGeneratorUltimate") / TEXT("gdd");
			const FString FilePath = GddDir / Files[i]->FilePath;
			FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*FilePath);
			Files.RemoveAt(i);
			break;
		}
	}

	FGddManager::Get().SaveManifest();

	if (!DeletedName.IsEmpty() && Bridge.IsValid())
	{
		Bridge->PushToast(FString::Printf(TEXT("GDD deleted: %s"), *DeletedName), TEXT("info"));
	}

	RefreshOverlay();
}

void FUECPGddCoordinator::ToggleFile(const FString& FileIdStr, bool bEnabled)
{
	FGuid Guid;
	if (!FGuid::Parse(FileIdStr, Guid)) return;

	TArray<TSharedPtr<FGddFileEntry>>& Files = FGddManager::Get().GetGddFiles();
	for (TSharedPtr<FGddFileEntry>& File : Files)
	{
		if (File.IsValid() && File->FileId == Guid)
		{
			File->bEnabled = bEnabled;
			FGddManager::Get().SaveManifest();
			break;
		}
	}

	RefreshOverlay();
}

void FUECPGddCoordinator::LoadFileContent(const FString& FileIdStr)
{
	FGuid Guid;
	if (!FGuid::Parse(FileIdStr, Guid)) return;

	const TArray<TSharedPtr<FGddFileEntry>>& Files = FGddManager::Get().GetGddFiles();
	for (const TSharedPtr<FGddFileEntry>& File : Files)
	{
		if (!File.IsValid() || File->FileId != Guid) continue;

		const FString GddDir  = FPaths::ProjectSavedDir() / TEXT("BpGeneratorUltimate") / TEXT("gdd");
		const FString Path    = GddDir / File->FilePath;
		FString Content;
		FFileHelper::LoadFileToString(Content, *Path);

		Content.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Content.ReplaceInline(TEXT("\""), TEXT("\\\""));
		Content.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		Content.ReplaceInline(TEXT("\r"), TEXT(""));

		if (UUECPAppBridge* B = Bridge.Get())
		{
			B->ExecJs(FString::Printf(TEXT("if(typeof onGddFileContent==='function')onGddFileContent(\"%s\")"), *Content));
		}
		return;
	}
}

FString FUECPGddCoordinator::GetContentForAI() const
{
	return FGddManager::Get().GetContentForAI();
}

int32 FUECPGddCoordinator::GetTokenCount() const
{
	return FGddManager::Get().GetTokenCount();
}

void FUECPGddCoordinator::RefreshOverlay()
{
	if (UUECPAppBridge* B = Bridge.Get())
	{
		B->PushOverlayHtml(BuildOverlayHtml());
	}
}

FString FUECPGddCoordinator::BuildOverlayHtml() const
{
	const TArray<TSharedPtr<FGddFileEntry>>& Files = FGddManager::Get().GetGddFiles();

	int32 EnabledCount = 0, TotalTokens = 0;
	for (const TSharedPtr<FGddFileEntry>& F : Files)
		if (F.IsValid() && F->bEnabled) { EnabledCount++; TotalTokens += F->EstimatedTokens; }

	auto EscAttr = [](const FString& In)
	{
		return FString(In)
			.Replace(TEXT("\""), TEXT("&quot;"))
			.Replace(TEXT("<"), TEXT("&lt;"))
			.Replace(TEXT(">"), TEXT("&gt;"));
	};
	auto EscJs = [](const FString& In)
	{
		return FString(In)
			.Replace(TEXT("\\"), TEXT("\\\\"))
			.Replace(TEXT("'"), TEXT("\\'"));
	};

	FString Html = TEXT("<div class='kb-root'>");

	Html += FString::Printf(TEXT(
		"<div class='kb-tabs'>"
		  "<button class='kb-tab active'>Files<span class='kb-tab-chip'>%d</span></button>"
		"</div>"),
		Files.Num());

	Html += TEXT("<div id='gdd-panel' class='kb-panel'>");

	Html += TEXT(
		"<div class='kb-toolbar'>"
		  "<div class='kb-search-wrap'>"
			"<svg class='kb-search-icon' width='14' height='14' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'>"
			  "<circle cx='11' cy='11' r='8'/><line x1='21' y1='21' x2='16.65' y2='16.65'/>"
			"</svg>"
			"<input type='text' class='kb-search' placeholder='Filter files...' oninput=\"gddFilter(this.value)\">"
		  "</div>"
		  "<button class='attach-btn' onclick=\"app('importgddfiles')\">&#128196; Import files</button>"
		"</div>");

	if (Files.Num() == 0)
	{
		Html += TEXT(
			"<div class='kb-empty'>No GDD files imported."
			"<span class='kb-empty-sub'>Import a .txt or .md file — design notes, lore docs, mechanics — and the AI will pick them up as context.</span>"
			"</div>");
	}
	else
	{
		for (const TSharedPtr<FGddFileEntry>& File : Files)
		{
			if (!File.IsValid()) continue;
			const FString FileIdStr  = File->FileId.ToString();
			const FString SafeName   = EscAttr(File->FileName);
			const FString JsSafeName = EscJs(File->FileName);

			Html += FString::Printf(TEXT(
				"<div class='kb-card%s' data-gdd-id='%s' data-gdd-name='%s'>"
				  "<div class='kb-card-row'>"
					"<input type='checkbox' class='kb-chk' %s onchange=\"event.stopPropagation();app('togglegddfile','%s',this.checked?'true':'false')\">"
					"<div class='kb-card-content' onclick=\"gddCardEdit(this.parentNode.parentNode)\">%s</div>"
					"<span class='kb-card-meta'>~%d tokens</span>"
					"<button class='kb-card-del' onclick=\"event.stopPropagation();gddDeleteCard('%s','%s')\" title='Delete'>&times;</button>"
				  "</div>"
				  "<div class='kb-card-edit'>"
					"<textarea class='kb-card-textarea' rows='12' placeholder='Loading...'></textarea>"
					"<div class='kb-card-edit-row'>"
					  "<button class='attach-btn' onclick=\"gddCardCancel(this)\">Cancel</button>"
					  "<button class='send-btn' onclick=\"gddCardSave(this,'%s')\">Save</button>"
					"</div>"
				  "</div>"
				"</div>"),
				File->bEnabled ? TEXT("") : TEXT(" disabled"),
				*FileIdStr, *SafeName,
				File->bEnabled ? TEXT("checked") : TEXT(""), *FileIdStr,
				*SafeName,
				File->EstimatedTokens,
				*FileIdStr, *JsSafeName,
				*FileIdStr);
		}

		constexpr int32 TokenWarnThreshold = 40000;
		const bool bHighTokens = TotalTokens > TokenWarnThreshold;

		Html += FString::Printf(TEXT(
			"<div class='kb-footer'>"
			  "<span class='kb-footer-stat'>%d of %d enabled &middot; ~%d tokens</span>"
			  "<span class='kb-footer-spacer'></span>"
			  "%s"
			"</div>"),
			EnabledCount, Files.Num(), TotalTokens,
			bHighTokens
				? *FString::Printf(TEXT(
					"<span class='kb-footer-warn' style='color:#e6a23c;font-weight:600;' "
					"title='GDD content is using a large slice of every request — disable files or split them up to free model context.'>"
					"&#9888; high context cost (&gt;%dk tokens)</span>"),
					TokenWarnThreshold / 1000)
				: TEXT(""));
	}

	Html += TEXT("</div>");
	Html += TEXT("</div>");
	return Html;
}
