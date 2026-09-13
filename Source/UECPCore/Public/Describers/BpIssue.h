// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

struct UECPCORE_API FBpIssue
{
	enum class ESeverity : uint8
	{
		Info,
		Warn,
		Critical
	};

	ESeverity Severity = ESeverity::Warn;

	FString Code;

	FString Message;

	static const TCHAR* SeverityToString(ESeverity S)
	{
		switch (S)
		{
		case ESeverity::Info:     return TEXT("info");
		case ESeverity::Warn:     return TEXT("warn");
		case ESeverity::Critical: return TEXT("critical");
		}
		return TEXT("info");
	}
};
