// Copyright 2026, BlueprintsLab, All rights reserved

#include "CppCodegen.h"

namespace CppCodegen
{

namespace
{
	FString StripTypePrefix(const FString& In)
	{
		if (In.Len() > 1 && (In[0] == TEXT('U') || In[0] == TEXT('A') ||
		                     In[0] == TEXT('F') || In[0] == TEXT('E') ||
		                     In[0] == TEXT('I') || In[0] == TEXT('S')))
		{
			if (In.Len() > 1 && FChar::IsUpper(In[1]))
			{
				return In.Mid(1);
			}
		}
		return In;
	}

	FString SanitizeMacroValue(const FString& In)
	{
		FString Out = In;
		Out.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Out.ReplaceInline(TEXT("\""), TEXT("\\\""));
		Out.ReplaceInline(TEXT("\r"), TEXT(""));
		Out.ReplaceInline(TEXT("\n"), TEXT(" "));
		return Out.TrimStartAndEnd();
	}

	FString DefaultUClassSpecifiers(const FUClassSpec& S)
	{
		TArray<FString> Out;
		if (S.bAbstract)      Out.Add(TEXT("Abstract"));
		if (S.bBlueprintable) Out.Add(TEXT("Blueprintable"));
		if (S.bBlueprintType) Out.Add(TEXT("BlueprintType"));
		if (S.bMinimalAPI)    Out.Add(TEXT("MinimalAPI"));

		if (S.bIsActorComponent)
		{
			Out.Add(TEXT("ClassGroup=(Custom)"));
		}

		TArray<FString> Meta;
		if (!S.DisplayName.IsEmpty())
		{
			Meta.Add(FString::Printf(TEXT("DisplayName=\"%s\""), *SanitizeMacroValue(S.DisplayName)));
		}
		if (S.bIsActorComponent && !S.bAbstract)
		{
			Meta.Add(TEXT("BlueprintSpawnableComponent"));
		}
		if (Meta.Num() > 0)
		{
			Out.Add(FString::Printf(TEXT("meta=(%s)"), *FString::Join(Meta, TEXT(", "))));
		}

		return FString::Join(Out, TEXT(", "));
	}

	FString FormatDocBlock(const FString& Description, int32 IndentTabs = 0)
	{
		if (Description.IsEmpty()) return FString();
		const FString Indent = FString::ChrN(IndentTabs, TEXT('\t'));
		TArray<FString> Lines;
		Description.ParseIntoArrayLines(Lines,  false);
		FString Out;
		Out += Indent + TEXT("/**\n");
		for (const FString& L : Lines)
		{
			FString Trimmed = L.TrimStart().TrimEnd();
			if (Trimmed.StartsWith(TEXT("// ")))    Trimmed.RightChopInline(3, EAllowShrinking::No);
			else if (Trimmed.StartsWith(TEXT("//"))) Trimmed.RightChopInline(2, EAllowShrinking::No);
			else if (Trimmed.StartsWith(TEXT("* ")))  Trimmed.RightChopInline(2, EAllowShrinking::No);
			else if (Trimmed.StartsWith(TEXT("*")))   Trimmed.RightChopInline(1, EAllowShrinking::No);
			Out += Indent + TEXT(" * ") + Trimmed + TEXT("\n");
		}
		Out += Indent + TEXT(" */\n");
		return Out;
	}

	FString StripCommentPrefix(const FString& In)
	{
		FString Out = In.TrimStart();
		if (Out.StartsWith(TEXT("/**"))) Out.RightChopInline(3, EAllowShrinking::No);
		else if (Out.StartsWith(TEXT("/*"))) Out.RightChopInline(2, EAllowShrinking::No);
		else if (Out.StartsWith(TEXT("//"))) Out.RightChopInline(2, EAllowShrinking::No);
		Out = Out.TrimEnd();
		if (Out.EndsWith(TEXT("*/"))) Out.LeftChopInline(2, EAllowShrinking::No);
		return Out.TrimStartAndEnd();
	}

