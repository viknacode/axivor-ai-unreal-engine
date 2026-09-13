// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/MaterialGraphTools.h"
#include "Tools/BatchToolHelper.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "MaterialShared.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialInstance.h"
#include "MaterialDomain.h"
#include "Factories/MaterialFunctionFactoryNew.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "UObject/SavePackage.h"
#include "Engine/Texture.h"
#include "Describers/MaterialGraphDescriber.h"
#include "Misc/EngineVersionComparison.h"
#include "RHIGlobals.h"
#include "ScopedTransaction.h"
#include "ShaderCompiler.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"

namespace MaterialGraphTools
{

/**
 * Reads a node handle argument that may be given as a JSON string (guid / "n3" / "3") or as a
 * JSON number (legacy index). Returns the first key present, normalised to a string.
 */
static FString ReadHandleArg(const TSharedPtr<FJsonObject>& Obj, std::initializer_list<const TCHAR*> Keys)
{
	if (!Obj.IsValid()) return FString();
	for (const TCHAR* Key : Keys)
	{
		const TSharedPtr<FJsonValue> V = Obj->TryGetField(Key);
		if (!V.IsValid() || V->IsNull()) continue;
		if (V->Type == EJson::Number) return FString::FromInt((int32)V->AsNumber());
		if (V->Type == EJson::String)
		{
			const FString S = V->AsString().TrimStartAndEnd();
			if (!S.IsEmpty()) return S;
		}
	}
	return FString();
}

static const std::initializer_list<const TCHAR*> NodeHandleKeys = { TEXT("node_id"), TEXT("node"), TEXT("node_guid"), TEXT("expression_id"), TEXT("node_index"), TEXT("index") };
static const std::initializer_list<const TCHAR*> FromNodeKeys   = { TEXT("from_node_id"), TEXT("from_node"), TEXT("from"), TEXT("source_node"), TEXT("from_node_index") };
static const std::initializer_list<const TCHAR*> ToNodeKeys     = { TEXT("to_node_id"), TEXT("to_node"), TEXT("to"), TEXT("target_node"), TEXT("to_node_index") };

static FString ResolveNodeAlias(const FString& NodeType)
{
	static TMap<FString, FString> Aliases =
	{
		{ TEXT("Add"),                    TEXT("MaterialExpressionAdd") },
		{ TEXT("Subtract"),               TEXT("MaterialExpressionSubtract") },
		{ TEXT("Multiply"),               TEXT("MaterialExpressionMultiply") },
		{ TEXT("Divide"),                 TEXT("MaterialExpressionDivide") },
		{ TEXT("Lerp"),                   TEXT("MaterialExpressionLinearInterpolate") },
		{ TEXT("LinearInterpolate"),      TEXT("MaterialExpressionLinearInterpolate") },
		{ TEXT("Constant"),               TEXT("MaterialExpressionConstant") },
		{ TEXT("Constant2"),              TEXT("MaterialExpressionConstant2Vector") },
		{ TEXT("Constant3"),              TEXT("MaterialExpressionConstant3Vector") },
		{ TEXT("Constant4"),              TEXT("MaterialExpressionConstant4Vector") },
		{ TEXT("ScalarParameter"),        TEXT("MaterialExpressionScalarParameter") },
		{ TEXT("VectorParameter"),        TEXT("MaterialExpressionVectorParameter") },
		{ TEXT("TextureParameter"),       TEXT("MaterialExpressionTextureSampleParameter2D") },
		{ TEXT("TextureSample"),          TEXT("MaterialExpressionTextureSample") },
		{ TEXT("TextureObject"),          TEXT("MaterialExpressionTextureObject") },
		{ TEXT("Fresnel"),                TEXT("MaterialExpressionFresnel") },
		{ TEXT("Panner"),                 TEXT("MaterialExpressionPanner") },
		{ TEXT("TexCoord"),               TEXT("MaterialExpressionTextureCoordinate") },
		{ TEXT("TextureCoordinate"),      TEXT("MaterialExpressionTextureCoordinate") },
		{ TEXT("Time"),                   TEXT("MaterialExpressionTime") },
		{ TEXT("Sine"),                   TEXT("MaterialExpressionSine") },
		{ TEXT("Cosine"),                 TEXT("MaterialExpressionCosine") },
		{ TEXT("Abs"),                    TEXT("MaterialExpressionAbs") },
		{ TEXT("Floor"),                  TEXT("MaterialExpressionFloor") },
		{ TEXT("Ceil"),                   TEXT("MaterialExpressionCeil") },
		{ TEXT("Frac"),                   TEXT("MaterialExpressionFrac") },
		{ TEXT("Saturate"),               TEXT("MaterialExpressionSaturate") },
		{ TEXT("Clamp"),                  TEXT("MaterialExpressionClamp") },
		{ TEXT("DotProduct"),             TEXT("MaterialExpressionDotProduct") },
		{ TEXT("CrossProduct"),           TEXT("MaterialExpressionCrossProduct") },
		{ TEXT("Append"),                 TEXT("MaterialExpressionAppendVector") },
		{ TEXT("AppendVector"),           TEXT("MaterialExpressionAppendVector") },
		{ TEXT("ComponentMask"),          TEXT("MaterialExpressionComponentMask") },
		{ TEXT("Mask"),                   TEXT("MaterialExpressionComponentMask") },
		{ TEXT("Custom"),                 TEXT("MaterialExpressionCustom") },
		{ TEXT("DepthFade"),              TEXT("MaterialExpressionDepthFade") },
		{ TEXT("WorldPosition"),          TEXT("MaterialExpressionWorldPosition") },
		{ TEXT("CameraVector"),           TEXT("MaterialExpressionCameraVectorWS") },
		{ TEXT("CameraVectorWS"),         TEXT("MaterialExpressionCameraVectorWS") },
		{ TEXT("CameraPosition"),         TEXT("MaterialExpressionCameraPositionWS") },
		{ TEXT("CameraPositionWS"),       TEXT("MaterialExpressionCameraPositionWS") },
		{ TEXT("Noise"),                  TEXT("MaterialExpressionNoise") },
		{ TEXT("VertexColor"),            TEXT("MaterialExpressionVertexColor") },
		{ TEXT("PixelNormal"),            TEXT("MaterialExpressionPixelNormalWS") },
		{ TEXT("PixelNormalWS"),          TEXT("MaterialExpressionPixelNormalWS") },
		{ TEXT("Normalize"),              TEXT("MaterialExpressionNormalize") },
		{ TEXT("RotateAboutAxis"),        TEXT("MaterialExpressionRotateAboutAxis") },
		{ TEXT("ScreenPosition"),         TEXT("MaterialExpressionScreenPosition") },
		{ TEXT("ObjectRadius"),           TEXT("MaterialExpressionObjectRadius") },
		{ TEXT("ObjectPosition"),         TEXT("MaterialExpressionObjectPositionWS") },
		{ TEXT("ObjectPositionWS"),       TEXT("MaterialExpressionObjectPositionWS") },
		{ TEXT("If"),                     TEXT("MaterialExpressionIf") },
		{ TEXT("Max"),                    TEXT("MaterialExpressionMax") },
		{ TEXT("Min"),                    TEXT("MaterialExpressionMin") },
		{ TEXT("Power"),                  TEXT("MaterialExpressionPower") },
		{ TEXT("Sqrt"),                   TEXT("MaterialExpressionSquareRoot") },
		{ TEXT("OneMinus"),               TEXT("MaterialExpressionOneMinus") },
		{ TEXT("Desaturation"),           TEXT("MaterialExpressionDesaturation") },
		{ TEXT("Distance"),               TEXT("MaterialExpressionDistance") },
		{ TEXT("SphereMask"),             TEXT("MaterialExpressionSphereMask") },
		{ TEXT("Comment"),                TEXT("MaterialExpressionComment") },
		{ TEXT("SceneDepth"),             TEXT("MaterialExpressionSceneDepth") },
		{ TEXT("SceneTexture"),           TEXT("MaterialExpressionSceneTexture") },
		{ TEXT("SceneColor"),             TEXT("MaterialExpressionSceneColor") },
		{ TEXT("ParticleColor"),          TEXT("MaterialExpressionParticleColor") },
		{ TEXT("Switch"),                 TEXT("MaterialExpressionStaticSwitchParameter") },
		{ TEXT("StaticSwitch"),           TEXT("MaterialExpressionStaticSwitchParameter") },
		{ TEXT("Step"),                   TEXT("MaterialExpressionStep") },
		{ TEXT("ReflectionVector"),       TEXT("MaterialExpressionReflectionVectorWS") },
		{ TEXT("ReflectionVectorWS"),     TEXT("MaterialExpressionReflectionVectorWS") },
		{ TEXT("VertexNormal"),           TEXT("MaterialExpressionVertexNormalWS") },
		{ TEXT("VertexNormalWS"),         TEXT("MaterialExpressionVertexNormalWS") },
		{ TEXT("TwoSidedSign"),           TEXT("MaterialExpressionTwoSidedSign") },
		{ TEXT("BumpOffset"),             TEXT("MaterialExpressionBumpOffset") },
		{ TEXT("ParticleSubUV"),          TEXT("MaterialExpressionParticleSubUV") },
		{ TEXT("SkyAtmosphere"),          TEXT("MaterialExpressionSkyAtmosphereLightDirection") },
		{ TEXT("FunctionCall"),              TEXT("MaterialExpressionMaterialFunctionCall") },
		{ TEXT("MaterialFunction"),          TEXT("MaterialExpressionMaterialFunctionCall") },
		{ TEXT("FunctionInput"),             TEXT("MaterialExpressionFunctionInput") },
		{ TEXT("MaterialFunctionInput"),     TEXT("MaterialExpressionFunctionInput") },
		{ TEXT("FunctionOutput"),            TEXT("MaterialExpressionFunctionOutput") },
		{ TEXT("MaterialFunctionOutput"),    TEXT("MaterialExpressionFunctionOutput") },
		{ TEXT("MakeMaterialAttributes"),    TEXT("MaterialExpressionMakeMaterialAttributes") },
		{ TEXT("BreakMaterialAttributes"),   TEXT("MaterialExpressionBreakMaterialAttributes") },
		{ TEXT("BlendMaterialAttributes"),   TEXT("MaterialExpressionBlendMaterialAttributes") },
		{ TEXT("CheapContrast"),             TEXT("MaterialExpressionCheapContrast") },
		{ TEXT("ShadingPathSwitch"),         TEXT("MaterialExpressionShadingPathSwitch") },
		{ TEXT("Transform"),                 TEXT("MaterialExpressionTransform") },
		{ TEXT("TransformPosition"),         TEXT("MaterialExpressionTransformPosition") },
		{ TEXT("DynamicParameter"),          TEXT("MaterialExpressionDynamicParameter") },
		{ TEXT("ParticleDirection"),         TEXT("MaterialExpressionParticleDirection") },
		{ TEXT("ParticleSpeed"),             TEXT("MaterialExpressionParticleSpeed") },
		{ TEXT("ParticlePositionWS"),        TEXT("MaterialExpressionParticlePositionWS") },
		{ TEXT("ParticleRadius"),            TEXT("MaterialExpressionParticleRadius") },
		{ TEXT("PerInstanceRandom"),         TEXT("MaterialExpressionPerInstanceRandom") },
		{ TEXT("PerInstanceFadeAmount"),     TEXT("MaterialExpressionPerInstanceFadeAmount") },
		{ TEXT("ObjectBounds"),              TEXT("MaterialExpressionObjectBounds") },
		{ TEXT("ActorPosition"),             TEXT("MaterialExpressionActorPositionWS") },
		{ TEXT("ActorPositionWS"),           TEXT("MaterialExpressionActorPositionWS") },
		{ TEXT("VectorNoise"),               TEXT("MaterialExpressionVectorNoise") },
		{ TEXT("DDX"),                       TEXT("MaterialExpressionDDX") },
		{ TEXT("DDY"),                       TEXT("MaterialExpressionDDY") },
		{ TEXT("Reroute"),                   TEXT("MaterialExpressionReroute") },
		{ TEXT("LandscapeLayerBlend"),       TEXT("MaterialExpressionLandscapeLayerBlend") },
		{ TEXT("LandscapeLayerCoords"),      TEXT("MaterialExpressionLandscapeLayerCoords") },
		{ TEXT("LandscapeLayerWeight"),      TEXT("MaterialExpressionLandscapeLayerWeight") },
		{ TEXT("LandscapeLayerSwitch"),      TEXT("MaterialExpressionLandscapeLayerSwitch") },
		{ TEXT("BlendAngleCorrectedNormals"), TEXT("MaterialExpressionMaterialFunctionCall") },
		{ TEXT("FlattenNormal"),              TEXT("MaterialExpressionMaterialFunctionCall") },
	};

	if (const FString* Found = Aliases.Find(NodeType))
		return *Found;

	if (NodeType.StartsWith(TEXT("MaterialExpression")))
		return NodeType;

	return TEXT("MaterialExpression") + NodeType;
}

static UClass* FindExpressionClass(const FString& NodeType)
{
	const FString ClassName = ResolveNodeAlias(NodeType);

	FString FullPath = FString::Printf(TEXT("/Script/Engine.%s"), *ClassName);
	UClass* Class = FindObject<UClass>(nullptr, *FullPath);
	if (Class && Class->IsChildOf(UMaterialExpression::StaticClass()))
		return Class;

	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->IsChildOf(UMaterialExpression::StaticClass()) && It->GetName() == ClassName)
			return *It;
	}

	return nullptr;
}

static UMaterial* LoadMaterial(const FString& MaterialPath, FString& OutError)
{
	UMaterial* Mat = LoadObject<UMaterial>(nullptr, *MaterialPath);
	if (!Mat)
	{
		FString Name = FPackageName::GetShortName(MaterialPath);
		Mat = LoadObject<UMaterial>(nullptr, *(MaterialPath + TEXT(".") + Name));
	}
	if (!Mat)
		OutError = FString::Printf(TEXT("Material not found: '%s'"), *MaterialPath);
	return Mat;
}

static FString DeriveOutputPinLabel(const FExpressionOutput& Out)
{
	if (!Out.OutputName.IsNone()) return Out.OutputName.ToString();
	if (Out.Mask == 0) return TEXT("RGB");
	const int32 ChannelCount = (int32)Out.MaskR + (int32)Out.MaskG + (int32)Out.MaskB + (int32)Out.MaskA;
	if (ChannelCount == 1)
	{
		if (Out.MaskR) return TEXT("R");
		if (Out.MaskG) return TEXT("G");
		if (Out.MaskB) return TEXT("B");
		if (Out.MaskA) return TEXT("A");
	}
	if (ChannelCount == 4) return TEXT("RGBA");
	FString Label;
	if (Out.MaskR) Label += TEXT("R");
	if (Out.MaskG) Label += TEXT("G");
	if (Out.MaskB) Label += TEXT("B");
	if (Out.MaskA) Label += TEXT("A");
	return Label.IsEmpty() ? FString(TEXT("(default)")) : Label;
}

static int32 ResolveOutputIndex(UMaterialExpression* Expr, const FString& OutputName)
{
	if (OutputName.IsEmpty()) return 0;
	if (OutputName.IsNumeric()) return FCString::Atoi(*OutputName);

	const TArray<FExpressionOutput>& Outputs = Expr->GetOutputs();
	for (int32 i = 0; i < Outputs.Num(); i++)
	{
		if (Outputs[i].OutputName.ToString().Equals(OutputName, ESearchCase::IgnoreCase))
			return i;
	}
	for (int32 i = 0; i < Outputs.Num(); i++)
	{
		if (DeriveOutputPinLabel(Outputs[i]).Equals(OutputName, ESearchCase::IgnoreCase))
			return i;
	}
	return 0;
}

static FExpressionInput* FindInputOnExpression(UMaterialExpression* Expr, const FString& InputName)
{
	if (UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expr))
	{
		for (FCustomInput& Entry : Custom->Inputs)
		{
			if (Entry.InputName.ToString().Equals(InputName, ESearchCase::IgnoreCase))
				return &Entry.Input;
		}
	}

	for (TFieldIterator<FStructProperty> It(Expr->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		if (!It->Struct) continue;
		const FString StructName = It->Struct->GetName();
		if (!StructName.Contains(TEXT("ExpressionInput")) && !StructName.Contains(TEXT("MaterialInput"))) continue;
		if (It->GetName().Equals(InputName, ESearchCase::IgnoreCase))
			return It->ContainerPtrToValuePtr<FExpressionInput>(Expr);
	}
	return nullptr;
}

static EMaterialProperty StringToMaterialProperty(const FString& Name)
{
	if (Name.Equals(TEXT("BaseColor"), ESearchCase::IgnoreCase))              return MP_BaseColor;
	if (Name.Equals(TEXT("Metallic"), ESearchCase::IgnoreCase))               return MP_Metallic;
	if (Name.Equals(TEXT("Specular"), ESearchCase::IgnoreCase))               return MP_Specular;
	if (Name.Equals(TEXT("Roughness"), ESearchCase::IgnoreCase))              return MP_Roughness;
	if (Name.Equals(TEXT("Anisotropy"), ESearchCase::IgnoreCase))             return MP_Anisotropy;
	if (Name.Equals(TEXT("EmissiveColor"), ESearchCase::IgnoreCase) ||
		Name.Equals(TEXT("Emissive"), ESearchCase::IgnoreCase))               return MP_EmissiveColor;
	if (Name.Equals(TEXT("Opacity"), ESearchCase::IgnoreCase))                return MP_Opacity;
	if (Name.Equals(TEXT("OpacityMask"), ESearchCase::IgnoreCase))            return MP_OpacityMask;
	if (Name.Equals(TEXT("Normal"), ESearchCase::IgnoreCase))                 return MP_Normal;
	if (Name.Equals(TEXT("Tangent"), ESearchCase::IgnoreCase))                return MP_Tangent;
	if (Name.Equals(TEXT("WorldPositionOffset"), ESearchCase::IgnoreCase))    return MP_WorldPositionOffset;
	if (Name.Equals(TEXT("SubsurfaceColor"), ESearchCase::IgnoreCase) ||
		Name.Equals(TEXT("Subsurface"), ESearchCase::IgnoreCase))             return MP_SubsurfaceColor;
	if (Name.Equals(TEXT("AmbientOcclusion"), ESearchCase::IgnoreCase) ||
		Name.Equals(TEXT("AO"), ESearchCase::IgnoreCase))                     return MP_AmbientOcclusion;
	if (Name.Equals(TEXT("PixelDepthOffset"), ESearchCase::IgnoreCase))       return MP_PixelDepthOffset;
	if (Name.Equals(TEXT("Refraction"), ESearchCase::IgnoreCase))             return MP_Refraction;
	return MP_MAX;
}

struct FMaterialGraphTarget
{
	UObject* Owner      = nullptr;
	bool     bIsFunction = false;

	bool Load(const FString& Path, FString& OutError)
	{
		UObject* Asset = LoadObject<UObject>(nullptr, *Path);
		if (!Asset)
		{
			const FString N = FPackageName::GetShortName(Path);
			Asset = LoadObject<UObject>(nullptr, *(Path + TEXT(".") + N));
		}
		if (!Asset) { OutError = FString::Printf(TEXT("Asset not found: '%s'"), *Path); return false; }
		if (Cast<UMaterial>(Asset))         { Owner = Asset; bIsFunction = false; return true; }
		if (Cast<UMaterialFunction>(Asset)) { Owner = Asset; bIsFunction = true;  return true; }
		if (Cast<UMaterialInstance>(Asset))
		{
			OutError = FString::Printf(TEXT("'%s' is a Material Instance, not a Material. Material instances inherit their graph from the parent material — use get_material_instance_parameters to inspect override values, or get_material_nodes / validate_material on the parent material path."), *Path);
			return false;
		}
		OutError = FString::Printf(TEXT("Not a Material or MaterialFunction: '%s' (got %s)"), *Path, *Asset->GetClass()->GetName());
		return false;
	}

	UMaterial*         AsMaterial() const { return bIsFunction ? nullptr : Cast<UMaterial>(Owner); }
	UMaterialFunction* AsFunction() const { return bIsFunction ? Cast<UMaterialFunction>(Owner) : nullptr; }
	bool IsFunction() const { return bIsFunction; }

	/** Editor-only data of a UMaterial; null for functions or when the data was stripped. */
	UMaterialEditorOnlyData* EditorData() const
	{
		UMaterial* M = AsMaterial();
		return M ? M->GetEditorOnlyData() : nullptr;
	}

