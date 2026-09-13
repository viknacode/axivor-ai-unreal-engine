// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/EditorUITools.h"
#include "EditorUIRefRegistry.h"
#include "UECPEditorUIExtModule.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/Base64.h"

#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericApplication.h"
#include "InputCoreTypes.h"
#include "Widgets/SWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"

#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

namespace
{
	TSharedPtr<FJsonObject> EnsureObj(const TSharedPtr<FJsonObject>& In)
	{
		return In.IsValid() ? In : MakeShared<FJsonObject>();
	}

	FString SerializeJson(const TSharedPtr<FJsonObject>& Obj)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
		return Out;
	}

	bool ReadModifiers(const TSharedPtr<FJsonObject>& Args, FModifierKeysState& Out)
	{
		const TSharedPtr<FJsonObject>* ModsObj = nullptr;
		if (!Args->TryGetObjectField(TEXT("modifiers"), ModsObj) || !ModsObj || !ModsObj->IsValid())
		{
			Out = FModifierKeysState();
			return true;
		}
		bool bShift = false, bCtrl = false, bAlt = false, bCmd = false;
		(*ModsObj)->TryGetBoolField(TEXT("shift"), bShift);
		(*ModsObj)->TryGetBoolField(TEXT("ctrl"), bCtrl);
		(*ModsObj)->TryGetBoolField(TEXT("alt"), bAlt);
		(*ModsObj)->TryGetBoolField(TEXT("cmd"), bCmd);
		Out = FModifierKeysState(bShift, bShift, bCtrl, bCtrl, bAlt, bAlt, bCmd, bCmd, false);
		return true;
	}

	FKey ParseMouseButton(const FString& Name)
	{
		if (Name.Equals(TEXT("right"), ESearchCase::IgnoreCase)) return EKeys::RightMouseButton;
		if (Name.Equals(TEXT("middle"), ESearchCase::IgnoreCase)) return EKeys::MiddleMouseButton;
		return EKeys::LeftMouseButton;
	}

	FVector2D WidgetCenter(const TSharedRef<SWidget>& Widget)
	{
		const FGeometry& Geo = Widget->GetCachedGeometry();
		const FVector2D LocalSize = Geo.GetLocalSize();
		const FVector2D LocalCenter = LocalSize * 0.5f;
		return Geo.LocalToAbsolute(LocalCenter);
	}

	bool SimulateMouseClickAt(const FVector2D& ScreenPos, FKey Button, bool bDouble, const FModifierKeysState& Mods, FString& OutError)
	{
		if (!FSlateApplication::IsInitialized())
		{
			OutError = TEXT("FSlateApplication not initialized");
			return false;
		}
		FSlateApplication& SA = FSlateApplication::Get();
		SA.SetCursorPos(ScreenPos);

		FPointerEvent DownEvent(
			0, 0,
			ScreenPos, ScreenPos,
			TSet<FKey>(), Button, 0.0f, Mods);

		FPointerEvent UpEvent = DownEvent;

		SA.ProcessMouseButtonDownEvent(nullptr, DownEvent);
		SA.ProcessMouseButtonUpEvent(UpEvent);
		if (bDouble)
		{
			FPointerEvent DoubleEvent = DownEvent;
			SA.ProcessMouseButtonDoubleClickEvent(nullptr, DoubleEvent);
			SA.ProcessMouseButtonUpEvent(UpEvent);
		}
		return true;
	}

	TSharedPtr<SWidget> ResolveRef(const FString& Ref, FString& OutError)
	{
		TSharedPtr<SWidget> W = FUECPEditorUIRefRegistry::Get().FindWidget(Ref);
		if (!W.IsValid())
		{
			OutError = FString::Printf(TEXT("ref '%s' not found (call editor_ui_snapshot or editor_ui_observe to populate)"), *Ref);
		}
		return W;
	}

	bool EncodePngBase64(const TArray<FColor>& Pixels, const FIntVector& Size, FString& OutDataUri, FString& OutError)
	{
		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
		if (!Wrapper.IsValid())
		{
			OutError = TEXT("Failed to create PNG image wrapper");
			return false;
		}
		if (!Wrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), Size.X, Size.Y, ERGBFormat::BGRA, 8))
		{
			OutError = TEXT("Failed to set raw pixel data");
			return false;
		}
		TArray64<uint8> Compressed = Wrapper->GetCompressed(85);
		if (Compressed.Num() == 0)
		{
			OutError = TEXT("PNG compression returned empty buffer");
			return false;
		}
		const FString Base64 = FBase64::Encode(Compressed.GetData(), Compressed.Num());
		OutDataUri = FString::Printf(TEXT("data:image/png;base64,%s"), *Base64);
		return true;
	}
}

