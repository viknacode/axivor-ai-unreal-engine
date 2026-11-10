// Copyright 2026, BlueprintsLab, All rights reserved

#if WITH_DEV_AUTOMATION_TESTS
#include "ACP/FUECPACPSchema.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUECPAntigravityAccountAuthTest,
	"Axivor.ACP.Antigravity.AccountAuthMigration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUECPAntigravityAccountAuthTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FJsonObject> Settings = MakeShared<FJsonObject>();
	const TSharedRef<FJsonObject> Auth = MakeShared<FJsonObject>();
	Auth->SetStringField(TEXT("type"), TEXT("gemini-api-key"));
	Auth->SetStringField(TEXT("otherOption"), TEXT("preserved"));
	Settings->SetObjectField(TEXT("auth"), Auth);
	Settings->SetStringField(TEXT("model"), TEXT("user-selected-model"));

	TestTrue(TEXT("An existing account login replaces the legacy injected API mode"),
		UECPACPSchema::PrepareAntigravityAccountSettings(Settings, true));
	TestEqual(TEXT("Use the account login"),
		Settings->GetObjectField(TEXT("auth"))->GetStringField(TEXT("type")), FString(TEXT("oauth-personal")));
	TestEqual(TEXT("Keep unrelated auth options"),
		Settings->GetObjectField(TEXT("auth"))->GetStringField(TEXT("otherOption")), FString(TEXT("preserved")));
	TestEqual(TEXT("Keep the selected model"), Settings->GetStringField(TEXT("model")), FString(TEXT("user-selected-model")));
	TestFalse(TEXT("Repeated probes do not rewrite a valid account configuration"),
		UECPACPSchema::PrepareAntigravityAccountSettings(Settings, true));

	TestTrue(TEXT("After logout, clear the selection that would reopen browser login"),
		UECPACPSchema::PrepareAntigravityAccountSettings(Settings, false));
	TestFalse(TEXT("No auth selection after logout"), Settings->HasField(TEXT("auth")));
	TestFalse(TEXT("Repeated probes leave a logged-out account alone"),
		UECPACPSchema::PrepareAntigravityAccountSettings(Settings, false));

	Auth->SetStringField(TEXT("type"), TEXT("gemini-api-key"));
	Settings->SetObjectField(TEXT("auth"), Auth);
	TestTrue(TEXT("A stale injected key selection is removed even without a login token"),
		UECPACPSchema::PrepareAntigravityAccountSettings(Settings, false));
	TestFalse(TEXT("Never require an API key for the CLI profile"), Settings->HasField(TEXT("auth")));
	return true;
}
#endif
