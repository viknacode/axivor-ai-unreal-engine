// Copyright 2026, BlueprintsLab, All rights reserved

#include "EditorUIRefRegistry.h"
#include "UECPEditorUIExtModule.h"

#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Misc/Guid.h"
#include "Widgets/SWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"

namespace
{
	constexpr float ObserverTickInterval = 0.1f;
	constexpr int32 RootObserverDepth = 4;

	static FString WidgetTypeOf(const TSharedRef<SWidget>& Widget)
	{
		return Widget->GetType().ToString();
	}

	static FString WidgetTextOf(const TSharedRef<SWidget>& Widget)
	{
		const FString Type = WidgetTypeOf(Widget);
		if (Type == TEXT("STextBlock"))
		{
			const TSharedRef<STextBlock> Tb = StaticCastSharedRef<STextBlock>(Widget);
			return Tb->GetText().ToString();
		}
		if (Type == TEXT("SEditableTextBox"))
		{
			const TSharedRef<SEditableTextBox> Et = StaticCastSharedRef<SEditableTextBox>(Widget);
			return Et->GetText().ToString();
		}
		if (Type == TEXT("SEditableText"))
		{
			const TSharedRef<SEditableText> Et = StaticCastSharedRef<SEditableText>(Widget);
			return Et->GetText().ToString();
		}
		if (Type == TEXT("SButton"))
		{
			const TSharedRef<SButton> Btn = StaticCastSharedRef<SButton>(Widget);
			TSharedRef<SWidget> Content = Btn->GetContent();
			if (Content != Widget && WidgetTypeOf(Content) == TEXT("STextBlock"))
			{
				return StaticCastSharedRef<STextBlock>(Content)->GetText().ToString();
			}
		}
		return FString();
	}

	static FString WidgetVisibilityOf(const TSharedRef<SWidget>& Widget)
	{
		const EVisibility V = Widget->GetVisibility();
		if (V == EVisibility::Visible)        return TEXT("visible");
		if (V == EVisibility::Collapsed)      return TEXT("collapsed");
		if (V == EVisibility::Hidden)         return TEXT("hidden");
		if (V == EVisibility::HitTestInvisible) return TEXT("hit_invisible");
		if (V == EVisibility::SelfHitTestInvisible) return TEXT("self_hit_invisible");
		return TEXT("?");
	}
}

FUECPEditorUIRefRegistry& FUECPEditorUIRefRegistry::Get()
{
	static FUECPEditorUIRefRegistry Singleton;
	return Singleton;
}

void FUECPEditorUIRefRegistry::Initialize()
{
	if (bInitialized) return;
	bInitialized = true;

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FUECPEditorUIRefRegistry::Tick),
		ObserverTickInterval);

	FObserver Root;
	Root.Id = TEXT("root");
	Root.MaxDepth = RootObserverDepth;
	Root.bIsRoot = true;
	Observers.Add(Root);
}

void FUECPEditorUIRefRegistry::Shutdown()
{
	if (!bInitialized) return;
	bInitialized = false;

	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	Observers.Reset();
	WidgetByRef.Reset();
	RefByWidgetPtr.Reset();
}

TSharedPtr<SWidget> FUECPEditorUIRefRegistry::FindWidget(const FString& Ref) const
{
	if (const TWeakPtr<SWidget>* Found = WidgetByRef.Find(Ref))
	{
		return Found->Pin();
	}
	return nullptr;
}

FString FUECPEditorUIRefRegistry::AllocateUnusedRef()
{
	while (true)
	{
		const uint32 Mix = (FPlatformTime::Cycles() * 0x9E3779B1u) ^ (++NextRefCounter * 0x85EBCA77u);
		FString Candidate = FString::Printf(TEXT("%08x"), Mix);
		if (!WidgetByRef.Contains(Candidate)) return Candidate;
	}
}

FString FUECPEditorUIRefRegistry::GetOrAssignRef(const TSharedRef<SWidget>& Widget)
{
	const SWidget* RawPtr = &Widget.Get();
	if (const FString* Existing = RefByWidgetPtr.Find(RawPtr))
	{
		return *Existing;
	}

	FString NewRef = AllocateUnusedRef();
	WidgetByRef.Add(NewRef, TWeakPtr<SWidget>(Widget));
	RefByWidgetPtr.Add(RawPtr, NewRef);
	return NewRef;
}

FString FUECPEditorUIRefRegistry::AddObserver(const FString& RootRef, int32 MaxDepth)
{
	FObserver Obs;
	Obs.Id = FGuid::NewGuid().ToString(EGuidFormats::DigitsLower).Left(8);
	Obs.MaxDepth = MaxDepth > 0 ? MaxDepth : 30;
	Obs.bIsRoot = false;

	if (!RootRef.IsEmpty())
	{
		Obs.Root = FindWidget(RootRef);
	}
	Observers.Add(Obs);
	return Obs.Id;
}

bool FUECPEditorUIRefRegistry::RemoveObserver(const FString& ObserverId)
{
	return Observers.RemoveAll([&](const FObserver& O) {
		return !O.bIsRoot && O.Id == ObserverId;
	}) > 0;
}

void FUECPEditorUIRefRegistry::GetTopLevelWindows(TArray<TSharedRef<SWindow>>& OutWindows)
{
	if (!FSlateApplication::IsInitialized()) return;
	OutWindows = FSlateApplication::Get().GetInteractiveTopLevelWindows();
}

