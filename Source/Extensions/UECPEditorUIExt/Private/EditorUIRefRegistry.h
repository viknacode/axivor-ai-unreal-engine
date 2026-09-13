// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Templates/SharedPointer.h"

class SWidget;
class SWindow;

class FUECPEditorUIRefRegistry
{
public:
	static FUECPEditorUIRefRegistry& Get();

	void Initialize();
	void Shutdown();

	TSharedPtr<SWidget> FindWidget(const FString& Ref) const;

	FString GetOrAssignRef(const TSharedRef<SWidget>& Widget);

	struct FObserver
	{
		FString Id;
		TWeakPtr<SWidget> Root;
		int32 MaxDepth = 30;
		int32 LastCachedCount = 0;
		bool bIsRoot = false;
	};

	FString AddObserver(const FString& RootRef, int32 MaxDepth);

	bool RemoveObserver(const FString& ObserverId);

	const TArray<FObserver>& GetObservers() const { return Observers; }

	int32 BuildSnapshot(const FString& RootRef, int32 MaxDepth, bool bIncludeSourceLocations,
		FString& OutText, FString& OutError);

	void CollectVisibleText(FString& OutText);

	static void GetTopLevelWindows(TArray<TSharedRef<SWindow>>& OutWindows);

private:
	FUECPEditorUIRefRegistry() = default;

	bool Tick(float DeltaSeconds);

	void WalkSubtree(const TSharedRef<SWidget>& Root, int32 MaxDepth, int32& OutVisited);
	void AppendSnapshotLine(const TSharedRef<SWidget>& Widget, int32 Depth,
		bool bIncludeSourceLocations, FString& OutText);

	FString AllocateUnusedRef();
	void SweepExpired();

	TMap<FString, TWeakPtr<SWidget>> WidgetByRef;
	TMap<const SWidget*, FString> RefByWidgetPtr;
	TArray<FObserver> Observers;
	FTSTicker::FDelegateHandle TickerHandle;
	uint32 NextRefCounter = 0;
	bool bInitialized = false;
};