	int32 Num() const
	{
		if (const UMaterialEditorOnlyData* ED = EditorData()) return ED->ExpressionCollection.Expressions.Num();
		if (UMaterialFunction* F = AsFunction()) return F->GetExpressionCollection().Expressions.Num();
		return 0;
	}

	UMaterialExpression* Get(int32 i) const
	{
		if (i < 0) return nullptr;
		if (const UMaterialEditorOnlyData* ED = EditorData())
		{
			const TArray<TObjectPtr<UMaterialExpression>>& Exprs = ED->ExpressionCollection.Expressions;
			return Exprs.IsValidIndex(i) ? Exprs[i].Get() : nullptr;
		}
		if (UMaterialFunction* F = AsFunction())
		{
			const TArray<TObjectPtr<UMaterialExpression>>& Exprs = F->GetExpressionCollection().Expressions;
			return Exprs.IsValidIndex(i) ? Exprs[i].Get() : nullptr;
		}
		return nullptr;
	}

	bool IsValidIndex(int32 i) const { return i >= 0 && i < Num(); }

	int32 IndexOf(const UMaterialExpression* Expr) const
	{
		if (!Expr) return INDEX_NONE;
		for (int32 i = 0; i < Num(); i++)
			if (Get(i) == Expr) return i;
		return INDEX_NONE;
	}

	void AddExpression(UMaterialExpression* Expr) const
	{
		if (UMaterialEditorOnlyData* ED = EditorData()) ED->ExpressionCollection.AddExpression(Expr);
		else if (UMaterialFunction* F = AsFunction()) F->GetExpressionCollection().AddExpression(Expr);
	}

	void RemoveAt(int32 i) const
	{
		if (!IsValidIndex(i)) return;
		if (UMaterialEditorOnlyData* ED = EditorData()) ED->ExpressionCollection.Expressions.RemoveAt(i);
		else if (UMaterialFunction* F = AsFunction()) F->GetExpressionCollection().Expressions.RemoveAt(i);
	}

	FExpressionInput* GetMaterialOutputSlot(const FString& SlotName) const
	{
		UMaterial* M = AsMaterial();
		if (!M) return nullptr;
		const EMaterialProperty Prop = StringToMaterialProperty(SlotName);
		return (Prop != MP_MAX) ? M->GetExpressionInputForProperty(Prop) : nullptr;
	}

	// ---- stable identity -----------------------------------------------------------------
	//
	// Array indices shift whenever a node is deleted, so every tool accepts and echoes the
	// expression's MaterialExpressionGuid ("node_id", 32 hex digits). Integer indices (and the
	// "n3" / "node[3]" spellings) stay accepted for backwards compatibility.

	static FString GuidOf(const UMaterialExpression* E)
	{
		return E ? E->MaterialExpressionGuid.ToString(EGuidFormats::Digits) : FString();
	}

	static void EnsureGuid(UMaterialExpression* E)
	{
		if (E && !E->MaterialExpressionGuid.IsValid())
			E->UpdateMaterialExpressionGuid(/*bForceGeneration*/ true, /*bAllowMarkingPackageDirty*/ false);
	}

	void EnsureAllGuids() const
	{
		for (int32 i = 0; i < Num(); i++) EnsureGuid(Get(i));
	}

	UMaterialExpression* FindByGuid(const FGuid& Guid) const
	{
		if (!Guid.IsValid()) return nullptr;
		for (int32 i = 0; i < Num(); i++)
		{
			UMaterialExpression* E = Get(i);
			if (E && E->MaterialExpressionGuid == Guid) return E;
		}
		return nullptr;
	}

	/** Empty / "-1" / "material" / "output" address the material's own output slots. */
	static bool IsMaterialOutputHandle(const FString& Handle)
	{
		const FString T = Handle.TrimStartAndEnd();
		return T.IsEmpty()
			|| T == TEXT("-1")
			|| T.Equals(TEXT("material"), ESearchCase::IgnoreCase)
			|| T.Equals(TEXT("output"), ESearchCase::IgnoreCase)
			|| T.Equals(TEXT("material_output"), ESearchCase::IgnoreCase)
			|| T.Equals(TEXT("material_outputs"), ESearchCase::IgnoreCase);
	}

	static bool ParseIndexHandle(const FString& Handle, int32& OutIndex)
	{
		FString T = Handle.TrimStartAndEnd();
		if (T.StartsWith(TEXT("node["), ESearchCase::IgnoreCase) && T.EndsWith(TEXT("]")))
			T = T.Mid(5, T.Len() - 6);
		else if (T.Len() > 1 && (T[0] == TEXT('n') || T[0] == TEXT('N') || T[0] == TEXT('#')) && T.Mid(1).IsNumeric())
			T = T.Mid(1);
		if (T.IsEmpty() || !T.IsNumeric()) return false;
		OutIndex = FCString::Atoi(*T);
		return true;
	}

	/** Resolve a node handle (guid string or index) to an expression; fills OutError on failure. */
	UMaterialExpression* Resolve(const FString& Handle, FString& OutError) const
	{
		FGuid Guid;
		if (FGuid::Parse(Handle.TrimStartAndEnd(), Guid) && Guid.IsValid())
		{
			if (UMaterialExpression* E = FindByGuid(Guid)) return E;
			OutError = FString::Printf(TEXT("No expression with node_id '%s' in '%s' (%d nodes). Call get_material_nodes to list the current node ids."),
				*Handle, Owner ? *Owner->GetName() : TEXT("?"), Num());
			return nullptr;
		}
		int32 Index = INDEX_NONE;
		if (ParseIndexHandle(Handle, Index))
		{
			if (UMaterialExpression* E = Get(Index)) return E;
			OutError = FString::Printf(TEXT("Node index %d out of range (%d nodes). Prefer the stable node_id from get_material_nodes — indices shift whenever a node is deleted."),
				Index, Num());
			return nullptr;
		}
		OutError = FString::Printf(TEXT("Unrecognised node handle '%s'. Pass the node_id (32-hex guid) returned by get_material_nodes / add_material_node, or an integer index."), *Handle);
		return nullptr;
	}

	/** Adds "<prefix>node_index", "<prefix>node_id" and "<prefix>node_class" to a result object. */
	void AddNodeRef(const TSharedPtr<FJsonObject>& Obj, UMaterialExpression* E, const TCHAR* Prefix = TEXT("")) const
	{
		if (!Obj.IsValid() || !E) return;
		EnsureGuid(E);
		Obj->SetNumberField(FString(Prefix) + TEXT("node_index"), IndexOf(E));
		Obj->SetStringField(FString(Prefix) + TEXT("node_id"), GuidOf(E));
		Obj->SetStringField(FString(Prefix) + TEXT("node_class"), E->GetClass()->GetName());
	}

	/** "node[3] (Multiply, id=...)" for messages. */
	FString Describe(const UMaterialExpression* E) const
	{
		if (!E) return TEXT("node[?]");
		return FString::Printf(TEXT("node[%d] (%s, node_id=%s)"), IndexOf(E), *E->GetClass()->GetName(), *GuidOf(E));
	}

	void Modify() const { if (Owner) Owner->Modify(); }
	void PreEditChange()  const { if (Owner) Owner->PreEditChange(nullptr); }
	void PostEditChange() const { if (Owner) { Owner->PostEditChange(); Owner->MarkPackageDirty(); } }
};