void EditorUITools::HandleSnapshotFromArgs(const TSharedPtr<FJsonObject>& InArgs, FString& OutJson, FString& OutError)
{
	const TSharedPtr<FJsonObject> Args = EnsureObj(InArgs);
	FString Ref;
	Args->TryGetStringField(TEXT("ref"), Ref);
	int32 MaxDepth = 30;
	Args->TryGetNumberField(TEXT("max_depth"), MaxDepth);
	bool bIncludeSrc = false;
	Args->TryGetBoolField(TEXT("include_source_locations"), bIncludeSrc);

	FString Text;
	const int32 Count = FUECPEditorUIRefRegistry::Get().BuildSnapshot(Ref, MaxDepth, bIncludeSrc, Text, OutError);
	if (!OutError.IsEmpty()) return;

	const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("count"), Count);
	Result->SetStringField(TEXT("text"), Text);
	OutJson = SerializeJson(Result);
}

void EditorUITools::HandleObserveFromArgs(const TSharedPtr<FJsonObject>& InArgs, FString& OutJson, FString& OutError)
{
	const TSharedPtr<FJsonObject> Args = EnsureObj(InArgs);
	FString Ref;
	Args->TryGetStringField(TEXT("ref"), Ref);
	int32 MaxDepth = 30;
	Args->TryGetNumberField(TEXT("max_depth"), MaxDepth);

	if (!Ref.IsEmpty())
	{
		TSharedPtr<SWidget> Found = FUECPEditorUIRefRegistry::Get().FindWidget(Ref);
		if (!Found.IsValid())
		{
			OutError = FString::Printf(TEXT("ref '%s' not found"), *Ref);
			return;
		}
	}

	const FString ObserverId = FUECPEditorUIRefRegistry::Get().AddObserver(Ref, MaxDepth);

	const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("observer_id"), ObserverId);
	OutJson = SerializeJson(Result);
}

void EditorUITools::HandleUnobserveFromArgs(const TSharedPtr<FJsonObject>& InArgs, FString& OutJson, FString& OutError)
{
	const TSharedPtr<FJsonObject> Args = EnsureObj(InArgs);
	FString Id;
	if (!Args->TryGetStringField(TEXT("observer_id"), Id) || Id.IsEmpty())
	{
		OutError = TEXT("Missing required arg: observer_id");
		return;
	}
	const bool bRemoved = FUECPEditorUIRefRegistry::Get().RemoveObserver(Id);

	const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bRemoved);
	OutJson = SerializeJson(Result);
}

void EditorUITools::HandleListObserversFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJson, FString& )
{
	const auto& Observers = FUECPEditorUIRefRegistry::Get().GetObservers();
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const auto& Obs : Observers)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("id"), Obs.Id);
		Entry->SetBoolField(TEXT("is_root"), Obs.bIsRoot);
		Entry->SetNumberField(TEXT("max_depth"), Obs.MaxDepth);
		Entry->SetNumberField(TEXT("last_cached_count"), Obs.LastCachedCount);
		if (!Obs.bIsRoot && Obs.Root.IsValid())
		{
			TSharedPtr<SWidget> RootW = Obs.Root.Pin();
			if (RootW.IsValid())
			{
				const FString RootRef = FUECPEditorUIRefRegistry::Get().GetOrAssignRef(RootW.ToSharedRef());
				Entry->SetStringField(TEXT("root_ref"), RootRef);
			}
		}
		Arr.Add(MakeShared<FJsonValueObject>(Entry));
	}
	const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetArrayField(TEXT("observers"), Arr);
	OutJson = SerializeJson(Result);
}

