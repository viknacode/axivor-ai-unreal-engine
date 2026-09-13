// Copyright 2026, BlueprintsLab, All rights reserved

// uecp_selftest — consistency report across the four places a tool can be described:
//   1. dispatcher handlers        (IUECPToolDispatcher::ListTools)
//   2. dispatcher metadata        (IUECPToolDispatcher::GetAllToolMetadata — umbrella/summary/params)
//   3. tool_docs                  (cached core tool_docs + loaded extensions' UmbrellaDocs)
//   4. extension manifests        (FUECPExtensionDescriptor::OwnedTools / OwnedUmbrellas)
// Drift between them is what makes the model call tools that do not exist or miss ones that do.

#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/IUECPExtensionService.h"
#include "Managers/ProjectStateCache.h"
#include "Managers/UpdateManager.h"
#include "Interfaces/IPluginManager.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace SelfTestTools
{
	namespace
	{
		struct FDocRef
		{
			FString Action;
			FString Umbrella;
		};

		// Same bullet convention search_tools indexes: "### umbrella" headings, "- `action` — ..." bullets.
		void ParseDocIndex(const FString& DocText, const FString& DefaultUmbrella,
			TArray<FDocRef>& OutRefs, TSet<FString>& OutUmbrellas)
		{
			FString CurUmbrella = DefaultUmbrella;
			if (!CurUmbrella.IsEmpty()) OutUmbrellas.Add(CurUmbrella);

			TArray<FString> Lines;
			DocText.ParseIntoArrayLines(Lines, false);
			for (const FString& Raw : Lines)
			{
				const FString Line = Raw.TrimStartAndEnd();
				if (Line.StartsWith(TEXT("### ")))
				{
					FString Head = Line.RightChop(4).TrimStartAndEnd();
					int32 Sp = INDEX_NONE;
					if (Head.FindChar(TEXT(' '), Sp)) Head = Head.Left(Sp);
					CurUmbrella = Head.ToLower();
					if (!CurUmbrella.IsEmpty()) OutUmbrellas.Add(CurUmbrella);
					continue;
				}
				if (!Line.StartsWith(TEXT("- `"))) continue;
				const FString Rest = Line.RightChop(3);
				int32 CloseTick = INDEX_NONE;
				if (!Rest.FindChar(TEXT('`'), CloseTick)) continue;
				const FString Action = Rest.Left(CloseTick).TrimStartAndEnd();
				if (Action.IsEmpty() || Action.Contains(TEXT(" "))) continue;
				OutRefs.Add({ Action, CurUmbrella });
			}
		}

		TArray<TSharedPtr<FJsonValue>> ToSortedJsonArray(TArray<FString> In)
		{
			In.Sort();
			TArray<TSharedPtr<FJsonValue>> Out;
			Out.Reserve(In.Num());
			for (const FString& S : In) Out.Add(MakeShared<FJsonValueString>(S));
			return Out;
		}

		TArray<TSharedPtr<FJsonValue>> ToSortedJsonArray(const TSet<FString>& In)
		{
			return ToSortedJsonArray(In.Array());
		}
	}

	void HandleSelfTestFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
	{
		if (!IUECPCoreModule::IsAvailable())
		{
			OutError = TEXT("UECPCore is not available — the tool dispatcher cannot be inspected.");
			return;
		}

		FString Scope = TEXT("all");
		bool bIncludeDocs = true;
		if (Args.IsValid())
		{
			Args->TryGetStringField(TEXT("scope"), Scope);
			Args->TryGetBoolField(TEXT("include_docs"), bIncludeDocs);
		}
		Scope = Scope.TrimStartAndEnd().ToLower();
		const bool bAllScopes = Scope.IsEmpty() || Scope == TEXT("all");
		if (bAllScopes) Scope = TEXT("all");

		IUECPToolDispatcher&  Dispatcher = IUECPCoreModule::Get().GetToolDispatcher();
		IUECPExtensionService& ExtSvc    = IUECPCoreModule::Get().GetExtensionService();

		// 1. Handlers -------------------------------------------------------------------------
		TSet<FString> RegisteredAll;
		TSet<FString> RegisteredCore;   // excludes "<ext>__<tool>" MCP proxies, which never carry metadata
		int32 ProxyCount = 0;
		for (const FName& N : Dispatcher.ListTools())
		{
			const FString S = N.ToString();
			if (S.IsEmpty()) continue;
			RegisteredAll.Add(S);
			if (S.Contains(TEXT("__"))) { ++ProxyCount; continue; }
			RegisteredCore.Add(S);
		}

		// 2. Metadata -------------------------------------------------------------------------
		TArray<FUECPToolMeta> Metas;
		Dispatcher.GetAllToolMetadata(Metas);
		TMap<FString, FUECPToolMeta> MetaByAction;   // scoped
		TSet<FString> MetaUmbrellas;                  // scoped
		int32 MetaTotal = 0;
		for (const FUECPToolMeta& M : Metas)
		{
			const FString Action   = M.Action.ToString();
			const FString Umbrella = M.Umbrella.ToString().ToLower();
			if (Action.IsEmpty()) continue;
			++MetaTotal;
			if (!bAllScopes && Umbrella != Scope) continue;
			MetaByAction.Add(Action, M);
			if (!Umbrella.IsEmpty()) MetaUmbrellas.Add(Umbrella);
		}

		// 4. Extension manifests (snapshot) ---------------------------------------------------
		struct FExtSnapshot
		{
			FString              Id;
			TArray<FName>        OwnedTools;
			TArray<FName>        OwnedUmbrellas;
			TMap<FName, FString> UmbrellaDocs;
			bool                 bLoaded = false;
			bool                 bHasMcpServer = false;
		};
		TArray<FExtSnapshot> Extensions;
		int32 ExtensionsLoaded = 0;
		for (const FUECPExtensionDescriptor& D : ExtSvc.GetExtensions())
		{
			FExtSnapshot S;
			S.Id             = D.ExtensionId.ToString();
			S.OwnedTools     = D.OwnedTools;
			S.OwnedUmbrellas = D.OwnedUmbrellas;
			S.UmbrellaDocs   = D.UmbrellaDocs;
			S.bLoaded        = ExtSvc.GetExtensionState(D.ExtensionId) == EUECPExtensionState::Loaded;
			S.bHasMcpServer  = D.McpServer.IsSet();
			if (S.bLoaded) ++ExtensionsLoaded;
			Extensions.Add(MoveTemp(S));
		}

		auto ExtensionInScope = [&](const FExtSnapshot& S) -> bool
		{
			if (bAllScopes) return true;
			for (const FName& U : S.OwnedUmbrellas)
				if (U.ToString().ToLower() == Scope) return true;
			return false;
		};

		TSet<FString> UmbrellasInScope = MetaUmbrellas;
		TSet<FString> ExtToolsInScope;   // tools declared by loaded extensions owning the scoped umbrella
		for (const FExtSnapshot& S : Extensions)
		{
			if (!S.bLoaded || !ExtensionInScope(S)) continue;
			for (const FName& U : S.OwnedUmbrellas)
			{
				const FString UStr = U.ToString().ToLower();
				if (bAllScopes || UStr == Scope) UmbrellasInScope.Add(UStr);
			}
			for (const FName& T : S.OwnedTools) ExtToolsInScope.Add(T.ToString());
		}

		// 3. Docs -----------------------------------------------------------------------------
		TArray<FDocRef> DocRefs;
		TSet<FString>   DocUmbrellas;
		bool bDocsAvailable    = false;
		bool bDocCacheCurrent  = FProjectStateCache::Get().IsDocCacheCurrent();
		if (bIncludeDocs)
		{
			if (IPluginManager::Get().FindPlugin(TEXT("BpGeneratorUltimate")).IsValid())
			{
				const FString CoreDocs = FUpdateManager::Get().LoadCachedRevision(TEXT("tool_docs"));
				if (!CoreDocs.IsEmpty())
				{
					bDocsAvailable = true;
					ParseDocIndex(CoreDocs, FString(), DocRefs, DocUmbrellas);
				}
			}
			for (const FExtSnapshot& S : Extensions)
			{
				if (!S.bLoaded) continue;
				for (const TPair<FName, FString>& KV : S.UmbrellaDocs)
				{
					if (KV.Value.IsEmpty()) continue;
					bDocsAvailable = true;
					ParseDocIndex(KV.Value, KV.Key.ToString().ToLower(), DocRefs, DocUmbrellas);
				}
			}
		}
		TSet<FString> DocActionsInScope;
		for (const FDocRef& R : DocRefs)
		{
			if (bAllScopes || R.Umbrella == Scope) DocActionsInScope.Add(R.Action);
		}

		// Scoped view of the handler table: everything when scope=all, otherwise only handlers
		// that some scoped source (metadata, docs, or an owning extension manifest) attributes
		// to the umbrella — a bare handler name carries no umbrella of its own.
		TSet<FString> RegisteredInScope;
		for (const FString& T : RegisteredCore)
		{
			if (bAllScopes || MetaByAction.Contains(T) || DocActionsInScope.Contains(T) || ExtToolsInScope.Contains(T))
				RegisteredInScope.Add(T);
		}

		// Reports -----------------------------------------------------------------------------
		TArray<FString> RegisteredWithoutMetadata;
		for (const FString& T : RegisteredInScope)
			if (!MetaByAction.Contains(T)) RegisteredWithoutMetadata.Add(T);

		TArray<FString> MetadataWithoutHandler;
		for (const TPair<FString, FUECPToolMeta>& KV : MetaByAction)
			if (!RegisteredAll.Contains(KV.Key)) MetadataWithoutHandler.Add(KV.Key);

		TArray<FString> DocumentedButUnregistered;
		int32 DocumentedButExtensionUnloaded = 0;
		{
			TSet<FString> Seen;
			for (const FDocRef& R : DocRefs)
			{
				if (!bAllScopes && R.Umbrella != Scope) continue;
				if (RegisteredAll.Contains(R.Action) || Seen.Contains(R.Action)) continue;
				Seen.Add(R.Action);

				// Docs for an extension that is simply not enabled are expected, not drift.
				const TOptional<FUECPExtensionDescriptor> Owner = ExtSvc.FindExtensionByTool(FName(*R.Action));
				if (Owner.IsSet() && ExtSvc.GetExtensionState(Owner->ExtensionId) != EUECPExtensionState::Loaded)
				{
					++DocumentedButExtensionUnloaded;
					continue;
				}
				DocumentedButUnregistered.Add(R.Umbrella.IsEmpty() ? R.Action : FString::Printf(TEXT("%s (%s)"), *R.Action, *R.Umbrella));
			}
		}

		TArray<FString> UmbrellasWithoutDocs;
		if (bIncludeDocs && bDocsAvailable)
		{
			for (const FString& U : UmbrellasInScope)
				if (!DocUmbrellas.Contains(U)) UmbrellasWithoutDocs.Add(U);
		}

		TArray<TSharedPtr<FJsonValue>> ParamsUnparseable;
		for (const TPair<FString, FUECPToolMeta>& KV : MetaByAction)
		{
			TArray<FUECPParsedParam> Parsed;
			TArray<FString> Bad;
			if (UECPToolDispatch::ParseParamSpec(KV.Value.Params, Parsed, &Bad)) continue;

			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("action"),   KV.Key);
			O->SetStringField(TEXT("umbrella"), KV.Value.Umbrella.ToString().ToLower());
			O->SetStringField(TEXT("params"),   KV.Value.Params);
			O->SetArrayField (TEXT("unparseable_tokens"), ToSortedJsonArray(Bad));
			ParamsUnparseable.Add(MakeShared<FJsonValueObject>(O));
		}
		ParamsUnparseable.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
		{
			return A->AsObject()->GetStringField(TEXT("action")) < B->AsObject()->GetStringField(TEXT("action"));
		});

		// Extension manifest vs. what actually registered (only meaningful once Loaded).
		TArray<TSharedPtr<FJsonValue>> ExtensionToolsMissing;
		TArray<TSharedPtr<FJsonValue>> ExtensionToolsUndeclared;
		for (const FExtSnapshot& S : Extensions)
		{
			if (!S.bLoaded || !ExtensionInScope(S)) continue;

			const TSet<FName> Declared(S.OwnedTools);
			for (const FName& T : S.OwnedTools)
			{
				if (T.IsNone() || Dispatcher.IsRegistered(T)) continue;
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("extension_id"), S.Id);
				O->SetStringField(TEXT("tool"), T.ToString());
				ExtensionToolsMissing.Add(MakeShared<FJsonValueObject>(O));
			}
			for (const FName& T : Dispatcher.GetToolsForOwner(FName(*S.Id)))
			{
				if (Declared.Contains(T)) continue;
				// MCP proxies are registered under the extension by the registry itself, not the manifest.
				if (S.bHasMcpServer && T.ToString().Contains(TEXT("__"))) continue;
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("extension_id"), S.Id);
				O->SetStringField(TEXT("tool"), T.ToString());
				ExtensionToolsUndeclared.Add(MakeShared<FJsonValueObject>(O));
			}
		}

		// Output ------------------------------------------------------------------------------
		const int32 IssueCount = RegisteredWithoutMetadata.Num() + MetadataWithoutHandler.Num()
			+ DocumentedButUnregistered.Num() + UmbrellasWithoutDocs.Num() + ParamsUnparseable.Num()
			+ ExtensionToolsMissing.Num() + ExtensionToolsUndeclared.Num();

		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField  (TEXT("success"), true);
		Root->SetBoolField  (TEXT("ok"), IssueCount == 0);
		Root->SetStringField(TEXT("scope"), Scope);
		Root->SetBoolField  (TEXT("include_docs"), bIncludeDocs);
		Root->SetBoolField  (TEXT("docs_available"), bDocsAvailable);
		Root->SetBoolField  (TEXT("docs_cache_current"), bDocCacheCurrent);

		Root->SetArrayField(TEXT("registered_without_metadata"),  ToSortedJsonArray(RegisteredWithoutMetadata));
		Root->SetArrayField(TEXT("metadata_without_handler"),     ToSortedJsonArray(MetadataWithoutHandler));
		Root->SetArrayField(TEXT("documented_but_unregistered"),  ToSortedJsonArray(DocumentedButUnregistered));
		Root->SetArrayField(TEXT("umbrellas_without_docs"),       ToSortedJsonArray(UmbrellasWithoutDocs));
		Root->SetArrayField(TEXT("params_unparseable"),           ParamsUnparseable);
		Root->SetArrayField(TEXT("extension_tools_missing"),      ExtensionToolsMissing);
		Root->SetArrayField(TEXT("extension_tools_undeclared"),   ExtensionToolsUndeclared);

		TSharedPtr<FJsonObject> Counts = MakeShared<FJsonObject>();
		Counts->SetNumberField(TEXT("registered_tools"),                 RegisteredAll.Num());
		Counts->SetNumberField(TEXT("registered_tools_in_scope"),        RegisteredInScope.Num());
		Counts->SetNumberField(TEXT("mcp_proxy_tools_skipped"),          ProxyCount);
		Counts->SetNumberField(TEXT("metadata_actions"),                 MetaTotal);
		Counts->SetNumberField(TEXT("metadata_actions_in_scope"),        MetaByAction.Num());
		Counts->SetNumberField(TEXT("umbrellas_in_scope"),               UmbrellasInScope.Num());
		Counts->SetNumberField(TEXT("documented_actions_in_scope"),      DocActionsInScope.Num());
		Counts->SetNumberField(TEXT("documented_umbrellas"),             DocUmbrellas.Num());
		Counts->SetNumberField(TEXT("documented_but_extension_unloaded"),DocumentedButExtensionUnloaded);
		Counts->SetNumberField(TEXT("extensions_total"),                 Extensions.Num());
		Counts->SetNumberField(TEXT("extensions_loaded"),                ExtensionsLoaded);
		Counts->SetNumberField(TEXT("registered_without_metadata"),      RegisteredWithoutMetadata.Num());
		Counts->SetNumberField(TEXT("metadata_without_handler"),         MetadataWithoutHandler.Num());
		Counts->SetNumberField(TEXT("documented_but_unregistered"),      DocumentedButUnregistered.Num());
		Counts->SetNumberField(TEXT("umbrellas_without_docs"),           UmbrellasWithoutDocs.Num());
		Counts->SetNumberField(TEXT("params_unparseable"),               ParamsUnparseable.Num());
		Counts->SetNumberField(TEXT("extension_tools_missing"),          ExtensionToolsMissing.Num());
		Counts->SetNumberField(TEXT("extension_tools_undeclared"),       ExtensionToolsUndeclared.Num());
		Counts->SetNumberField(TEXT("issues"),                           IssueCount);
		Root->SetObjectField(TEXT("counts"), Counts);

		Root->SetStringField(TEXT("hint"), IssueCount == 0
			? TEXT("Registry, metadata, docs and extension manifests agree for this scope.")
			: TEXT("registered_without_metadata: add RegisterToolMetadata for the action (it is invisible to search_tools/schemas). "
			       "metadata_without_handler: metadata names an action no handler serves (typo or unregistered tool). "
			       "documented_but_unregistered: tool_docs bullet for a tool that is not registered. "
			       "umbrellas_without_docs: no '### <umbrella>' section in tool_docs or the extension's UmbrellaDocs. "
			       "params_unparseable: Params spec tokens without a leading identifier (see FUECPToolMeta convention). "
			       "extension_tools_missing/undeclared: manifest owned_tools vs handlers actually registered under that extension."));

		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	}
}
