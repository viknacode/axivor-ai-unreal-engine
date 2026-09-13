// Axivor AI — World Builder: direct landscape data edits through FLandscapeEditDataInterface
// (heightmap import with resampling, brush-like sculpting, rule-based layer painting).

#include "WorldExtCommon.h"
#include "UECPWorldExtModule.h"

#include "ScopedTransaction.h"
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeEdit.h"
#include "LandscapeEditLayer.h"
#include "LandscapeDataAccess.h"
#include "LandscapeComponent.h"
#include "LandscapeImportHelper.h"
#include "LandscapeEditorModule.h"
#include "Components/SplineComponent.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Modules/ModuleManager.h"

namespace WorldExt
{
	// ═════════════════════════════════════════════════════════════════════════
	// Shared landscape context
	// ═════════════════════════════════════════════════════════════════════════
	struct FLandscapeCtx
	{
		ALandscape* Landscape = nullptr;
		ULandscapeInfo* Info = nullptr;
		int32 MinX = 0, MinY = 0, MaxX = 0, MaxY = 0;
		FTransform LToW = FTransform::Identity;
		FVector Scale = FVector::OneVector;
		FGuid EditLayerGuid;
		FString EditLayerName;

		int32 SizeX() const { return MaxX - MinX + 1; }
		int32 SizeY() const { return MaxY - MinY + 1; }
		// World XY → landscape vertex coordinates (quads are 1 unit in landscape-local space).
		FVector2D WorldToVertex(const FVector& W) const
		{
			const FVector L = LToW.InverseTransformPosition(W);
			return FVector2D(L.X, L.Y);
		}
		double VertexWorldZ(int32 VX, int32 VY, uint16 H) const
		{
			return LToW.TransformPosition(FVector((double)VX, (double)VY, (double)LandscapeDataAccess::GetLocalHeight(H))).Z;
		}
		uint16 WorldZToTex(double VX, double VY, double WorldZ) const
		{
			const FVector L = LToW.InverseTransformPosition(FVector(VX, VY, WorldZ));
			return LandscapeDataAccess::GetTexHeight((float)L.Z);
		}
		// Height delta in world cm → uint16 delta.
		double CmToTex(double Cm) const
		{
			const double SZ = FMath::Max((double)KINDA_SMALL_NUMBER, FMath::Abs(Scale.Z));
			return Cm / SZ * (double)LANDSCAPE_INV_ZSCALE;
		}
		double CmToVertex(double Cm) const
		{
			const double SX = FMath::Max((double)KINDA_SMALL_NUMBER, FMath::Abs(Scale.X));
			return Cm / SX;
		}
	};

	static ALandscape* FindLandscapeActor(UWorld* World, const FString& Label)
	{
		for (TActorIterator<ALandscape> It(World); It; ++It)
		{
			if (Label.IsEmpty() || It->GetActorLabel().Equals(Label, ESearchCase::IgnoreCase)) return *It;
		}
		for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
		{
			if (Label.IsEmpty() || It->GetActorLabel().Equals(Label, ESearchCase::IgnoreCase))
			{
				if (ALandscape* L = It->GetLandscapeActor()) return L;
			}
		}
		return nullptr;
	}

	static bool PickEditLayer(ALandscape* L, const FString& Requested, FGuid& OutGuid, FString& OutName, FString& OutErr)
	{
		OutGuid = FGuid();
		if (!L->HasLayersContent()) { OutName = TEXT("(landscape has no edit layers)"); return true; }
		auto Use = [&OutGuid, &OutName](const ULandscapeEditLayerBase* EL)
		{
			OutGuid = EL->GetGuid();
			OutName = EL->GetName().ToString();
		};
		if (!Requested.IsEmpty())
		{
			const ULandscapeEditLayerBase* EL = L->GetEditLayerConst(FName(*Requested));
			if (!EL)
			{
				FString Names;
				for (const ULandscapeEditLayerBase* E : L->GetEditLayersConst()) { if (!E) continue; if (!Names.IsEmpty()) Names += TEXT(", "); Names += E->GetName().ToString(); }
				OutErr = FString::Printf(TEXT("edit_layer '%s' not found. Available: %s"), *Requested, *Names);
				return false;
			}
			if (!EL->SupportsEditingTools()) { OutErr = FString::Printf(TEXT("edit_layer '%s' is procedural and cannot be edited by tools."), *Requested); return false; }
			Use(EL);
			return true;
		}
		const FGuid Current = L->GetEditingLayer();
		if (Current.IsValid())
		{
			if (const ULandscapeEditLayerBase* EL = L->GetEditLayerConst(Current)) { if (EL->SupportsEditingTools()) { Use(EL); return true; } }
		}
		for (const ULandscapeEditLayerBase* EL : L->GetEditLayersConst())
		{
			if (EL && EL->SupportsEditingTools()) { Use(EL); return true; }
		}
		OutErr = TEXT("Landscape uses edit layers but none of them supports tool editing. Create a standard layer in Landscape Mode first.");
		return false;
	}

	static bool OpenLandscape(const TSharedPtr<FJsonObject>& Args, FLandscapeCtx& Ctx, FString& OutErr)
	{
		UWorld* World = EditorWorld();
		if (!World) { OutErr = TEXT("No editor world available."); return false; }
		const FString Label = ArgStr(Args, TEXT("landscape_label"));
		Ctx.Landscape = FindLandscapeActor(World, Label);
		if (!Ctx.Landscape)
		{
			OutErr = Label.IsEmpty() ? TEXT("No Landscape actor in the level (create one with create_landscape).") : FString::Printf(TEXT("No Landscape with label '%s'."), *Label);
			return false;
		}
		Ctx.Info = Ctx.Landscape->GetLandscapeInfo();
		if (!Ctx.Info) { OutErr = TEXT("Landscape has no LandscapeInfo (is the level fully loaded?)."); return false; }
		if (!Ctx.Info->GetLandscapeExtent(Ctx.MinX, Ctx.MinY, Ctx.MaxX, Ctx.MaxY)) { OutErr = TEXT("Could not get the landscape extent (no components loaded?)."); return false; }
		Ctx.LToW = Ctx.Landscape->LandscapeActorToWorld();
		Ctx.Scale = Ctx.LToW.GetScale3D();
		return PickEditLayer(Ctx.Landscape, ArgStr(Args, TEXT("edit_layer")), Ctx.EditLayerGuid, Ctx.EditLayerName, OutErr);
	}