void EditorUITools::HandleScreenshotFromArgs(const TSharedPtr<FJsonObject>& InArgs, FString& OutJson, FString& OutError)
{
	if (!FSlateApplication::IsInitialized())
	{
		OutError = TEXT("FSlateApplication not initialized");
		return;
	}
	const TSharedPtr<FJsonObject> Args = EnsureObj(InArgs);
	FString Ref;
	Args->TryGetStringField(TEXT("ref"), Ref);

	TSharedPtr<SWidget> Target;
	if (Ref.IsEmpty())
	{
		Target = FSlateApplication::Get().GetActiveTopLevelWindow();
	}
	else
	{
		Target = ResolveRef(Ref, OutError);
	}
	if (!Target.IsValid())
	{
		if (OutError.IsEmpty()) OutError = TEXT("No widget to screenshot");
		return;
	}

	TArray<FColor> Pixels;
	FIntVector Size;
	if (!FSlateApplication::Get().TakeScreenshot(Target.ToSharedRef(), Pixels, Size))
	{
		OutError = TEXT("FSlateApplication::TakeScreenshot failed");
		return;
	}

	FString DataUri;
	if (!EncodePngBase64(Pixels, Size, DataUri, OutError)) return;

	const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("width"), Size.X);
	Result->SetNumberField(TEXT("height"), Size.Y);
	Result->SetStringField(TEXT("data_uri"), DataUri);
	OutJson = SerializeJson(Result);
}

void EditorUITools::HandleWindowsFromArgs(const TSharedPtr<FJsonObject>& InArgs, FString& OutJson, FString& OutError)
{
	if (!FSlateApplication::IsInitialized())
	{
		OutError = TEXT("FSlateApplication not initialized");
		return;
	}
	const TSharedPtr<FJsonObject> Args = EnsureObj(InArgs);
	FString Mode = TEXT("list");
	if (!Args->TryGetStringField(TEXT("mode"), Mode))
	{
		Args->TryGetStringField(TEXT("op"), Mode);
	}
	int32 Index = -1;
	Args->TryGetNumberField(TEXT("index"), Index);

	TArray<TSharedRef<SWindow>> Windows;
	FUECPEditorUIRefRegistry::GetTopLevelWindows(Windows);

	if (Mode.Equals(TEXT("list"), ESearchCase::IgnoreCase))
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (int32 i = 0; i < Windows.Num(); ++i)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetNumberField(TEXT("index"), i);
			Entry->SetStringField(TEXT("title"), Windows[i]->GetTitle().ToString());
			Entry->SetStringField(TEXT("ref"), FUECPEditorUIRefRegistry::Get().GetOrAssignRef(Windows[i]));
			Arr.Add(MakeShared<FJsonValueObject>(Entry));
		}
		const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetArrayField(TEXT("windows"), Arr);
		OutJson = SerializeJson(Result);
		return;
	}

	if (Index < 0 || Index >= Windows.Num())
	{
		OutError = FString::Printf(TEXT("Window index %d out of range [0, %d)"), Index, Windows.Num());
		return;
	}
	const TSharedRef<SWindow>& Win = Windows[Index];

	if (Mode.Equals(TEXT("select"), ESearchCase::IgnoreCase))
	{
		Win->BringToFront(true);
	}
	else if (Mode.Equals(TEXT("close"), ESearchCase::IgnoreCase))
	{
		Win->RequestDestroyWindow();
	}
	else
	{
		OutError = FString::Printf(TEXT("Unknown mode '%s' (expected list|select|close)"), *Mode);
		return;
	}

	const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	OutJson = SerializeJson(Result);
}

