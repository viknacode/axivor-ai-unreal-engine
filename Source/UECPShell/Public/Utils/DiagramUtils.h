// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

class SWindow;

namespace DiagramUtils
{
	UECPSHELL_API FString CreateDiagramPopoutHtml(const FString& DiagramData);

	UECPSHELL_API TSharedPtr<SWindow> CreateDiagramWindow(const FString& DiagramData);

	UECPSHELL_API FString GetDiagramCloseResponseHtml();

	DECLARE_DELEGATE_RetVal_ThreeParams(bool, FOnDashboardLoadUrl, const FString&, const FString&, FString&);

	UECPSHELL_API TSharedPtr<SWindow> CreateProjectDashboardWindow(
		const FString& DepGraphJson,
		const FString& HeatmapData,
		const FString& PieChartData,
		const FString& InheritanceNomnoml,
		const FString& PerformanceJson,
		const FString& PastTracesJson,
		const FString& VisNetworkJs,
		const FString& NomnomlJs,
		const FString& GraphreJs,
		FOnDashboardLoadUrl OnLoadUrl = FOnDashboardLoadUrl());
}
