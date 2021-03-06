// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisplayClusterUtils/DisplayClusterCommonStrings.h"


namespace DisplayClusterStrings
{
	namespace cfg
	{
		namespace data
		{
			namespace projection
			{
				namespace simple
				{
					static constexpr auto Screen = TEXT("screen");
				}

				namespace mpcdi
				{
					static constexpr auto File   = TEXT("file");
					static constexpr auto Buffer = TEXT("buffer");
					static constexpr auto Region = TEXT("region");
					static constexpr auto Origin = TEXT("origin");
				}

				namespace easyblend
				{
					static constexpr auto File   = TEXT("file");
					static constexpr auto Origin = TEXT("origin");
					static constexpr auto Scale  = TEXT("scale");
				}
			}
		}
	}

	namespace projection
	{
		static constexpr auto Simple    = TEXT("simple");
		static constexpr auto MPCDI     = TEXT("mpcdi");
		static constexpr auto EasyBlend = TEXT("easyblend");
	}
};