void EditorUITools::HandleWaitForFromArgs(const TSharedPtr<FJsonObject>& InArgs, FString& OutJson, FString& )
{
	const TSharedPtr<FJsonObject> Args = EnsureObj(InArgs);
	FString MustPresent;
	Args->TryGetStringField(TEXT("text"), MustPresent);
	FString MustGone;
	Args->TryGetStringField(TEXT("text_gone"), MustGone);

	FString Visible;
	FUECPEditorUIRefRegistry::Get().CollectVisibleText(Visible);

	const bool bPresent = MustPresent.IsEmpty() || Visible.Contains(MustPresent);
	const bool bGone = MustGone.IsEmpty() || !Visible.Contains(MustGone);

	const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("present"), bPresent);
	Result->SetBoolField(TEXT("gone"), bGone);
	Result->SetBoolField(TEXT("satisfied"), bPresent && bGone);
	OutJson = SerializeJson(Result);
}

void EditorUITools::HandleClickFromArgs(const TSharedPtr<FJsonObject>& InArgs, FString& OutJson, FString& OutError)
{
	const TSharedPtr<FJsonObject> Args = EnsureObj(InArgs);
	FString Ref;
	if (!Args->TryGetStringField(TEXT("ref"), Ref) || Ref.IsEmpty())
	{
		OutError = TEXT("Missing required arg: ref");
		return;
	}
	FString ButtonName = TEXT("left");
	Args->TryGetStringField(TEXT("button"), ButtonName);
	bool bDouble = false;
	Args->TryGetBoolField(TEXT("double"), bDouble);
	FModifierKeysState Mods;
	ReadModifiers(Args, Mods);

	TSharedPtr<SWidget> Target = ResolveRef(Ref, OutError);
	if (!Target.IsValid()) return;

	if (!Target->IsEnabled())
	{
		OutError = FString::Printf(TEXT("Widget '%s' is disabled"), *Ref);
		return;
	}

	const FVector2D Pos = WidgetCenter(Target.ToSharedRef());
	if (!SimulateMouseClickAt(Pos, ParseMouseButton(ButtonName), bDouble, Mods, OutError)) return;

	const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	OutJson = SerializeJson(Result);
}

void EditorUITools::HandleHoverFromArgs(const TSharedPtr<FJsonObject>& InArgs, FString& OutJson, FString& OutError)
{
	const TSharedPtr<FJsonObject> Args = EnsureObj(InArgs);
	FString Ref;
	if (!Args->TryGetStringField(TEXT("ref"), Ref) || Ref.IsEmpty())
	{
		OutError = TEXT("Missing required arg: ref");
		return;
	}
	TSharedPtr<SWidget> Target = ResolveRef(Ref, OutError);
	if (!Target.IsValid()) return;

	const FVector2D Pos = WidgetCenter(Target.ToSharedRef());
	FSlateApplication& SA = FSlateApplication::Get();
	SA.SetCursorPos(Pos);
	FPointerEvent MoveEvent(0, 0, Pos, Pos, TSet<FKey>(), EKeys::Invalid, 0.0f, FModifierKeysState());
	SA.ProcessMouseMoveEvent(MoveEvent);

	const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	OutJson = SerializeJson(Result);
}

void EditorUITools::HandleTypeFromArgs(const TSharedPtr<FJsonObject>& InArgs, FString& OutJson, FString& OutError)
{
	const TSharedPtr<FJsonObject> Args = EnsureObj(InArgs);
	FString Ref;
	if (!Args->TryGetStringField(TEXT("ref"), Ref) || Ref.IsEmpty())
	{
		OutError = TEXT("Missing required arg: ref");
		return;
	}
	FString Text;
	Args->TryGetStringField(TEXT("text"), Text);
	bool bSubmit = false;
	Args->TryGetBoolField(TEXT("submit"), bSubmit);

	TSharedPtr<SWidget> Target = ResolveRef(Ref, OutError);
	if (!Target.IsValid()) return;

	FSlateApplication& SA = FSlateApplication::Get();
	SA.SetKeyboardFocus(Target);

	for (const TCHAR Ch : Text)
	{
		FCharacterEvent CharEvt(Ch, FModifierKeysState(), 0, false);
		SA.ProcessKeyCharEvent(CharEvt);
	}

	if (bSubmit)
	{
		FKeyEvent EnterDown(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0);
		SA.ProcessKeyDownEvent(EnterDown);
		FKeyEvent EnterUp(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0);
		SA.ProcessKeyUpEvent(EnterUp);
	}

	const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	OutJson = SerializeJson(Result);
}

