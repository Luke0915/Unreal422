// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class UdpMessaging : ModuleRules
	{
		public UdpMessaging(ReadOnlyTargetRules Target) : base(Target)
		{
			DynamicallyLoadedModuleNames.AddRange(
				new string[] {
					"Messaging",
				});

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
				});

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"CoreUObject",
					"Json",
					"Cbor",
					"Networking",
					"Serialization",
					"Sockets",
				});

			PrivateIncludePathModuleNames.AddRange(
				new string[] {
					"Messaging",
					"MessagingCommon",
				});

			PrivateIncludePaths.AddRange(
				new string[] {
					"UdpMessaging/Private",
					"UdpMessaging/Private/Shared",
					"UdpMessaging/Private/Transport",
					"UdpMessaging/Private/Tunnel",
				});

			if (Target.Type == TargetType.Editor || Target.Type == TargetType.Program)
			{
				DynamicallyLoadedModuleNames.AddRange(
					new string[] {
						"Settings",
						"TargetPlatform",
					});

				PrivateIncludePathModuleNames.AddRange(
					new string[] {
						"Settings",
						"TargetPlatform",
					});
			}
		}
	}
}
