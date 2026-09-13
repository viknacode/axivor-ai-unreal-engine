// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

struct FCrewTemplate;

namespace UECPCrew
{

	UECPCREW_API extern const TCHAR* const BuildVerifyTemplateId;

	UECPCREW_API FCrewTemplate MakeBuildVerifyTemplate();

	UECPCREW_API TArray<FCrewTemplate> MakeAllBuiltInTemplates();
}