void EditorUITools::HandlePressKeyFromArgs(const TSharedPtr<FJsonObject>& InArgs, FString& OutJson, FString& OutError)
{
	const TSharedPtr<FJsonObject> Args = EnsureObj(InArgs);
	FString KeyStr;
	if (!Args->TryGetStringField(TEXT("key"), KeyStr) || KeyStr.IsEmpty())
	{
		OutError = TEXT("Missing required arg: key");
		return;
	}

	bool bShift = false, bCtrl = false, bAlt = false, bCmd = false;
	TArray<FString> Tokens;
	KeyStr.ParseIntoArray(Tokens, TEXT("+"), true);
	FString FinalKey;
	for (const FString& T : Tokens)
	{
		const FString Trim = T.TrimStartAndEnd();
		if (Trim.Equals(TEXT("Shift"), ESearchCase::IgnoreCase)) bShift = true;
		else if (Trim.Equals(TEXT("Ctrl"), ESearchCase::IgnoreCase) || Trim.Equals(TEXT("Control"), ESearchCase::IgnoreCase)) bCtrl = true;
		else if (Trim.Equals(TEXT("Alt"), ESearchCase::IgnoreCase)) bAlt = true;
		else if (Trim.Equals(TEXT("Cmd"), ESearchCase::IgnoreCase) || Trim.Equals(TEXT("Command"), ESearchCase::IgnoreCase)) bCmd = true;
		else FinalKey = Trim;
	}

	const FKey K(*FinalKey);
	if (!K.IsValid())
	{
		OutError = FString::Printf(TEXT("Unknown key '%s'"), *FinalKey);
		return;
	}

	const FModifierKeysState Mods(bShift, bShift, bCtrl, bCtrl, bAlt, bAlt, bCmd, bCmd, false);
	FSlateApplication& SA = FSlateApplication::Get();
	FKeyEvent Down(K, Mods, 0, false, 0, 0);
	SA.ProcessKeyDownEvent(Down);
	FKeyEvent Up(K, Mods, 0, false, 0, 0);
	SA.ProcessKeyUpEvent(Up);

	const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	OutJson = SerializeJson(Result);
}

