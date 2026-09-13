// Copyright 2026, BlueprintsLab, All rights reserved

#include "Utils/ContentBrowserUtils.h"

#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserItemPath.h"

namespace UECPContentBrowserUtils
{
	void GetFocusedContentBrowserPath(FString& OutPath, FString& OutError)
	{
		FContentBrowserModule& Module = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		IContentBrowserSingleton& Browser = Module.Get();
		FContentBrowserItemPath CurrentPathItem = Browser.GetCurrentPath();

		if (CurrentPathItem.HasInternalPath())
		{
			OutPath = CurrentPathItem.GetInternalPathString();
		}

		if (OutPath.IsEmpty())
		{
			TArray<FString> SelectedFolders;
			Browser.GetSelectedPathViewFolders(SelectedFolders);
			if (SelectedFolders.Num() > 0)
			{
				OutPath = SelectedFolders[0];
			}
			else
			{
				OutError = TEXT("Could not determine the current Content Browser path. Please click on a folder in the Content Browser.");
				OutPath = TEXT("/Game/");
			}
		}
	}
}