	// Runs `Fn` inside the target edit layer (when the landscape has layers) and requests the layer merge afterwards.
	template<typename TFn>
	static void WithEditLayer(const FLandscapeCtx& Ctx, TFn&& Fn)
	{
		if (Ctx.EditLayerGuid.IsValid())
		{
			ALandscape* L = Ctx.Landscape;
			FScopedSetLandscapeEditingLayer Scope(L, Ctx.EditLayerGuid, [L]() { L->RequestLayersContentUpdateForceAll(); });
			Fn();
		}
		else
		{
			Fn();
		}
	}

	static void FinishLandscapeEdit(const FLandscapeCtx& Ctx, bool bFullPostEdit)
	{
		if (bFullPostEdit && !Ctx.EditLayerGuid.IsValid()) Ctx.Landscape->PostEditChange();
		Ctx.Landscape->MarkPackageDirty();
		if (GEditor) GEditor->RedrawLevelEditingViewports();
	}

	// Radial brush weight: 1 inside the plateau, smoothstep to 0 across the falloff band.
	static double BrushWeight(double DistNorm, double Falloff)
	{
		if (DistNorm >= 1.0) return 0.0;
		const double F = FMath::Clamp(Falloff, 0.0, 1.0);
		if (F <= KINDA_SMALL_NUMBER || DistNorm <= 1.0 - F) return 1.0;
		const double T = FMath::Clamp((1.0 - DistNorm) / F, 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	static TSharedRef<FJsonObject> RegionJson(int32 X1, int32 Y1, int32 X2, int32 Y2)
	{
		TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetNumberField(TEXT("x1"), X1); R->SetNumberField(TEXT("y1"), Y1); R->SetNumberField(TEXT("x2"), X2); R->SetNumberField(TEXT("y2"), Y2);
		R->SetNumberField(TEXT("vertices"), (double)(X2 - X1 + 1) * (double)(Y2 - Y1 + 1));
		return R;
	}

	// ═════════════════════════════════════════════════════════════════════════
	// world_landscape_import_heightmap
	// ═════════════════════════════════════════════════════════════════════════
	FUECPToolResult HandleLandscapeImportHeightmap(const TSharedPtr<FJsonObject>& Args)
	{
		const FString FilePath = ArgStr(Args, TEXT("file_path"));
		if (FilePath.IsEmpty()) return Fail(TEXT("file_path is required (absolute path to a 16-bit grayscale .png or a .r16/.raw heightmap)."));
		const FString FullPath = FPaths::ConvertRelativePathToFull(FilePath);
		if (!IFileManager::Get().FileExists(*FullPath)) return Fail(FString::Printf(TEXT("File not found: %s"), *FullPath));
		const bool bFlipY = ArgBool(Args, TEXT("flip_y"), false);
		const double ScaleZ = ArgNum(Args, TEXT("scale_z"), 0.0);

		FLandscapeCtx Ctx; FString Err;
		if (!OpenLandscape(Args, Ctx, Err)) return Fail(Err);

		// The file formats (PNG / RAW / R16) are registered by the LandscapeEditor module.
		FModuleManager::LoadModuleChecked<ILandscapeEditorModule>("LandscapeEditor");

		FLandscapeImportDescriptor Desc;
		FText Msg;
		const ELandscapeImportResult DescRes = FLandscapeImportHelper::GetHeightmapImportDescriptor(FullPath, true, bFlipY, Desc, Msg);
		if (DescRes == ELandscapeImportResult::Error) return Fail(FString::Printf(TEXT("Heightmap descriptor failed: %s"), *Msg.ToString()));
		if (Desc.ImportResolutions.Num() == 0) return Fail(TEXT("Heightmap file reports no resolution (unsupported format? use 16-bit grayscale PNG or .r16 raw)."));
		const FString DescWarning = (DescRes == ELandscapeImportResult::Warning) ? Msg.ToString() : FString();

		// For raw files the resolution can be ambiguous; prefer the entry matching the landscape, else the first.
		int32 DescIndex = Desc.FindDescriptorIndex(Ctx.SizeX(), Ctx.SizeY());
		if (DescIndex == INDEX_NONE) DescIndex = 0;
		const FLandscapeImportResolution ImportRes = Desc.ImportResolutions[DescIndex];

		TArray<uint16> Data;
		const ELandscapeImportResult DataRes = FLandscapeImportHelper::GetHeightmapImportData(Desc, DescIndex, Data, Msg);
		if (DataRes == ELandscapeImportResult::Error) return Fail(FString::Printf(TEXT("Heightmap read failed: %s"), *Msg.ToString()));
		if (Data.Num() != (int32)(ImportRes.Width * ImportRes.Height)) return Fail(FString::Printf(TEXT("Heightmap data size mismatch (%d values for %ux%u)."), Data.Num(), ImportRes.Width, ImportRes.Height));

		const FLandscapeImportResolution LandRes((uint32)Ctx.SizeX(), (uint32)Ctx.SizeY());
		TArray<uint16> Final;
		const bool bResampled = ImportRes != LandRes;
		if (bResampled) FLandscapeImportHelper::TransformHeightmapImportData(Data, Final, ImportRes, LandRes, ELandscapeImportTransformType::Resample);
		else Final = MoveTemp(Data);
		if (Final.Num() != Ctx.SizeX() * Ctx.SizeY()) return Fail(TEXT("Resampled heightmap does not match the landscape size."));

		uint16 HMin = 65535, HMax = 0;
		for (const uint16 H : Final) { HMin = FMath::Min(HMin, H); HMax = FMath::Max(HMax, H); }

		const FScopedTransaction Transaction(NSLOCTEXT("AxivorAI", "WorldLandscapeImportHeightmap", "World: Import Landscape Heightmap"));
		Ctx.Landscape->Modify();
		int32 ScaledProxies = 0;
		if (ScaleZ > 0.0)
		{
			for (TActorIterator<ALandscapeProxy> It(Ctx.Landscape->GetWorld()); It; ++It)
			{
				if (It->GetLandscapeInfo() != Ctx.Info) continue;
				It->Modify();
				const FVector S = It->GetActorRelativeScale3D();
				It->SetActorRelativeScale3D(FVector(S.X, S.Y, ScaleZ));
				++ScaledProxies;
			}
			Ctx.LToW = Ctx.Landscape->LandscapeActorToWorld();
			Ctx.Scale = Ctx.LToW.GetScale3D();
		}
		WithEditLayer(Ctx, [&Ctx, &Final]()
		{
			FLandscapeEditDataInterface LandscapeEdit(Ctx.Info);
			LandscapeEdit.SetHeightData(Ctx.MinX, Ctx.MinY, Ctx.MaxX, Ctx.MaxY, Final.GetData(), 0, true);
			LandscapeEdit.Flush();
		});
		FinishLandscapeEdit(Ctx, true);

		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("landscape"), Ctx.Landscape->GetActorLabel());
		Out->SetStringField(TEXT("file_path"), FullPath);
		Out->SetStringField(TEXT("edit_layer"), Ctx.EditLayerName);
		Out->SetNumberField(TEXT("file_width"), ImportRes.Width);
		Out->SetNumberField(TEXT("file_height"), ImportRes.Height);
		Out->SetNumberField(TEXT("landscape_width"), Ctx.SizeX());
		Out->SetNumberField(TEXT("landscape_height"), Ctx.SizeY());
		Out->SetBoolField(TEXT("resampled"), bResampled);
		Out->SetBoolField(TEXT("flip_y"), bFlipY);
		Out->SetNumberField(TEXT("height_min_world_z"), FMath::RoundToDouble(Ctx.VertexWorldZ(Ctx.MinX, Ctx.MinY, HMin)));
		Out->SetNumberField(TEXT("height_max_world_z"), FMath::RoundToDouble(Ctx.VertexWorldZ(Ctx.MinX, Ctx.MinY, HMax)));
		if (ScaleZ > 0.0) { Out->SetNumberField(TEXT("scale_z"), ScaleZ); Out->SetNumberField(TEXT("scaled_proxies"), ScaledProxies); }
		if (!DescWarning.IsEmpty()) Out->SetStringField(TEXT("warning"), DescWarning);
		Out->SetObjectField(TEXT("region"), RegionJson(Ctx.MinX, Ctx.MinY, Ctx.MaxX, Ctx.MaxY));
		Out->SetStringField(TEXT("hint"), TEXT("Heights map 0..65535 → -256m..+256m at scale_z=100 (uint16 32768 = 0). Sculpt details with world_landscape_sculpt, then paint layers with world_landscape_paint_layer (height/slope rules)."));
		return Ok(Out);
	}

	// ═════════════════════════════════════════════════════════════════════════
	// world_landscape_sculpt
	// ═════════════════════════════════════════════════════════════════════════
	static bool ParseCenter(const TSharedPtr<FJsonObject>& Args, FVector& OutCenter, FString& OutErr)
	{
		if (!Args.IsValid() || !Args->HasField(TEXT("center"))) { OutErr = TEXT("center is required ({x,y} or [x,y] in world cm)."); return false; }
		const TSharedPtr<FJsonValue> V = Args->TryGetField(TEXT("center"));
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (V->TryGetArray(Arr) && Arr && Arr->Num() >= 2)
		{
			double X = 0, Y = 0, Z = 0;
			(*Arr)[0]->TryGetNumber(X); (*Arr)[1]->TryGetNumber(Y);
			if (Arr->Num() >= 3) (*Arr)[2]->TryGetNumber(Z);
			OutCenter = FVector(X, Y, Z);
			return true;
		}
		const TSharedPtr<FJsonObject>* O = nullptr;
		if (V->TryGetObject(O) && O && O->IsValid())
		{
			double X = 0, Y = 0, Z = 0;
			(*O)->TryGetNumberField(TEXT("x"), X); (*O)->TryGetNumberField(TEXT("y"), Y); (*O)->TryGetNumberField(TEXT("z"), Z);
			OutCenter = FVector(X, Y, Z);
			return true;
		}
		OutErr = TEXT("center must be {x,y} or [x,y].");
		return false;
	}

	FUECPToolResult HandleLandscapeSculpt(const TSharedPtr<FJsonObject>& Args)
	{
		const FString Mode = ArgStr(Args, TEXT("mode"), TEXT("raise")).ToLower();
		if (Mode != TEXT("raise") && Mode != TEXT("lower") && Mode != TEXT("flatten") && Mode != TEXT("smooth"))
			return Fail(TEXT("mode must be raise | lower | flatten | smooth."));
		FVector Center(ForceInit); FString Err;
		if (!ParseCenter(Args, Center, Err)) return Fail(Err);
		const double Radius = ArgNum(Args, TEXT("radius"), 0.0);
		if (Radius <= 0.0) return Fail(TEXT("radius (world cm) must be > 0."));
		const double Strength = ArgNum(Args, TEXT("strength"), (Mode == TEXT("raise") || Mode == TEXT("lower")) ? 500.0 : 1.0);
		const double Falloff = FMath::Clamp(ArgNum(Args, TEXT("falloff"), 0.5), 0.0, 1.0);
		const bool bHasTarget = Args.IsValid() && Args->HasField(TEXT("target_height"));
		const double TargetHeight = ArgNum(Args, TEXT("target_height"), 0.0);
		const int32 SmoothKernel = FMath::Clamp((int32)ArgNum(Args, TEXT("smooth_kernel"), 2.0), 1, 16);

		FLandscapeCtx Ctx;
		if (!OpenLandscape(Args, Ctx, Err)) return Fail(Err);

		const FVector2D CV = Ctx.WorldToVertex(Center);
		const double RV = Ctx.CmToVertex(Radius);
		const int32 Pad = (Mode == TEXT("smooth")) ? SmoothKernel : 0;
		int32 X1 = FMath::Clamp(FMath::FloorToInt(CV.X - RV) - Pad, Ctx.MinX, Ctx.MaxX);
		int32 Y1 = FMath::Clamp(FMath::FloorToInt(CV.Y - RV) - Pad, Ctx.MinY, Ctx.MaxY);
		int32 X2 = FMath::Clamp(FMath::CeilToInt(CV.X + RV) + Pad, Ctx.MinX, Ctx.MaxX);
		int32 Y2 = FMath::Clamp(FMath::CeilToInt(CV.Y + RV) + Pad, Ctx.MinY, Ctx.MaxY);
		if (CV.X + RV < Ctx.MinX || CV.X - RV > Ctx.MaxX || CV.Y + RV < Ctx.MinY || CV.Y - RV > Ctx.MaxY)
			return Fail(FString::Printf(TEXT("Brush at (%.0f, %.0f) r=%.0f is outside the landscape (vertex extent %d..%d x %d..%d, vertex (%.1f, %.1f))."), Center.X, Center.Y, Radius, Ctx.MinX, Ctx.MaxX, Ctx.MinY, Ctx.MaxY, CV.X, CV.Y));

		const double DeltaTex = Ctx.CmToTex(FMath::Abs(Strength));
		const double Blend = FMath::Clamp(Strength, 0.0, 1.0);

		const FScopedTransaction Transaction(NSLOCTEXT("AxivorAI", "WorldLandscapeSculpt", "World: Sculpt Landscape"));
		Ctx.Landscape->Modify();
		int32 Changed = 0;
		double MinDelta = 0.0, MaxDelta = 0.0;
		uint16 TargetTex = 32768;
		WithEditLayer(Ctx, [&]()
		{
			FLandscapeEditDataInterface LandscapeEdit(Ctx.Info);
			TArray<uint16> Data;
			Data.AddZeroed((X2 - X1 + 1) * (Y2 - Y1 + 1));
			LandscapeEdit.GetHeightData(X1, Y1, X2, Y2, Data.GetData(), 0); // may shrink the region to loaded components
			const int32 W = X2 - X1 + 1;
			const int32 H = Y2 - Y1 + 1;
			if (W <= 0 || H <= 0) return;
			Data.SetNum(W * H, EAllowShrinking::No);
			TArray<uint16> Src = Data;

			if (Mode == TEXT("flatten"))
			{
				if (bHasTarget) TargetTex = Ctx.WorldZToTex(CV.X, CV.Y, TargetHeight);
				else
				{
					const int32 CX = FMath::Clamp(FMath::RoundToInt(CV.X) - X1, 0, W - 1);
					const int32 CY = FMath::Clamp(FMath::RoundToInt(CV.Y) - Y1, 0, H - 1);
					TargetTex = Src[CY * W + CX];
				}
			}
			for (int32 y = 0; y < H; ++y)
			{
				for (int32 x = 0; x < W; ++x)
				{
					const double DX = (double)(X1 + x) - CV.X;
					const double DY = (double)(Y1 + y) - CV.Y;
					const double D = FMath::Sqrt(DX * DX + DY * DY) / RV;
					const double Wgt = BrushWeight(D, Falloff);
					if (Wgt <= 0.0) continue;
					const double Old = (double)Src[y * W + x];
					double New = Old;
					if (Mode == TEXT("raise")) New = Old + DeltaTex * Wgt;
					else if (Mode == TEXT("lower")) New = Old - DeltaTex * Wgt;
					else if (Mode == TEXT("flatten")) New = FMath::Lerp(Old, (double)TargetTex, Wgt * Blend);
					else // smooth: box average over the kernel, clamped to the fetched region
					{
						double Sum = 0.0; int32 Cnt = 0;
						for (int32 ky = FMath::Max(0, y - SmoothKernel); ky <= FMath::Min(H - 1, y + SmoothKernel); ++ky)
							for (int32 kx = FMath::Max(0, x - SmoothKernel); kx <= FMath::Min(W - 1, x + SmoothKernel); ++kx) { Sum += (double)Src[ky * W + kx]; ++Cnt; }
						if (Cnt > 0) New = FMath::Lerp(Old, Sum / (double)Cnt, Wgt * Blend);
					}
					const uint16 NewTex = (uint16)FMath::Clamp(FMath::RoundToInt(New), 0, 65535);
					if (NewTex != Src[y * W + x])
					{
						Data[y * W + x] = NewTex;
						++Changed;
						const double Delta = ((double)NewTex - Old) * (double)LANDSCAPE_ZSCALE * Ctx.Scale.Z;
						MinDelta = FMath::Min(MinDelta, Delta);
						MaxDelta = FMath::Max(MaxDelta, Delta);
					}
				}
			}
			if (Changed > 0)
			{
				LandscapeEdit.SetHeightData(X1, Y1, X2, Y2, Data.GetData(), 0, true);
				LandscapeEdit.Flush();
			}
		});
		FinishLandscapeEdit(Ctx, false);

		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("landscape"), Ctx.Landscape->GetActorLabel());
		Out->SetStringField(TEXT("edit_layer"), Ctx.EditLayerName);
		Out->SetStringField(TEXT("mode"), Mode);
		Out->SetObjectField(TEXT("center"), VecJson(Center));
		Out->SetNumberField(TEXT("radius_cm"), Radius);
		Out->SetNumberField(TEXT("radius_vertices"), FMath::RoundToDouble(RV * 10.0) / 10.0);
		Out->SetNumberField(TEXT("strength"), Strength);
		Out->SetNumberField(TEXT("falloff"), Falloff);
		if (Mode == TEXT("flatten")) Out->SetNumberField(TEXT("target_world_z"), FMath::RoundToDouble(Ctx.VertexWorldZ(FMath::RoundToInt(CV.X), FMath::RoundToInt(CV.Y), TargetTex)));
		Out->SetObjectField(TEXT("region"), RegionJson(X1, Y1, X2, Y2));
		Out->SetNumberField(TEXT("vertices_changed"), Changed);
		Out->SetNumberField(TEXT("delta_min_cm"), FMath::RoundToDouble(MinDelta));
		Out->SetNumberField(TEXT("delta_max_cm"), FMath::RoundToDouble(MaxDelta));
		Out->SetStringField(TEXT("units"), TEXT("raise/lower: strength = height change in cm at full weight; flatten/smooth: strength = 0..1 blend. falloff = fraction of the radius used for the soft edge."));
		return Ok(Out);
	}