	FString CommentLine(const FString& Comment, int32 IndentTabs = 0)
	{
		const FString Cleaned = StripCommentPrefix(Comment);
		if (Cleaned.IsEmpty()) return FString();
		const FString Indent = FString::ChrN(IndentTabs, TEXT('\t'));
		if (Cleaned.Contains(TEXT("\n")))
		{
			return FormatDocBlock(Cleaned, IndentTabs);
		}
		return Indent + TEXT("/** ") + Cleaned + TEXT(" */\n");
	}
}

FString EmitUClassHeader(const FUClassSpec& Spec)
{
	const FString FileBase     = StripTypePrefix(Spec.ClassName);
	const FString GeneratedInc = FileBase + TEXT(".generated.h");
	const FString Specifiers   = DefaultUClassSpecifiers(Spec);

	FString Out;
	Out += TEXT("// Copyright 2026, All rights reserved\n");
	Out += TEXT("\n");
	Out += TEXT("#pragma once\n");
	Out += TEXT("\n");
	Out += TEXT("#include \"CoreMinimal.h\"\n");

	if      (Spec.bIsActor || Spec.ParentClass == TEXT("AActor"))
	{
		Out += TEXT("#include \"GameFramework/Actor.h\"\n");
	}
	else if (Spec.bIsActorComponent || Spec.ParentClass == TEXT("UActorComponent"))
	{
		Out += TEXT("#include \"Components/ActorComponent.h\"\n");
	}
	else if (Spec.ParentClass == TEXT("USceneComponent"))
	{
		Out += TEXT("#include \"Components/SceneComponent.h\"\n");
	}
	else if (Spec.ParentClass == TEXT("UObject"))
	{
		Out += TEXT("#include \"UObject/Object.h\"\n");
	}
	else if (Spec.ParentClass == TEXT("APawn"))
	{
		Out += TEXT("#include \"GameFramework/Pawn.h\"\n");
	}
	else if (Spec.ParentClass == TEXT("ACharacter"))
	{
		Out += TEXT("#include \"GameFramework/Character.h\"\n");
	}
	else if (Spec.ParentClass == TEXT("APlayerController"))
	{
		Out += TEXT("#include \"GameFramework/PlayerController.h\"\n");
	}
	else if (Spec.ParentClass == TEXT("UGameInstance"))
	{
		Out += TEXT("#include \"Engine/GameInstance.h\"\n");
	}
	else if (Spec.ParentClass == TEXT("UUserWidget"))
	{
		Out += TEXT("#include \"Blueprint/UserWidget.h\"\n");
	}

	Out += FString::Printf(TEXT("#include \"%s\"\n"), *GeneratedInc);
	Out += TEXT("\n");

	if (!Spec.Description.IsEmpty())
	{
		Out += FormatDocBlock(Spec.Description);
	}

	Out += FString::Printf(TEXT("UCLASS(%s)\n"), *Specifiers);
	if (!Spec.ApiMacro.IsEmpty())
	{
		Out += FString::Printf(TEXT("class %s %s : public %s\n"), *Spec.ApiMacro, *Spec.ClassName, *Spec.ParentClass);
	}
	else
	{
		Out += FString::Printf(TEXT("class %s : public %s\n"), *Spec.ClassName, *Spec.ParentClass);
	}
	Out += TEXT("{\n");
	Out += TEXT("\tGENERATED_BODY()\n");

	if (!Spec.bAbstract)
	{
		Out += TEXT("\n");
		Out += TEXT("public:\n");
		Out += FString::Printf(TEXT("\t%s();\n"), *Spec.ClassName);

		if (Spec.bIsActor)
		{
			Out += TEXT("\n");
			Out += TEXT("protected:\n");
			Out += TEXT("\tvirtual void BeginPlay() override;\n");
			Out += TEXT("\n");
			Out += TEXT("public:\n");
			Out += TEXT("\tvirtual void Tick(float DeltaTime) override;\n");
		}
		else if (Spec.bIsActorComponent)
		{
			Out += TEXT("\n");
			Out += TEXT("protected:\n");
			Out += TEXT("\tvirtual void BeginPlay() override;\n");
			Out += TEXT("\n");
			Out += TEXT("public:\n");
			Out += TEXT("\tvirtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;\n");
		}
	}
	else if (Spec.Properties.Num() > 0 || Spec.Functions.Num() > 0)
	{
		Out += TEXT("\n");
		Out += TEXT("public:\n");
	}

	for (const FAddUPropertySpec& P : Spec.Properties)
	{
		Out += TEXT("\n");
		Out += EmitUProperty(P);
	}
	for (const FAddUFunctionSpec& F : Spec.Functions)
	{
		Out += TEXT("\n");
		Out += EmitUFunctionDeclaration(F);
	}

	Out += TEXT("};\n");
	return Out;
}

FString EmitUClassSource(const FUClassSpec& Spec)
{
	const FString FileBase = StripTypePrefix(Spec.ClassName);
	const FString HeaderInc = FileBase + TEXT(".h");

	FString Out;
	Out += TEXT("// Copyright 2026, All rights reserved\n");
	Out += TEXT("\n");
	Out += FString::Printf(TEXT("#include \"%s\"\n"), *HeaderInc);

	if (!Spec.ModuleSubfolder.IsEmpty())
	{
		Out = FString::Printf(TEXT("// Copyright 2026, All rights reserved\n\n#include \"%s/%s\"\n"),
			*Spec.ModuleSubfolder, *HeaderInc);
	}

	Out += TEXT("\n");

	if (Spec.bAbstract)
	{
		Out += FString::Printf(TEXT("// %s is abstract - subclasses provide the implementation.\n"), *Spec.ClassName);
		return Out;
	}

	Out += FString::Printf(TEXT("%s::%s()\n"), *Spec.ClassName, *Spec.ClassName);
	Out += TEXT("{\n");
	if (Spec.bIsActorComponent)
	{
		Out += TEXT("\t// Set this component to tick every frame. Off by default for performance —\n");
		Out += TEXT("\t// flip to true when you actually need TickComponent().\n");
		Out += TEXT("\tPrimaryComponentTick.bCanEverTick = false;\n");
	}
	else if (Spec.bIsActor)
	{
		Out += TEXT("\tPrimaryActorTick.bCanEverTick = true;\n");
	}
	Out += TEXT("}\n");

	if (Spec.bIsActor)
	{
		Out += TEXT("\n");
		Out += FString::Printf(TEXT("void %s::BeginPlay()\n{\n\tSuper::BeginPlay();\n}\n"), *Spec.ClassName);
		Out += TEXT("\n");
		Out += FString::Printf(TEXT("void %s::Tick(float DeltaTime)\n{\n\tSuper::Tick(DeltaTime);\n}\n"), *Spec.ClassName);
	}
	else if (Spec.bIsActorComponent)
	{
		Out += TEXT("\n");
		Out += FString::Printf(TEXT("void %s::BeginPlay()\n{\n\tSuper::BeginPlay();\n}\n"), *Spec.ClassName);
		Out += TEXT("\n");
		Out += FString::Printf(TEXT("void %s::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)\n"
			"{\n\tSuper::TickComponent(DeltaTime, TickType, ThisTickFunction);\n}\n"), *Spec.ClassName);
	}

	for (const FAddUFunctionSpec& F : Spec.Functions)
	{
		Out += TEXT("\n");
		Out += EmitUFunctionDefinition(Spec.ClassName, F);
	}

	return Out;
}

FString EmitUStructHeader(const FUStructSpec& Spec)
{
	const FString FileBase     = StripTypePrefix(Spec.StructName);
	const FString GeneratedInc = FileBase + TEXT(".generated.h");

	FString Out;
	Out += TEXT("// Copyright 2026, All rights reserved\n");
	Out += TEXT("\n");
	Out += TEXT("#pragma once\n");
	Out += TEXT("\n");
	Out += TEXT("#include \"CoreMinimal.h\"\n");
	Out += FString::Printf(TEXT("#include \"%s\"\n"), *GeneratedInc);
	Out += TEXT("\n");

	if (!Spec.Description.IsEmpty()) Out += FormatDocBlock(Spec.Description);

	const FString StructSpec = Spec.bBlueprintType ? TEXT("BlueprintType") : FString();
	Out += FString::Printf(TEXT("USTRUCT(%s)\n"), *StructSpec);
	if (!Spec.ApiMacro.IsEmpty())
	{
		Out += FString::Printf(TEXT("struct %s %s\n"), *Spec.ApiMacro, *Spec.StructName);
	}
	else
	{
		Out += FString::Printf(TEXT("struct %s\n"), *Spec.StructName);
	}
	Out += TEXT("{\n");
	Out += TEXT("\tGENERATED_BODY()\n");

	for (const FUStructMemberSpec& M : Spec.Members)
	{
		Out += TEXT("\n");
		if (!M.Comment.IsEmpty()) Out += CommentLine(M.Comment, 1);

		const FString Spec_ = M.Specifiers.IsEmpty()
			? FString(TEXT("EditAnywhere, BlueprintReadWrite, Category = \"Default\""))
			: M.Specifiers;
		Out += FString::Printf(TEXT("\tUPROPERTY(%s)\n"), *Spec_);
		if (M.DefaultValue.IsEmpty())
		{
			Out += FString::Printf(TEXT("\t%s %s;\n"), *M.Type, *M.Name);
		}
		else
		{
			Out += FString::Printf(TEXT("\t%s %s = %s;\n"), *M.Type, *M.Name, *M.DefaultValue);
		}
	}

	Out += TEXT("};\n");
	return Out;
}

FString EmitUEnumHeader(const FUEnumSpec& Spec)
{
	const FString FileBase     = StripTypePrefix(Spec.EnumName);
	const FString GeneratedInc = FileBase + TEXT(".generated.h");
	const FString Underlying   = Spec.UnderlyingType.IsEmpty() ? FString(TEXT("uint8")) : Spec.UnderlyingType;

	FString Out;
	Out += TEXT("// Copyright 2026, All rights reserved\n");
	Out += TEXT("\n");
	Out += TEXT("#pragma once\n");
	Out += TEXT("\n");
	Out += TEXT("#include \"CoreMinimal.h\"\n");
	Out += FString::Printf(TEXT("#include \"%s\"\n"), *GeneratedInc);
	Out += TEXT("\n");

	if (!Spec.Description.IsEmpty()) Out += FormatDocBlock(Spec.Description);

	const FString EnumSpec = Spec.bBlueprintType ? TEXT("BlueprintType") : FString();
	Out += FString::Printf(TEXT("UENUM(%s)\n"), *EnumSpec);
	Out += FString::Printf(TEXT("enum class %s : %s\n"), *Spec.EnumName, *Underlying);
	Out += TEXT("{\n");

	for (int32 i = 0; i < Spec.Values.Num(); ++i)
	{
		const FUEnumValueSpec& V = Spec.Values[i];
		if (!V.Comment.IsEmpty()) Out += CommentLine(V.Comment, 1);

		FString Suffix;
		if (!V.DisplayName.IsEmpty())
		{
			Suffix = FString::Printf(TEXT(" UMETA(DisplayName = \"%s\")"), *V.DisplayName);
		}
		Out += FString::Printf(TEXT("\t%s%s%s\n"),
			*V.Name, *Suffix, (i + 1 < Spec.Values.Num()) ? TEXT(",") : TEXT(""));
	}

	Out += TEXT("};\n");
	return Out;
}

FString EmitUInterfaceHeader(const FUInterfaceSpec& Spec)
{
	const FString IfaceName    = Spec.InterfaceName;
	const FString UInterfaceName = IfaceName.Len() > 0 && IfaceName[0] == TEXT('I')
		? TEXT("U") + IfaceName.Mid(1)
		: TEXT("U") + IfaceName;
	const FString FileBase     = StripTypePrefix(IfaceName);
	const FString GeneratedInc = FileBase + TEXT(".generated.h");

	FString Out;
	Out += TEXT("// Copyright 2026, All rights reserved\n");
	Out += TEXT("\n");
	Out += TEXT("#pragma once\n");
	Out += TEXT("\n");
	Out += TEXT("#include \"CoreMinimal.h\"\n");
	Out += TEXT("#include \"UObject/Interface.h\"\n");
	Out += FString::Printf(TEXT("#include \"%s\"\n"), *GeneratedInc);
	Out += TEXT("\n");

	const FString IfSpec = Spec.bMinimalAPI ? TEXT("MinimalAPI, Blueprintable") : TEXT("Blueprintable");
	Out += FString::Printf(TEXT("UINTERFACE(%s)\n"), *IfSpec);
	if (!Spec.ApiMacro.IsEmpty())
	{
		Out += FString::Printf(TEXT("class %s %s : public UInterface\n"), *Spec.ApiMacro, *UInterfaceName);
	}
	else
	{
		Out += FString::Printf(TEXT("class %s : public UInterface\n"), *UInterfaceName);
	}
	Out += TEXT("{\n");
	Out += TEXT("\tGENERATED_BODY()\n");
	Out += TEXT("};\n");
	Out += TEXT("\n");

	if (!Spec.Description.IsEmpty()) Out += FormatDocBlock(Spec.Description);

	if (!Spec.ApiMacro.IsEmpty())
	{
		Out += FString::Printf(TEXT("class %s %s\n"), *Spec.ApiMacro, *IfaceName);
	}
	else
	{
		Out += FString::Printf(TEXT("class %s\n"), *IfaceName);
	}
	Out += TEXT("{\n");
	Out += TEXT("\tGENERATED_BODY()\n");
	Out += TEXT("\n");
	Out += TEXT("public:\n");

	for (const FUInterfaceMethodSpec& M : Spec.Methods)
	{
		const FString Ret  = M.ReturnType.IsEmpty() ? FString(TEXT("void")) : M.ReturnType;
		const FString Spec_ = M.Specifiers.IsEmpty()
			? FString(TEXT("BlueprintCallable, BlueprintNativeEvent, Category = \"Interaction\""))
			: M.Specifiers;

		if (!M.Comment.IsEmpty()) Out += CommentLine(M.Comment, 1);
		Out += FString::Printf(TEXT("\tUFUNCTION(%s)\n"), *Spec_);
		Out += FString::Printf(TEXT("\t%s %s(%s);\n\n"), *Ret, *M.Name, *M.Params);
	}

	Out += TEXT("};\n");
	return Out;
}

FString EmitUInterfaceSource(const FUInterfaceSpec& Spec)
{
	const FString FileBase = StripTypePrefix(Spec.InterfaceName);
	const FString HeaderInc = FileBase + TEXT(".h");

	FString Out;
	Out += TEXT("// Copyright 2026, All rights reserved\n");
	Out += TEXT("\n");
	Out += FString::Printf(TEXT("#include \"%s\"\n"), *HeaderInc);
	Out += TEXT("\n");
	Out += FString::Printf(TEXT("// %s methods are pure virtual / BlueprintNativeEvent — no concrete impl needed here.\n"),
		*Spec.InterfaceName);
	return Out;
}

FString EmitUProperty(const FAddUPropertySpec& Spec)
{
	const FString Spec_ = Spec.Specifiers.IsEmpty()
		? FString(TEXT("EditAnywhere, BlueprintReadWrite, Category = \"Default\""))
		: Spec.Specifiers;

	FString Out;
	if (!Spec.Comment.IsEmpty()) Out += CommentLine(Spec.Comment, 1);
	Out += FString::Printf(TEXT("\tUPROPERTY(%s)\n"), *Spec_);
	if (Spec.DefaultValue.IsEmpty())
	{
		Out += FString::Printf(TEXT("\t%s %s;\n"), *Spec.Type, *Spec.Name);
	}
	else
	{
		Out += FString::Printf(TEXT("\t%s %s = %s;\n"), *Spec.Type, *Spec.Name, *Spec.DefaultValue);
	}
	return Out;
}

FString EmitUFunctionDeclaration(const FAddUFunctionSpec& Spec)
{
	const FString Ret = Spec.ReturnType.IsEmpty() ? FString(TEXT("void")) : Spec.ReturnType;
	const FString Spec_ = Spec.Specifiers.IsEmpty()
		? FString(TEXT("BlueprintCallable, Category = \"Default\""))
		: Spec.Specifiers;

	FString Qualifiers;
	if (Spec.bStatic)  Qualifiers += TEXT("static ");
	if (Spec.bVirtual) Qualifiers += TEXT("virtual ");

	FString TrailingQuals;
	if (Spec.bConst) TrailingQuals = TEXT(" const");

	FString Out;
	if (!Spec.Comment.IsEmpty()) Out += CommentLine(Spec.Comment, 1);
	Out += FString::Printf(TEXT("\tUFUNCTION(%s)\n"), *Spec_);
	Out += FString::Printf(TEXT("\t%s%s %s(%s)%s;\n"), *Qualifiers, *Ret, *Spec.Name, *Spec.Params, *TrailingQuals);
	return Out;
}

FString EmitUFunctionDefinition(const FString& OwningClass, const FAddUFunctionSpec& Spec)
{
	if (Spec.Specifiers.Contains(TEXT("BlueprintImplementableEvent")))
	{
		return FString();
	}

	const FString Ret = Spec.ReturnType.IsEmpty() ? FString(TEXT("void")) : Spec.ReturnType;
	FString TrailingQuals;
	if (Spec.bConst) TrailingQuals = TEXT(" const");

	FString Out;

	const bool bIsNativeEvent = Spec.Specifiers.Contains(TEXT("BlueprintNativeEvent"));
	const FString FuncName = bIsNativeEvent
		? Spec.Name + TEXT("_Implementation")
		: Spec.Name;

	Out += FString::Printf(TEXT("%s %s::%s(%s)%s\n"),
		*Ret, *OwningClass, *FuncName, *Spec.Params, *TrailingQuals);
	Out += TEXT("{\n");

	if (Ret == TEXT("bool"))   Out += TEXT("\treturn false;\n");
	else if (Ret == TEXT("int32") || Ret == TEXT("int") || Ret == TEXT("float") || Ret == TEXT("double"))
		Out += TEXT("\treturn 0;\n");
	else if (Ret != TEXT("void"))
		Out += FString::Printf(TEXT("\treturn %s();\n"), *Ret);

	Out += TEXT("}\n");
	return Out;
}

}
