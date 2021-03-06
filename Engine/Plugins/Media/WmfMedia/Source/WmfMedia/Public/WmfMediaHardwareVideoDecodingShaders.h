// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GlobalShader.h"
#include "RenderUtils.h"
#include "RHIStaticStates.h"
#include "PipelineStateCache.h"
#include "ShaderParameterUtils.h"
#include "MediaShaders.h"

/**
 * Shaders which allow the conversion of NV12 texture data into RGB textures.
 */
class FWmfMediaHardwareVideoDecodingShader : public FGlobalShader
{
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM4);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	FWmfMediaHardwareVideoDecodingShader() {}

	FWmfMediaHardwareVideoDecodingShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		TextureY.Bind(Initializer.ParameterMap, TEXT("TextureY"));
		TextureUV.Bind(Initializer.ParameterMap, TEXT("TextureUV"));

		PointClampedSamplerY.Bind(Initializer.ParameterMap, TEXT("PointClampedSamplerY"));
		BilinearClampedSamplerUV.Bind(Initializer.ParameterMap, TEXT("BilinearClampedSamplerUV"));

		ColorTransform.Bind(Initializer.ParameterMap, TEXT("ColorTransform"));
		SrgbToLinear.Bind(Initializer.ParameterMap, TEXT("SrgbToLinear"));
	}

	template<typename TShaderRHIParamRef>
	void SetParameters(
		FRHICommandListImmediate& RHICmdList,
		const TShaderRHIParamRef ShaderRHI,
		const FShaderResourceViewRHIRef& InTextureY,
		const FShaderResourceViewRHIRef& InTextureUV,
		const bool InIsOutputSrgb
	)
	{
		SetSRVParameter(RHICmdList, ShaderRHI, TextureY, InTextureY);
		SetSRVParameter(RHICmdList, ShaderRHI, TextureUV, InTextureUV);

		SetSamplerParameter(RHICmdList, ShaderRHI, PointClampedSamplerY, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
		SetSamplerParameter(RHICmdList, ShaderRHI, BilinearClampedSamplerUV, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());

		SetShaderValue(RHICmdList, ShaderRHI, ColorTransform, MediaShaders::CombineColorTransformAndOffset(MediaShaders::YuvToSrgbDefault, MediaShaders::YUVOffset8bits));
		SetShaderValue(RHICmdList, ShaderRHI, SrgbToLinear, InIsOutputSrgb ? 1 : 0); // Explicitly specify integer value, as using boolean falls over on XboxOne.
	}

	template<typename TShaderRHIParamRef>
	void SetParameters(
		FRHICommandListImmediate& RHICmdList,
		const TShaderRHIParamRef ShaderRHI,
		const FShaderResourceViewRHIRef& InTextureRGBA,
		const bool InIsOutputSrgb
	)
	{
		SetSRVParameter(RHICmdList, ShaderRHI, TextureY, InTextureRGBA);

		SetSamplerParameter(RHICmdList, ShaderRHI, PointClampedSamplerY, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
		SetSamplerParameter(RHICmdList, ShaderRHI, BilinearClampedSamplerUV, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());

		SetShaderValue(RHICmdList, ShaderRHI, ColorTransform, MediaShaders::CombineColorTransformAndOffset(MediaShaders::YuvToSrgbDefault, MediaShaders::YUVOffset8bits));
		SetShaderValue(RHICmdList, ShaderRHI, SrgbToLinear, InIsOutputSrgb ? 1 : 0); // Explicitly specify integer value, as using boolean falls over on XboxOne.
	}


	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << TextureY << TextureUV << PointClampedSamplerY << BilinearClampedSamplerUV << ColorTransform << SrgbToLinear;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderResourceParameter TextureY;
	FShaderResourceParameter TextureUV;
	FShaderResourceParameter PointClampedSamplerY;
	FShaderResourceParameter BilinearClampedSamplerUV;
	FShaderParameter ColorTransform;
	FShaderParameter SrgbToLinear;
};

class FHardwareVideoDecodingVS : public FWmfMediaHardwareVideoDecodingShader
{
	DECLARE_SHADER_TYPE(FHardwareVideoDecodingVS, Global);

public:

	/** Default constructor. */
	FHardwareVideoDecodingVS() {}

	/** Initialization constructor. */
	FHardwareVideoDecodingVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FWmfMediaHardwareVideoDecodingShader(Initializer)
	{
	}
};

class FHardwareVideoDecodingPS : public FWmfMediaHardwareVideoDecodingShader
{
	DECLARE_SHADER_TYPE(FHardwareVideoDecodingPS, Global);

public:

	/** Default constructor. */
	FHardwareVideoDecodingPS() {}

	/** Initialization constructor. */
	FHardwareVideoDecodingPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FWmfMediaHardwareVideoDecodingShader(Initializer)
	{ }
};

class FHardwareVideoDecodingPassThroughPS : public FWmfMediaHardwareVideoDecodingShader
{
	DECLARE_SHADER_TYPE(FHardwareVideoDecodingPassThroughPS, Global);

public:

	/** Default constructor. */
	FHardwareVideoDecodingPassThroughPS() {}

	/** Initialization constructor. */
	FHardwareVideoDecodingPassThroughPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FWmfMediaHardwareVideoDecodingShader(Initializer)
	{ }
};

IMPLEMENT_SHADER_TYPE(, FHardwareVideoDecodingVS, TEXT("/Plugin/WmfMedia/Private/MediaHardwareVideoDecoding.usf"), TEXT("MainVS"), SF_Vertex)
IMPLEMENT_SHADER_TYPE(, FHardwareVideoDecodingPS, TEXT("/Plugin/WmfMedia/Private/MediaHardwareVideoDecoding.usf"), TEXT("NV12ConvertPS"), SF_Pixel)
IMPLEMENT_SHADER_TYPE(, FHardwareVideoDecodingPassThroughPS, TEXT("/Plugin/WmfMedia/Private/MediaHardwareVideoDecoding.usf"), TEXT("PassThroughPS"), SF_Pixel)