	// ═════════════════════════════════════════════════════════════════════════
	// world_landscape_paint_layer
	// ═════════════════════════════════════════════════════════════════════════
	FUECPToolResult HandleLandscapePaintLayer(const TSharedPtr<FJsonObject>& Args)
	{
		const FString LayerName = ArgStr(Args, TEXT("layer_name"));
		if (LayerName.IsEmpty()) return Fail(TEXT("layer_name is required (a landscape material layer, e.g. Grass / Rock / Dirt)."));
		const FString Mode = ArgStr(Args, TEXT("mode"), TEXT("fill")).ToLower();
		if (Mode != TEXT("fill") && Mode != TEXT("height_rule") && Mode != TEXT("slope_rule") && Mode != TEXT("circle"))
			return Fail(TEXT("mode must be fill | height_rule | slope_rule | circle."));
		const bool bHasMinH = Args.IsValid() && Args->HasField(TEXT("min_height"));
		const bool bHasMaxH = Args.IsValid() && Args->HasField(TEXT("max_height"));
		const bool bHasMinS = Args.IsValid() && Args->HasField(TEXT("min_slope"));
		const bool bHasMaxS = Args.IsValid() && Args->HasField(TEXT("max_slope"));
		const double MinH = ArgNum(Args, TEXT("min_height"), -1e12);
		const double MaxH = ArgNum(Args, TEXT("max_height"), 1e12);
		const double MinS = ArgNum(Args, TEXT("min_slope"), 0.0);
		const double MaxS = ArgNum(Args, TEXT("max_slope"), 90.0);
		if (Mode == TEXT("height_rule") && !bHasMinH && !bHasMaxH) return Fail(TEXT("height_rule needs min_height and/or max_height (world Z in cm)."));
		if (Mode == TEXT("slope_rule") && !bHasMinS && !bHasMaxS) return Fail(TEXT("slope_rule needs min_slope and/or max_slope (degrees)."));
		const double Strength = FMath::Clamp(ArgNum(Args, TEXT("strength"), 1.0), 0.0, 1.0);
		const double Falloff = FMath::Clamp(ArgNum(Args, TEXT("falloff"), 0.5), 0.0, 1.0);
		const bool bInvert = ArgBool(Args, TEXT("invert"), false);
		const bool bErase = ArgBool(Args, TEXT("erase"), false);
		const double HeightBlend = FMath::Max(0.0, ArgNum(Args, TEXT("height_blend"), 0.0));
		const double SlopeBlend = FMath::Max(0.0, ArgNum(Args, TEXT("slope_blend"), 0.0));

		FVector Center(ForceInit); double Radius = 0.0; FString Err;
		if (Mode == TEXT("circle"))
		{
			if (!ParseCenter(Args, Center, Err)) return Fail(Err);
			Radius = ArgNum(Args, TEXT("radius"), 0.0);
			if (Radius <= 0.0) return Fail(TEXT("radius (world cm) must be > 0 for mode=circle."));
		}

		FLandscapeCtx Ctx;
		if (!OpenLandscape(Args, Ctx, Err)) return Fail(Err);

		// Layer info object: must already be registered on the landscape (add_landscape_layer_info) or assigned in the material's layer list.
		ULandscapeLayerInfoObject* LayerInfo = nullptr;
		bool bLayerListed = false;
		TArray<FString> Known;
		for (const FLandscapeInfoLayerSettings& LS : Ctx.Info->Layers)
		{
			const FString N = LS.GetLayerName().ToString();
			Known.Add(N);
			if (N.Equals(LayerName, ESearchCase::IgnoreCase)) { bLayerListed = true; LayerInfo = LS.LayerInfoObj; }
		}
		bool bSelfHealed = false;
		if ((!bLayerListed || !LayerInfo))
		{
			// Not listed / no LayerInfo yet — if the landscape material actually declares this layer
			// name, self-heal by creating the LayerInfo asset via add_landscape_layer_info and retry
			// instead of failing outright.
			const TArray<FName> MaterialLayers = Ctx.Landscape->RetrieveTargetLayerNamesFromMaterials();
			const bool bDeclaredByMaterial = MaterialLayers.ContainsByPredicate([&LayerName](const FName& N) { return N.ToString().Equals(LayerName, ESearchCase::IgnoreCase); })
				|| Ctx.Landscape->HasTargetLayer(FName(*LayerName));
			if (!bDeclaredByMaterial)
			{
				TArray<FString> MatLayerNames;
				for (const FName& N : MaterialLayers) MatLayerNames.Add(N.ToString());
				return Fail(FString::Printf(TEXT("Layer '%s' has no LayerInfo on landscape '%s' (%s) and the landscape material does not declare a layer with that name. Known layers: %s. Material layers: %s"),
					*LayerName, *Ctx.Landscape->GetActorLabel(), bLayerListed ? TEXT("listed but LayerInfoObj is null") : TEXT("not listed"), *FString::Join(Known, TEXT(", ")), *FString::Join(MatLayerNames, TEXT(", "))));
			}

			TSharedRef<FJsonObject> HealArgs = MakeShared<FJsonObject>();
			HealArgs->SetStringField(TEXT("actor_label"), Ctx.Landscape->GetActorLabel());
			HealArgs->SetStringField(TEXT("layer_name"), LayerName);
			TSharedPtr<FJsonObject> HealResult; FString HealErr;
			if (!CallTool(TEXT("add_landscape_layer_info"), HealArgs, HealResult, HealErr))
			{
				return Fail(FString::Printf(TEXT("Layer '%s' is declared by the material but add_landscape_layer_info self-heal failed: %s"), *LayerName, *HealErr));
			}
			bSelfHealed = true;

			// Re-open the landscape context: add_landscape_layer_info refreshed Info->Layers/TargetLayers.
			if (!OpenLandscape(Args, Ctx, Err)) return Fail(Err);
			Known.Reset();
			bLayerListed = false; LayerInfo = nullptr;
			for (const FLandscapeInfoLayerSettings& LS : Ctx.Info->Layers)
			{
				const FString N = LS.GetLayerName().ToString();
				Known.Add(N);
				if (N.Equals(LayerName, ESearchCase::IgnoreCase)) { bLayerListed = true; LayerInfo = LS.LayerInfoObj; }
			}
			if (!bLayerListed || !LayerInfo)
			{
				return Fail(FString::Printf(TEXT("Self-heal via add_landscape_layer_info('%s') did not produce a usable LayerInfo. Known layers: %s"), *LayerName, *FString::Join(Known, TEXT(", "))));
			}
		}

		// Region: whole landscape, or the circle's bounding box.
		int32 X1 = Ctx.MinX, Y1 = Ctx.MinY, X2 = Ctx.MaxX, Y2 = Ctx.MaxY;
		FVector2D CV(ForceInit); double RV = 0.0;
		if (Mode == TEXT("circle"))
		{
			CV = Ctx.WorldToVertex(Center);
			RV = Ctx.CmToVertex(Radius);
			X1 = FMath::Clamp(FMath::FloorToInt(CV.X - RV), Ctx.MinX, Ctx.MaxX);
			Y1 = FMath::Clamp(FMath::FloorToInt(CV.Y - RV), Ctx.MinY, Ctx.MaxY);
			X2 = FMath::Clamp(FMath::CeilToInt(CV.X + RV), Ctx.MinX, Ctx.MaxX);
			Y2 = FMath::Clamp(FMath::CeilToInt(CV.Y + RV), Ctx.MinY, Ctx.MaxY);
			if (CV.X + RV < Ctx.MinX || CV.X - RV > Ctx.MaxX || CV.Y + RV < Ctx.MinY || CV.Y - RV > Ctx.MaxY)
				return Fail(TEXT("Circle is outside the landscape extent."));
		}
		const bool bNeedHeights = bHasMinH || bHasMaxH || bHasMinS || bHasMaxS;
		const bool bNeedSlope = bHasMinS || bHasMaxS;

		const FScopedTransaction Transaction(NSLOCTEXT("AxivorAI", "WorldLandscapePaintLayer", "World: Paint Landscape Layer"));
		Ctx.Landscape->Modify();
		int32 Changed = 0, FullWeight = 0;
		WithEditLayer(Ctx, [&]()
		{
			FLandscapeEditDataInterface LandscapeEdit(Ctx.Info);
			const int32 W = X2 - X1 + 1;
			const int32 H = Y2 - Y1 + 1;
			TArray<uint8> Weights;
			Weights.AddZeroed(W * H);
			LandscapeEdit.GetWeightDataFast(LayerInfo, X1, Y1, X2, Y2, Weights.GetData(), 0);

			// Heights are fetched with a 1-vertex border so slopes can use central differences.
			TArray<uint16> Heights;
			const int32 HX1 = FMath::Max(Ctx.MinX, X1 - 1), HY1 = FMath::Max(Ctx.MinY, Y1 - 1);
			const int32 HX2 = FMath::Min(Ctx.MaxX, X2 + 1), HY2 = FMath::Min(Ctx.MaxY, Y2 + 1);
			const int32 HW = HX2 - HX1 + 1, HH = HY2 - HY1 + 1;
			if (bNeedHeights)
			{
				Heights.AddZeroed(HW * HH);
				LandscapeEdit.GetHeightDataFast(HX1, HY1, HX2, HY2, Heights.GetData(), 0);
			}
			const double ZPerTex = (double)LANDSCAPE_ZSCALE * Ctx.Scale.Z; // world cm per uint16 step
			const double XYScale = FMath::Max((double)KINDA_SMALL_NUMBER, FMath::Abs(Ctx.Scale.X));
			auto HeightAt = [&Heights, HX1, HY1, HW, HH](int32 VX, int32 VY) -> uint16
			{
				const int32 IX = FMath::Clamp(VX - HX1, 0, HW - 1);
				const int32 IY = FMath::Clamp(VY - HY1, 0, HH - 1);
				return Heights[IY * HW + IX];
			};
			auto RangeMask = [](double V, bool bHasMin, double Mn, bool bHasMax, double Mx, double BlendWidth) -> double
			{
				double M = 1.0;
				if (bHasMin)
				{
					if (BlendWidth > 0.0) M = FMath::Min(M, FMath::Clamp((V - Mn) / BlendWidth + 0.5, 0.0, 1.0));
					else if (V < Mn) M = 0.0;
				}
				if (bHasMax)
				{
					if (BlendWidth > 0.0) M = FMath::Min(M, FMath::Clamp((Mx - V) / BlendWidth + 0.5, 0.0, 1.0));
					else if (V > Mx) M = 0.0;
				}
				return M;
			};

			for (int32 y = 0; y < H; ++y)
			{
				for (int32 x = 0; x < W; ++x)
				{
					const int32 VX = X1 + x, VY = Y1 + y;
					double Mask = 1.0;
					if (Mode == TEXT("circle"))
					{
						const double DX = (double)VX - CV.X, DY = (double)VY - CV.Y;
						Mask = BrushWeight(FMath::Sqrt(DX * DX + DY * DY) / RV, Falloff);
					}
					if (Mask > 0.0 && (bHasMinH || bHasMaxH))
					{
						const double WZ = Ctx.VertexWorldZ(VX, VY, HeightAt(VX, VY));
						Mask *= RangeMask(WZ, bHasMinH, MinH, bHasMaxH, MaxH, HeightBlend);
					}
					if (Mask > 0.0 && bNeedSlope)
					{
						const int32 XA = FMath::Max(Ctx.MinX, VX - 1), XB = FMath::Min(Ctx.MaxX, VX + 1);
						const int32 YA = FMath::Max(Ctx.MinY, VY - 1), YB = FMath::Min(Ctx.MaxY, VY + 1);
						const double GX = ((double)HeightAt(XB, VY) - (double)HeightAt(XA, VY)) * ZPerTex / (FMath::Max(1, XB - XA) * XYScale);
						const double GY = ((double)HeightAt(VX, YB) - (double)HeightAt(VX, YA)) * ZPerTex / (FMath::Max(1, YB - YA) * XYScale);
						const double SlopeDeg = FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(GX * GX + GY * GY)));
						Mask *= RangeMask(SlopeDeg, bHasMinS, MinS, bHasMaxS, MaxS, SlopeBlend);
					}
					if (bInvert) Mask = 1.0 - Mask;
					if (Mask <= 0.0) continue;
					const double Old = (double)Weights[y * W + x];
					const double Target = bErase ? 0.0 : 255.0;
					const uint8 NewW = (uint8)FMath::Clamp(FMath::RoundToInt(FMath::Lerp(Old, Target, Mask * Strength)), 0, 255);
					if (NewW == 255) ++FullWeight;
					if (NewW != Weights[y * W + x]) { Weights[y * W + x] = NewW; ++Changed; }
				}
			}
			if (Changed > 0)
			{
				LandscapeEdit.SetAlphaData(LayerInfo, X1, Y1, X2, Y2, Weights.GetData(), 0, ELandscapeLayerPaintingRestriction::None);
				LandscapeEdit.Flush();
			}
		});
		if (Changed > 0) Ctx.Info->UpdateLayerInfoMap();
		FinishLandscapeEdit(Ctx, false);

		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("landscape"), Ctx.Landscape->GetActorLabel());
		Out->SetStringField(TEXT("edit_layer"), Ctx.EditLayerName);
		Out->SetStringField(TEXT("layer_name"), LayerName);
		Out->SetStringField(TEXT("layer_info"), LayerInfo->GetPathName());
		Out->SetBoolField(TEXT("self_healed_layer_info"), bSelfHealed);
		Out->SetStringField(TEXT("mode"), Mode);
		if (bHasMinH) Out->SetNumberField(TEXT("min_height"), MinH);
		if (bHasMaxH) Out->SetNumberField(TEXT("max_height"), MaxH);
		if (bHasMinS) Out->SetNumberField(TEXT("min_slope"), MinS);
		if (bHasMaxS) Out->SetNumberField(TEXT("max_slope"), MaxS);
		if (Mode == TEXT("circle")) { Out->SetObjectField(TEXT("center"), VecJson(Center)); Out->SetNumberField(TEXT("radius_cm"), Radius); Out->SetNumberField(TEXT("falloff"), Falloff); }
		Out->SetNumberField(TEXT("strength"), Strength);
		Out->SetBoolField(TEXT("invert"), bInvert);
		Out->SetBoolField(TEXT("erase"), bErase);
		Out->SetObjectField(TEXT("region"), RegionJson(X1, Y1, X2, Y2));
		Out->SetNumberField(TEXT("vertices_changed"), Changed);
		Out->SetNumberField(TEXT("vertices_full_weight"), FullWeight);
		Out->SetStringField(TEXT("hint"), TEXT("Other layers are re-normalised automatically. Typical stack: fill 'Grass', then slope_rule(min_slope=35) 'Rock', then height_rule(max_height=...) 'Sand'. height_blend / slope_blend (cm / degrees) soften rule edges."));
		return Ok(Out);
	}

	// ═════════════════════════════════════════════════════════════════════════
	// world_landscape_flatten_spline
	// ═════════════════════════════════════════════════════════════════════════
	FUECPToolResult HandleLandscapeFlattenSpline(const TSharedPtr<FJsonObject>& Args)
	{
		const FString SplineActorLabel = ArgStr(Args, TEXT("spline_actor"));
		if (SplineActorLabel.IsEmpty()) return Fail(TEXT("spline_actor is required (label of a level actor with a SplineComponent, e.g. a world_build_road actor)."));
		const double Width = FMath::Max(1.0, ArgNum(Args, TEXT("width"), 600.0));
		const double Falloff = FMath::Clamp(ArgNum(Args, TEXT("falloff"), 0.5), 0.0, 1.0);
		const double SampleStep = FMath::Max(1.0, ArgNum(Args, TEXT("sample_step"), Width / 4.0));
		const double HeightOffset = ArgNum(Args, TEXT("height_offset"), 0.0);

		UWorld* World = EditorWorld();
		if (!World) return Fail(TEXT("No editor world available."));
		AActor* SplineActor = FindActorByLabel(World, SplineActorLabel);
		if (!SplineActor) return Fail(FString::Printf(TEXT("spline_actor '%s' not found in the level."), *SplineActorLabel));
		USplineComponent* Spline = SplineActor->FindComponentByClass<USplineComponent>();
		if (!Spline) return Fail(FString::Printf(TEXT("Actor '%s' has no SplineComponent."), *SplineActorLabel));

		FLandscapeCtx Ctx; FString Err;
		if (!OpenLandscape(Args, Ctx, Err)) return Fail(Err);

		const double SplineLength = (double)Spline->GetSplineLength();
		if (SplineLength <= KINDA_SMALL_NUMBER) return Fail(FString::Printf(TEXT("Spline '%s' has zero length."), *SplineActorLabel));

		const double RV = Ctx.CmToVertex(Width * 0.5);
		if (RV <= 0.0) return Fail(TEXT("width produced a zero-radius brush (check the landscape scale)."));

		// Sample the spline every sample_step cm (world space), converting each sample to landscape
		// vertex-space plus its target height in texel units.
		struct FSplineSample { FVector2D VertexPos; uint16 TargetTex; };
		TArray<FSplineSample> Samples;
		double MinVX = TNumericLimits<double>::Max(), MinVY = TNumericLimits<double>::Max();
		double MaxVX = TNumericLimits<double>::Lowest(), MaxVY = TNumericLimits<double>::Lowest();
		for (double Dist = 0.0;; Dist += SampleStep)
		{
			const bool bLast = Dist >= SplineLength;
			const double UseDist = bLast ? SplineLength : Dist;
			const FVector WorldPos = Spline->GetLocationAtDistanceAlongSpline(UseDist, ESplineCoordinateSpace::World);
			const FVector2D CV = Ctx.WorldToVertex(WorldPos);
			const uint16 TargetTex = Ctx.WorldZToTex(CV.X, CV.Y, WorldPos.Z + HeightOffset);
			Samples.Add({ CV, TargetTex });
			MinVX = FMath::Min(MinVX, CV.X); MinVY = FMath::Min(MinVY, CV.Y);
			MaxVX = FMath::Max(MaxVX, CV.X); MaxVY = FMath::Max(MaxVY, CV.Y);
			if (bLast) break;
		}
		if (Samples.Num() == 0) return Fail(TEXT("No samples generated along the spline."));

		// Non-const: GetHeightData takes these by reference and may shrink the region
		// to whatever landscape components are actually loaded.
		int32 X1 = FMath::Clamp(FMath::FloorToInt(MinVX - RV), Ctx.MinX, Ctx.MaxX);
		int32 Y1 = FMath::Clamp(FMath::FloorToInt(MinVY - RV), Ctx.MinY, Ctx.MaxY);
		int32 X2 = FMath::Clamp(FMath::CeilToInt(MaxVX + RV), Ctx.MinX, Ctx.MaxX);
		int32 Y2 = FMath::Clamp(FMath::CeilToInt(MaxVY + RV), Ctx.MinY, Ctx.MaxY);
		if (X2 < X1 || Y2 < Y1) return Fail(TEXT("Spline is entirely outside the landscape extent."));

		const FScopedTransaction Transaction(NSLOCTEXT("UECPWorldExt", "WorldLandscapeFlattenSpline", "World: Flatten Landscape Under Spline"));
		Ctx.Landscape->Modify();
		int32 Changed = 0;
		double MinDelta = 0.0, MaxDelta = 0.0;
		WithEditLayer(Ctx, [&]()
		{
			FLandscapeEditDataInterface LandscapeEdit(Ctx.Info);
			TArray<uint16> Data;
			Data.AddZeroed((X2 - X1 + 1) * (Y2 - Y1 + 1));
			LandscapeEdit.GetHeightData(X1, Y1, X2, Y2, Data.GetData(), 0); // may shrink the region to loaded components
			const int32 W = X2 - X1 + 1;
			const int32 H = Y2 - Y1 + 1;
			if (W <= 0 || H <= 0) return;
			Data.SetNum(W * H, EAllowShrinking::No);
			TArray<uint16> Src = Data;

			for (int32 y = 0; y < H; ++y)
			{
				for (int32 x = 0; x < W; ++x)
				{
					const double VX = (double)(X1 + x), VY = (double)(Y1 + y);
					double BestWeight = 0.0;
					uint16 BestTargetTex = 0;
					for (const FSplineSample& S : Samples)
					{
						const double DX = VX - S.VertexPos.X, DY = VY - S.VertexPos.Y;
						const double DistNorm = FMath::Sqrt(DX * DX + DY * DY) / RV;
						if (DistNorm >= 1.0) continue;
						const double Wgt = BrushWeight(DistNorm, Falloff);
						if (Wgt > BestWeight) { BestWeight = Wgt; BestTargetTex = S.TargetTex; }
					}
					if (BestWeight <= 0.0) continue;
					const double Old = (double)Src[y * W + x];
					const double New = FMath::Lerp(Old, (double)BestTargetTex, BestWeight);
					const uint16 NewTex = (uint16)FMath::Clamp(FMath::RoundToInt(New), 0, 65535);
					if (NewTex != Src[y * W + x])
					{
						Data[y * W + x] = NewTex;
						++Changed;
						const double Delta = ((double)NewTex - Old) * (double)LANDSCAPE_ZSCALE * Ctx.Scale.Z;
						MinDelta = FMath::Min(MinDelta, Delta);
						MaxDelta = FMath::Max(MaxDelta, Delta);
					}
				}
			}
			if (Changed > 0)
			{
				LandscapeEdit.SetHeightData(X1, Y1, X2, Y2, Data.GetData(), 0, true);
				LandscapeEdit.Flush();
			}
		});
		FinishLandscapeEdit(Ctx, false);

		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("landscape"), Ctx.Landscape->GetActorLabel());
		Out->SetStringField(TEXT("edit_layer"), Ctx.EditLayerName);
		Out->SetStringField(TEXT("spline_actor"), SplineActorLabel);
		Out->SetNumberField(TEXT("width_cm"), Width);
		Out->SetNumberField(TEXT("falloff"), Falloff);
		Out->SetNumberField(TEXT("sample_step_cm"), SampleStep);
		Out->SetNumberField(TEXT("height_offset_cm"), HeightOffset);
		Out->SetNumberField(TEXT("samples"), Samples.Num());
		Out->SetObjectField(TEXT("region"), RegionJson(X1, Y1, X2, Y2));
		Out->SetNumberField(TEXT("vertices_changed"), Changed);
		Out->SetNumberField(TEXT("delta_min_cm"), FMath::RoundToDouble(MinDelta));
		Out->SetNumberField(TEXT("delta_max_cm"), FMath::RoundToDouble(MaxDelta));
		Out->SetStringField(TEXT("hint"), TEXT("Each sample carves a disc (radius width/2, falloff-softened edge) toward the spline's height there; overlapping discs keep the strongest (nearest) sample so they don't fight. Paint the road shoulder afterwards with world_landscape_paint_layer."));
		return Ok(Out);
	}
}
