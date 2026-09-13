// Axivor AI — project conventions digest injected into the Architect system prompt.
#include "UECPProjectConventions.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameMapsSettings.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformFileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Settings/LevelEditorViewportSettings.h"

namespace
{
	FString GCachedBlock;
	double  GCachedAt = -1.0;
	constexpr double CacheSeconds = 120.0;

	static FString ShortClassName(const FString& Path)
	{
		FString S = Path;
		int32 Dot = INDEX_NONE;
		if (S.FindLastChar(TEXT('.'), Dot)) S = S.Mid(Dot + 1);
		if (S.EndsWith(TEXT("_C"))) S.LeftChopInline(2);
		return S;
	}
}

void UECPProjectConventions::Invalidate() { GCachedAt = -1.0; }

FString UECPProjectConventions::BuildBlock()
{
	const double Now = FPlatformTime::Seconds();
	if (GCachedAt >= 0.0 && Now - GCachedAt < CacheSeconds) return GCachedBlock;

	FString Out;
	Out += TEXT("\n\n=== PROJECT CONVENTIONS (auto-detected — follow them unless the user says otherwise) ===\n");
	Out += FString::Printf(TEXT("Project: %s · Engine %s · Units: centimetres (1 uu = 1 cm), Z up, rotation in degrees, actor rotation arrays are [pitch,yaw,roll] — prefer {\"yaw\":N} objects.\n"),
		FApp::GetProjectName(), *FEngineVersion::Current().ToString(EVersionComponent::Patch));

	// Grid / snapping the user is working with.
	if (GEditor)
	{
		const ULevelEditorViewportSettings* VS = GetDefault<ULevelEditorViewportSettings>();
		const bool bGrid = VS ? VS->GridEnabled : true;
		const bool bRot  = VS ? VS->RotGridEnabled : true;
		Out += FString::Printf(TEXT("Editor snapping: location grid %s (%.0f uu), rotation grid %s (%.1f°), scale grid %.2f. Snap placed actors to this grid unless told not to.\n"),
			bGrid ? TEXT("ON") : TEXT("off"), GEditor->GetGridSize(), bRot ? TEXT("ON") : TEXT("off"), GEditor->GetRotGridSize().Yaw, GEditor->GetScaleGridSize());
	}

	// Default framework classes and maps.
	if (const UGameMapsSettings* GM = GetDefault<UGameMapsSettings>())
	{
		const FString GameMode = GM->GetGlobalDefaultGameMode();
		Out += FString::Printf(TEXT("Defaults: GameMode=%s · GameInstance=%s · EditorStartupMap=%s · GameDefaultMap=%s\n"),
			GameMode.IsEmpty() ? TEXT("(none)") : *ShortClassName(GameMode),
			GM->GameInstanceClass.IsValid() ? *ShortClassName(GM->GameInstanceClass.ToString()) : TEXT("(none)"),
			GM->EditorStartupMap.IsValid() ? *FPackageName::GetShortName(GM->EditorStartupMap.GetLongPackageName()) : TEXT("(none)"),
			GM->GetGameDefaultMap().IsEmpty() ? TEXT("(none)") : *FPackageName::GetShortName(GM->GetGameDefaultMap()));
	}

	// Content layout + naming prefixes from the asset registry (cheap: no asset loads).
	{
		IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> Assets;
		FARFilter F; F.PackagePaths.Add(TEXT("/Game")); F.bRecursivePaths = true;
		AR.GetAssets(F, Assets);

		TMap<FString, int32> TopFolders;
		TMap<FString, int32> Prefixes;
		TMap<FString, int32> Classes;
		for (const FAssetData& A : Assets)
		{
			const FString Pkg = A.PackagePath.ToString(); // /Game/Foo/Bar
			FString Rest = Pkg.Mid(6);                    // Foo/Bar
			int32 Slash = INDEX_NONE;
			const FString Top = Rest.FindChar(TEXT('/'), Slash) ? Rest.Left(Slash) : Rest;
			if (!Top.IsEmpty()) TopFolders.FindOrAdd(Top)++;
			const FString Name = A.AssetName.ToString();
			int32 Us = INDEX_NONE;
			if (Name.FindChar(TEXT('_'), Us) && Us >= 1 && Us <= 5) Prefixes.FindOrAdd(Name.Left(Us))++;
			Classes.FindOrAdd(A.AssetClassPath.GetAssetName().ToString())++;
		}
		auto TopN = [](TMap<FString, int32>& M, int32 N, int32 MinCount) -> FString
		{
			TArray<TPair<FString, int32>> Arr; for (auto& KV : M) if (KV.Value >= MinCount) Arr.Add(KV);
			Arr.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B) { return A.Value > B.Value; });
			FString S;
			for (int32 i = 0; i < Arr.Num() && i < N; ++i) S += FString::Printf(TEXT("%s%s(%d)"), i ? TEXT(", ") : TEXT(""), *Arr[i].Key, Arr[i].Value);
			return S;
		};
		Out += FString::Printf(TEXT("Content: %d assets under /Game. Top folders: %s\n"), Assets.Num(), *TopN(TopFolders, 12, 1));
		const FString Pref = TopN(Prefixes, 10, 5);
		if (!Pref.IsEmpty()) Out += FString::Printf(TEXT("Naming prefixes in use (keep them): %s\n"), *Pref);
		Out += FString::Printf(TEXT("Asset classes: %s\n"), *TopN(Classes, 10, 3));
	}

	// Gameplay/engine plugins that change how things should be built.
	{
		static const TCHAR* Watch[] = { TEXT("GameplayAbilities"), TEXT("EnhancedInput"), TEXT("Mover"), TEXT("PCG"), TEXT("StateTree"), TEXT("CommonUI"), TEXT("Niagara"),
			TEXT("MetaHumanCharacter"), TEXT("Water"), TEXT("ChaosVehiclesPlugin"), TEXT("ModelViewViewModel"), TEXT("GameFeatures"), TEXT("ProceduralVegetationEditor"), TEXT("ControlRigPhysics") };
		FString On;
		for (const TCHAR* N : Watch)
		{
			const TSharedPtr<IPlugin> P = IPluginManager::Get().FindPlugin(N);
			if (P.IsValid() && P->IsEnabled()) On += FString::Printf(TEXT("%s%s"), On.IsEmpty() ? TEXT("") : TEXT(", "), N);
		}
		if (!On.IsEmpty()) Out += FString::Printf(TEXT("Enabled gameplay plugins: %s\n"), *On);
	}

	// Scanner index availability.
	{
		const FString Index = FPaths::ProjectSavedDir() / TEXT("AI") / TEXT("ProjectIndex.json");
		Out += FPlatformFileManager::Get().GetPlatformFile().FileExists(*Index)
			? TEXT("Project scanner index exists — use the scanner/discovery tools before broad asset listing.\n")
			: TEXT("No project scanner index yet — suggest scanning the project before large refactors.\n");
	}
	Out += TEXT("=== END PROJECT CONVENTIONS ===\n");

	GCachedBlock = Out;
	GCachedAt = Now;
	return GCachedBlock;
}
