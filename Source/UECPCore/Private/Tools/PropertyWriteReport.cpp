// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/PropertyWriteReport.h"
#include "UObject/UnrealType.h"

namespace UECPProps
{
	FString ExportPropertyValueString(const FProperty* Prop, const void* ValuePtr)
	{
		FString Out;
		if (Prop && ValuePtr)
		{
			Prop->ExportText_Direct(Out, ValuePtr,  nullptr,  nullptr, PPF_None);
		}
		return Out;
	}
}