void EditorUITools::HandleSelectOptionFromArgs(const TSharedPtr<FJsonObject>& InArgs, FString& OutJson, FString& OutError)
{
	const TSharedPtr<FJsonObject> Args = EnsureObj(InArgs);
	FString Ref;
	if (!Args->TryGetStringField(TEXT("ref"), Ref) || Ref.IsEmpty())
	{
		OutError = TEXT("Missing required arg: ref");
		return;
	}
	FString Value;
	if (!Args->TryGetStringField(TEXT("value"), Value))
	{
		OutError = TEXT("Missing required arg: value");
		return;
	}

	TSharedPtr<SWidget> Target = ResolveRef(Ref, OutError);
	if (!Target.IsValid()) return;

	const FVector2D Pos = WidgetCenter(Target.ToSharedRef());
	FString TempErr;
	if (!SimulateMouseClickAt(Pos, EKeys::LeftMouseButton, false, FModifierKeysState(), TempErr))
	{
		OutError = TempErr;
		return;
	}

	FSlateApplication& SA = FSlateApplication::Get();
	SA.Tick();

	TArray<TSharedRef<SWindow>> Windows;
	FUECPEditorUIRefRegistry::GetTopLevelWindows(Windows);
	TSharedPtr<SWidget> Found;
	for (const TSharedRef<SWindow>& Win : Windows)
	{
		struct FFrame { TSharedRef<SWidget> Widget; };
		TArray<FFrame> Stack;
		Stack.Add({Win});
		while (Stack.Num() > 0 && !Found.IsValid())
		{
			FFrame Cur = Stack.Pop();
			if (Cur.Widget->GetType().ToString() == TEXT("STextBlock"))
			{
				const FString T = StaticCastSharedRef<STextBlock>(Cur.Widget)->GetText().ToString();
				if (T.Equals(Value, ESearchCase::CaseSensitive))
				{
					Found = Cur.Widget;
					break;
				}
			}
			if (FChildren* Children = Cur.Widget->GetChildren())
			{
				for (int32 i = 0; i < Children->Num(); ++i)
				{
					Stack.Add({Children->GetChildAt(i)});
				}
			}
		}
		if (Found.IsValid()) break;
	}

	if (!Found.IsValid())
	{
		OutError = FString::Printf(TEXT("Option '%s' not found in any open menu"), *Value);
		return;
	}

	const FVector2D OptionPos = WidgetCenter(Found.ToSharedRef());
	if (!SimulateMouseClickAt(OptionPos, EKeys::LeftMouseButton, false, FModifierKeysState(), OutError)) return;

	const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	OutJson = SerializeJson(Result);
}

void EditorUITools::HandleDragFromArgs(const TSharedPtr<FJsonObject>& InArgs, FString& OutJson, FString& OutError)
{
	const TSharedPtr<FJsonObject> Args = EnsureObj(InArgs);
	FString StartRef, EndRef;
	if (!Args->TryGetStringField(TEXT("start_ref"), StartRef) || StartRef.IsEmpty()
		|| !Args->TryGetStringField(TEXT("end_ref"), EndRef) || EndRef.IsEmpty())
	{
		OutError = TEXT("Missing required arg: start_ref / end_ref");
		return;
	}
	FModifierKeysState Mods;
	ReadModifiers(Args, Mods);

	TSharedPtr<SWidget> Start = ResolveRef(StartRef, OutError);
	if (!Start.IsValid()) return;
	TSharedPtr<SWidget> End = ResolveRef(EndRef, OutError);
	if (!End.IsValid()) return;

	const FVector2D From = WidgetCenter(Start.ToSharedRef());
	const FVector2D To = WidgetCenter(End.ToSharedRef());

	FSlateApplication& SA = FSlateApplication::Get();
	SA.SetCursorPos(From);

	FPointerEvent Down(0, 0, From, From, TSet<FKey>(), EKeys::LeftMouseButton, 0.0f, Mods);
	SA.ProcessMouseButtonDownEvent(nullptr, Down);

	const int32 Steps = 8;
	for (int32 i = 1; i <= Steps; ++i)
	{
		const float A = static_cast<float>(i) / Steps;
		const FVector2D Mid = FMath::Lerp(From, To, A);
		SA.SetCursorPos(Mid);
		FPointerEvent Move(0, 0, Mid, Mid - From, TSet<FKey>{EKeys::LeftMouseButton}, EKeys::Invalid, 0.0f, Mods);
		SA.ProcessMouseMoveEvent(Move);
	}

	FPointerEvent Up(0, 0, To, To, TSet<FKey>(), EKeys::LeftMouseButton, 0.0f, Mods);
	SA.ProcessMouseButtonUpEvent(Up);

	const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	OutJson = SerializeJson(Result);
}

