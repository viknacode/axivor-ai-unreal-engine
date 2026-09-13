// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

namespace UECPProjectIndex
{
	constexpr int32 SchemaVersion = 2;

	inline const TCHAR* MetaVersion       = TEXT("_version");
	inline const TCHAR* MetaScanTimestamp = TEXT("_scan_timestamp");
	inline const TCHAR* MetaCounts        = TEXT("_counts");

	inline const TCHAR* FieldType         = TEXT("type");
	inline const TCHAR* FieldSubtype      = TEXT("subtype");
	inline const TCHAR* FieldParent       = TEXT("parent");
	inline const TCHAR* FieldComplexity   = TEXT("complexity");
	inline const TCHAR* FieldMtime        = TEXT("mtime");
	inline const TCHAR* FieldIssues       = TEXT("issues");
	inline const TCHAR* FieldSummary      = TEXT("summary");

	inline const TCHAR* FieldShortSummary = TEXT("short_summary");

	inline const TCHAR* IssueSeverity     = TEXT("sev");
	inline const TCHAR* IssueCode         = TEXT("code");
	inline const TCHAR* IssueMessage      = TEXT("msg");

	namespace Type
	{
		inline const TCHAR* Blueprint    = TEXT("Blueprint");
		inline const TCHAR* Interface    = TEXT("Interface");
		inline const TCHAR* BehaviorTree = TEXT("BehaviorTree");
		inline const TCHAR* Enum         = TEXT("Enum");
		inline const TCHAR* Struct       = TEXT("Struct");
		inline const TCHAR* DataAsset    = TEXT("DataAsset");
		inline const TCHAR* DataTable    = TEXT("DataTable");
		inline const TCHAR* Level        = TEXT("Level");
		inline const TCHAR* CppHeader    = TEXT("CppHeader");
		inline const TCHAR* Material     = TEXT("Material");
		inline const TCHAR* Texture      = TEXT("Texture");
		inline const TCHAR* StaticMesh   = TEXT("StaticMesh");
		inline const TCHAR* SkeletalMesh = TEXT("SkeletalMesh");
	}

	namespace Count
	{
		inline const TCHAR* Total          = TEXT("total");
		inline const TCHAR* Widget         = TEXT("widget");
		inline const TCHAR* Actor          = TEXT("actor");
		inline const TCHAR* AnimBp         = TEXT("anim_bp");
		inline const TCHAR* BehaviorTree   = TEXT("behavior_tree");
		inline const TCHAR* Enum           = TEXT("enum");
		inline const TCHAR* Struct         = TEXT("struct");
		inline const TCHAR* Interface      = TEXT("interface");
		inline const TCHAR* DataAsset      = TEXT("data_asset");
		inline const TCHAR* DataTable      = TEXT("data_table");
		inline const TCHAR* Level          = TEXT("level");
		inline const TCHAR* CppFile        = TEXT("cpp_file");
		inline const TCHAR* CppClass       = TEXT("cpp_class");
		inline const TCHAR* CppStruct      = TEXT("cpp_struct");
		inline const TCHAR* CppEnum        = TEXT("cpp_enum");
		inline const TCHAR* Other          = TEXT("other");
		inline const TCHAR* Material       = TEXT("material");
		inline const TCHAR* Texture        = TEXT("texture");
		inline const TCHAR* StaticMesh     = TEXT("static_mesh");
		inline const TCHAR* SkeletalMesh   = TEXT("skeletal_mesh");
	}
}
