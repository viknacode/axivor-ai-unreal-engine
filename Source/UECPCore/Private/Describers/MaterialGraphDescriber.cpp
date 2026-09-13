// Copyright 2026, BlueprintsLab, All rights reserved

#include "Describers/MaterialGraphDescriber.h"

#if WITH_EDITOR
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Engine/Texture.h"
#endif

FString FMaterialGraphDescriber::Describe(UMaterial* Material)
{
#if WITH_EDITOR
	if (!Material)
	{
		return TEXT("Invalid Material provided.");
	}

	Material->ConditionalPostLoad();

	TStringBuilder<8192> Report;
	Report.Appendf(TEXT("--- MATERIAL SUMMARY: %s ---\n\n"), *Material->GetName());

	TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions = Material->GetExpressions();

	if (Expressions.Num() == 0)
	{
		return TEXT("This material has no expression nodes in its graph.");
	}

	auto GetConnectedIdx = [&](const UMaterialExpression* Expr) -> int32
	{
		for (int32 i = 0; i < Expressions.Num(); i++)
		{
			if (Expressions[i] == Expr) return i;
		}
		return -1;
	};

	Report.Append(TEXT("--- Material Output Connections ---\n"));
	struct FMatOutputDef { const TCHAR* Name; EMaterialProperty Prop; };
	static const FMatOutputDef MatOutputDefs[] = {
		{ TEXT("BaseColor"),           MP_BaseColor },
		{ TEXT("Metallic"),            MP_Metallic },
		{ TEXT("Roughness"),           MP_Roughness },
		{ TEXT("EmissiveColor"),       MP_EmissiveColor },
		{ TEXT("Opacity"),             MP_Opacity },
		{ TEXT("OpacityMask"),         MP_OpacityMask },
		{ TEXT("Normal"),              MP_Normal },
		{ TEXT("AmbientOcclusion"),    MP_AmbientOcclusion },
		{ TEXT("WorldPositionOffset"), MP_WorldPositionOffset },
	};

	auto ShortClass = [](const UClass* C) -> FString
	{
		FString Name = C ? C->GetName() : FString();
		Name.RemoveFromStart(TEXT("MaterialExpression"));
		return Name;
	};

	bool bAnyOutputConnected = false;
	TArray<FString> UnconnectedSlots;
	for (const FMatOutputDef& Def : MatOutputDefs)
	{
		const FExpressionInput* Slot = Material->GetExpressionInputForProperty(Def.Prop);
		if (Slot && Slot->Expression)
		{
			Report.Appendf(TEXT("  %s -> n%d\n"), Def.Name, GetConnectedIdx(Slot->Expression));
			bAnyOutputConnected = true;
		}
		else
		{
			UnconnectedSlots.Add(FString(Def.Name));
		}
	}
	if (!bAnyOutputConnected)
	{
		Report.Append(TEXT("  (none connected)\n"));
	}
	if (UnconnectedSlots.Num() > 0)
	{
		Report.Appendf(TEXT("  Unconnected: %s\n"), *FString::Join(UnconnectedSlots, TEXT(", ")));
	}
	Report.Append(TEXT("\n"));

	Report.Append(TEXT("--- Expression Nodes ---\n"));
	for (int32 i = 0; i < Expressions.Num(); i++)
	{
		const UMaterialExpression* Expression = Expressions[i];
		if (!Expression) continue;

		TArray<FString> Captions;
		Expression->GetCaption(Captions);
		const FString ShortName = ShortClass(Expression->GetClass());
		const FString NodeCaption = Captions.Num() > 0 ? Captions[0] : ShortName;
		if (NodeCaption.Equals(ShortName))
			Report.Appendf(TEXT("n%d %s\n"), i, *NodeCaption);
		else
			Report.Appendf(TEXT("n%d %s [%s]\n"), i, *NodeCaption, *ShortName);

		if (const UMaterialExpressionTextureSample* TextureSampler = Cast<const UMaterialExpressionTextureSample>(Expression))
		{
			Report.Appendf(TEXT("  Texture: %s\n"),
				TextureSampler->Texture ? *TextureSampler->Texture->GetName() : TEXT("None"));
		}

		TArray<FString> UnconnectedInputs;
		for (TFieldIterator<FStructProperty> PropIt(Expression->GetClass(), EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
		{
			if (!PropIt->Struct) continue;
			const FString StructName = PropIt->Struct->GetName();
			if (!StructName.Contains(TEXT("ExpressionInput")) && !StructName.Contains(TEXT("MaterialInput"))) continue;

			const FExpressionInput* Input = PropIt->ContainerPtrToValuePtr<FExpressionInput>(Expression);
			if (Input && Input->Expression)
			{
				Report.Appendf(TEXT("  .%s<-n%d\n"), *PropIt->GetName(), GetConnectedIdx(Input->Expression));
			}
			else
			{
				UnconnectedInputs.Add(PropIt->GetName());
			}
		}
		if (UnconnectedInputs.Num() > 0)
		{
			Report.Appendf(TEXT("  Unconnected: %s\n"), *FString::Join(UnconnectedInputs, TEXT(", ")));
		}
	}

	return FString(Report);
#else
	return TEXT("Material analysis is only available in an editor build.");
#endif
}