bool FUECPEditorUIRefRegistry::Tick(float DeltaSeconds)
{
	if (!FSlateApplication::IsInitialized()) return true;

	int32 Visited = 0;
	for (FObserver& Obs : Observers)
	{
		if (Obs.bIsRoot)
		{
			TArray<TSharedRef<SWindow>> Windows;
			GetTopLevelWindows(Windows);
			for (const TSharedRef<SWindow>& Win : Windows)
			{
				WalkSubtree(Win, Obs.MaxDepth, Visited);
			}
		}
		else if (TSharedPtr<SWidget> Root = Obs.Root.Pin())
		{
			WalkSubtree(Root.ToSharedRef(), Obs.MaxDepth, Visited);
		}
		Obs.LastCachedCount = Visited;
	}

	SweepExpired();
	return true;
}

void FUECPEditorUIRefRegistry::WalkSubtree(const TSharedRef<SWidget>& Root, int32 MaxDepth, int32& OutVisited)
{
	struct FFrame { TSharedRef<SWidget> Widget; int32 Depth; };
	TArray<FFrame> Stack;
	Stack.Add({Root, 0});

	while (Stack.Num() > 0)
	{
		FFrame Cur = Stack.Pop();
		GetOrAssignRef(Cur.Widget);
		++OutVisited;

		if (Cur.Depth >= MaxDepth) continue;

		FChildren* Children = Cur.Widget->GetChildren();
		if (!Children) continue;
		for (int32 i = 0; i < Children->Num(); ++i)
		{
			TSharedRef<SWidget> Child = Children->GetChildAt(i);
			Stack.Add({Child, Cur.Depth + 1});
		}
	}
}

void FUECPEditorUIRefRegistry::SweepExpired()
{
	TArray<FString> ToRemove;
	for (auto It = WidgetByRef.CreateIterator(); It; ++It)
	{
		if (!It->Value.IsValid())
		{
			ToRemove.Add(It->Key);
			It.RemoveCurrent();
		}
	}
	for (auto It = RefByWidgetPtr.CreateIterator(); It; ++It)
	{
		if (ToRemove.Contains(It->Value))
		{
			It.RemoveCurrent();
		}
	}
}

void FUECPEditorUIRefRegistry::AppendSnapshotLine(const TSharedRef<SWidget>& Widget, int32 Depth,
	bool bIncludeSourceLocations, FString& OutText)
{
	const FString Indent = FString::ChrN(Depth * 2, TEXT(' '));
	const FString Ref = GetOrAssignRef(Widget);
	const FString Type = WidgetTypeOf(Widget);
	const FString Text = WidgetTextOf(Widget);
	const FString Vis = WidgetVisibilityOf(Widget);
	const bool bEnabled = Widget->IsEnabled();

	FString Line = FString::Printf(TEXT("%s[ref=%s] %s"), *Indent, *Ref, *Type);
	if (!Text.IsEmpty())
	{
		FString Escaped = Text.Replace(TEXT("\""), TEXT("\\\""));
		if (Escaped.Len() > 80) { Escaped = Escaped.Left(77) + TEXT("..."); }
		Line += FString::Printf(TEXT(" \"%s\""), *Escaped);
	}
	Line += FString::Printf(TEXT(" [%s%s]"), *Vis, bEnabled ? TEXT(",enabled") : TEXT(",disabled"));

#if WITH_SLATE_DEBUGGING
	if (bIncludeSourceLocations)
	{
		const FString Created = Widget->GetCreatedInLocation().ToString();
		if (!Created.IsEmpty())
		{
			Line += FString::Printf(TEXT(" [src=%s]"), *Created);
		}
	}
#endif

	OutText += Line + LINE_TERMINATOR;
}

int32 FUECPEditorUIRefRegistry::BuildSnapshot(const FString& RootRef, int32 MaxDepth, bool bIncludeSourceLocations,
	FString& OutText, FString& OutError)
{
	OutText.Reset();
	if (!FSlateApplication::IsInitialized())
	{
		OutError = TEXT("FSlateApplication not initialized");
		return 0;
	}

	TArray<TSharedRef<SWidget>> Roots;
	if (RootRef.IsEmpty())
	{
		TArray<TSharedRef<SWindow>> Windows;
		GetTopLevelWindows(Windows);
		for (const TSharedRef<SWindow>& W : Windows) Roots.Add(W);
	}
	else
	{
		TSharedPtr<SWidget> Found = FindWidget(RootRef);
		if (!Found.IsValid())
		{
			OutError = FString::Printf(TEXT("ref '%s' not found in registry"), *RootRef);
			return 0;
		}
		Roots.Add(Found.ToSharedRef());
	}

	int32 Visited = 0;
	for (const TSharedRef<SWidget>& Root : Roots)
	{
		struct FFrame { TSharedRef<SWidget> Widget; int32 Depth; };
		TArray<FFrame> Stack;
		Stack.Add({Root, 0});

		while (Stack.Num() > 0)
		{
			FFrame Cur = Stack.Pop();
			AppendSnapshotLine(Cur.Widget, Cur.Depth, bIncludeSourceLocations, OutText);
			++Visited;

			if (Cur.Depth >= MaxDepth) continue;
			FChildren* Children = Cur.Widget->GetChildren();
			if (!Children) continue;
			for (int32 i = Children->Num() - 1; i >= 0; --i)
			{
				Stack.Add({Children->GetChildAt(i), Cur.Depth + 1});
			}
		}
	}

	return Visited;
}

void FUECPEditorUIRefRegistry::CollectVisibleText(FString& OutText)
{
	OutText.Reset();
	for (const auto& Pair : WidgetByRef)
	{
		if (TSharedPtr<SWidget> W = Pair.Value.Pin())
		{
			const FString Text = WidgetTextOf(W.ToSharedRef());
			if (!Text.IsEmpty())
			{
				OutText += Text + LINE_TERMINATOR;
			}
		}
	}
}