void EditorUITools::HandleFillFormFromArgs(const TSharedPtr<FJsonObject>& InArgs, FString& OutJson, FString& OutError)
{
	const TSharedPtr<FJsonObject> Args = EnsureObj(InArgs);
	const TArray<TSharedPtr<FJsonValue>>* Fields = nullptr;
	if (!Args->TryGetArrayField(TEXT("fields"), Fields) || !Fields)
	{
		OutError = TEXT("Missing required arg: fields (array of {ref,value,field_type})");
		return;
	}

	int32 Completed = 0;
	int32 Failed = 0;
	TArray<TSharedPtr<FJsonValue>> Results;

	for (int32 i = 0; i < Fields->Num(); ++i)
	{
		const TSharedPtr<FJsonObject>& Field = (*Fields)[i]->AsObject();
		if (!Field.IsValid())
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetNumberField(TEXT("index"), i);
			Entry->SetBoolField(TEXT("success"), false);
			Entry->SetStringField(TEXT("error"), TEXT("invalid field entry"));
			Results.Add(MakeShared<FJsonValueObject>(Entry));
			++Failed;
			continue;
		}
		FString Ref, Value, FieldType;
		Field->TryGetStringField(TEXT("ref"), Ref);
		Field->TryGetStringField(TEXT("value"), Value);
		Field->TryGetStringField(TEXT("field_type"), FieldType);

		FString FieldErr;
		TSharedPtr<SWidget> Target = ResolveRef(Ref, FieldErr);
		if (!Target.IsValid())
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetNumberField(TEXT("index"), i);
			Entry->SetBoolField(TEXT("success"), false);
			Entry->SetStringField(TEXT("error"), FieldErr);
			Results.Add(MakeShared<FJsonValueObject>(Entry));
			++Failed;
			continue;
		}

		bool bOk = false;
		FString InnerJson, InnerErr;
		if (FieldType.Equals(TEXT("textbox"), ESearchCase::IgnoreCase))
		{
			TSharedPtr<FJsonObject> Sub = MakeShared<FJsonObject>();
			Sub->SetStringField(TEXT("ref"), Ref);
			Sub->SetStringField(TEXT("text"), Value);
			HandleTypeFromArgs(Sub, InnerJson, InnerErr);
			bOk = InnerErr.IsEmpty();
		}
		else if (FieldType.Equals(TEXT("checkbox"), ESearchCase::IgnoreCase))
		{
			const bool bWant = Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value == TEXT("1");
			if (Target->GetType().ToString() == TEXT("SCheckBox"))
			{
				TSharedRef<SCheckBox> Cb = StaticCastSharedRef<SCheckBox>(Target.ToSharedRef());
				const bool bIsChecked = Cb->IsChecked();
				if (bIsChecked != bWant)
				{
					TSharedPtr<FJsonObject> Sub = MakeShared<FJsonObject>();
					Sub->SetStringField(TEXT("ref"), Ref);
					HandleClickFromArgs(Sub, InnerJson, InnerErr);
				}
				bOk = InnerErr.IsEmpty();
			}
			else
			{
				InnerErr = TEXT("widget is not SCheckBox");
			}
		}
		else if (FieldType.Equals(TEXT("combobox"), ESearchCase::IgnoreCase))
		{
			TSharedPtr<FJsonObject> Sub = MakeShared<FJsonObject>();
			Sub->SetStringField(TEXT("ref"), Ref);
			Sub->SetStringField(TEXT("value"), Value);
			HandleSelectOptionFromArgs(Sub, InnerJson, InnerErr);
			bOk = InnerErr.IsEmpty();
		}
		else
		{
			InnerErr = FString::Printf(TEXT("Unknown field_type '%s'"), *FieldType);
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("index"), i);
		Entry->SetStringField(TEXT("ref"), Ref);
		Entry->SetBoolField(TEXT("success"), bOk);
		if (!bOk) Entry->SetStringField(TEXT("error"), InnerErr);
		Results.Add(MakeShared<FJsonValueObject>(Entry));
		if (bOk) ++Completed; else ++Failed;
	}

	const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), Failed == 0);
	Result->SetNumberField(TEXT("completed"), Completed);
	Result->SetNumberField(TEXT("failed"), Failed);
	Result->SetArrayField(TEXT("results"), Results);
	OutJson = SerializeJson(Result);
}
