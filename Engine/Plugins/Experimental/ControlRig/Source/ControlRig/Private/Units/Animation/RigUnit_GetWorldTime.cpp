// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Animation/RigUnit_GetWorldTime.h"
#include "Units/RigUnitContext.h"

void FRigUnit_GetWorldTime::Execute(const FRigUnitContext& Context)
{
	FDateTime Now = FDateTime::Now();
	Year = (float)Now.GetYear();
	Month = (float)Now.GetMonth();
	Day = (float)Now.GetDay();
	WeekDay = (float)Now.GetDayOfWeek();
	Hours = (float)Now.GetHour();
	Minutes = (float)Now.GetMinute();
	Seconds = float(Now.GetSecond()) + float(Now.GetMillisecond()) * 0.001f;
	OverallSeconds = (float)FPlatformTime::Seconds();
}

