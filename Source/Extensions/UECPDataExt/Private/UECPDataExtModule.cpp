// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPDataExtModule.h"

#include "Tools/DataAssetTools.h"
#include "Tools/DataTableTools.h"
#include "Tools/StructEnumTools.h"

#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/IUECPCreateAssetRegistry.h"

DEFINE_LOG_CATEGORY(LogUECPDataExt);

namespace
{
	static const TArray<FName>& OwnedToolNames()
	{
		static const TArray<FName> Names = {
			TEXT("edit_data_asset_defaults"),
			TEXT("get_data_asset_details"), TEXT("list_data_asset_types"),

			TEXT("add_data_table_rows"), TEXT("set_data_table_rows"),
			TEXT("delete_data_table_row"),
			TEXT("edit_data_table_rows"), TEXT("get_data_table_rows"),
			TEXT("import_data_table_csv"), TEXT("export_data_table_csv"),

			TEXT("add_enum_value"),
			TEXT("add_struct_member"), TEXT("add_struct_members"),
			TEXT("remove_enum_value"), TEXT("remove_struct_member"),
			TEXT("get_enum_values"),
			TEXT("get_struct_members"), TEXT("get_struct"),
		};
		return Names;
	}

	using FHandlerFn = void(*)(const TSharedPtr<FJsonObject>&, FString&, FString&);

	static IUECPToolDispatcher::FToolHandler MakeHandler(FHandlerFn Fn)
	{
		return [Fn](const TSharedPtr<FJsonObject>& Args) -> FUECPToolResult
		{
			FUECPToolResult R;
			Fn(Args, R.ResultJson, R.ErrorMessage);
			R.bSuccess = R.ErrorMessage.IsEmpty();
			return R;
		};
	}
}

void FUECPDataExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	D.RegisterHandler(TEXT("edit_data_asset_defaults"),MakeHandler(DataAssetTools::HandleEditDataAssetDefaultsFromArgs));
	D.RegisterHandler(TEXT("get_data_asset_details"),  MakeHandler(DataAssetTools::HandleGetDataAssetDetailsFromArgs));
	D.RegisterHandler(TEXT("list_data_asset_types"),   MakeHandler(DataAssetTools::HandleListDataAssetTypesFromArgs));

	D.RegisterHandler(TEXT("add_data_table_rows"),     MakeHandler(DataTableTools::HandleAddDataTableRowsFromArgs));
	D.RegisterHandler(TEXT("set_data_table_rows"),     MakeHandler(DataTableTools::HandleAddDataTableRowsFromArgs));
	D.RegisterHandler(TEXT("edit_data_table_rows"),    MakeHandler(DataTableTools::HandleEditDataTableRowsFromArgs));
	D.RegisterHandler(TEXT("get_data_table_rows"),     MakeHandler(DataTableTools::HandleGetDataTableRowsFromArgs));
	D.RegisterHandler(TEXT("import_data_table_csv"),   MakeHandler(DataTableTools::HandleImportDataTableCSVFromArgs));
	D.RegisterHandler(TEXT("export_data_table_csv"),   MakeHandler(DataTableTools::HandleExportDataTableCSVFromArgs));
	D.RegisterHandler(TEXT("delete_data_table_row"),   MakeHandler(DataTableTools::HandleDeleteDataTableRowFromArgs));

	D.RegisterHandler(TEXT("add_enum_value"),          MakeHandler(StructEnumTools::HandleAddEnumValueFromArgs));
	D.RegisterHandler(TEXT("add_struct_member"),       MakeHandler(StructEnumTools::HandleAddStructMemberFromArgs));
	D.RegisterHandler(TEXT("add_struct_members"),      MakeHandler(StructEnumTools::HandleAddStructMemberFromArgs));
	D.RegisterHandler(TEXT("remove_enum_value"),       MakeHandler(StructEnumTools::HandleRemoveEnumValueFromArgs));
	D.RegisterHandler(TEXT("remove_struct_member"),    MakeHandler(StructEnumTools::HandleRemoveStructMemberFromArgs));
	D.RegisterHandler(TEXT("get_enum_values"),         MakeHandler(StructEnumTools::HandleGetEnumValuesFromArgs));
	D.RegisterHandler(TEXT("get_struct_members"),      MakeHandler(StructEnumTools::HandleGetStructMembersFromArgs));
	D.RegisterHandler(TEXT("get_struct"),              MakeHandler(StructEnumTools::HandleGetStructMembersFromArgs));

	{
		const FName U(TEXT("data"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("edit_data_asset_defaults"), TEXT("Edit a DataAsset's default property values (UE export-string syntax for structs / struct-arrays / asset refs)."), TEXT("asset_path, edits=[{property_name, value}]"));
		Meta(TEXT("get_data_asset_details"),   TEXT("Get a DataAsset's properties and current values."), TEXT("asset_path"));
		Meta(TEXT("list_data_asset_types"),    TEXT("List available DataAsset subclasses."), TEXT("filter?"));
		Meta(TEXT("add_data_table_rows"),      TEXT("Add rows to a DataTable (batch; enum fields use the short display name)."), TEXT("table_path, rows=[{row_name, field...}]"));
		Meta(TEXT("set_data_table_rows"),      TEXT("Alias of add_data_table_rows."), TEXT("table_path, rows=[{row_name, field...}]"));
		Meta(TEXT("edit_data_table_rows"),     TEXT("Edit existing DataTable rows (only changed fields written)."), TEXT("table_path, edits"));
		Meta(TEXT("delete_data_table_row"),    TEXT("Delete a row from a DataTable."), TEXT("data_table_path, row_name"));
		Meta(TEXT("get_data_table_rows"),      TEXT("Read a DataTable's rows."), TEXT("table_path"));
		Meta(TEXT("import_data_table_csv"),    TEXT("Import rows into a DataTable from a CSV file."), TEXT("table_path, csv_file_path"));
		Meta(TEXT("export_data_table_csv"),    TEXT("Export a DataTable's rows to a CSV file."), TEXT("table_path, export_path"));
		Meta(TEXT("add_enum_value"),           TEXT("Add an enumerator to a User Enum (batch supported)."), TEXT("enum_path, value"));
		Meta(TEXT("remove_enum_value"),        TEXT("Remove an enumerator from a User Enum."), TEXT("enum_path, value"));
		Meta(TEXT("get_enum_values"),          TEXT("List a User Enum's enumerators."), TEXT("enum_path"));
		Meta(TEXT("add_struct_member"),        TEXT("Add a member to a User Struct (batch via items=[{member_name, member_type}])."), TEXT("struct_path, member_name, member_type"));
		Meta(TEXT("add_struct_members"),       TEXT("Alias of add_struct_member (batch)."), TEXT("struct_path, items=[{member_name, member_type}]"));
		Meta(TEXT("remove_struct_member"),     TEXT("Remove a member from a User Struct (batch via items or a string array)."), TEXT("struct_path, member_name"));
		Meta(TEXT("get_struct_members"),       TEXT("List a User Struct's members."), TEXT("struct_path"));
		Meta(TEXT("get_struct"),               TEXT("Alias of get_struct_members."), TEXT("struct_path"));
	}

	{
		IUECPCreateAssetRegistry& Reg = IUECPCoreModule::Get().GetCreateAssetRegistry();
		const FName ExtId(TEXT("Data"));
		Reg.RegisterType(TEXT("DataTable"),  UECPCreateAsset::FactoryFromArgsFn(&DataTableTools::HandleCreateDataTableFromArgs, TEXT("DataTable")),  ExtId);
		Reg.RegisterType(TEXT("DataAsset"),  UECPCreateAsset::FactoryFromArgsFn(&DataAssetTools::HandleCreateDataAssetFromArgs, TEXT("DataAsset")),  ExtId);
		Reg.RegisterType(TEXT("UserStruct"), UECPCreateAsset::FactoryFromArgsFn(&StructEnumTools::HandleCreateStructFromArgs,  TEXT("UserStruct")), ExtId);
		Reg.RegisterType(TEXT("UserEnum"),   UECPCreateAsset::FactoryFromArgsFn(&StructEnumTools::HandleCreateEnumFromArgs,    TEXT("UserEnum")),   ExtId);
	}

	UE_LOG(LogUECPDataExt, Log, TEXT("Registered %d Data tools (data umbrella)"),
		OwnedToolNames().Num());
}

void FUECPDataExtModule::ShutdownModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const FName& N : OwnedToolNames()) D.UnregisterHandler(N);
	IUECPCreateAssetRegistry& Reg = IUECPCoreModule::Get().GetCreateAssetRegistry();
	for (const TCHAR* T : { TEXT("DataTable"), TEXT("DataAsset"), TEXT("UserStruct"), TEXT("UserEnum") })
	{
		Reg.UnregisterType(T);
	}
}

IMPLEMENT_MODULE(FUECPDataExtModule, UECPDataExt)
