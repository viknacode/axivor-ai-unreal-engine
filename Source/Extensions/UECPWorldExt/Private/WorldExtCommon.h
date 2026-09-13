// Axivor AI — World Builder: shared JSON / editor helpers and handler declarations.
#pragma once

#include "CoreMinimal.h"
#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Editor.h"
#include "Selection.h"
#include "CollisionQueryParams.h"
#include "Engine/HitResult.h"

namespace WorldExt
{
	// ── JSON helpers ────────────────────────────────────────────────────────
	inline FString ToJson(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj, W);
		W->Close();
		return Out;
	}
	inline TSharedPtr<FJsonObject> ParseJson(const FString& In)
	{
		TSharedPtr<FJsonObject> Obj;
		const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(In);
		if (!FJsonSerializer::Deserialize(R, Obj)) return nullptr;
		return Obj;
	}
	inline FString ArgStr(const TSharedPtr<FJsonObject>& A, const TCHAR* K, const FString& D = FString())
	{
		FString V; if (A.IsValid() && A->TryGetStringField(K, V)) return V; return D;
	}
	inline bool ArgBool(const TSharedPtr<FJsonObject>& A, const TCHAR* K, bool D)
	{
		bool V = D; if (A.IsValid() && A->HasTypedField<EJson::Boolean>(K)) V = A->GetBoolField(K); return V;
	}
	inline double ArgNum(const TSharedPtr<FJsonObject>& A, const TCHAR* K, double D)
	{
		double V = D; if (A.IsValid()) A->TryGetNumberField(K, V); return V;
	}
	inline TSharedPtr<FJsonObject> ArgObj(const TSharedPtr<FJsonObject>& A, const TCHAR* K)
	{
		const TSharedPtr<FJsonObject>* O = nullptr;
		if (A.IsValid() && A->TryGetObjectField(K, O) && O && O->IsValid()) return *O;
		return nullptr;
	}
	inline const TArray<TSharedPtr<FJsonValue>>* ArgArr(const TSharedPtr<FJsonObject>& A, const TCHAR* K)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (A.IsValid() && A->TryGetArrayField(K, Arr) && Arr) return Arr;
		return nullptr;
	}
	inline TArray<FString> ArgStrArray(const TSharedPtr<FJsonObject>& A, const TCHAR* K)
	{
		TArray<FString> Out;
		if (const TArray<TSharedPtr<FJsonValue>>* Arr = ArgArr(A, K))
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				FString S; if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty()) Out.Add(S);
			}
		}
		return Out;
	}
	// Accepts [x,y,z] or {x,y,z}. Returns false when the value is not a vector.
	inline bool ParseVector(const TSharedPtr<FJsonValue>& V, FVector& Out)
	{
		if (!V.IsValid()) return false;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (V->TryGetArray(Arr) && Arr && Arr->Num() >= 3)
		{
			double X = 0, Y = 0, Z = 0;
			(*Arr)[0]->TryGetNumber(X); (*Arr)[1]->TryGetNumber(Y); (*Arr)[2]->TryGetNumber(Z);
			Out = FVector(X, Y, Z);
			return true;
		}
		const TSharedPtr<FJsonObject>* O = nullptr;
		if (V->TryGetObject(O) && O && O->IsValid())
		{
			double X = 0, Y = 0, Z = 0;
			bool bAny = false;
			if ((*O)->TryGetNumberField(TEXT("x"), X)) bAny = true;
			if ((*O)->TryGetNumberField(TEXT("y"), Y)) bAny = true;
			if ((*O)->TryGetNumberField(TEXT("z"), Z)) bAny = true;
			if (!bAny) return false;
			Out = FVector(X, Y, Z);
			return true;
		}
		return false;
	}
	inline bool ArgVector(const TSharedPtr<FJsonObject>& A, const TCHAR* K, FVector& Out)
	{
		if (!A.IsValid() || !A->HasField(K)) return false;
		return ParseVector(A->TryGetField(K), Out);
	}
	inline TArray<FVector> ArgVectorArray(const TSharedPtr<FJsonObject>& A, const TCHAR* K)
	{
		TArray<FVector> Out;
		if (const TArray<TSharedPtr<FJsonValue>>* Arr = ArgArr(A, K))
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr) { FVector P; if (ParseVector(V, P)) Out.Add(P); }
		}
		return Out;
	}
	// Accepts [pitch, yaw, roll] or {pitch, yaw, roll} (degrees). Returns false when absent.
	inline bool ArgRotator(const TSharedPtr<FJsonObject>& A, const TCHAR* K, FRotator& Out)
	{
		if (!A.IsValid() || !A->HasField(K)) return false;
		const TSharedPtr<FJsonValue> V = A->TryGetField(K);
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (V->TryGetArray(Arr) && Arr && Arr->Num() >= 3)
		{
			double P = 0, Y = 0, R = 0;
			(*Arr)[0]->TryGetNumber(P); (*Arr)[1]->TryGetNumber(Y); (*Arr)[2]->TryGetNumber(R);
			Out = FRotator(P, Y, R);
			return true;
		}
		const TSharedPtr<FJsonObject>* O = nullptr;
		if (V->TryGetObject(O) && O && O->IsValid())
		{
			double P = 0, Y = 0, R = 0;
			(*O)->TryGetNumberField(TEXT("pitch"), P); (*O)->TryGetNumberField(TEXT("yaw"), Y); (*O)->TryGetNumberField(TEXT("roll"), R);
			Out = FRotator(P, Y, R);
			return true;
		}
		return false;
	}
	inline TSharedRef<FJsonObject> VecJson(const FVector& V)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("x"), FMath::RoundToDouble(V.X * 100.0) / 100.0);
		O->SetNumberField(TEXT("y"), FMath::RoundToDouble(V.Y * 100.0) / 100.0);
		O->SetNumberField(TEXT("z"), FMath::RoundToDouble(V.Z * 100.0) / 100.0);
		return O;
	}
	inline TSharedRef<FJsonObject> RotJson(const FRotator& R)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("pitch"), FMath::RoundToDouble(R.Pitch * 100.0) / 100.0);
		O->SetNumberField(TEXT("yaw"), FMath::RoundToDouble(R.Yaw * 100.0) / 100.0);
		O->SetNumberField(TEXT("roll"), FMath::RoundToDouble(R.Roll * 100.0) / 100.0);
		return O;
	}
	inline FUECPToolResult Fail(const FString& Msg) { FUECPToolResult R; R.bSuccess = false; R.ErrorMessage = Msg; return R; }
	inline FUECPToolResult Ok(const TSharedRef<FJsonObject>& O) { FUECPToolResult R; R.bSuccess = true; R.ResultJson = ToJson(O); return R; }

	// ── Dispatcher composition ──────────────────────────────────────────────
	inline IUECPToolDispatcher* Dispatcher()
	{
		return IUECPCoreModule::IsAvailable() ? &IUECPCoreModule::Get().GetToolDispatcher() : nullptr;
	}
	// Run another Axivor tool and hand back its parsed JSON (or an error string).
	inline bool CallTool(const TCHAR* Tool, const TSharedRef<FJsonObject>& Args, TSharedPtr<FJsonObject>& OutJson, FString& OutErr)
	{
		IUECPToolDispatcher* D = Dispatcher();
		if (!D) { OutErr = TEXT("Tool dispatcher unavailable."); return false; }
		if (!D->IsRegistered(FName(Tool))) { OutErr = FString::Printf(TEXT("Tool '%s' is not registered (is the Level Design extension enabled?)."), Tool); return false; }
		const FUECPToolResult R = D->ExecuteFromArgs(FName(Tool), Args);
		if (!R.bSuccess) { OutErr = FString::Printf(TEXT("%s: %s"), Tool, *R.ErrorMessage); return false; }
		OutJson = ParseJson(R.ResultJson);
		if (!OutJson.IsValid()) OutJson = MakeShared<FJsonObject>();
		OutJson->SetStringField(TEXT("_raw"), R.ResultJson.Left(2000));
		return true;
	}

	// ── Editor world / actor helpers ────────────────────────────────────────
	inline UWorld* EditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}
	// Last actor with a matching label (SetActorLabel does not enforce uniqueness, later spawns win).
	inline AActor* FindActorByLabel(UWorld* World, const FString& Label)
	{
		if (!World || Label.IsEmpty()) return nullptr;
		AActor* Found = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetActorLabel().Equals(Label, ESearchCase::IgnoreCase)) Found = *It;
		}
		return Found;
	}
	// Resolves `actors[]` labels, or the current editor selection when the array is absent / `selection` is true.
	inline TArray<AActor*> ResolveActorsOrSelection(const TSharedPtr<FJsonObject>& Args, UWorld* World, TArray<FString>& OutMissing, bool& bOutUsedSelection)
	{
		TArray<AActor*> Out;
		bOutUsedSelection = false;
		const TArray<FString> Labels = ArgStrArray(Args, TEXT("actors"));
		if (Labels.Num() > 0 && !ArgBool(Args, TEXT("selection"), false))
		{
			for (const FString& L : Labels)
			{
				if (AActor* A = FindActorByLabel(World, L)) Out.AddUnique(A); else OutMissing.Add(L);
			}
			return Out;
		}
		bOutUsedSelection = true;
		if (!GEditor) return Out;
		USelection* Sel = GEditor->GetSelectedActors();
		if (!Sel) return Out;
		for (FSelectionIterator It(*Sel); It; ++It)
		{
			if (AActor* A = Cast<AActor>(*It)) { if (A->GetWorld() == World) Out.AddUnique(A); }
		}
		return Out;
	}
	// Downward line trace from `Above` cm over the point; returns the hit (WorldStatic, complex).
	inline bool TraceGround(UWorld* World, const FVector& Point, double Above, double Below, FHitResult& OutHit, const AActor* Ignore = nullptr, const TArray<AActor*>* IgnoreAll = nullptr)
	{
		if (!World) return false;
		const FVector Start(Point.X, Point.Y, Point.Z + Above);
		const FVector End(Point.X, Point.Y, Point.Z - Below);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(AxivorWorldTrace), true);
		if (Ignore) Params.AddIgnoredActor(Ignore);
		if (IgnoreAll) Params.AddIgnoredActors(*IgnoreAll);
		return World->LineTraceSingleByChannel(OutHit, Start, End, ECC_WorldStatic, Params);
	}
	inline FString SanitizeLabel(const FString& In)
	{
		FString Out;
		for (const TCHAR C : In) Out.AppendChar(FChar::IsAlnum(C) || C == TEXT('_') ? C : TEXT('_'));
		return Out.IsEmpty() ? FString(TEXT("Axivor")) : Out;
	}

	// ── Handlers (Private/Tools/*.cpp) ──────────────────────────────────────
	FUECPToolResult HandleBuildBiome(const TSharedPtr<FJsonObject>& Args);
	FUECPToolResult HandleBuildRoad(const TSharedPtr<FJsonObject>& Args);
	FUECPToolResult HandleBuildRiver(const TSharedPtr<FJsonObject>& Args);
	FUECPToolResult HandleSnapToGrid(const TSharedPtr<FJsonObject>& Args);
	FUECPToolResult HandleAlignToSurface(const TSharedPtr<FJsonObject>& Args);
	FUECPToolResult HandlePlacePrefab(const TSharedPtr<FJsonObject>& Args);
	FUECPToolResult HandleMassEdit(const TSharedPtr<FJsonObject>& Args);
	FUECPToolResult HandleLandscapeImportHeightmap(const TSharedPtr<FJsonObject>& Args);
	FUECPToolResult HandleLandscapeSculpt(const TSharedPtr<FJsonObject>& Args);
	FUECPToolResult HandleLandscapePaintLayer(const TSharedPtr<FJsonObject>& Args);
	FUECPToolResult HandleLandscapeFlattenSpline(const TSharedPtr<FJsonObject>& Args);
}