void HandleAddMaterialNode(
	const FString& MaterialPath,
	const FString& NodeType,
	int32 PosX, int32 PosY,
	const FString& Desc,
	FString& OutJsonString, FString& OutError)
{
	FMaterialGraphTarget Target;
	if (!Target.Load(MaterialPath, OutError)) return;

	UClass* ExprClass = FindExpressionClass(NodeType);
	if (!ExprClass)
	{
		OutError = FString::Printf(
			TEXT("Unknown material expression type: '%s'. Use aliases like Add, Multiply, Lerp, "
				 "TextureSample, ScalarParameter, VectorParameter, Constant, Constant3, Fresnel, "
				 "TexCoord, Time, Sine, Panner, Custom, If, Power, Clamp, Append, ComponentMask, "
				 "FunctionInput, FunctionOutput, MakeMaterialAttributes, BreakMaterialAttributes, etc."),
			*NodeType);
		return;
	}

	if (PosX != INT32_MIN && PosX > -100) { PosX = -600; }

	if (PosX == INT32_MIN)
	{
		static const TMap<FString, int32> ColumnX =
		{
			{ TEXT("Constant"),               -1200 }, { TEXT("Constant2"),              -1200 },
			{ TEXT("Constant3"),              -1200 }, { TEXT("Constant4"),              -1200 },
			{ TEXT("ScalarParameter"),        -1200 }, { TEXT("VectorParameter"),        -1200 },
			{ TEXT("StaticBoolParameter"),    -1200 }, { TEXT("StaticSwitchParameter"),  -1200 },
			{ TEXT("Switch"),                 -1200 }, { TEXT("StaticSwitch"),           -1200 },
			{ TEXT("Time"),                   -1200 }, { TEXT("TwoSidedSign"),           -1200 },
			{ TEXT("ObjectRadius"),           -1200 }, { TEXT("ObjectBounds"),           -1200 },
			{ TEXT("PerInstanceRandom"),      -1200 }, { TEXT("PerInstanceFadeAmount"),  -1200 },
			{ TEXT("DynamicParameter"),       -1200 },
			{ TEXT("TextureSample"),          -850 }, { TEXT("TextureParameter"),        -850 },
			{ TEXT("TextureObject"),          -850 }, { TEXT("TextureObjectParameter"),  -850 },
			{ TEXT("TexCoord"),               -850 }, { TEXT("TextureCoordinate"),       -850 },
			{ TEXT("VertexColor"),            -850 }, { TEXT("VertexNormal"),            -850 },
			{ TEXT("VertexNormalWS"),         -850 }, { TEXT("CameraVector"),            -850 },
			{ TEXT("CameraVectorWS"),         -850 }, { TEXT("PixelNormal"),             -850 },
			{ TEXT("PixelNormalWS"),          -850 }, { TEXT("ReflectionVector"),        -850 },
			{ TEXT("ReflectionVectorWS"),     -850 }, { TEXT("WorldPosition"),           -850 },
			{ TEXT("ObjectPositionWS"),       -850 }, { TEXT("ObjectPosition"),          -850 },
			{ TEXT("ActorPositionWS"),        -850 }, { TEXT("ActorPosition"),           -850 },
			{ TEXT("ScreenPosition"),         -850 }, { TEXT("ParticleColor"),           -850 },
			{ TEXT("ParticleDirection"),      -850 }, { TEXT("ParticleSpeed"),           -850 },
			{ TEXT("ParticlePositionWS"),     -850 }, { TEXT("SceneDepth"),              -850 },
			{ TEXT("SceneTexture"),           -850 }, { TEXT("SceneColor"),              -850 },
			{ TEXT("SkyAtmosphere"),          -850 }, { TEXT("LandscapeLayerCoords"),    -850 },
			{ TEXT("MakeMaterialAttributes"),   -300 }, { TEXT("BreakMaterialAttributes"),  -300 },
			{ TEXT("BlendMaterialAttributes"),  -300 }, { TEXT("FunctionOutput"),           -300 },
			{ TEXT("MaterialFunctionOutput"),   -300 }, { TEXT("FunctionInput"),            -300 },
			{ TEXT("MaterialFunctionInput"),    -300 },
		};

		if (const int32* Col = ColumnX.Find(NodeType))
			PosX = *Col;
		else
			PosX = -600;

		int32 ColMaxY = INT32_MIN;
		for (int32 i = 0; i < Target.Num(); i++)
		{
			UMaterialExpression* E = Target.Get(i);
			if (E && FMath::Abs(E->MaterialExpressionEditorX - PosX) <= 120)
			{
				if (E->MaterialExpressionEditorY > ColMaxY) ColMaxY = E->MaterialExpressionEditorY;
			}
		}

		const bool bIsTexture = NodeType.Contains(TEXT("Texture")) ||
			NodeType.Equals(TEXT("TexCoord"), ESearchCase::IgnoreCase) ||
			NodeType.Equals(TEXT("TextureCoordinate"), ESearchCase::IgnoreCase);
		const bool bIsCustom = NodeType.Equals(TEXT("Custom"), ESearchCase::IgnoreCase);
		const int32 NodeHeight = bIsTexture ? 300 : (bIsCustom ? 280 : 220);
		PosY = (ColMaxY == INT32_MIN) ? 0 : ColMaxY + NodeHeight;
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("Axivor: Add Material Node")));
	Target.Modify();
	Target.PreEditChange();

	UMaterialExpression* Expr = NewObject<UMaterialExpression>(Target.Owner, ExprClass, NAME_None, RF_Transactional);
	FMaterialGraphTarget::EnsureGuid(Expr);
	Expr->MaterialExpressionEditorX = PosX;
	Expr->MaterialExpressionEditorY = PosY;
	if (!Desc.IsEmpty()) Expr->Desc = Desc;

	if (UMaterialExpressionMaterialFunctionCall* FuncCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expr))
	{
		static const TMap<FString, FString> EngineFunctionPaths =
		{
			{ TEXT("BlendAngleCorrectedNormals"), TEXT("/Engine/Functions/Engine_MaterialFunctions02/Utility/BlendAngleCorrectedNormals") },
			{ TEXT("FlattenNormal"),              TEXT("/Engine/Functions/Engine_MaterialFunctions01/Shading/FlattenNormal") },
		};
		if (const FString* FuncPath = EngineFunctionPaths.Find(NodeType))
		{
			UMaterialFunctionInterface* Func = LoadObject<UMaterialFunctionInterface>(nullptr, **FuncPath);
			if (!Func)
				Func = LoadObject<UMaterialFunctionInterface>(nullptr, *(*FuncPath + TEXT(".") + FPackageName::GetShortName(*FuncPath)));
			if (Func)
				FuncCall->SetMaterialFunction(Func);
		}
	}

	Target.AddExpression(Expr);
	const int32 NodeIndex = Target.IndexOf(Expr);

	Target.PostEditChange();

	TArray<FString> Captions;
	Expr->GetCaption(Captions);
	const FString Caption = Captions.Num() > 0 ? Captions[0] : ExprClass->GetName();

	TArray<FString> InputNames;
	for (TFieldIterator<FStructProperty> It(Expr->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		if (It->Struct && (It->Struct->GetName().Contains(TEXT("ExpressionInput")) || It->Struct->GetName().Contains(TEXT("MaterialInput"))))
			InputNames.Add(It->GetName());
	}

	const TArray<FExpressionOutput>& Outputs = Expr->GetOutputs();
	TArray<FString> OutputNames;
	for (const FExpressionOutput& Out : Outputs)
		OutputNames.Add(DeriveOutputPinLabel(Out));

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("node_index"), NodeIndex);
	Result->SetStringField(TEXT("node_id"), FMaterialGraphTarget::GuidOf(Expr));
	Result->SetStringField(TEXT("node_class"), ExprClass->GetName());
	Result->SetStringField(TEXT("caption"), Caption);
	Result->SetStringField(TEXT("note"), TEXT("Address this node by node_id in later calls — node_index shifts when nodes are deleted."));

	TArray<TSharedPtr<FJsonValue>> InArr, OutArr;
	for (const FString& S : InputNames)  InArr.Add(MakeShareable(new FJsonValueString(S)));
	for (const FString& S : OutputNames) OutArr.Add(MakeShareable(new FJsonValueString(S)));
	Result->SetArrayField(TEXT("input_pins"),  InArr);
	Result->SetArrayField(TEXT("output_pins"), OutArr);

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleConnectMaterialNodes(
	const FString& MaterialPath,
	const FString& FromNode,
	const FString& FromOutput,
	const FString& ToNode,
	const FString& ToInput,
	FString& OutJsonString, FString& OutError)
{
	FMaterialGraphTarget Target;
	if (!Target.Load(MaterialPath, OutError)) return;

	FString ResolveError;
	UMaterialExpression* FromExpr = Target.Resolve(FromNode, ResolveError);
	if (!FromExpr) { OutError = TEXT("from_node: ") + ResolveError; return; }
	const int32 FromNodeIndex = Target.IndexOf(FromExpr);

	const bool bToMaterialOutput = FMaterialGraphTarget::IsMaterialOutputHandle(ToNode);
	UMaterialExpression* ToExpr = nullptr;
	if (!bToMaterialOutput)
	{
		ToExpr = Target.Resolve(ToNode, ResolveError);
		if (!ToExpr) { OutError = TEXT("to_node: ") + ResolveError; return; }
	}

	if (!FromOutput.IsEmpty() && !FromOutput.IsNumeric())
	{
		const TArray<FExpressionOutput>& FromOutputs = FromExpr->GetOutputs();
		bool bMatched = false;
		TArray<FString> AvailableOutputs;
		for (const FExpressionOutput& Out : FromOutputs)
		{
			const FString Label = DeriveOutputPinLabel(Out);
			AvailableOutputs.Add(Label);
			if (Label.Equals(FromOutput, ESearchCase::IgnoreCase) ||
				(!Out.OutputName.IsNone() && Out.OutputName.ToString().Equals(FromOutput, ESearchCase::IgnoreCase)))
				bMatched = true;
		}
		if (!bMatched)
		{
			OutError = FString::Printf(TEXT("Output pin '%s' not found on %s (node[%d]). Available: [%s]"),
				*FromOutput, *FromExpr->GetClass()->GetName(), FromNodeIndex, *FString::Join(AvailableOutputs, TEXT(", ")));
			return;
		}
	}

	const int32 OutputIndex = ResolveOutputIndex(FromExpr, FromOutput);

	const FScopedTransaction Transaction(FText::FromString(TEXT("Axivor: Connect Material Nodes")));
	Target.Modify();
	if (ToExpr) ToExpr->Modify();
	Target.PreEditChange();

	if (bToMaterialOutput)
	{
		if (Target.IsFunction())
		{
			OutError = TEXT("Cannot connect to a material output slot on a MaterialFunction. "
				"Add a FunctionOutput node (node_type='FunctionOutput') and connect to it by passing its node_id as to_node.");
			Target.PostEditChange();
			return;
		}

		FExpressionInput* Input = Target.GetMaterialOutputSlot(ToInput);
		if (!Input)
		{
			OutError = FString::Printf(
				TEXT("Unknown material output pin: '%s'. Valid: BaseColor, Metallic, Roughness, "
					 "Specular, EmissiveColor, Opacity, OpacityMask, Normal, Tangent, "
					 "WorldPositionOffset, AmbientOcclusion, PixelDepthOffset, Refraction"),
				*ToInput);
			Target.PostEditChange();
			return;
		}

		Input->Expression  = FromExpr;
		Input->OutputIndex = OutputIndex;
	}
	else
	{
		FExpressionInput* Input = FindInputOnExpression(ToExpr, ToInput);
		if (!Input)
		{
			TArray<FString> Available;
			if (UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(ToExpr))
			{
				for (const FCustomInput& Entry : Custom->Inputs)
					Available.Add(Entry.InputName.ToString());
			}
			for (TFieldIterator<FStructProperty> It(ToExpr->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
			{
				if (It->Struct && (It->Struct->GetName().Contains(TEXT("ExpressionInput")) || It->Struct->GetName().Contains(TEXT("MaterialInput"))))
					Available.Add(It->GetName());
			}
			OutError = FString::Printf(TEXT("Input pin '%s' not found on %s. Available: [%s]"),
				*ToInput, *ToExpr->GetClass()->GetName(), *FString::Join(Available, TEXT(", ")));
			Target.PostEditChange();
			return;
		}

		Input->Expression  = FromExpr;
		Input->OutputIndex = OutputIndex;
	}

	Target.PostEditChange();

	const FString ToDesc = bToMaterialOutput
		? FString::Printf(TEXT("material.%s"), *ToInput)
		: FString::Printf(TEXT("%s.%s"), *Target.Describe(ToExpr), *ToInput);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"),
		FString::Printf(TEXT("Connected %s output '%s' (index %d) -> %s"),
			*Target.Describe(FromExpr), *FromOutput, OutputIndex, *ToDesc));
	Target.AddNodeRef(Result, FromExpr, TEXT("from_"));
	Result->SetStringField(TEXT("from_output"), FromOutput);
	Result->SetNumberField(TEXT("from_output_index"), OutputIndex);
	if (bToMaterialOutput)
	{
		Result->SetStringField(TEXT("to_node_id"), TEXT("material"));
		Result->SetNumberField(TEXT("to_node_index"), -1);
	}
	else
	{
		Target.AddNodeRef(Result, ToExpr, TEXT("to_"));
	}
	Result->SetStringField(TEXT("to_input"), ToInput);

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleConnectMaterialNodes(
	const FString& MaterialPath,
	int32 FromNodeIndex,
	const FString& FromOutput,
	int32 ToNodeIndex,
	const FString& ToInput,
	FString& OutJsonString, FString& OutError)
{
	HandleConnectMaterialNodes(MaterialPath, FString::FromInt(FromNodeIndex), FromOutput,
		ToNodeIndex < 0 ? FString() : FString::FromInt(ToNodeIndex), ToInput, OutJsonString, OutError);
}

void HandleSetMaterialNodeValue(
	const FString& MaterialPath,
	const FString& NodeHandle,
	const FString& PropertyName,
	const TSharedPtr<FJsonObject>& ValueJson,
	FString& OutJsonString, FString& OutError)
{
	FMaterialGraphTarget Target;
	if (!Target.Load(MaterialPath, OutError)) return;
	if (!ValueJson.IsValid()) { OutError = TEXT("Missing value payload."); return; }

	UMaterialExpression* Expr = Target.Resolve(NodeHandle, OutError);
	if (!Expr) return;
	const int32 NodeIndex = Target.IndexOf(Expr);

	{
		static const TCHAR* TypedKeys[] = {
			TEXT("float_value"), TEXT("vector_value"), TEXT("string_value"),
			TEXT("bool_value"), TEXT("texture_path") };
		bool bHasTyped = false;
		for (const TCHAR* K : TypedKeys) { if (ValueJson->HasField(K)) { bHasTyped = true; break; } }
		if (!bHasTyped)
		{
			const TSharedPtr<FJsonValue> Generic = ValueJson->TryGetField(TEXT("value"));
			if (Generic.IsValid())
			{
				switch (Generic->Type)
				{
				case EJson::Number:  ValueJson->SetNumberField(TEXT("float_value"),  Generic->AsNumber()); break;
				case EJson::Boolean: ValueJson->SetBoolField  (TEXT("bool_value"),   Generic->AsBool());   break;
				case EJson::Array:   ValueJson->SetArrayField (TEXT("vector_value"), Generic->AsArray());  break;
				case EJson::String:  ValueJson->SetStringField(TEXT("string_value"), Generic->AsString()); break;
				default: break;
				}
			}
		}
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("Axivor: Set Material Node Value")));
	Target.Modify();
	Expr->Modify();
	Target.PreEditChange();

	if (UMaterialExpressionCustom* CustomExpr = Cast<UMaterialExpressionCustom>(Expr))
	{
		if (PropertyName.Equals(TEXT("AddInput"), ESearchCase::IgnoreCase) ||
		    PropertyName.Equals(TEXT("Inputs"), ESearchCase::IgnoreCase))
		{
			FString InputName;
			if (ValueJson->TryGetStringField(TEXT("string_value"), InputName) && !InputName.IsEmpty())
			{
				FCustomInput NewInput;
				NewInput.InputName = FName(*InputName);
				CustomExpr->Inputs.Add(NewInput);

				Target.PostEditChange();

				TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
				Result->SetBoolField(TEXT("success"), true);
				Result->SetStringField(TEXT("message"),
					FString::Printf(TEXT("Added input '%s' to Custom node[%d]"), *InputName, NodeIndex));
				Target.AddNodeRef(Result, Expr);
				TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
				FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
			}
			else
			{
				OutError = TEXT("AddInput requires string_value with the input pin name.");
			}
			return;
		}
	}

	if (UMaterialExpressionMaterialFunctionCall* FnCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expr))
	{
		if (PropertyName.Equals(TEXT("MaterialFunction"), ESearchCase::IgnoreCase) ||
			PropertyName.Equals(TEXT("Function"), ESearchCase::IgnoreCase))
		{
			FString FnPath;
			if (!ValueJson->TryGetStringField(TEXT("string_value"), FnPath) || FnPath.IsEmpty())
			{
				ValueJson->TryGetStringField(TEXT("object_path"), FnPath);
				if (FnPath.IsEmpty()) ValueJson->TryGetStringField(TEXT("texture_path"), FnPath);
			}
			if (FnPath.IsEmpty())
			{
				OutError = TEXT("MaterialFunction requires string_value with the function asset path (e.g. '/Game/Materials/XRay/MF_Foo.MF_Foo').");
				return;
			}
			UMaterialFunctionInterface* Fn = LoadObject<UMaterialFunctionInterface>(nullptr, *FnPath);
			if (!Fn)
				Fn = LoadObject<UMaterialFunctionInterface>(nullptr, *(FnPath + TEXT(".") + FPackageName::GetShortName(FnPath)));
			if (!Fn)
			{
				OutError = FString::Printf(TEXT("MaterialFunction not found: '%s'. Pass a valid material-function asset path."), *FnPath);
				return;
			}
			FnCall->SetMaterialFunction(Fn);
			Target.PostEditChange();

			TArray<FString> Captions; FnCall->GetCaption(Captions);
			TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
			Result->SetBoolField(TEXT("success"), true);
			Result->SetStringField(TEXT("message"),
				FString::Printf(TEXT("Assigned MaterialFunction '%s' to node[%d] (%s)"),
					*Fn->GetName(), NodeIndex, Captions.Num() > 0 ? *Captions[0] : TEXT("MaterialFunctionCall")));
			Target.AddNodeRef(Result, Expr);
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
			FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
			return;
		}
	}

	bool bSet = false;
	for (TFieldIterator<FProperty> It(Expr->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop->GetName().Equals(PropertyName, ESearchCase::IgnoreCase))
			continue;

		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Expr);

		if (FFloatProperty* FP = CastField<FFloatProperty>(Prop))
		{
			double Val = 0;
			if (ValueJson->TryGetNumberField(TEXT("float_value"), Val))
			{
				FP->SetPropertyValue(ValuePtr, (float)Val);
				bSet = true;
			}
		}
		else if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop))
		{
			double Val = 0;
			if (ValueJson->TryGetNumberField(TEXT("float_value"), Val))
			{
				DP->SetPropertyValue(ValuePtr, Val);
				bSet = true;
			}
		}
		else if (FIntProperty* IP = CastField<FIntProperty>(Prop))
		{
			double Val = 0;
			if (ValueJson->TryGetNumberField(TEXT("float_value"), Val))
			{
				IP->SetPropertyValue(ValuePtr, (int32)Val);
				bSet = true;
			}
		}
		else if (FBoolProperty* BP = CastField<FBoolProperty>(Prop))
		{
			bool Val = false;
			if (ValueJson->TryGetBoolField(TEXT("bool_value"), Val))
			{
				BP->SetPropertyValue(ValuePtr, Val);
				bSet = true;
			}
		}
		else if (FStrProperty* SP = CastField<FStrProperty>(Prop))
		{
			FString Val;
			if (ValueJson->TryGetStringField(TEXT("string_value"), Val))
			{
				SP->SetPropertyValue(ValuePtr, Val);
				bSet = true;
			}
		}
		else if (FNameProperty* NP = CastField<FNameProperty>(Prop))
		{
			FString Val;
			if (ValueJson->TryGetStringField(TEXT("string_value"), Val))
			{
				NP->SetPropertyValue(ValuePtr, FName(*Val));
				bSet = true;
			}
		}
		else if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
		{
			if (ByteProp->Enum)
			{
				FString StrVal;
				if (ValueJson->TryGetStringField(TEXT("string_value"), StrVal))
				{
					int64 EnumVal = ByteProp->Enum->GetValueByNameString(StrVal);
					if (EnumVal == INDEX_NONE)
					{
						for (int32 i = 0; i < ByteProp->Enum->NumEnums(); i++)
						{
							FString EnumFullName = ByteProp->Enum->GetNameStringByIndex(i);
							FString ShortName = EnumFullName;
							int32 LastUnderscore;
							if (EnumFullName.FindLastChar(':', LastUnderscore))
								ShortName = EnumFullName.RightChop(LastUnderscore + 1);
							if (ShortName.Equals(StrVal, ESearchCase::IgnoreCase) ||
								EnumFullName.Equals(StrVal, ESearchCase::IgnoreCase))
							{
								EnumVal = ByteProp->Enum->GetValueByIndex(i);
								break;
							}
						}
					}
					if (EnumVal != INDEX_NONE)
					{
						ByteProp->SetPropertyValue(ValuePtr, (uint8)EnumVal);
						bSet = true;
					}
					else
					{
						TArray<FString> ValidNames;
						for (int32 i = 0; i < ByteProp->Enum->NumEnums() - 1; i++)
							ValidNames.Add(ByteProp->Enum->GetNameStringByIndex(i));
						OutError = FString::Printf(
							TEXT("Enum value '%s' not found in %s. Valid values: %s"),
							*StrVal, *ByteProp->Enum->GetName(), *FString::Join(ValidNames, TEXT(", ")));
						return;
					}
				}
			}
			else
			{
				double Val = 0;
				if (ValueJson->TryGetNumberField(TEXT("float_value"), Val))
				{
					ByteProp->SetPropertyValue(ValuePtr, (uint8)Val);
					bSet = true;
				}
			}
		}
		else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			FString StrVal;
			if (ValueJson->TryGetStringField(TEXT("string_value"), StrVal))
			{
				int64 EnumVal = EnumProp->GetEnum()->GetValueByNameString(StrVal);
				if (EnumVal == INDEX_NONE)
				{
					for (int32 i = 0; i < EnumProp->GetEnum()->NumEnums(); i++)
					{
						FString EnumFullName = EnumProp->GetEnum()->GetNameStringByIndex(i);
						FString ShortName = EnumFullName;
						int32 LastUnderscore;
						if (EnumFullName.FindLastChar(':', LastUnderscore))
							ShortName = EnumFullName.RightChop(LastUnderscore + 1);
						if (ShortName.Equals(StrVal, ESearchCase::IgnoreCase) ||
							EnumFullName.Equals(StrVal, ESearchCase::IgnoreCase))
						{
							EnumVal = EnumProp->GetEnum()->GetValueByIndex(i);
							break;
						}
					}
				}
				if (EnumVal != INDEX_NONE)
				{
					EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, EnumVal);
					bSet = true;
				}
				else
				{
					TArray<FString> ValidNames;
					for (int32 i = 0; i < EnumProp->GetEnum()->NumEnums() - 1; i++)
						ValidNames.Add(EnumProp->GetEnum()->GetNameStringByIndex(i));
					OutError = FString::Printf(
						TEXT("Enum value '%s' not found in %s. Valid values: %s"),
						*StrVal, *EnumProp->GetEnum()->GetName(), *FString::Join(ValidNames, TEXT(", ")));
					return;
				}
			}
		}
		else if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			if (StructProp->Struct == TBaseStructure<FLinearColor>::Get())
			{
				FLinearColor* ColorPtr = reinterpret_cast<FLinearColor*>(ValuePtr);
				const TArray<TSharedPtr<FJsonValue>>* VecArray = nullptr;
				if (ValueJson->TryGetArrayField(TEXT("vector_value"), VecArray) && VecArray && VecArray->Num() >= 3)
				{
					const float R = (float)(*VecArray)[0]->AsNumber();
					const float G = (float)(*VecArray)[1]->AsNumber();
					const float B = (float)(*VecArray)[2]->AsNumber();
					const float A = VecArray->Num() >= 4 ? (float)(*VecArray)[3]->AsNumber() : 1.f;
					*ColorPtr = FLinearColor(R, G, B, A);
					bSet = true;
				}
			}
			else if (StructProp->Struct == TBaseStructure<FVector>::Get())
			{
				FVector* VecPtr = reinterpret_cast<FVector*>(ValuePtr);
				const TArray<TSharedPtr<FJsonValue>>* VecArray = nullptr;
				if (ValueJson->TryGetArrayField(TEXT("vector_value"), VecArray) && VecArray && VecArray->Num() >= 3)
				{
					VecPtr->X = (*VecArray)[0]->AsNumber();
					VecPtr->Y = (*VecArray)[1]->AsNumber();
					VecPtr->Z = (*VecArray)[2]->AsNumber();
					bSet = true;
				}
			}
			else if (StructProp->Struct == TBaseStructure<FVector2D>::Get())
			{
				FVector2D* Vec2Ptr = reinterpret_cast<FVector2D*>(ValuePtr);
				const TArray<TSharedPtr<FJsonValue>>* VecArray = nullptr;
				if (ValueJson->TryGetArrayField(TEXT("vector_value"), VecArray) && VecArray && VecArray->Num() >= 2)
				{
					Vec2Ptr->X = (*VecArray)[0]->AsNumber();
					Vec2Ptr->Y = (*VecArray)[1]->AsNumber();
					bSet = true;
				}
			}
		}
		else if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
		{
			if (ObjProp->PropertyClass && ObjProp->PropertyClass->IsChildOf(UTexture::StaticClass()))
			{
				FString TexPath;
				if (ValueJson->TryGetStringField(TEXT("texture_path"), TexPath))
				{
					UTexture* Tex = LoadObject<UTexture>(nullptr, *TexPath);
					if (!Tex)
					{
						FString TexName = FPackageName::GetShortName(TexPath);
						Tex = LoadObject<UTexture>(nullptr, *(TexPath + TEXT(".") + TexName));
					}
					if (Tex)
					{
						ObjProp->SetObjectPropertyValue(ValuePtr, Tex);
						bSet = true;

						if (UMaterialExpressionTextureBase* TexBase = Cast<UMaterialExpressionTextureBase>(Expr))
						{
							if (PropertyName.Equals(TEXT("Texture"), ESearchCase::IgnoreCase))
							{
								const EMaterialSamplerType AutoType =
									UMaterialExpressionTextureBase::GetSamplerTypeForTexture(Tex);
								if (AutoType != TexBase->SamplerType)
								{
									TexBase->SamplerType = AutoType;
								}
							}
						}
					}
					else
					{
						OutError = FString::Printf(TEXT("Texture not found: '%s'"), *TexPath);
					}
				}
			}
		}

		if (bSet) break;
	}

	Target.PostEditChange();

	if (!bSet && OutError.IsEmpty())
	{
		TArray<FString> AvailableProps;
		for (TFieldIterator<FProperty> PropIt(Expr->GetClass(), EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
		{
			const FString PName = PropIt->GetName();
			if (!PName.StartsWith(TEXT("MaterialExpression")) && !PName.StartsWith(TEXT("bRealtimePreview")))
				AvailableProps.Add(PName);
		}
		OutError = FString::Printf(
			TEXT("Property '%s' not found or type mismatch on %s. "
				 "Use float_value, vector_value [R,G,B,A], string_value, texture_path, or bool_value. "
				 "For a MaterialFunctionCall node, set property 'MaterialFunction' with string_value = the function asset path. "
				 "Available properties: [%s]"),
			*PropertyName, *Expr->GetClass()->GetName(), *FString::Join(AvailableProps, TEXT(", ")));
	}
	if (!OutError.IsEmpty()) return;

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"),
		FString::Printf(TEXT("Set '%s' on node[%d] (%s)"), *PropertyName, NodeIndex, *Expr->GetClass()->GetName()));
	Target.AddNodeRef(Result, Expr);
	Result->SetStringField(TEXT("property_name"), PropertyName);

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleSetMaterialNodeValue(
	const FString& MaterialPath,
	int32 NodeIndex,
	const FString& PropertyName,
	const TSharedPtr<FJsonObject>& ValueJson,
	FString& OutJsonString, FString& OutError)
{
	HandleSetMaterialNodeValue(MaterialPath, FString::FromInt(NodeIndex), PropertyName, ValueJson, OutJsonString, OutError);
}

void HandleSetMaterialProperty(
	const FString& MaterialPath,
	const TSharedPtr<FJsonObject>& Params,
	FString& OutJsonString, FString& OutError)
{
	UMaterial* M = LoadMaterial(MaterialPath, OutError);
	if (!M) return;

	FString PropNameRaw;
	if (Params->TryGetStringField(TEXT("property_name"), PropNameRaw) && !PropNameRaw.IsEmpty())
	{
		const FString P = PropNameRaw.ToLower().Replace(TEXT("_"), TEXT(""));
		FString CanonicalField;
		FString SourceField;
		if      (P == TEXT("twosided"))               { CanonicalField = TEXT("two_sided");                SourceField = TEXT("bool_value");   }
		else if (P == TEXT("blendmode"))              { CanonicalField = TEXT("blend_mode");               SourceField = TEXT("string_value"); }
		else if (P == TEXT("shadingmodel"))           { CanonicalField = TEXT("shading_model");            SourceField = TEXT("string_value"); }
		else if (P == TEXT("domain") || P == TEXT("materialdomain"))
		                                              { CanonicalField = TEXT("domain");                   SourceField = TEXT("string_value"); }
		else if (P == TEXT("decalblendmode"))         { CanonicalField = TEXT("decal_blend_mode");         SourceField = TEXT("string_value"); }
		else if (P == TEXT("opacitymaskclipvalue"))   { CanonicalField = TEXT("opacity_mask_clip_value");  SourceField = TEXT("float_value");  }
		else if (P == TEXT("usageflags"))             { CanonicalField = TEXT("usage_flags");              SourceField = TEXT("usage_flags");  }
		else
		{
			OutError = FString::Printf(
				TEXT("Unknown property_name '%s'. set_material_property accepts: two_sided (bool), blend_mode (string), shading_model (string), domain (string), decal_blend_mode (string), opacity_mask_clip_value (float), usage_flags (array)."),
				*PropNameRaw);
			return;
		}

		const TSharedPtr<FJsonValue> SrcVal = Params->TryGetField(SourceField);
		if (SrcVal.IsValid())
		{
			Params->SetField(CanonicalField, SrcVal);
		}
	}

	FString BlendMode, ShadingModel, Domain, DecalBlendMode;
	Params->TryGetStringField(TEXT("blend_mode"),    BlendMode);
	Params->TryGetStringField(TEXT("shading_model"), ShadingModel);
	Params->TryGetStringField(TEXT("domain"),        Domain);
	Params->TryGetStringField(TEXT("decal_blend_mode"), DecalBlendMode);

	{
		auto StripEnumPrefix = [](FString& S)
		{
			int32 Colon;
			if (S.FindLastChar(TEXT(':'), Colon)) S = S.Mid(Colon + 1);
			for (const TCHAR* Pfx : { TEXT("BLEND_"), TEXT("MSM_"), TEXT("MD_"), TEXT("DBM_") })
			{
				if (S.StartsWith(Pfx, ESearchCase::IgnoreCase)) { S = S.RightChop(FCString::Strlen(Pfx)); break; }
			}
		};
		StripEnumPrefix(BlendMode);
		StripEnumPrefix(ShadingModel);
		StripEnumPrefix(Domain);
		StripEnumPrefix(DecalBlendMode);
	}

	bool bTwoSided = false;
	const bool bSetTwoSided = Params->TryGetBoolField(TEXT("two_sided"), bTwoSided);

	double ClipValue = 0.333;
	const bool bSetClip = Params->TryGetNumberField(TEXT("opacity_mask_clip_value"), ClipValue);

	const TArray<TSharedPtr<FJsonValue>>* UsageFlagsArr = nullptr;
	Params->TryGetArrayField(TEXT("usage_flags"), UsageFlagsArr);

	TArray<FString> Unrecognized;

	const FScopedTransaction Transaction(FText::FromString(TEXT("Axivor: Set Material Property")));
	M->Modify();
	M->PreEditChange(nullptr);

	if (!BlendMode.IsEmpty())
	{
		if      (BlendMode.Equals(TEXT("Opaque"),         ESearchCase::IgnoreCase)) M->BlendMode = BLEND_Opaque;
		else if (BlendMode.Equals(TEXT("Masked"),         ESearchCase::IgnoreCase)) M->BlendMode = BLEND_Masked;
		else if (BlendMode.Equals(TEXT("Translucent"),    ESearchCase::IgnoreCase)) M->BlendMode = BLEND_Translucent;
		else if (BlendMode.Equals(TEXT("Additive"),       ESearchCase::IgnoreCase)) M->BlendMode = BLEND_Additive;
		else if (BlendMode.Equals(TEXT("Modulate"),       ESearchCase::IgnoreCase)) M->BlendMode = BLEND_Modulate;
		else if (BlendMode.Equals(TEXT("AlphaComposite"), ESearchCase::IgnoreCase)) M->BlendMode = BLEND_AlphaComposite;
		else if (BlendMode.Equals(TEXT("AlphaHoldout"),   ESearchCase::IgnoreCase)) M->BlendMode = BLEND_AlphaHoldout;
		else Unrecognized.Add(FString::Printf(TEXT("blend_mode='%s'"), *BlendMode));
	}

	if (!ShadingModel.IsEmpty())
	{
		if      (ShadingModel.Equals(TEXT("DefaultLit"),        ESearchCase::IgnoreCase)) M->SetShadingModel(MSM_DefaultLit);
		else if (ShadingModel.Equals(TEXT("Unlit"),             ESearchCase::IgnoreCase)) M->SetShadingModel(MSM_Unlit);
		else if (ShadingModel.Equals(TEXT("Subsurface"),        ESearchCase::IgnoreCase)) M->SetShadingModel(MSM_Subsurface);
		else if (ShadingModel.Equals(TEXT("ClearCoat"),         ESearchCase::IgnoreCase)) M->SetShadingModel(MSM_ClearCoat);
		else if (ShadingModel.Equals(TEXT("Hair"),              ESearchCase::IgnoreCase)) M->SetShadingModel(MSM_Hair);
		else if (ShadingModel.Equals(TEXT("Eye"),               ESearchCase::IgnoreCase)) M->SetShadingModel(MSM_Eye);
		else if (ShadingModel.Equals(TEXT("TwoSidedFoliage"),   ESearchCase::IgnoreCase)) M->SetShadingModel(MSM_TwoSidedFoliage);
		else if (ShadingModel.Equals(TEXT("SingleLayerWater"),  ESearchCase::IgnoreCase)) M->SetShadingModel(MSM_SingleLayerWater);
		else if (ShadingModel.Equals(TEXT("ThinTranslucent"),   ESearchCase::IgnoreCase)) M->SetShadingModel(MSM_ThinTranslucent);
		else Unrecognized.Add(FString::Printf(TEXT("shading_model='%s'"), *ShadingModel));
	}

	if (!Domain.IsEmpty())
	{
		if      (Domain.Equals(TEXT("Surface"),        ESearchCase::IgnoreCase)) M->MaterialDomain = MD_Surface;
		else if (Domain.Equals(TEXT("DeferredDecal"),  ESearchCase::IgnoreCase)) M->MaterialDomain = MD_DeferredDecal;
		else if (Domain.Equals(TEXT("LightFunction"),  ESearchCase::IgnoreCase)) M->MaterialDomain = MD_LightFunction;
		else if (Domain.Equals(TEXT("PostProcess"),    ESearchCase::IgnoreCase)) M->MaterialDomain = MD_PostProcess;
		else if (Domain.Equals(TEXT("UI"),             ESearchCase::IgnoreCase)) M->MaterialDomain = MD_UI;
		else if (Domain.Equals(TEXT("Volume"),         ESearchCase::IgnoreCase)) M->MaterialDomain = MD_Volume;
		else Unrecognized.Add(FString::Printf(TEXT("domain='%s'"), *Domain));
	}

	if (!DecalBlendMode.IsEmpty())
	{
		if      (DecalBlendMode.Equals(TEXT("Translucent"),                  ESearchCase::IgnoreCase)) M->DecalBlendMode = DBM_Translucent;
		else if (DecalBlendMode.Equals(TEXT("Stain"),                        ESearchCase::IgnoreCase)) M->DecalBlendMode = DBM_Stain;
		else if (DecalBlendMode.Equals(TEXT("Normal"),                       ESearchCase::IgnoreCase)) M->DecalBlendMode = DBM_Normal;
		else if (DecalBlendMode.Equals(TEXT("Emissive"),                     ESearchCase::IgnoreCase)) M->DecalBlendMode = DBM_Emissive;
		else if (DecalBlendMode.Equals(TEXT("AmbientOcclusion"),             ESearchCase::IgnoreCase)) M->DecalBlendMode = DBM_AmbientOcclusion;
		else if (DecalBlendMode.Equals(TEXT("AlphaComposite"),               ESearchCase::IgnoreCase)) M->DecalBlendMode = DBM_AlphaComposite;
		else if (DecalBlendMode.Equals(TEXT("DBuffer_Color"),                ESearchCase::IgnoreCase)) M->DecalBlendMode = DBM_DBuffer_Color;
		else if (DecalBlendMode.Equals(TEXT("DBuffer_ColorNormal"),          ESearchCase::IgnoreCase)) M->DecalBlendMode = DBM_DBuffer_ColorNormal;
		else if (DecalBlendMode.Equals(TEXT("DBuffer_ColorNormalRoughness"), ESearchCase::IgnoreCase)) M->DecalBlendMode = DBM_DBuffer_ColorNormalRoughness;
		else if (DecalBlendMode.Equals(TEXT("DBuffer_Normal"),               ESearchCase::IgnoreCase)) M->DecalBlendMode = DBM_DBuffer_Normal;
		else if (DecalBlendMode.Equals(TEXT("DBuffer_NormalRoughness"),      ESearchCase::IgnoreCase)) M->DecalBlendMode = DBM_DBuffer_NormalRoughness;
		else if (DecalBlendMode.Equals(TEXT("DBuffer_Roughness"),            ESearchCase::IgnoreCase)) M->DecalBlendMode = DBM_DBuffer_Roughness;
		else Unrecognized.Add(FString::Printf(TEXT("decal_blend_mode='%s'"), *DecalBlendMode));
	}

	if (bSetTwoSided) M->TwoSided = bTwoSided;
	if (bSetClip)     M->OpacityMaskClipValue = (float)ClipValue;

	TArray<FString> UnknownUsageFlags;
	TArray<FString> AppliedUsageFlags;
	if (UsageFlagsArr)
	{
		for (const TSharedPtr<FJsonValue>& FlagVal : *UsageFlagsArr)
		{
			const FString Flag = FlagVal->AsString();
			if      (Flag.Equals(TEXT("SkeletalMesh"),               ESearchCase::IgnoreCase)) { M->bUsedWithSkeletalMesh = true; AppliedUsageFlags.Add(Flag); }
			else if (Flag.Equals(TEXT("ParticleSprites"),            ESearchCase::IgnoreCase)) { M->bUsedWithParticleSprites = true; AppliedUsageFlags.Add(Flag); }
			else if (Flag.Equals(TEXT("MeshParticles"),              ESearchCase::IgnoreCase)) { M->bUsedWithMeshParticles = true; AppliedUsageFlags.Add(Flag); }
			else if (Flag.Equals(TEXT("NiagaraRibbons"),             ESearchCase::IgnoreCase)) { M->bUsedWithNiagaraRibbons = true; AppliedUsageFlags.Add(Flag); }
			else if (Flag.Equals(TEXT("NiagaraMeshParticles"),       ESearchCase::IgnoreCase)) { M->bUsedWithNiagaraMeshParticles = true; AppliedUsageFlags.Add(Flag); }
			else if (Flag.Equals(TEXT("NiagaraSprites"),             ESearchCase::IgnoreCase)
			      || Flag.Equals(TEXT("NiagaraSpriteParticles"),     ESearchCase::IgnoreCase)) { M->bUsedWithNiagaraSprites = true; AppliedUsageFlags.Add(Flag); }
			else if (Flag.Equals(TEXT("InstancedStaticMeshes"),      ESearchCase::IgnoreCase)) { M->bUsedWithInstancedStaticMeshes = true; AppliedUsageFlags.Add(Flag); }
			else if (Flag.Equals(TEXT("MorphTargets"),               ESearchCase::IgnoreCase)) { M->bUsedWithMorphTargets = true; AppliedUsageFlags.Add(Flag); }
			else if (Flag.Equals(TEXT("Clothing"),                   ESearchCase::IgnoreCase)) { M->bUsedWithClothing = true; AppliedUsageFlags.Add(Flag); }
			else if (Flag.Equals(TEXT("HairStrands"),                ESearchCase::IgnoreCase)) { M->bUsedWithHairStrands = true; AppliedUsageFlags.Add(Flag); }
			else if (Flag.Equals(TEXT("Water"),                      ESearchCase::IgnoreCase)) { M->bUsedWithWater = true; AppliedUsageFlags.Add(Flag); }
			else if (Flag.Equals(TEXT("Nanite"),                     ESearchCase::IgnoreCase)) { M->bUsedWithNanite = true; AppliedUsageFlags.Add(Flag); }
			else if (Flag.Equals(TEXT("StaticLighting"),             ESearchCase::IgnoreCase)) { M->bUsedWithStaticLighting = true; AppliedUsageFlags.Add(Flag); }
			else                                                                                { UnknownUsageFlags.Add(Flag); }
		}
	}

	M->PostEditChange();
	M->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), Unrecognized.Num() == 0);
	if (Unrecognized.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> UnrecJson;
		for (const FString& U : Unrecognized) UnrecJson.Add(MakeShareable(new FJsonValueString(U)));
		Result->SetArrayField(TEXT("unrecognized"), UnrecJson);
		Result->SetStringField(TEXT("unrecognized_warning"),
			TEXT("These enum values matched no valid option and were NOT applied (field left unchanged). Check spelling against the allowed values."));
	}
	if (!BlendMode.IsEmpty())     Result->SetStringField(TEXT("blend_mode"),    BlendMode);
	if (!ShadingModel.IsEmpty())  Result->SetStringField(TEXT("shading_model"), ShadingModel);
	if (!Domain.IsEmpty())        Result->SetStringField(TEXT("domain"),        Domain);
	if (!DecalBlendMode.IsEmpty()) Result->SetStringField(TEXT("decal_blend_mode"), DecalBlendMode);
	if (bSetTwoSided)             Result->SetBoolField  (TEXT("two_sided"),     bTwoSided);
	if (bSetClip)                 Result->SetNumberField(TEXT("opacity_mask_clip_value"), ClipValue);

	if (UsageFlagsArr)
	{
		TArray<TSharedPtr<FJsonValue>> AppliedJson;
		for (const FString& F : AppliedUsageFlags) AppliedJson.Add(MakeShareable(new FJsonValueString(F)));
		Result->SetArrayField(TEXT("usage_flags_applied"), AppliedJson);

		if (UnknownUsageFlags.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> UnknownJson;
			for (const FString& F : UnknownUsageFlags) UnknownJson.Add(MakeShareable(new FJsonValueString(F)));
			Result->SetArrayField(TEXT("usage_flags_unknown"), UnknownJson);
			Result->SetStringField(TEXT("usage_flags_warning"),
				TEXT("Unknown usage flag names were ignored. Valid: SkeletalMesh, ParticleSprites, MeshParticles, NiagaraRibbons, NiagaraMeshParticles, NiagaraSprites (alias: NiagaraSpriteParticles), InstancedStaticMeshes, MorphTargets, Clothing, HairStrands, Water, Nanite, StaticLighting."));
		}
	}

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleGetMaterialNodes(
	const FString& MaterialPath,
	FString& OutJsonString, FString& OutError)
{
	FMaterialGraphTarget Target;
	if (!Target.Load(MaterialPath, OutError)) return;
	Target.EnsureAllGuids();

	auto GetConnectedIdx = [&](const FExpressionInput* Input) -> int32
	{
		if (!Input || !Input->Expression) return -1;
		return Target.IndexOf(Input->Expression);
	};
	auto GetConnectedId = [&](const FExpressionInput* Input) -> FString
	{
		if (!Input || !Input->Expression) return FString();
		return FMaterialGraphTarget::GuidOf(Input->Expression);
	};

	TArray<TSharedPtr<FJsonValue>> NodesArray;
	for (int32 i = 0; i < Target.Num(); i++)
	{
		UMaterialExpression* Expr = Target.Get(i);
		if (!Expr) continue;

		TArray<FString> NodeCaptions;
		Expr->GetCaption(NodeCaptions);
		const FString NodeCaption = NodeCaptions.Num() > 0 ? NodeCaptions[0] : Expr->GetClass()->GetName();

		TSharedPtr<FJsonObject> Node = MakeShareable(new FJsonObject);
		Node->SetNumberField(TEXT("index"), i);
		Node->SetStringField(TEXT("node_id"), FMaterialGraphTarget::GuidOf(Expr));
		Node->SetStringField(TEXT("class"), Expr->GetClass()->GetName());
		Node->SetStringField(TEXT("caption"), NodeCaption);
		if (!Expr->Desc.IsEmpty()) Node->SetStringField(TEXT("desc"), Expr->Desc);
		Node->SetNumberField(TEXT("pos_x"), Expr->MaterialExpressionEditorX);
		Node->SetNumberField(TEXT("pos_y"), Expr->MaterialExpressionEditorY);

		TArray<TSharedPtr<FJsonValue>> Inputs, Outputs;
		TSharedPtr<FJsonObject> Connections = MakeShareable(new FJsonObject);
		TSharedPtr<FJsonObject> ConnectionsById = MakeShareable(new FJsonObject);

		auto RecordConnection = [&](const FString& PinName, const FExpressionInput* Input)
		{
			const int32 ConnIdx = GetConnectedIdx(Input);
			if (ConnIdx >= 0)
			{
				Connections->SetNumberField(PinName, ConnIdx);
				ConnectionsById->SetStringField(PinName, GetConnectedId(Input));
			}
			else
			{
				Connections->SetStringField(PinName, TEXT("unconnected"));
				ConnectionsById->SetStringField(PinName, TEXT("unconnected"));
			}
		};

		if (UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expr))
		{
			for (FCustomInput& Entry : Custom->Inputs)
			{
				const FString PinName = Entry.InputName.ToString();
				Inputs.Add(MakeShareable(new FJsonValueString(PinName)));
				RecordConnection(PinName, &Entry.Input);
			}
		}
		else
		{
			for (TFieldIterator<FStructProperty> It(Expr->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
			{
				if (!It->Struct) continue;
				const FString StructName = It->Struct->GetName();
				if (!StructName.Contains(TEXT("ExpressionInput")) && !StructName.Contains(TEXT("MaterialInput"))) continue;

				const FString PinName = It->GetName();
				Inputs.Add(MakeShareable(new FJsonValueString(PinName)));

				FExpressionInput* InputPtr = It->ContainerPtrToValuePtr<FExpressionInput>(Expr);
				RecordConnection(PinName, InputPtr);
			}
		}

		const TArray<FExpressionOutput>& ExprOutputs = Expr->GetOutputs();
		for (const FExpressionOutput& Out : ExprOutputs)
			Outputs.Add(MakeShareable(new FJsonValueString(DeriveOutputPinLabel(Out))));

		Node->SetArrayField(TEXT("input_pins"),  Inputs);
		Node->SetArrayField(TEXT("output_pins"), Outputs);
		Node->SetObjectField(TEXT("connections"), Connections);
		Node->SetObjectField(TEXT("connections_by_id"), ConnectionsById);
		NodesArray.Add(MakeShareable(new FJsonValueObject(Node)));
	}

	struct FMatOutputDef { const TCHAR* Name; EMaterialProperty Prop; };
	static const FMatOutputDef MatOutputDefs[] = {
		{ TEXT("BaseColor"),           MP_BaseColor },
		{ TEXT("Metallic"),            MP_Metallic },
		{ TEXT("Roughness"),           MP_Roughness },
		{ TEXT("Specular"),            MP_Specular },
		{ TEXT("EmissiveColor"),       MP_EmissiveColor },
		{ TEXT("Opacity"),             MP_Opacity },
		{ TEXT("OpacityMask"),         MP_OpacityMask },
		{ TEXT("Normal"),              MP_Normal },
		{ TEXT("WorldPositionOffset"), MP_WorldPositionOffset },
		{ TEXT("AmbientOcclusion"),    MP_AmbientOcclusion },
		{ TEXT("Refraction"),          MP_Refraction },
	};

	TSharedPtr<FJsonObject> MatOutputsObj = MakeShareable(new FJsonObject);
	TSharedPtr<FJsonObject> MatOutputsByIdObj = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> UnconnectedOutputs;
	UMaterial* MatForOutputs = Target.AsMaterial();
	for (const FMatOutputDef& Def : MatOutputDefs)
	{
		FExpressionInput* Slot = MatForOutputs ? MatForOutputs->GetExpressionInputForProperty(Def.Prop) : nullptr;
		const int32 ConnIdx = GetConnectedIdx(Slot);
		if (ConnIdx >= 0)
		{
			MatOutputsObj->SetNumberField(Def.Name, ConnIdx);
			MatOutputsByIdObj->SetStringField(Def.Name, GetConnectedId(Slot));
		}
		else
		{
			MatOutputsObj->SetStringField(Def.Name, TEXT("unconnected"));
			MatOutputsByIdObj->SetStringField(Def.Name, TEXT("unconnected"));
			UnconnectedOutputs.Add(MakeShareable(new FJsonValueString(FString(Def.Name))));
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("count"), Target.Num());
	Result->SetArrayField(TEXT("nodes"), NodesArray);
	Result->SetStringField(TEXT("id_note"), TEXT("Use node_id (stable guid) to address nodes in other material tools; 'index' shifts when nodes are deleted. connections / material_outputs give indices, *_by_id give node ids."));

	if (!Target.IsFunction())
	{
		if (UMaterial* HeaderM = Target.AsMaterial())
		{
			TSharedPtr<FJsonObject> Info = MakeShareable(new FJsonObject);

			switch (HeaderM->MaterialDomain)
			{
				case MD_Surface:       Info->SetStringField(TEXT("domain"), TEXT("Surface")); break;
				case MD_DeferredDecal: Info->SetStringField(TEXT("domain"), TEXT("DeferredDecal")); break;
				case MD_LightFunction: Info->SetStringField(TEXT("domain"), TEXT("LightFunction")); break;
				case MD_PostProcess:   Info->SetStringField(TEXT("domain"), TEXT("PostProcess")); break;
				case MD_UI:            Info->SetStringField(TEXT("domain"), TEXT("UI")); break;
				case MD_Volume:        Info->SetStringField(TEXT("domain"), TEXT("Volume")); break;
				default: break;
			}
			switch (HeaderM->BlendMode)
			{
				case BLEND_Opaque:         Info->SetStringField(TEXT("blend_mode"), TEXT("Opaque")); break;
				case BLEND_Masked:         Info->SetStringField(TEXT("blend_mode"), TEXT("Masked")); break;
				case BLEND_Translucent:    Info->SetStringField(TEXT("blend_mode"), TEXT("Translucent")); break;
				case BLEND_Additive:       Info->SetStringField(TEXT("blend_mode"), TEXT("Additive")); break;
				case BLEND_Modulate:       Info->SetStringField(TEXT("blend_mode"), TEXT("Modulate")); break;
				case BLEND_AlphaComposite: Info->SetStringField(TEXT("blend_mode"), TEXT("AlphaComposite")); break;
				default: break;
			}
			const EMaterialShadingModel SM = HeaderM->GetShadingModels().GetFirstShadingModel();
			switch (SM)
			{
				case MSM_DefaultLit:       Info->SetStringField(TEXT("shading_model"), TEXT("DefaultLit")); break;
				case MSM_Unlit:            Info->SetStringField(TEXT("shading_model"), TEXT("Unlit")); break;
				case MSM_Subsurface:       Info->SetStringField(TEXT("shading_model"), TEXT("Subsurface")); break;
				case MSM_ClearCoat:        Info->SetStringField(TEXT("shading_model"), TEXT("ClearCoat")); break;
				case MSM_Hair:             Info->SetStringField(TEXT("shading_model"), TEXT("Hair")); break;
				case MSM_Eye:              Info->SetStringField(TEXT("shading_model"), TEXT("Eye")); break;
				case MSM_TwoSidedFoliage:  Info->SetStringField(TEXT("shading_model"), TEXT("TwoSidedFoliage")); break;
				case MSM_SingleLayerWater: Info->SetStringField(TEXT("shading_model"), TEXT("SingleLayerWater")); break;
				case MSM_ThinTranslucent:  Info->SetStringField(TEXT("shading_model"), TEXT("ThinTranslucent")); break;
				default: break;
			}
			Info->SetBoolField(TEXT("two_sided"), HeaderM->TwoSided);
			Info->SetNumberField(TEXT("opacity_mask_clip_value"), HeaderM->OpacityMaskClipValue);

			if (HeaderM->MaterialDomain == MD_DeferredDecal)
			{
				switch (HeaderM->DecalBlendMode)
				{
					case DBM_Translucent:                  Info->SetStringField(TEXT("decal_blend_mode"), TEXT("Translucent")); break;
					case DBM_Stain:                        Info->SetStringField(TEXT("decal_blend_mode"), TEXT("Stain")); break;
					case DBM_Normal:                       Info->SetStringField(TEXT("decal_blend_mode"), TEXT("Normal")); break;
					case DBM_Emissive:                     Info->SetStringField(TEXT("decal_blend_mode"), TEXT("Emissive")); break;
					case DBM_DBuffer_Color:                Info->SetStringField(TEXT("decal_blend_mode"), TEXT("DBuffer_Color")); break;
					case DBM_DBuffer_ColorNormal:          Info->SetStringField(TEXT("decal_blend_mode"), TEXT("DBuffer_ColorNormal")); break;
					case DBM_DBuffer_ColorNormalRoughness: Info->SetStringField(TEXT("decal_blend_mode"), TEXT("DBuffer_ColorNormalRoughness")); break;
					case DBM_DBuffer_Normal:               Info->SetStringField(TEXT("decal_blend_mode"), TEXT("DBuffer_Normal")); break;
					case DBM_DBuffer_NormalRoughness:      Info->SetStringField(TEXT("decal_blend_mode"), TEXT("DBuffer_NormalRoughness")); break;
					case DBM_DBuffer_Roughness:            Info->SetStringField(TEXT("decal_blend_mode"), TEXT("DBuffer_Roughness")); break;
					default: break;
				}
			}

			TArray<TSharedPtr<FJsonValue>> Flags;
			if (HeaderM->bUsedWithSkeletalMesh)          Flags.Add(MakeShareable(new FJsonValueString(TEXT("SkeletalMesh"))));
			if (HeaderM->bUsedWithParticleSprites)        Flags.Add(MakeShareable(new FJsonValueString(TEXT("ParticleSprites"))));
			if (HeaderM->bUsedWithMeshParticles)          Flags.Add(MakeShareable(new FJsonValueString(TEXT("MeshParticles"))));
			if (HeaderM->bUsedWithNiagaraRibbons)         Flags.Add(MakeShareable(new FJsonValueString(TEXT("NiagaraRibbons"))));
			if (HeaderM->bUsedWithNiagaraMeshParticles)   Flags.Add(MakeShareable(new FJsonValueString(TEXT("NiagaraMeshParticles"))));
			if (HeaderM->bUsedWithNiagaraSprites)         Flags.Add(MakeShareable(new FJsonValueString(TEXT("NiagaraSprites"))));
			if (HeaderM->bUsedWithInstancedStaticMeshes)  Flags.Add(MakeShareable(new FJsonValueString(TEXT("InstancedStaticMeshes"))));
			if (HeaderM->bUsedWithMorphTargets)           Flags.Add(MakeShareable(new FJsonValueString(TEXT("MorphTargets"))));
			if (Flags.Num() > 0) Info->SetArrayField(TEXT("usage_flags"), Flags);

			Result->SetObjectField(TEXT("material_info"), Info);
		}
		Result->SetObjectField(TEXT("material_outputs"), MatOutputsObj);
		Result->SetObjectField(TEXT("material_outputs_by_id"), MatOutputsByIdObj);
		Result->SetArrayField(TEXT("unconnected_outputs"), UnconnectedOutputs);
	}
	else
	{
		Result->SetStringField(TEXT("note"), TEXT("This is a MaterialFunction — no material output slots. Connect nodes to a FunctionOutput node (pass its node_id as to_node)."));
	}

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleCreateMaterialFunction(
	const FString& Name,
	const FString& SavePath,
	FString& OutJsonString, FString& OutError)
{
	const FString TargetPath = SavePath.IsEmpty() ? TEXT("/Game/Materials/Functions") : SavePath;
	const FString FullPath   = TargetPath + TEXT("/") + Name;

	if (UEditorAssetLibrary::DoesAssetExist(FullPath))
	{
		OutError = FString::Printf(TEXT("Material Function already exists at: '%s'"), *FullPath);
		return;
	}

	UMaterialFunctionFactoryNew* Factory = NewObject<UMaterialFunctionFactoryNew>();
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

	UObject* NewAsset = AssetToolsModule.Get().CreateAsset(Name, TargetPath, UMaterialFunction::StaticClass(), Factory);
	UMaterialFunction* MatFunc = Cast<UMaterialFunction>(NewAsset);

	if (!MatFunc)
	{
		OutError = TEXT("Failed to create Material Function asset.");
		return;
	}

	MatFunc->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(MatFunc);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("path"), MatFunc->GetPathName());
	Result->SetStringField(TEXT("message"),
		FString::Printf(TEXT("Created material function '%s' at '%s'"), *Name, *TargetPath));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleDeleteMaterialNode(const FString& MaterialPath, const FString& NodeHandle, FString& OutJsonString, FString& OutError)
{
	FMaterialGraphTarget Target;
	if (!Target.Load(MaterialPath, OutError)) return;

	UMaterialExpression* ToDelete = Target.Resolve(NodeHandle, OutError);
	if (!ToDelete) return;
	const int32 NodeIndex = Target.IndexOf(ToDelete);

	const FString DeletedClass = ToDelete->GetClass()->GetName();
	const FString DeletedId    = FMaterialGraphTarget::GuidOf(ToDelete);

	const FScopedTransaction Transaction(FText::FromString(TEXT("Axivor: Delete Material Node")));
	Target.Modify();
	ToDelete->Modify();
	for (int32 i = 0; i < Target.Num(); i++)
	{
		if (UMaterialExpression* Other = Target.Get(i)) Other->Modify();
	}
	Target.PreEditChange();

	auto BreakInputsReferencingNode = [&](UObject* Container, UClass* ContainerClass)
	{
		for (TFieldIterator<FStructProperty> PropIt(ContainerClass, EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
		{
			FStructProperty* SP = *PropIt;
			if (!SP->Struct) continue;
			const FString StructName = SP->Struct->GetName();
			if (!StructName.Contains(TEXT("ExpressionInput")) && !StructName.Contains(TEXT("MaterialInput"))) continue;
			void* StructPtr = SP->ContainerPtrToValuePtr<void>(Container);
			FObjectProperty* ExprProp = CastField<FObjectProperty>(SP->Struct->FindPropertyByName(FName("Expression")));
			if (!ExprProp) continue;
			if (ExprProp->GetObjectPropertyValue(ExprProp->ContainerPtrToValuePtr<void>(StructPtr)) == ToDelete)
				ExprProp->SetObjectPropertyValue(ExprProp->ContainerPtrToValuePtr<void>(StructPtr), nullptr);
		}
	};

	for (int32 i = 0; i < Target.Num(); i++)
	{
		UMaterialExpression* OtherExpr = Target.Get(i);
		if (!OtherExpr || OtherExpr == ToDelete) continue;
		BreakInputsReferencingNode(OtherExpr, OtherExpr->GetClass());
	}

	if (UMaterialEditorOnlyData* EditorData = Target.EditorData())
	{
		EditorData->Modify();
		BreakInputsReferencingNode(EditorData, EditorData->GetClass());
	}

	Target.RemoveAt(NodeIndex);
	ToDelete->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_ForceNoResetLoaders);

	Target.PostEditChange();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("deleted_class"), DeletedClass);
	Result->SetNumberField(TEXT("deleted_index"), NodeIndex);
	Result->SetStringField(TEXT("deleted_node_id"), DeletedId);
	Result->SetNumberField(TEXT("remaining_nodes"), Target.Num());
	Result->SetStringField(TEXT("note"), TEXT("Indices of nodes after the deleted one have shifted down by 1; node_id values of the remaining nodes are unchanged."));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleDeleteMaterialNode(const FString& MaterialPath, int32 NodeIndex, FString& OutJsonString, FString& OutError)
{
	HandleDeleteMaterialNode(MaterialPath, FString::FromInt(NodeIndex), OutJsonString, OutError);
}

void HandleDisconnectMaterialPin(const FString& MaterialPath, const FString& NodeHandle, const FString& InputPinName, FString& OutJsonString, FString& OutError)
{
	FMaterialGraphTarget Target;
	if (!Target.Load(MaterialPath, OutError)) return;

	UMaterialExpression* Expr = Target.Resolve(NodeHandle, OutError);
	if (!Expr) return;
	const int32 NodeIndex = Target.IndexOf(Expr);

	FExpressionInput* Input = FindInputOnExpression(Expr, InputPinName);
	if (!Input)
	{
		TArray<FString> Available;
		for (TFieldIterator<FStructProperty> It(Expr->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			if (It->Struct && (It->Struct->GetName().Contains(TEXT("ExpressionInput")) || It->Struct->GetName().Contains(TEXT("MaterialInput"))))
				Available.Add(It->GetName());
		}
		OutError = FString::Printf(TEXT("Pin '%s' not found on %s. Available: [%s]"),
			*InputPinName, *Expr->GetClass()->GetName(), *FString::Join(Available, TEXT(", ")));
		return;
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("Axivor: Disconnect Material Pin")));
	Target.Modify();
	Expr->Modify();
	Target.PreEditChange();
	Input->Expression  = nullptr;
	Input->OutputIndex = 0;
	Target.PostEditChange();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Disconnected pin '%s' on node[%d] (%s)"),
		*InputPinName, NodeIndex, *Expr->GetClass()->GetName()));
	Target.AddNodeRef(Result, Expr);
	Result->SetStringField(TEXT("input_pin_name"), InputPinName);
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleDisconnectMaterialPin(const FString& MaterialPath, int32 NodeIndex, const FString& InputPinName, FString& OutJsonString, FString& OutError)
{
	HandleDisconnectMaterialPin(MaterialPath, FString::FromInt(NodeIndex), InputPinName, OutJsonString, OutError);
}

void HandleMoveMaterialNode(const FString& MaterialPath, const FString& NodeHandle, int32 NewPosX, int32 NewPosY, FString& OutJsonString, FString& OutError)
{
	FMaterialGraphTarget Target;
	if (!Target.Load(MaterialPath, OutError)) return;

	UMaterialExpression* Expr = Target.Resolve(NodeHandle, OutError);
	if (!Expr) return;
	const int32 NodeIndex = Target.IndexOf(Expr);

	if (NewPosX > -100) NewPosX = -600;

	const FScopedTransaction Transaction(FText::FromString(TEXT("Axivor: Move Material Node")));
	Target.Modify();
	Expr->Modify();
	Target.PreEditChange();
	Expr->MaterialExpressionEditorX = NewPosX;
	Expr->MaterialExpressionEditorY = NewPosY;
	Target.PostEditChange();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Moved node[%d] (%s) to (%d, %d)"),
		NodeIndex, *Expr->GetClass()->GetName(), NewPosX, NewPosY));
	Target.AddNodeRef(Result, Expr);
	Result->SetNumberField(TEXT("pos_x"), NewPosX);
	Result->SetNumberField(TEXT("pos_y"), NewPosY);
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleMoveMaterialNode(const FString& MaterialPath, int32 NodeIndex, int32 NewPosX, int32 NewPosY, FString& OutJsonString, FString& OutError)
{
	HandleMoveMaterialNode(MaterialPath, FString::FromInt(NodeIndex), NewPosX, NewPosY, OutJsonString, OutError);
}

void HandleDuplicateMaterialNode(const FString& MaterialPath, const FString& SourceNodeHandle, int32 OffsetX, int32 OffsetY, FString& OutJsonString, FString& OutError)
{
	FMaterialGraphTarget Target;
	if (!Target.Load(MaterialPath, OutError)) return;

	UMaterialExpression* SourceExpr = Target.Resolve(SourceNodeHandle, OutError);
	if (!SourceExpr) return;
	const int32 SourceNodeIndex = Target.IndexOf(SourceExpr);

	const FScopedTransaction Transaction(FText::FromString(TEXT("Axivor: Duplicate Material Node")));
	Target.Modify();
	Target.PreEditChange();

	UMaterialExpression* NewExpr = DuplicateObject<UMaterialExpression>(SourceExpr, Target.Owner);
	if (!NewExpr) { OutError = TEXT("DuplicateObject failed."); Target.PostEditChange(); return; }

	// The duplicate inherits the source guid — give it its own identity.
	NewExpr->SetFlags(RF_Transactional);
	NewExpr->UpdateMaterialExpressionGuid(/*bForceGeneration*/ true, /*bAllowMarkingPackageDirty*/ false);

	NewExpr->MaterialExpressionEditorX = SourceExpr->MaterialExpressionEditorX + OffsetX;
	NewExpr->MaterialExpressionEditorY = SourceExpr->MaterialExpressionEditorY + OffsetY;
	if (NewExpr->MaterialExpressionEditorX > -100) NewExpr->MaterialExpressionEditorX = -600;

	for (TFieldIterator<FStructProperty> PropIt(NewExpr->GetClass(), EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
	{
		if (!PropIt->Struct) continue;
		const FString SN = PropIt->Struct->GetName();
		if (!SN.Contains(TEXT("ExpressionInput")) && !SN.Contains(TEXT("MaterialInput"))) continue;
		FExpressionInput* Input = PropIt->ContainerPtrToValuePtr<FExpressionInput>(NewExpr);
		if (Input) { Input->Expression = nullptr; Input->OutputIndex = 0; }
	}

	Target.AddExpression(NewExpr);
	const int32 NewIndex = Target.IndexOf(NewExpr);

	Target.PostEditChange();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("new_node_index"), NewIndex);
	Result->SetStringField(TEXT("new_node_id"), FMaterialGraphTarget::GuidOf(NewExpr));
	Target.AddNodeRef(Result, NewExpr);
	Target.AddNodeRef(Result, SourceExpr, TEXT("source_"));
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Duplicated node[%d] -> node[%d] (node_id=%s) at (%d,%d)"),
		SourceNodeIndex, NewIndex, *FMaterialGraphTarget::GuidOf(NewExpr), NewExpr->MaterialExpressionEditorX, NewExpr->MaterialExpressionEditorY));
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleDuplicateMaterialNode(const FString& MaterialPath, int32 SourceNodeIndex, int32 OffsetX, int32 OffsetY, FString& OutJsonString, FString& OutError)
{
	HandleDuplicateMaterialNode(MaterialPath, FString::FromInt(SourceNodeIndex), OffsetX, OffsetY, OutJsonString, OutError);
}

void HandleConnectMaterialNodesBulk(const FString& MaterialPath, const TArray<TSharedPtr<FJsonValue>>& Connections, FString& OutJsonString, FString& OutError)
{
	if (Connections.Num() == 0) { OutError = TEXT("connections array is empty."); return; }

	TArray<TSharedPtr<FJsonValue>> Results;
	int32 SuccessCount = 0;

	for (const TSharedPtr<FJsonValue>& ConnVal : Connections)
	{
		const TSharedPtr<FJsonObject>* ConnObj = nullptr;
		if (!ConnVal.IsValid() || !ConnVal->TryGetObject(ConnObj) || !ConnObj)
		{
			TSharedPtr<FJsonObject> R = MakeShareable(new FJsonObject);
			R->SetBoolField(TEXT("success"), false);
			R->SetStringField(TEXT("error"), TEXT("connection entry is not a JSON object"));
			Results.Add(MakeShareable(new FJsonValueObject(R)));
			continue;
		}

		const FString FromNode = ReadHandleArg(*ConnObj, FromNodeKeys);
		const FString ToNode   = ReadHandleArg(*ConnObj, ToNodeKeys);

		FString FromOutput, ToInput;
		(*ConnObj)->TryGetStringField(TEXT("from_output"), FromOutput);
		(*ConnObj)->TryGetStringField(TEXT("to_input"),    ToInput);

		FString ConnJson, ConnError;
		HandleConnectMaterialNodes(MaterialPath, FromNode, FromOutput, ToNode, ToInput, ConnJson, ConnError);

		TSharedPtr<FJsonObject> R = MakeShareable(new FJsonObject);
		R->SetBoolField(TEXT("success"), ConnError.IsEmpty());
		R->SetStringField(TEXT("from"), FString::Printf(TEXT("%s.%s"), *FromNode, *FromOutput));
		R->SetStringField(TEXT("to"),   FString::Printf(TEXT("%s.%s"), FMaterialGraphTarget::IsMaterialOutputHandle(ToNode) ? TEXT("material") : *ToNode, *ToInput));
		if (!ConnError.IsEmpty())
		{
			R->SetStringField(TEXT("error"), ConnError);
		}
		else
		{
			SuccessCount++;
			TSharedPtr<FJsonObject> Parsed;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ConnJson);
			if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
			{
				FString FromId, ToId;
				if (Parsed->TryGetStringField(TEXT("from_node_id"), FromId)) R->SetStringField(TEXT("from_node_id"), FromId);
				if (Parsed->TryGetStringField(TEXT("to_node_id"), ToId))     R->SetStringField(TEXT("to_node_id"), ToId);
			}
		}
		Results.Add(MakeShareable(new FJsonValueObject(R)));
	}

	const int32 FailCount = Connections.Num() - SuccessCount;
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), FailCount == 0 && SuccessCount > 0);
	Result->SetNumberField(TEXT("connected"), SuccessCount);
	Result->SetNumberField(TEXT("failed"),    FailCount);
	Result->SetArrayField(TEXT("results"), Results);

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleAutoLayoutMaterial(const FString& MaterialPath, FString& OutJsonString, FString& OutError)
{
	FMaterialGraphTarget Target;
	if (!Target.Load(MaterialPath, OutError)) return;

	const int32 N = Target.Num();
	if (N == 0)
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("message"), TEXT("No nodes to layout."));
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(Result.ToSharedRef(), W);
		return;
	}

	TMap<int32, TSet<int32>> Predecessors, Successors;
	for (int32 i = 0; i < N; i++) { Predecessors.FindOrAdd(i); Successors.FindOrAdd(i); }

	auto RecordEdge = [&](int32 FromIdx, int32 ToIdx)
	{
		if (FromIdx >= 0 && FromIdx < N && ToIdx >= 0 && ToIdx < N && FromIdx != ToIdx)
		{
			Predecessors.FindOrAdd(ToIdx).Add(FromIdx);
			Successors.FindOrAdd(FromIdx).Add(ToIdx);
		}
	};

	for (int32 i = 0; i < N; i++)
	{
		UMaterialExpression* E = Target.Get(i);
		if (!E) continue;

		for (TFieldIterator<FStructProperty> PropIt(E->GetClass(), EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
		{
			if (!PropIt->Struct) continue;
			const FString SN = PropIt->Struct->GetName();
			if (!SN.Contains(TEXT("ExpressionInput")) && !SN.Contains(TEXT("MaterialInput"))) continue;
			FExpressionInput* Input = PropIt->ContainerPtrToValuePtr<FExpressionInput>(E);
			if (Input && Input->Expression)
				RecordEdge(Target.IndexOf(Input->Expression), i);
		}

		if (UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(E))
		{
			for (FCustomInput& CI : Custom->Inputs)
				if (CI.Input.Expression)
					RecordEdge(Target.IndexOf(CI.Input.Expression), i);
		}
	}

	TMap<int32, int32> Depths;
	TQueue<int32> Queue;

	if (UMaterial* M = Target.AsMaterial())
	{
		static const EMaterialProperty OutputProps[] = {
			MP_BaseColor, MP_Metallic, MP_Roughness, MP_Specular, MP_EmissiveColor,
			MP_Opacity, MP_OpacityMask, MP_Normal, MP_WorldPositionOffset, MP_AmbientOcclusion, MP_Refraction
		};
		for (EMaterialProperty Prop : OutputProps)
		{
			FExpressionInput* Slot = M->GetExpressionInputForProperty(Prop);
			if (Slot && Slot->Expression)
			{
				int32 Idx = Target.IndexOf(Slot->Expression);
				if (Idx >= 0 && !Depths.Contains(Idx)) { Depths.Add(Idx, 0); Queue.Enqueue(Idx); }
			}
		}
	}
	for (int32 i = 0; i < N; i++)
	{
		if (Successors[i].Num() == 0 && !Depths.Contains(i)) { Depths.Add(i, 0); Queue.Enqueue(i); }
	}

	int32 MaxDepth = 0;
	while (!Queue.IsEmpty())
	{
		int32 Cur; Queue.Dequeue(Cur);
		int32 CurD = Depths[Cur];
		for (int32 Pred : Predecessors[Cur])
		{
			int32 NewD = CurD + 1;
			if (!Depths.Contains(Pred) || Depths[Pred] < NewD)
			{
				Depths.Add(Pred, NewD);
				Queue.Enqueue(Pred);
				MaxDepth = FMath::Max(MaxDepth, NewD);
			}
		}
	}
	for (int32 i = 0; i < N; i++)
		if (!Depths.Contains(i)) Depths.Add(i, MaxDepth + 1);

	TMap<int32, TArray<int32>> Columns;
	for (auto& P : Depths) Columns.FindOrAdd(P.Value).Add(P.Key);
	for (auto& P : Columns) P.Value.Sort();

	const int32 ColumnWidth = 380;
	const int32 NodeSpacing = 280;

	const FScopedTransaction Transaction(FText::FromString(TEXT("Axivor: Auto Layout Material")));
	Target.Modify();
	for (int32 i = 0; i < N; i++)
	{
		if (UMaterialExpression* E = Target.Get(i)) E->Modify();
	}
	Target.PreEditChange();
	for (auto& ColPair : Columns)
	{
		const TArray<int32>& ColNodes = ColPair.Value;
		int32 NewX = -(ColPair.Key + 1) * ColumnWidth;
		int32 StartY = -((ColNodes.Num() - 1) * NodeSpacing) / 2;
		for (int32 j = 0; j < ColNodes.Num(); j++)
		{
			UMaterialExpression* E = Target.Get(ColNodes[j]);
			if (E) { E->MaterialExpressionEditorX = NewX; E->MaterialExpressionEditorY = StartY + j * NodeSpacing; }
		}
	}
	Target.PostEditChange();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Auto-laid out %d nodes across %d columns. Call get_material_nodes to see updated positions."), N, Columns.Num()));
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), W);
}

void HandleAddMaterialNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString MaterialPath;
	Args->TryGetStringField(TEXT("material_path"), MaterialPath);
	if (MaterialPath.IsEmpty())
	{
		const TArray<TSharedPtr<FJsonValue>>* Peek = nullptr;
		if (Args->TryGetArrayField(TEXT("items"), Peek) && Peek && Peek->Num() > 0)
			if (auto First = (*Peek)[0]->AsObject())
				First->TryGetStringField(TEXT("material_path"), MaterialPath);
	}
	if (MaterialPath.IsEmpty()) { OutError = TEXT("material_path is required (top-level or in first item)."); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("nodes"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		TArray<int32> CreatedIndices;
		TArray<FString> CreatedIds;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			const TSharedPtr<FJsonObject>* ItemObj = nullptr;
			if (!(*ItemsArray)[i]->TryGetObject(ItemObj) || !ItemObj)
			{
				Batch.AddFailure(i, TEXT("Item is not a JSON object."));
				continue;
			}
			FString NodeType, Desc;
			(*ItemObj)->TryGetStringField(TEXT("node_type"), NodeType);
			(*ItemObj)->TryGetStringField(TEXT("desc"), Desc);
			double PosXd = 0, PosYd = 0;
			int32 PosX = (*ItemObj)->TryGetNumberField(TEXT("pos_x"), PosXd) ? (int32)PosXd : INT32_MIN;
			int32 PosY = (*ItemObj)->TryGetNumberField(TEXT("pos_y"), PosYd) ? (int32)PosYd : INT32_MIN;
			if (NodeType.IsEmpty()) { Batch.AddFailure(i, TEXT("node_type is required.")); continue; }

			FString ItemJson, ItemError;
			HandleAddMaterialNode(MaterialPath, NodeType, PosX, PosY, Desc, ItemJson, ItemError);
			if (ItemError.IsEmpty())
			{
				TSharedPtr<FJsonObject> Parsed;
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ItemJson);
				if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
				{
					TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
					double NodeIdx = 0;
					if (Parsed->TryGetNumberField(TEXT("node_index"), NodeIdx))
					{
						Extra->SetNumberField(TEXT("node_index"), NodeIdx);
						CreatedIndices.Add((int32)NodeIdx);
					}
					FString NodeId;
					if (Parsed->TryGetStringField(TEXT("node_id"), NodeId))
					{
						Extra->SetStringField(TEXT("node_id"), NodeId);
						CreatedIds.Add(NodeId);
					}
					FString Caption;
					if (Parsed->TryGetStringField(TEXT("caption"), Caption))
						Extra->SetStringField(TEXT("caption"), Caption);
					Batch.AddSuccess(i, Extra);
				}
				else
				{
					Batch.AddSuccess(i);
				}
			}
			else
			{
				Batch.AddFailure(i, ItemError);
			}
		}
		Batch.Finalize(OutJsonString);
		if (CreatedIndices.Num() > 0 || CreatedIds.Num() > 0)
		{
			TSharedPtr<FJsonObject> Parsed;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(OutJsonString);
			if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
			{
				TArray<TSharedPtr<FJsonValue>> IdxArr;
				for (int32 Idx : CreatedIndices) IdxArr.Add(MakeShared<FJsonValueNumber>(Idx));
				Parsed->SetArrayField(TEXT("node_indices"), IdxArr);
				TArray<TSharedPtr<FJsonValue>> IdArr;
				for (const FString& Id : CreatedIds) IdArr.Add(MakeShared<FJsonValueString>(Id));
				Parsed->SetArrayField(TEXT("node_ids"), IdArr);
				OutJsonString.Reset();
				TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
				FJsonSerializer::Serialize(Parsed.ToSharedRef(), Writer);
			}
		}
		return;
	}

	FString NodeType, Desc;
	Args->TryGetStringField(TEXT("node_type"), NodeType);
	Args->TryGetStringField(TEXT("desc"), Desc);
	double PosXd = 0, PosYd = 0;
	int32 PosX = Args->TryGetNumberField(TEXT("pos_x"), PosXd) ? (int32)PosXd : INT32_MIN;
	int32 PosY = Args->TryGetNumberField(TEXT("pos_y"), PosYd) ? (int32)PosYd : INT32_MIN;

	HandleAddMaterialNode(MaterialPath, NodeType, PosX, PosY, Desc, OutJsonString, OutError);
}

void HandleConnectMaterialNodesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString MaterialPath;
	Args->TryGetStringField(TEXT("material_path"), MaterialPath);
	if (MaterialPath.IsEmpty()) { OutError = TEXT("material_path is required."); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("connections"), ItemsArray))
	{
		HandleConnectMaterialNodesBulk(MaterialPath, *ItemsArray, OutJsonString, OutError);
		return;
	}

	FString FromOutput, ToInput;
	Args->TryGetStringField(TEXT("from_output"), FromOutput);
	Args->TryGetStringField(TEXT("to_input"), ToInput);
	FString FromNode = ReadHandleArg(Args, FromNodeKeys);
	if (FromNode.IsEmpty()) FromNode = TEXT("0");
	const FString ToNode = ReadHandleArg(Args, ToNodeKeys);   // empty => material output slot

	HandleConnectMaterialNodes(MaterialPath, FromNode, FromOutput, ToNode, ToInput, OutJsonString, OutError);
}

void HandleSetMaterialNodeValueFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString MaterialPath;
	Args->TryGetStringField(TEXT("material_path"), MaterialPath);
	if (MaterialPath.IsEmpty()) { OutError = TEXT("material_path is required."); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("values"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			const TSharedPtr<FJsonObject>* ItemObj = nullptr;
			if (!(*ItemsArray)[i]->TryGetObject(ItemObj) || !ItemObj)
			{
				Batch.AddFailure(i, TEXT("Item is not a JSON object."));
				continue;
			}
			FString NodeHandle = ReadHandleArg(*ItemObj, NodeHandleKeys);
			if (NodeHandle.IsEmpty()) NodeHandle = TEXT("0");
			FString PropertyName;
			(*ItemObj)->TryGetStringField(TEXT("property_name"), PropertyName);
			if (PropertyName.IsEmpty()) { Batch.AddFailure(i, TEXT("property_name is required.")); continue; }

			FString ItemJson, ItemError;
			HandleSetMaterialNodeValue(MaterialPath, NodeHandle, PropertyName, *ItemObj, ItemJson, ItemError);
			if (ItemError.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				TSharedPtr<FJsonObject> Parsed;
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ItemJson);
				FString NodeId;
				if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid() && Parsed->TryGetStringField(TEXT("node_id"), NodeId))
					Extra->SetStringField(TEXT("node_id"), NodeId);
				Extra->SetStringField(TEXT("property_name"), PropertyName);
				Batch.AddSuccess(i, Extra);
			}
			else
				Batch.AddFailure(i, ItemError);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString PropertyName;
	Args->TryGetStringField(TEXT("property_name"), PropertyName);
	FString NodeHandle = ReadHandleArg(Args, NodeHandleKeys);
	if (NodeHandle.IsEmpty()) NodeHandle = TEXT("0");

	HandleSetMaterialNodeValue(MaterialPath, NodeHandle, PropertyName, Args, OutJsonString, OutError);
}

void HandleDeleteMaterialNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString MaterialPath;
	Args->TryGetStringField(TEXT("material_path"), MaterialPath);
	if (MaterialPath.IsEmpty()) { OutError = TEXT("material_path is required."); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("nodes"), ItemsArray))
	{
		// Resolve every handle to a guid up front: deleting by index shifts the indices of the
		// remaining nodes, deleting by guid does not.
		FMaterialGraphTarget Target;
		if (!Target.Load(MaterialPath, OutError)) return;
		Target.EnsureAllGuids();

		BatchToolHelper::FBatchResultBuilder Batch;
		TArray<TPair<int32, FString>> ToDelete;   // (item index, guid)
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			const TSharedPtr<FJsonValue>& Item = (*ItemsArray)[i];
			FString Handle;
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Item is null.")); continue; }
			if (Item->Type == EJson::Number)      Handle = FString::FromInt((int32)Item->AsNumber());
			else if (Item->Type == EJson::String) Handle = Item->AsString();
			else if (Item->Type == EJson::Object)
			{
				const TSharedPtr<FJsonObject>* ItemObj = nullptr;
				if (Item->TryGetObject(ItemObj) && ItemObj) Handle = ReadHandleArg(*ItemObj, NodeHandleKeys);
			}
			if (Handle.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing node_id / node_index.")); continue; }

			FString ResolveError;
			UMaterialExpression* Expr = Target.Resolve(Handle, ResolveError);
			if (!Expr) { Batch.AddFailure(i, ResolveError); continue; }
			ToDelete.Emplace(i, FMaterialGraphTarget::GuidOf(Expr));
		}

		for (const TPair<int32, FString>& Entry : ToDelete)
		{
			FString ItemJson, ItemError;
			HandleDeleteMaterialNode(MaterialPath, Entry.Value, ItemJson, ItemError);
			if (ItemError.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("deleted_node_id"), Entry.Value);
				TSharedPtr<FJsonObject> Parsed;
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ItemJson);
				double DeletedIdx = -1;
				if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid() && Parsed->TryGetNumberField(TEXT("deleted_index"), DeletedIdx))
					Extra->SetNumberField(TEXT("deleted_index"), DeletedIdx);
				Batch.AddSuccess(Entry.Key, Extra);
			}
			else
			{
				Batch.AddFailure(Entry.Key, ItemError);
			}
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString NodeHandle = ReadHandleArg(Args, NodeHandleKeys);
	if (NodeHandle.IsEmpty()) NodeHandle = TEXT("0");
	HandleDeleteMaterialNode(MaterialPath, NodeHandle, OutJsonString, OutError);
}

void HandleSetMaterialPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("properties"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString MatPath = BatchToolHelper::GetItemString(Item, TEXT("material_path"));
			if (MatPath.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing material_path")); continue; }
			FString ItemOut, ItemErr;
			HandleSetMaterialProperty(MatPath, Item, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("material_path"), MatPath);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString MaterialPath;
	Args->TryGetStringField(TEXT("material_path"), MaterialPath);
	HandleSetMaterialProperty(MaterialPath, Args, OutJsonString, OutError);
}

static void CreateMaterialLayerAsset(
	const FString& Name, const FString& SavePath,
	EMaterialFunctionUsage Usage,
	FString& OutJsonString, FString& OutError)
{
	const FString TargetPath = SavePath.IsEmpty() ? TEXT("/Game/Materials/Layers") : SavePath;
	const FString FullPath   = TargetPath + TEXT("/") + Name;

	if (UEditorAssetLibrary::DoesAssetExist(FullPath))
	{
		OutError = FString::Printf(TEXT("Asset already exists at: '%s'"), *FullPath);
		return;
	}

	UMaterialFunctionFactoryNew* Factory = NewObject<UMaterialFunctionFactoryNew>();
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

	UObject* NewAsset = AssetToolsModule.Get().CreateAsset(Name, TargetPath, UMaterialFunction::StaticClass(), Factory);
	UMaterialFunction* MatFunc = Cast<UMaterialFunction>(NewAsset);

	if (!MatFunc)
	{
		OutError = TEXT("Failed to create Material Layer asset.");
		return;
	}

	MatFunc->SetMaterialFunctionUsage(Usage);
	MatFunc->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(MatFunc);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("path"), MatFunc->GetPathName());
	Result->SetStringField(TEXT("usage"),
		Usage == EMaterialFunctionUsage::MaterialLayer ? TEXT("MaterialLayer") : TEXT("MaterialLayerBlend"));
	Result->SetStringField(TEXT("message"),
		FString::Printf(TEXT("Created '%s' at '%s'"), *Name, *TargetPath));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleCreateMaterialLayer(const FString& Name, const FString& SavePath,
	FString& OutJsonString, FString& OutError)
{
	CreateMaterialLayerAsset(Name, SavePath, EMaterialFunctionUsage::MaterialLayer, OutJsonString, OutError);
}

void HandleCreateMaterialLayerBlend(const FString& Name, const FString& SavePath,
	FString& OutJsonString, FString& OutError)
{
	CreateMaterialLayerAsset(Name, SavePath, EMaterialFunctionUsage::MaterialLayerBlend, OutJsonString, OutError);
}

void HandleFindMaterialNode(
	const FString& MaterialPath,
	const FString& Desc,
	const FString& ClassName,
	const FString& ParameterName,
	FString& OutJsonString, FString& OutError)
{
	if (Desc.IsEmpty() && ClassName.IsEmpty() && ParameterName.IsEmpty())
	{
		OutError = TEXT("At least one of: desc, class_name, or parameter_name must be provided.");
		return;
	}

	FMaterialGraphTarget Target;
	if (!Target.Load(MaterialPath, OutError)) return;
	Target.EnsureAllGuids();

	const FString ResolvedClassName = ClassName.IsEmpty() ? TEXT("") : ResolveNodeAlias(ClassName).ToLower();

	TArray<TSharedPtr<FJsonValue>> Matches;
	for (int32 i = 0; i < Target.Num(); i++)
	{
		UMaterialExpression* Expr = Target.Get(i);
		if (!Expr) continue;

		bool bMatch = false;

		if (!Desc.IsEmpty())
		{
			bMatch = bMatch || Expr->Desc.Contains(Desc, ESearchCase::IgnoreCase);
		}

		if (!ResolvedClassName.IsEmpty())
		{
			const FString NodeClassName = Expr->GetClass()->GetName().ToLower();
			bMatch = bMatch || NodeClassName.Contains(ResolvedClassName);
		}

		if (!ParameterName.IsEmpty())
		{
			if (FNameProperty* NameProp = FindFProperty<FNameProperty>(Expr->GetClass(), TEXT("ParameterName")))
			{
				const FName ParamVal = NameProp->GetPropertyValue_InContainer(Expr);
				bMatch = bMatch || ParamVal.ToString().Equals(ParameterName, ESearchCase::IgnoreCase);
			}
		}

		if (!bMatch) continue;

		TArray<FString> NodeCaptions;
		Expr->GetCaption(NodeCaptions);
		const FString NodeCaption = NodeCaptions.Num() > 0 ? NodeCaptions[0] : Expr->GetClass()->GetName();

		TSharedPtr<FJsonObject> Node = MakeShareable(new FJsonObject);
		Node->SetNumberField(TEXT("index"), i);
		Node->SetStringField(TEXT("node_id"), FMaterialGraphTarget::GuidOf(Expr));
		Node->SetStringField(TEXT("class"), Expr->GetClass()->GetName());
		Node->SetStringField(TEXT("caption"), NodeCaption);
		if (!Expr->Desc.IsEmpty()) Node->SetStringField(TEXT("desc"), Expr->Desc);
		Node->SetNumberField(TEXT("pos_x"), Expr->MaterialExpressionEditorX);
		Node->SetNumberField(TEXT("pos_y"), Expr->MaterialExpressionEditorY);

		if (FNameProperty* NameProp = FindFProperty<FNameProperty>(Expr->GetClass(), TEXT("ParameterName")))
		{
			const FName ParamVal = NameProp->GetPropertyValue_InContainer(Expr);
			if (!ParamVal.IsNone())
				Node->SetStringField(TEXT("parameter_name"), ParamVal.ToString());
		}

		Matches.Add(MakeShareable(new FJsonValueObject(Node)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("count"), Matches.Num());
	Result->SetArrayField(TEXT("nodes"), Matches);
	if (Matches.Num() == 0)
		Result->SetStringField(TEXT("message"), TEXT("No nodes matched the search criteria."));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleGetMaterialNodesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString MaterialPath;
	Args->TryGetStringField(TEXT("material_path"), MaterialPath);
	if (MaterialPath.IsEmpty())
	{
		OutError = TEXT("material_path is required.");
		return;
	}
	HandleGetMaterialNodes(MaterialPath, OutJsonString, OutError);
}

void HandleCreateMaterialFunctionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name, SavePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (Name.IsEmpty()) { OutError = TEXT("name is required."); return; }
	HandleCreateMaterialFunction(Name, SavePath, OutJsonString, OutError);
}

void HandleDisconnectMaterialPinFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString MaterialPath, PinName;
	Args->TryGetStringField(TEXT("material_path"), MaterialPath);
	FString NodeHandle = ReadHandleArg(Args, NodeHandleKeys);
	if (NodeHandle.IsEmpty()) NodeHandle = TEXT("0");
	Args->TryGetStringField(TEXT("input_pin_name"), PinName);
	if (PinName.IsEmpty()) Args->TryGetStringField(TEXT("pin_name"), PinName);
	if (PinName.IsEmpty()) Args->TryGetStringField(TEXT("to_input"), PinName);
	HandleDisconnectMaterialPin(MaterialPath, NodeHandle, PinName, OutJsonString, OutError);
}

void HandleMoveMaterialNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString MaterialPath;
	double PosX = -600, PosY = 0;
	Args->TryGetStringField(TEXT("material_path"), MaterialPath);
	FString NodeHandle = ReadHandleArg(Args, NodeHandleKeys);
	if (NodeHandle.IsEmpty()) NodeHandle = TEXT("0");
	Args->TryGetNumberField(TEXT("pos_x"), PosX);
	Args->TryGetNumberField(TEXT("pos_y"), PosY);
	HandleMoveMaterialNode(MaterialPath, NodeHandle, (int32)PosX, (int32)PosY, OutJsonString, OutError);
}

void HandleDuplicateMaterialNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString MaterialPath;
	double OffX = 0, OffY = 250;
	Args->TryGetStringField(TEXT("material_path"), MaterialPath);
	FString NodeHandle = ReadHandleArg(Args, NodeHandleKeys);
	if (NodeHandle.IsEmpty()) NodeHandle = TEXT("0");
	Args->TryGetNumberField(TEXT("offset_x"), OffX);
	Args->TryGetNumberField(TEXT("offset_y"), OffY);
	HandleDuplicateMaterialNode(MaterialPath, NodeHandle, (int32)OffX, (int32)OffY, OutJsonString, OutError);
}

void HandleConnectMaterialNodesBulkFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString MaterialPath;
	Args->TryGetStringField(TEXT("material_path"), MaterialPath);
	const TArray<TSharedPtr<FJsonValue>>* Conns = nullptr;
	if (!Args->TryGetArrayField(TEXT("connections"), Conns) || !Conns || Conns->Num() == 0)
	{
		OutError = TEXT("connections array is required.");
		return;
	}
	HandleConnectMaterialNodesBulk(MaterialPath, *Conns, OutJsonString, OutError);
}

void HandleAutoLayoutMaterialFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString MaterialPath;
	Args->TryGetStringField(TEXT("material_path"), MaterialPath);
	HandleAutoLayoutMaterial(MaterialPath, OutJsonString, OutError);
}

void HandleCreateMaterialLayerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name, SavePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	HandleCreateMaterialLayer(Name, SavePath, OutJsonString, OutError);
}

void HandleCreateMaterialLayerBlendFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name, SavePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	HandleCreateMaterialLayerBlend(Name, SavePath, OutJsonString, OutError);
}

void HandleFindMaterialNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString MaterialPath, Desc, ClassName, ParameterName;
	Args->TryGetStringField(TEXT("material_path"), MaterialPath);
	Args->TryGetStringField(TEXT("desc"), Desc);
	Args->TryGetStringField(TEXT("class_name"), ClassName);
	Args->TryGetStringField(TEXT("parameter_name"), ParameterName);
	HandleFindMaterialNode(MaterialPath, Desc, ClassName, ParameterName, OutJsonString, OutError);
}

void HandleGetMaterialGraphSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString MaterialPath;
	Args->TryGetStringField(TEXT("material_path"), MaterialPath);
	if (MaterialPath.IsEmpty())
	{
		OutError = TEXT("material_path is required.");
		return;
	}

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(MaterialPath);
	if (!LoadedAsset)
	{
		OutError = FString::Printf(TEXT("Failed to load any asset at path: %s."), *MaterialPath);
		return;
	}
	if (LoadedAsset->IsA(UMaterialInstance::StaticClass()))
	{
		OutError = TEXT("The selected asset is a Material Instance, which does not have a node graph.");
		return;
	}

	// {index, node_id, class} for every expression so callers can address nodes by stable id.
	auto BuildNodeIdArray = [](const FMaterialGraphTarget& T) -> TArray<TSharedPtr<FJsonValue>>
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		T.EnsureAllGuids();
		for (int32 i = 0; i < T.Num(); i++)
		{
			UMaterialExpression* E = T.Get(i);
			if (!E) continue;
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("index"), i);
			O->SetStringField(TEXT("node_id"), FMaterialGraphTarget::GuidOf(E));
			FString Short = E->GetClass()->GetName();
			Short.RemoveFromStart(TEXT("MaterialExpression"));
			O->SetStringField(TEXT("class"), Short);
			Arr.Add(MakeShared<FJsonValueObject>(O));
		}
		return Arr;
	};

	if (UMaterialFunction* MatFunc = Cast<UMaterialFunction>(LoadedAsset))
	{
		MatFunc->ConditionalPostLoad();
		FMaterialGraphTarget FuncTarget;
		FuncTarget.Owner = MatFunc;
		FuncTarget.bIsFunction = true;
		FuncTarget.EnsureAllGuids();
		TConstArrayView<TObjectPtr<UMaterialExpression>> FuncExprs = MatFunc->GetExpressions();

		TStringBuilder<4096> Report;
		Report.Appendf(TEXT("--- MATERIAL FUNCTION: %s ---\n\n"), *MatFunc->GetName());

		auto IndexOf = [&](const UMaterialExpression* E) -> int32
		{
			for (int32 i = 0; i < FuncExprs.Num(); i++) if (FuncExprs[i] == E) return i;
			return -1;
		};
		auto Short = [](const UClass* C) { FString N = C ? C->GetName() : FString(); N.RemoveFromStart(TEXT("MaterialExpression")); return N; };

		Report.Append(TEXT("--- Function Inputs ---\n"));
		bool bHasInput = false;
		for (int32 i = 0; i < FuncExprs.Num(); i++)
		{
			if (UMaterialExpressionFunctionInput* In = Cast<UMaterialExpressionFunctionInput>(FuncExprs[i]))
			{
				Report.Appendf(TEXT("  n%d %s (%s)\n"), i,
					In->InputName.IsNone() ? TEXT("(unnamed)") : *In->InputName.ToString(),
					*StaticEnum<EFunctionInputType>()->GetNameStringByValue((int64)In->InputType));
				bHasInput = true;
			}
		}
		if (!bHasInput) Report.Append(TEXT("  (none)\n"));

		Report.Append(TEXT("\n--- Function Outputs ---\n"));
		bool bHasOutput = false;
		for (int32 i = 0; i < FuncExprs.Num(); i++)
		{
			if (UMaterialExpressionFunctionOutput* Out = Cast<UMaterialExpressionFunctionOutput>(FuncExprs[i]))
			{
				FString WiredFrom;
				if (Out->A.Expression) WiredFrom = FString::Printf(TEXT(" <- n%d"), IndexOf(Out->A.Expression));
				else                   WiredFrom = TEXT(" (unconnected)");
				Report.Appendf(TEXT("  n%d %s%s\n"), i,
					Out->OutputName.IsNone() ? TEXT("(unnamed)") : *Out->OutputName.ToString(),
					*WiredFrom);
				bHasOutput = true;
			}
		}
		if (!bHasOutput) Report.Append(TEXT("  (none)\n"));

		Report.Append(TEXT("\n--- Expression Nodes ---\n"));
		for (int32 i = 0; i < FuncExprs.Num(); i++)
		{
			const UMaterialExpression* E = FuncExprs[i];
			if (!E) continue;
			TArray<FString> Captions; E->GetCaption(Captions);
			const FString ShortName = Short(E->GetClass());
			const FString Caption = Captions.Num() > 0 ? Captions[0] : ShortName;
			const FString NodeId = FMaterialGraphTarget::GuidOf(E);
			if (Caption.Equals(ShortName)) Report.Appendf(TEXT("n%d %s id=%s\n"), i, *Caption, *NodeId);
			else                            Report.Appendf(TEXT("n%d %s [%s] id=%s\n"), i, *Caption, *ShortName, *NodeId);

			for (TFieldIterator<FStructProperty> PropIt(E->GetClass(), EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
			{
				if (!PropIt->Struct) continue;
				const FString SN = PropIt->Struct->GetName();
				if (!SN.Contains(TEXT("ExpressionInput")) && !SN.Contains(TEXT("MaterialInput"))) continue;
				const FExpressionInput* In = PropIt->ContainerPtrToValuePtr<FExpressionInput>(E);
				if (In && In->Expression)
					Report.Appendf(TEXT("  .%s<-n%d\n"), *PropIt->GetName(), IndexOf(In->Expression));
			}
		}

		TSharedPtr<FJsonObject> FuncObj = MakeShareable(new FJsonObject);
		FuncObj->SetBoolField(TEXT("success"), true);
		FuncObj->SetStringField(TEXT("summary"), FString(Report));
		FuncObj->SetStringField(TEXT("material_path"), MaterialPath);
		FuncObj->SetStringField(TEXT("asset_class"), TEXT("MaterialFunction"));
		FuncObj->SetArrayField(TEXT("node_ids"), BuildNodeIdArray(FuncTarget));
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(FuncObj.ToSharedRef(), Writer);
		return;
	}

	UMaterial* TargetMaterial = Cast<UMaterial>(LoadedAsset);
	if (!TargetMaterial)
	{
		OutError = FString::Printf(TEXT("The asset at path '%s' is not a base Material or MaterialFunction."), *MaterialPath);
		return;
	}

	FMaterialGraphDescriber Describer;
	FString Summary = Describer.Describe(TargetMaterial);
	if (Summary.IsEmpty())
	{
		OutError = TEXT("The summarizer returned an empty string or an error occurred.");
		return;
	}

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("summary"), Summary);
	Obj->SetStringField(TEXT("material_path"), MaterialPath);
	{
		FMaterialGraphTarget MatTarget;
		MatTarget.Owner = TargetMaterial;
		MatTarget.bIsFunction = false;
		Obj->SetArrayField(TEXT("node_ids"), BuildNodeIdArray(MatTarget));
	}
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
}

void HandleValidateMaterialFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString MaterialPath;
	Args->TryGetStringField(TEXT("material_path"), MaterialPath);
	if (MaterialPath.IsEmpty()) Args->TryGetStringField(TEXT("asset_path"), MaterialPath);
	if (MaterialPath.IsEmpty()) { OutError = TEXT("Missing required parameter: material_path"); return; }

	double CompileTimeoutSeconds = 60.0;
	Args->TryGetNumberField(TEXT("compile_timeout_seconds"), CompileTimeoutSeconds);
	CompileTimeoutSeconds = FMath::Clamp(CompileTimeoutSeconds, 0.0, 600.0);

	FMaterialGraphTarget Target;
	if (!Target.Load(MaterialPath, OutError)) return;
	Target.EnsureAllGuids();

	UMaterial* Mat = Target.AsMaterial();

	TArray<TSharedPtr<FJsonValue>> Issues;
	auto AddIssue = [&](const FString& Severity, const FString& Message)
	{
		TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
		Issue->SetStringField(TEXT("severity"), Severity);
		Issue->SetStringField(TEXT("message"), Message);
		Issues.Add(MakeShared<FJsonValueObject>(Issue));
	};

	const int32 TotalCount = Target.Num();

	TSet<UMaterialExpression*> Reachable;
	TArray<UMaterialExpression*> Queue;
	bool bFuncMissingOutput = false;

	auto EnqueueInput = [&](const FExpressionInput* Input)
	{
		if (Input && Input->Expression && !Reachable.Contains(Input->Expression))
		{
			Reachable.Add(Input->Expression);
			Queue.Add(Input->Expression);
		}
	};

	if (Mat)
	{
		static const EMaterialProperty OutputProps[] = {
			MP_BaseColor, MP_Metallic, MP_Roughness, MP_Specular,
			MP_EmissiveColor, MP_Opacity, MP_OpacityMask, MP_Normal,
			MP_WorldPositionOffset, MP_AmbientOcclusion, MP_Refraction
		};
		for (EMaterialProperty Prop : OutputProps)
		{
			FExpressionInput* Slot = Mat->GetExpressionInputForProperty(Prop);
			EnqueueInput(Slot);
		}
	}
	else
	{
		int32 SeededOutputs = 0;
		for (int32 i = 0; i < TotalCount; i++)
		{
			UMaterialExpression* Expr = Target.Get(i);
			if (Expr && Expr->IsA<UMaterialExpressionFunctionOutput>() && !Reachable.Contains(Expr))
			{
				Reachable.Add(Expr);
				Queue.Add(Expr);
				SeededOutputs++;
			}
		}
		if (SeededOutputs == 0 && TotalCount > 0)
		{
			AddIssue(TEXT("warning"),
				TEXT("MaterialFunction has no FunctionOutput node — nothing is reachable from an output. "
				     "Add a FunctionOutput and wire the graph into it."));
			bFuncMissingOutput = true;
		}
	}

	while (Queue.Num() > 0)
	{
		UMaterialExpression* Expr = Queue.Pop(EAllowShrinking::No);
		for (TFieldIterator<FStructProperty> It(Expr->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			if (!It->Struct) continue;
			const FString SN = It->Struct->GetName();
			if (!SN.Contains(TEXT("ExpressionInput")) && !SN.Contains(TEXT("MaterialInput"))) continue;
			const FExpressionInput* Input = It->ContainerPtrToValuePtr<FExpressionInput>(Expr);
			EnqueueInput(Input);
		}
		if (UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expr))
		{
			for (FCustomInput& CI : Custom->Inputs)
				EnqueueInput(&CI.Input);
		}
	}

	int32 IslandCount = 0;
	for (int32 i = 0; !bFuncMissingOutput && i < TotalCount; i++)
	{
		UMaterialExpression* Expr = Target.Get(i);
		if (Expr && !Reachable.Contains(Expr))
		{
			IslandCount++;
			AddIssue(TEXT("warning"),
				FString::Printf(TEXT("Island node at index %d (%s, node_id=%s) has no path to any material output."),
					i, *Expr->GetClass()->GetName(), *FMaterialGraphTarget::GuidOf(Expr)));
		}
	}

	for (int32 i = 0; i < TotalCount; i++)
	{
		UMaterialExpression* Expr = Target.Get(i);
		if (!Expr) continue;

		if (UMaterialExpressionTextureSample* TS = Cast<UMaterialExpressionTextureSample>(Expr))
		{
			if (!TS->Texture)
				AddIssue(TEXT("error"),
					FString::Printf(TEXT("TextureSample at index %d (node_id=%s) has no Texture assigned."), i, *FMaterialGraphTarget::GuidOf(Expr)));
		}

		if (UMaterialExpressionMaterialFunctionCall* FC = Cast<UMaterialExpressionMaterialFunctionCall>(Expr))
		{
			if (!FC->MaterialFunction)
				AddIssue(TEXT("error"),
					FString::Printf(TEXT("MaterialFunctionCall at index %d (node_id=%s) has no MaterialFunction assigned."), i, *FMaterialGraphTarget::GuidOf(Expr)));
		}
	}

	TMap<FString, TArray<int32>> ScalarParams, VectorParams, TextureParams;
	for (int32 i = 0; i < TotalCount; i++)
	{
		UMaterialExpression* Expr = Target.Get(i);
		if (!Expr) continue;
		FProperty* ParamProp = Expr->GetClass()->FindPropertyByName(FName(TEXT("ParameterName")));
		if (!ParamProp) continue;
		FName ParamName;
		void* ValPtr = ParamProp->ContainerPtrToValuePtr<void>(Expr);
		if (FNameProperty* NP = CastField<FNameProperty>(ParamProp))
			ParamName = NP->GetPropertyValue(ValPtr);
		if (ParamName.IsNone()) continue;
		const FString PN = ParamName.ToString();
		const FString CN = Expr->GetClass()->GetName();
		if (CN.Contains(TEXT("ScalarParameter"))) ScalarParams.FindOrAdd(PN).Add(i);
		else if (CN.Contains(TEXT("VectorParameter"))) VectorParams.FindOrAdd(PN).Add(i);
		else if (CN.Contains(TEXT("TextureSampleParameter"))) TextureParams.FindOrAdd(PN).Add(i);
	}
	for (auto& Pair : ScalarParams)
		if (Pair.Value.Num() > 1)
			AddIssue(TEXT("warning"), FString::Printf(TEXT("Duplicate ScalarParameter name '%s' on nodes %s."),
				*Pair.Key, *FString::JoinBy(Pair.Value, TEXT(", "), [](int32 N){ return FString::FromInt(N); })));
	for (auto& Pair : VectorParams)
		if (Pair.Value.Num() > 1)
			AddIssue(TEXT("warning"), FString::Printf(TEXT("Duplicate VectorParameter name '%s' on nodes %s."),
				*Pair.Key, *FString::JoinBy(Pair.Value, TEXT(", "), [](int32 N){ return FString::FromInt(N); })));
	for (auto& Pair : TextureParams)
		if (Pair.Value.Num() > 1)
			AddIssue(TEXT("warning"), FString::Printf(TEXT("Duplicate TextureParameter name '%s' on nodes %s."),
				*Pair.Key, *FString::JoinBy(Pair.Value, TEXT(", "), [](int32 N){ return FString::FromInt(N); })));

	if (TotalCount > 200)
		AddIssue(TEXT("warning"),
			FString::Printf(TEXT("High expression count (%d). Consider simplifying or splitting into material functions."), TotalCount));

	bool bCompilationPending = false;
	double WaitedSeconds = 0.0;
	if (Mat)
	{
		// Recompile the way the material editor does: inside an FMaterialUpdateContext so
		// dependent instances / components are refreshed once the shader maps come back.
		{
			FMaterialUpdateContext UpdateContext;
			UpdateContext.AddMaterial(Mat);
			Mat->PreEditChange(nullptr);
			Mat->PostEditChange();
		}

		const ERHIFeatureLevel::Type Levels[] = {
			ERHIFeatureLevel::SM6, ERHIFeatureLevel::SM5, ERHIFeatureLevel::ES3_1
		};
		auto CollectResources = [&]() -> TArray<const FMaterialResource*>
		{
			TArray<const FMaterialResource*> Out;
			for (ERHIFeatureLevel::Type Lvl : Levels)
			{
#if UE_VERSION_OLDER_THAN(5,7,0)
				const FMaterialResource* Res = Mat->GetMaterialResource(Lvl);
#else
				const FMaterialResource* Res = Mat->GetMaterialResource(GetFeatureLevelShaderPlatform(Lvl));
#endif
				if (Res) Out.AddUnique(Res);
			}
			return Out;
		};
		auto AllFinished = [&](const TArray<const FMaterialResource*>& Resources) -> bool
		{
			for (const FMaterialResource* Res : Resources)
				if (Res && !Res->IsCompilationFinished()) return false;
			return true;
		};

		TArray<const FMaterialResource*> Resources = CollectResources();
		if (GShaderCompilingManager && !AllFinished(Resources))
		{
			// Block only on this material's shader maps (not the whole project), then pump the
			// async results so finished maps are assigned back to the material. Bounded by
			// compile_timeout_seconds so a stuck compile never hangs the tool.
			TArray<int32> ShaderMapIds;
			for (const FMaterialResource* Res : Resources)
			{
				if (!Res || Res->IsCompilationFinished()) continue;
				if (const FMaterialShaderMap* ShaderMap = Res->GetGameThreadShaderMap())
					ShaderMapIds.AddUnique((int32)ShaderMap->GetCompilingId());
			}
			if (ShaderMapIds.Num() > 0)
			{
				GShaderCompilingManager->FinishCompilation(*Mat->GetName(), ShaderMapIds);
			}
			GShaderCompilingManager->ProcessAsyncResults(/*bLimitExecutionTime*/ false, /*bBlockOnGlobalShaderCompletion*/ false);

			const double StartTime = FPlatformTime::Seconds();
			const double Deadline  = StartTime + CompileTimeoutSeconds;
			Resources = CollectResources();
			while (!AllFinished(Resources) && FPlatformTime::Seconds() < Deadline)
			{
				FPlatformProcess::Sleep(0.05f);
				GShaderCompilingManager->ProcessAsyncResults(false, false);
				Resources = CollectResources();
			}
			WaitedSeconds = FPlatformTime::Seconds() - StartTime;
		}
		bCompilationPending = !AllFinished(Resources);

		TSet<FString> SeenErrors;
		for (const FMaterialResource* Res : Resources)
		{
			if (!Res) continue;
			for (const FString& Err : Res->GetCompileErrors())
			{
				if (SeenErrors.Contains(Err)) continue;
				SeenErrors.Add(Err);
				AddIssue(TEXT("error"),
					FString::Printf(TEXT("Shader compile: %s"), *Err));
			}
		}
	}

	bool bIsValid = true;
	for (const auto& Issue : Issues)
	{
		FString Sev;
		Issue->AsObject()->TryGetStringField(TEXT("severity"), Sev);
		if (Sev == TEXT("error")) { bIsValid = false; break; }
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	if (bCompilationPending)
	{
		// Shader maps are still compiling: errors may still surface, so a clean run cannot be
		// reported as valid yet. Errors already known are reported as-is.
		Result->SetBoolField(TEXT("compilation_pending"), true);
		if (!bIsValid) Result->SetBoolField(TEXT("is_valid"), false);
		Result->SetStringField(TEXT("compilation_note"), FString::Printf(
			TEXT("Shader compilation for this material did not finish within %.0fs (waited %.1fs). Call validate_material again shortly, or pass compile_timeout_seconds to wait longer."),
			CompileTimeoutSeconds, WaitedSeconds));
	}
	else
	{
		Result->SetBoolField(TEXT("is_valid"), bIsValid);
		Result->SetBoolField(TEXT("compilation_pending"), false);
	}
	if (WaitedSeconds > 0.0) Result->SetNumberField(TEXT("compile_wait_seconds"), WaitedSeconds);
	Result->SetStringField(TEXT("material_path"), MaterialPath);
	Result->SetNumberField(TEXT("expression_count"), TotalCount);
	Result->SetNumberField(TEXT("reachable_count"), Reachable.Num());
	Result->SetNumberField(TEXT("island_count"), IslandCount);
	Result->SetNumberField(TEXT("issue_count"), Issues.Num());
	Result->SetArrayField(TEXT("issues"), Issues);
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleGetMaterialCompilationStatsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString MaterialPath;
	Args->TryGetStringField(TEXT("material_path"), MaterialPath);
	if (MaterialPath.IsEmpty()) Args->TryGetStringField(TEXT("asset_path"), MaterialPath);
	if (MaterialPath.IsEmpty()) { OutError = TEXT("Missing required parameter: material_path"); return; }

	FMaterialGraphTarget Target;
	if (!Target.Load(MaterialPath, OutError)) return;

	UMaterial* Mat = Target.AsMaterial();

	const int32 TotalCount = Target.Num();
	int32 SamplerCount = 0, ScalarParamCount = 0, VectorParamCount = 0, TextureParamCount = 0, FuncCallCount = 0;

	for (int32 i = 0; i < TotalCount; i++)
	{
		UMaterialExpression* Expr = Target.Get(i);
		if (!Expr) continue;
		const FString CN = Expr->GetClass()->GetName();
		if (CN.Contains(TEXT("TextureSample")))    SamplerCount++;
		if (CN.Contains(TEXT("ScalarParameter")))  ScalarParamCount++;
		if (CN.Contains(TEXT("VectorParameter")))  VectorParamCount++;
		if (CN.Contains(TEXT("TextureSampleParameter"))) TextureParamCount++;
		if (CN.Contains(TEXT("MaterialFunctionCall")))   FuncCallCount++;
	}

	FString BlendMode   = TEXT("Opaque");
	FString ShadingModel = TEXT("DefaultLit");
	FString Domain      = TEXT("Surface");
	bool bTwoSided      = false;

	if (Mat)
	{
		switch (Mat->BlendMode)
		{
			case BLEND_Opaque:        BlendMode = TEXT("Opaque");        break;
			case BLEND_Masked:        BlendMode = TEXT("Masked");        break;
			case BLEND_Translucent:   BlendMode = TEXT("Translucent");   break;
			case BLEND_Additive:      BlendMode = TEXT("Additive");      break;
			case BLEND_Modulate:      BlendMode = TEXT("Modulate");      break;
			default:                  BlendMode = TEXT("Other");         break;
		}
		switch (Mat->GetShadingModels().GetFirstShadingModel())
		{
			case MSM_DefaultLit:  ShadingModel = TEXT("DefaultLit"); break;
			case MSM_Unlit:       ShadingModel = TEXT("Unlit");      break;
			case MSM_Subsurface:  ShadingModel = TEXT("Subsurface"); break;
			case MSM_ClearCoat:   ShadingModel = TEXT("ClearCoat");  break;
			case MSM_Hair:        ShadingModel = TEXT("Hair");       break;
			case MSM_Eye:         ShadingModel = TEXT("Eye");        break;
			default:              ShadingModel = TEXT("Other");      break;
		}
		switch (Mat->MaterialDomain)
		{
			case MD_Surface:       Domain = TEXT("Surface");       break;
			case MD_DeferredDecal: Domain = TEXT("DeferredDecal"); break;
			case MD_LightFunction: Domain = TEXT("LightFunction"); break;
			case MD_PostProcess:   Domain = TEXT("PostProcess");   break;
			default:               Domain = TEXT("Other");         break;
		}
		bTwoSided = Mat->TwoSided;
	}

	FString ComplexityTier = TEXT("Low");
	if (TotalCount > 200 || SamplerCount > 16) ComplexityTier = TEXT("High");
	else if (TotalCount > 80 || SamplerCount > 8) ComplexityTier = TEXT("Medium");

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("material_path"), MaterialPath);
	Result->SetNumberField(TEXT("expression_count"), TotalCount);
	Result->SetNumberField(TEXT("sampler_count"), SamplerCount);
	Result->SetNumberField(TEXT("scalar_parameter_count"), ScalarParamCount);
	Result->SetNumberField(TEXT("vector_parameter_count"), VectorParamCount);
	Result->SetNumberField(TEXT("texture_parameter_count"), TextureParamCount);
	Result->SetNumberField(TEXT("function_call_count"), FuncCallCount);
	Result->SetStringField(TEXT("blend_mode"), BlendMode);
	Result->SetStringField(TEXT("shading_model"), ShadingModel);
	Result->SetStringField(TEXT("domain"), Domain);
	Result->SetBoolField(TEXT("two_sided"), bTwoSided);
	Result->SetStringField(TEXT("complexity_tier"), ComplexityTier);
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleExportMaterialGraphFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString MaterialPath;
	Args->TryGetStringField(TEXT("material_path"), MaterialPath);
	if (MaterialPath.IsEmpty()) Args->TryGetStringField(TEXT("asset_path"), MaterialPath);
	if (MaterialPath.IsEmpty()) { OutError = TEXT("Missing required parameter: material_path"); return; }

	FMaterialGraphTarget Target;
	if (!Target.Load(MaterialPath, OutError)) return;
	Target.EnsureAllGuids();

	UMaterial* Mat = Target.AsMaterial();

	auto GetConnIdx = [&](const FExpressionInput* Input) -> int32
	{
		if (!Input || !Input->Expression) return -1;
		return Target.IndexOf(Input->Expression);
	};

	TArray<TSharedPtr<FJsonValue>> NodesArray;
	TArray<TSharedPtr<FJsonValue>> ConnectionsArray;

	for (int32 i = 0; i < Target.Num(); i++)
	{
		UMaterialExpression* Expr = Target.Get(i);
		if (!Expr) continue;

		TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
		Node->SetNumberField(TEXT("index"), i);
		Node->SetStringField(TEXT("node_id"), FMaterialGraphTarget::GuidOf(Expr));
		Node->SetStringField(TEXT("class"), Expr->GetClass()->GetName());
		Node->SetNumberField(TEXT("pos_x"), Expr->MaterialExpressionEditorX);
		Node->SetNumberField(TEXT("pos_y"), Expr->MaterialExpressionEditorY);
		if (!Expr->Desc.IsEmpty()) Node->SetStringField(TEXT("desc"), Expr->Desc);

		if (FProperty* PNProp = Expr->GetClass()->FindPropertyByName(FName(TEXT("ParameterName"))))
		{
			void* VP = PNProp->ContainerPtrToValuePtr<void>(Expr);
			if (FNameProperty* NP = CastField<FNameProperty>(PNProp))
			{
				FName PN = NP->GetPropertyValue(VP);
				if (!PN.IsNone()) Node->SetStringField(TEXT("parameter_name"), PN.ToString());
			}
		}
		if (FProperty* DVProp = Expr->GetClass()->FindPropertyByName(FName(TEXT("DefaultValue"))))
		{
			void* VP = DVProp->ContainerPtrToValuePtr<void>(Expr);
			if (FFloatProperty* FP = CastField<FFloatProperty>(DVProp))
				Node->SetNumberField(TEXT("default_value"), FP->GetPropertyValue(VP));
			else if (FDoubleProperty* DP = CastField<FDoubleProperty>(DVProp))
				Node->SetNumberField(TEXT("default_value"), DP->GetPropertyValue(VP));
		}

		NodesArray.Add(MakeShared<FJsonValueObject>(Node));

		auto CollectConnections = [&](const FString& InputPin, const FExpressionInput* Input)
		{
			int32 SrcIdx = GetConnIdx(Input);
			if (SrcIdx < 0) return;
			FString OutputName;
			if (Input->Expression)
			{
				const TArray<FExpressionOutput>& SrcOuts = Input->Expression->GetOutputs();
				if (SrcOuts.IsValidIndex(Input->OutputIndex))
					OutputName = SrcOuts[Input->OutputIndex].OutputName.IsNone() ? TEXT("") : SrcOuts[Input->OutputIndex].OutputName.ToString();
			}
			TSharedPtr<FJsonObject> Conn = MakeShared<FJsonObject>();
			Conn->SetNumberField(TEXT("from_node"), SrcIdx);
			Conn->SetStringField(TEXT("from_node_id"), FMaterialGraphTarget::GuidOf(Input->Expression));
			Conn->SetStringField(TEXT("from_output"), OutputName);
			Conn->SetNumberField(TEXT("to_node"), i);
			Conn->SetStringField(TEXT("to_node_id"), FMaterialGraphTarget::GuidOf(Expr));
			Conn->SetStringField(TEXT("to_input"), InputPin);
			ConnectionsArray.Add(MakeShared<FJsonValueObject>(Conn));
		};

		if (UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expr))
		{
			for (FCustomInput& CI : Custom->Inputs)
				CollectConnections(CI.InputName.ToString(), &CI.Input);
		}
		else
		{
			for (TFieldIterator<FStructProperty> It(Expr->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
			{
				if (!It->Struct) continue;
				const FString SN = It->Struct->GetName();
				if (!SN.Contains(TEXT("ExpressionInput")) && !SN.Contains(TEXT("MaterialInput"))) continue;
				const FExpressionInput* Input = It->ContainerPtrToValuePtr<FExpressionInput>(Expr);
				CollectConnections(It->GetName(), Input);
			}
		}
	}

	TSharedPtr<FJsonObject> MaterialOutputs = MakeShared<FJsonObject>();
	if (Mat)
	{
		static const TPair<const TCHAR*, EMaterialProperty> OutputDefs[] = {
			{ TEXT("BaseColor"),           MP_BaseColor },
			{ TEXT("Metallic"),            MP_Metallic },
			{ TEXT("Roughness"),           MP_Roughness },
			{ TEXT("Specular"),            MP_Specular },
			{ TEXT("EmissiveColor"),       MP_EmissiveColor },
			{ TEXT("Opacity"),             MP_Opacity },
			{ TEXT("OpacityMask"),         MP_OpacityMask },
			{ TEXT("Normal"),              MP_Normal },
			{ TEXT("WorldPositionOffset"), MP_WorldPositionOffset },
			{ TEXT("AmbientOcclusion"),    MP_AmbientOcclusion },
			{ TEXT("Refraction"),          MP_Refraction },
		};
		for (const auto& Def : OutputDefs)
		{
			FExpressionInput* Slot = Mat->GetExpressionInputForProperty(Def.Value);
			int32 ConnIdx = GetConnIdx(Slot);
			if (ConnIdx >= 0)
			{
				FString OutputName;
				if (Slot->Expression)
				{
					const TArray<FExpressionOutput>& SrcOuts = Slot->Expression->GetOutputs();
					if (SrcOuts.IsValidIndex(Slot->OutputIndex))
						OutputName = SrcOuts[Slot->OutputIndex].OutputName.IsNone() ? TEXT("") : SrcOuts[Slot->OutputIndex].OutputName.ToString();
				}
				TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
				SlotObj->SetNumberField(TEXT("node_index"), ConnIdx);
				SlotObj->SetStringField(TEXT("node_id"), FMaterialGraphTarget::GuidOf(Slot->Expression));
				SlotObj->SetStringField(TEXT("output"), OutputName);
				MaterialOutputs->SetObjectField(Def.Key, SlotObj);
			}
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("material_path"), MaterialPath);
	Result->SetNumberField(TEXT("node_count"), Target.Num());
	Result->SetArrayField(TEXT("nodes"), NodesArray);
	Result->SetArrayField(TEXT("connections"), ConnectionsArray);
	Result->SetObjectField(TEXT("material_outputs"), MaterialOutputs);
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleImportMaterialGraphFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString MaterialPath, Mode;
	Args->TryGetStringField(TEXT("material_path"), MaterialPath);
	if (MaterialPath.IsEmpty()) Args->TryGetStringField(TEXT("asset_path"), MaterialPath);
	if (MaterialPath.IsEmpty()) { OutError = TEXT("Missing required parameter: material_path"); return; }
	Args->TryGetStringField(TEXT("mode"), Mode);
	if (Mode.IsEmpty()) Mode = TEXT("overwrite");

	const TArray<TSharedPtr<FJsonValue>>* NodesArray    = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* ConnsArray    = nullptr;
	Args->TryGetArrayField(TEXT("nodes"),       NodesArray);
	Args->TryGetArrayField(TEXT("connections"), ConnsArray);

	if (!NodesArray || NodesArray->Num() == 0)
	{
		OutError = TEXT("Missing required parameter: nodes[]");
		return;
	}

	FMaterialGraphTarget Target;
	if (!Target.Load(MaterialPath, OutError)) return;

	const FScopedTransaction Transaction(FText::FromString(TEXT("Axivor: Import Material Graph")));

	if (Mode.Equals(TEXT("overwrite"), ESearchCase::IgnoreCase))
	{
		UMaterial* Mat = Target.AsMaterial();
		if (Mat)
		{
			HandleAutoLayoutMaterial(MaterialPath, OutJsonString, OutError);
			if (UMaterialEditorOnlyData* ED = Mat->GetEditorOnlyData())
			{
				Mat->Modify();
				ED->Modify();
				ED->ExpressionCollection.Empty();
				Mat->MarkPackageDirty();
			}
		}
		OutJsonString.Reset();
		OutError.Reset();
		if (!Target.Load(MaterialPath, OutError)) return;
	}

	const int32 XOffset = Mode.Equals(TEXT("merge"), ESearchCase::IgnoreCase) ? 500 : 0;

	// Source handle (index string and/or exported node_id) -> new node_id.
	TMap<FString, FString> HandleMap;
	int32 TotalAdded = 0, TotalFailed = 0;

	for (const auto& NodeVal : *NodesArray)
	{
		TSharedPtr<FJsonObject> NodeObj = NodeVal.IsValid() ? NodeVal->AsObject() : nullptr;
		if (!NodeObj.IsValid()) { TotalFailed++; continue; }

		int32 SrcIndex = 0;
		double PosX = 0, PosY = 0;
		FString NodeClass, SrcNodeId;
		NodeObj->TryGetStringField(TEXT("class"), NodeClass);
		NodeObj->TryGetNumberField(TEXT("index"), SrcIndex);
		NodeObj->TryGetStringField(TEXT("node_id"), SrcNodeId);
		NodeObj->TryGetNumberField(TEXT("pos_x"), PosX);
		NodeObj->TryGetNumberField(TEXT("pos_y"), PosY);

		FString NodeType = NodeClass;
		if (NodeType.StartsWith(TEXT("MaterialExpression")))
			NodeType.RemoveFromStart(TEXT("MaterialExpression"));

		TSharedPtr<FJsonObject> AddArgs = MakeShared<FJsonObject>();
		AddArgs->SetStringField(TEXT("material_path"), MaterialPath);
		AddArgs->SetStringField(TEXT("node_type"), NodeType);
		AddArgs->SetNumberField(TEXT("pos_x"), PosX + XOffset);
		AddArgs->SetNumberField(TEXT("pos_y"), PosY);
		if (NodeObj->HasField(TEXT("desc")))
			AddArgs->SetStringField(TEXT("desc"), NodeObj->GetStringField(TEXT("desc")));

		FString AddOut, AddErr;
		HandleAddMaterialNode(MaterialPath, NodeType, (int32)(PosX + XOffset), (int32)PosY,
			NodeObj->HasField(TEXT("desc")) ? NodeObj->GetStringField(TEXT("desc")) : TEXT(""),
			AddOut, AddErr);

		if (!AddErr.IsEmpty()) { TotalFailed++; continue; }

		TSharedPtr<FJsonObject> AddResult;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(AddOut);
		if (FJsonSerializer::Deserialize(Reader, AddResult) && AddResult.IsValid())
		{
			FString NewNodeId;
			AddResult->TryGetStringField(TEXT("node_id"), NewNodeId);
			if (!NewNodeId.IsEmpty())
			{
				HandleMap.Add(FString::FromInt(SrcIndex), NewNodeId);
				if (!SrcNodeId.IsEmpty()) HandleMap.Add(SrcNodeId, NewNodeId);
				TotalAdded++;

				if (NodeObj->HasField(TEXT("parameter_name")))
				{
					TSharedPtr<FJsonObject> ValJson = MakeShared<FJsonObject>();
					ValJson->SetStringField(TEXT("string_value"), NodeObj->GetStringField(TEXT("parameter_name")));
					FString ValOut, ValErr;
					HandleSetMaterialNodeValue(MaterialPath, NewNodeId, TEXT("ParameterName"), ValJson, ValOut, ValErr);
				}

				if (NodeObj->HasField(TEXT("default_value")))
				{
					TSharedPtr<FJsonObject> ValJson = MakeShared<FJsonObject>();
					ValJson->SetNumberField(TEXT("float_value"), NodeObj->GetNumberField(TEXT("default_value")));
					FString ValOut, ValErr;
					HandleSetMaterialNodeValue(MaterialPath, NewNodeId, TEXT("DefaultValue"), ValJson, ValOut, ValErr);
				}
			}
			else { TotalFailed++; }
		}
		else { TotalFailed++; }
	}

	int32 ConnAdded = 0, ConnFailed = 0;
	TArray<TSharedPtr<FJsonValue>> ConnErrors;
	if (ConnsArray)
	{
		for (const auto& ConnVal : *ConnsArray)
		{
			TSharedPtr<FJsonObject> ConnObj = ConnVal.IsValid() ? ConnVal->AsObject() : nullptr;
			if (!ConnObj.IsValid()) { ConnFailed++; continue; }

			FString FromOutput, ToInput;
			ConnObj->TryGetStringField(TEXT("from_output"), FromOutput);
			ConnObj->TryGetStringField(TEXT("to_input"),    ToInput);
			const FString SrcHandle = ReadHandleArg(ConnObj, FromNodeKeys);
			const FString DstHandle = ReadHandleArg(ConnObj, ToNodeKeys);

			const FString* NewSrc = HandleMap.Find(SrcHandle);
			const FString* NewDst = HandleMap.Find(DstHandle);
			if (!NewSrc || !NewDst)
			{
				ConnFailed++;
				ConnErrors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%s.%s -> %s.%s: source or target node was not imported"), *SrcHandle, *FromOutput, *DstHandle, *ToInput)));
				continue;
			}

			FString ConnOut, ConnErr;
			HandleConnectMaterialNodes(MaterialPath, *NewSrc, FromOutput, *NewDst, ToInput, ConnOut, ConnErr);
			if (ConnErr.IsEmpty()) ConnAdded++;
			else { ConnFailed++; ConnErrors.Add(MakeShared<FJsonValueString>(ConnErr)); }
		}
	}

	// Rebind material output slots from an exported spec ({"BaseColor": {"node_index": 3, "node_id": "...", "output": ""}}).
	int32 OutputsBound = 0, OutputsFailed = 0;
	const TSharedPtr<FJsonObject>* MatOutputsObj = nullptr;
	if (Args->TryGetObjectField(TEXT("material_outputs"), MatOutputsObj) && MatOutputsObj && MatOutputsObj->IsValid() && !Target.IsFunction())
	{
		for (const auto& Pair : (*MatOutputsObj)->Values)
		{
			const TSharedPtr<FJsonObject>* SlotObj = nullptr;
			if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(SlotObj) || !SlotObj) { OutputsFailed++; continue; }
			const FString SrcHandle = ReadHandleArg(*SlotObj, NodeHandleKeys);
			FString OutputName;
			(*SlotObj)->TryGetStringField(TEXT("output"), OutputName);
			const FString* NewSrc = HandleMap.Find(SrcHandle);
			if (!NewSrc) { OutputsFailed++; continue; }
			FString ConnOut, ConnErr;
			HandleConnectMaterialNodes(MaterialPath, *NewSrc, OutputName, FString(), FString(Pair.Key), ConnOut, ConnErr);
			if (ConnErr.IsEmpty()) OutputsBound++;
			else { OutputsFailed++; ConnErrors.Add(MakeShared<FJsonValueString>(ConnErr)); }
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), TotalFailed == 0);
	Result->SetStringField(TEXT("material_path"), MaterialPath);
	Result->SetStringField(TEXT("mode"), Mode);
	Result->SetNumberField(TEXT("nodes_added"), TotalAdded);
	Result->SetNumberField(TEXT("nodes_failed"), TotalFailed);
	Result->SetNumberField(TEXT("connections_added"), ConnAdded);
	Result->SetNumberField(TEXT("connections_failed"), ConnFailed);
	Result->SetNumberField(TEXT("material_outputs_bound"), OutputsBound);
	Result->SetNumberField(TEXT("material_outputs_failed"), OutputsFailed);
	if (ConnErrors.Num() > 0) Result->SetArrayField(TEXT("connection_errors"), ConnErrors);
	{
		TSharedPtr<FJsonObject> IdMapJson = MakeShared<FJsonObject>();
		for (const auto& Pair : HandleMap) IdMapJson->SetStringField(Pair.Key, Pair.Value);
		Result->SetObjectField(TEXT("node_id_map"), IdMapJson);
	}
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleRejectMaterialParameterAliasFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJsonString, FString& OutError)
{
	static const FString Msg = TEXT("This action does not exist. To add a named parameter node: use add_material_node(node_type='ScalarParameter'|'VectorParameter'|'TextureParameter', ...) then set_material_node_value to set ParameterName and DefaultValue. See the material section of get_tool_docs for full details.");
	OutError = Msg;
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), false);
	Obj->SetStringField(TEXT("error"), Msg);
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
}

}
