// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPMVVMExtModule.h"
#include "Tools/MVVMTools.h"
#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/IUECPCreateAssetRegistry.h"

DEFINE_LOG_CATEGORY(LogUECPMVVMExt);

namespace
{
	static const TArray<FName>& OwnedToolNames()
	{
		static const TArray<FName> Names = {
			TEXT("add_view_model_field"),
			TEXT("add_widget_view_model_context"),
			TEXT("remove_widget_view_model_context"),
			TEXT("add_widget_binding"),
			TEXT("list_widget_bindings"),
		};
		return Names;
	}

	static auto MakeHandler(TFunction<void(const TSharedPtr<FJsonObject>&, FString&, FString&)> Fn)
	{
		return [Fn = MoveTemp(Fn)](const TSharedPtr<FJsonObject>& Args) -> FUECPToolResult
		{
			FUECPToolResult R;
			Fn(Args, R.ResultJson, R.ErrorMessage);
			R.bSuccess = R.ErrorMessage.IsEmpty();
			return R;
		};
	}
}

void FUECPMVVMExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	D.RegisterHandler(TEXT("add_view_model_field"),              MakeHandler(MVVMTools::HandleAddViewModelFieldFromArgs));
	D.RegisterHandler(TEXT("add_widget_view_model_context"),     MakeHandler(MVVMTools::HandleAddWidgetViewModelContextFromArgs));
	D.RegisterHandler(TEXT("remove_widget_view_model_context"),  MakeHandler(MVVMTools::HandleRemoveWidgetViewModelContextFromArgs));
	D.RegisterHandler(TEXT("add_widget_binding"),                MakeHandler(MVVMTools::HandleAddWidgetBindingFromArgs));
	D.RegisterHandler(TEXT("list_widget_bindings"),              MakeHandler(MVVMTools::HandleListWidgetBindingsFromArgs));

	IUECPCoreModule::Get().GetCreateAssetRegistry().RegisterType(TEXT("ViewModel"),
		UECPCreateAsset::FactoryFromArgsFn(&MVVMTools::HandleCreateViewModelFromArgs, TEXT("ViewModel")),
		TEXT("MVVM"));

	{
		const FName U(TEXT("mvvm"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("add_view_model_field"),            TEXT("Add a FieldNotify field to a ViewModel (var_type like blueprint.add_variable)."), TEXT("view_model_path, var_name, var_type, default_value?, category?"));
		Meta(TEXT("add_widget_view_model_context"),   TEXT("Add a viewmodel context to a widget."), TEXT("widget_path, view_model_class, view_model_name, creation_type?, global_identifier?, property_path?"));
		Meta(TEXT("remove_widget_view_model_context"),TEXT("Remove a viewmodel context from a widget."), TEXT("widget_path, view_model_name"));
		Meta(TEXT("add_widget_binding"),              TEXT("Bind a widget property to a VM field (dotted chains OK)."), TEXT("widget_path, widget_name?, widget_property, view_model_name, view_model_field, binding_mode?"));
		Meta(TEXT("list_widget_bindings"),            TEXT("List a widget's viewmodels + bindings."), TEXT("widget_path"));
	}

	UE_LOG(LogUECPMVVMExt, Log, TEXT("Registered %d MVVM tools"), OwnedToolNames().Num());
}

void FUECPMVVMExtModule::ShutdownModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const FName& N : OwnedToolNames()) D.UnregisterHandler(N);
	IUECPCoreModule::Get().GetCreateAssetRegistry().UnregisterType(TEXT("ViewModel"));
}

IMPLEMENT_MODULE(FUECPMVVMExtModule, UECPMVVMExt)
