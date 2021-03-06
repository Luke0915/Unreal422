// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class UnrealMultiUserServerTarget : TargetRules
{
	public UnrealMultiUserServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Program;
		LinkType = TargetLinkType.Modular;
		LaunchModuleName = "UnrealMultiUserServer";
		AdditionalPlugins.Add("UdpMessaging");
		AdditionalPlugins.Add("ConcertSyncServer");

		// This app compiles against Core/CoreUObject, but not the Engine or Editor, so compile out Engine and Editor references from Core/CoreUObject
		bCompileAgainstCoreUObject = true;
		bCompileAgainstEngine = false;
		bBuildWithEditorOnlyData = false;

		// Enable Developer plugins (like Concert!)
		bCompileWithPluginSupport = true;
		bBuildDeveloperTools = true;

		// This app is a console application (sets entry point to main(), instead of WinMain())
		bIsBuildingConsoleApplication = true;
	}
}
