// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MPCDIShader.h"

#include "CoreMinimal.h"
#include "RHI.h"
#include "RHIResources.h"

#include "CommonRenderResources.h"
#include "PixelShaderUtils.h"

#include "ShaderParameterUtils.h"

// Select mpcdi stereo mode
 enum class EVarMPCDIShaderType : uint8
{
	Default,
	Passthrought,
	ShowWarpTexture,
	Disable,
};
static int CVarMPCDIShaderType_Value = (int)EVarMPCDIShaderType::Default;
static FAutoConsoleVariableRef CVarMPCDIShaderType(
	TEXT("nDisplay.render.mpcdi.shader"),
	CVarMPCDIShaderType_Value,
	TEXT("Select shader for mpcdi:\n (0 = default warp shader)\n(1 = PassThrought shader)\n(2 = ShowWarpTexture shader)\n(3 = disable warp shader)\n")
);



#define MPCDIShaderFileName TEXT("/Plugin/nDisplay/Private/MPCDIShaders.usf")

// Implement shaders inside UE4
IMPLEMENT_SHADER_TYPE(, FMpcdiWarpVS, MPCDIShaderFileName, TEXT("MainVS"), SF_Vertex);

IMPLEMENT_SHADER_TYPE(template<>, FMPCDIPassThroughtPS, MPCDIShaderFileName, TEXT("PassThrought_PS"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, FMPCDIShowWarpTexture, MPCDIShaderFileName, TEXT("ShowWarpTexture_PS"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, FMPCDIWarpAndBlendPS, MPCDIShaderFileName, TEXT("WarpAndBlend_PS"), SF_Pixel);

#if 0
//Cubemap support
IMPLEMENT_SHADER_TYPE(template<>, FMPCDIWarpAndBlendCubemapPS, MPCDIShaderFileName, TEXT("WarpAndBlendCube_PS"), SF_Pixel);
#endif
#if 0
// Gpu perfect frustum
IMPLEMENT_SHADER_TYPE(template<>, FMPCDICalcBoundBoxPS, MPCDIShaderFileName, TEXT("BuildProjectedAABB_PS"), SF_Pixel);
#endif



static void GetUniformBufferParameters(IMPCDI::FTextureWarpData& WarpData, FVector4& OutPosScaleBias, FVector4& OutUVScaleBias, FVector4& OutInvTargetSizeAndTextureSize)
{
	FIntPoint WarpDataSrcSize = WarpData.SrcTexture->GetSizeXY();
	FIntPoint WarpDataDstSize = WarpData.DstTexture->GetSizeXY();

	float U = WarpData.SrcRect.Min.X / (float)WarpDataSrcSize.X;
	float V = WarpData.SrcRect.Min.Y / (float)WarpDataSrcSize.Y;
	float USize = WarpData.SrcRect.Width() / (float)WarpDataSrcSize.X;
	float VSize = WarpData.SrcRect.Height() / (float)WarpDataSrcSize.Y;

	OutUVScaleBias = FVector4(USize, VSize, U, V);

	OutPosScaleBias = FVector4(1, 1, 0, 0);
	OutInvTargetSizeAndTextureSize = FVector4(1, 1, 1, 1);
}

static void GetStereoMatrix(IMPCDI::FTextureWarpData& WarpData, FMatrix& OutStereoMatrix)
{
	FIntPoint WarpDataSrcSize = WarpData.SrcTexture->GetSizeXY();
	FIntPoint WarpDataDstSize = WarpData.DstTexture->GetSizeXY();

	OutStereoMatrix = FMatrix::Identity;
	OutStereoMatrix.M[0][0] = float(WarpData.SrcRect.Width()) / float(WarpDataSrcSize.X);
	OutStereoMatrix.M[1][1] = float(WarpData.SrcRect.Height()) / float(WarpDataSrcSize.Y);
	OutStereoMatrix.M[3][0] = float(WarpData.SrcRect.Min.X) / float(WarpDataSrcSize.X);
	OutStereoMatrix.M[3][1] = float(WarpData.SrcRect.Min.Y) / float(WarpDataSrcSize.Y);
}



static void GetViewportRect(const IMPCDI::FTextureWarpData& TextureWarpData, const MPCDI::FMPCDIRegion& MPCDIRegion, FIntRect& OutViewportRect)
{
	int vpPosX = TextureWarpData.DstRect.Min.X;
	int vpPosY = TextureWarpData.DstRect.Min.Y;
	int vpSizeX = TextureWarpData.DstRect.Width();
	int vpSizeY = TextureWarpData.DstRect.Height();

	OutViewportRect.Min = FIntPoint(vpPosX, vpPosY);
	OutViewportRect.Max = FIntPoint(vpPosX + vpSizeX, vpPosY + vpSizeY);
}


static EMPCDIShader GetPixelShaderType(FMPCDIData *MPCDIData)
{
	if (MPCDIData && MPCDIData->IsValid())
	{
		switch (MPCDIData->GetProfileType())
		{
		case EMPCDIProfileType::mpcdi_A3D:
		case EMPCDIProfileType::mpcdi_SL:
			if (MPCDIData->IsCubeMapEnabled())
			{
#if 0
				// Cubemap support
				return EMPCDIShader::WarpAndBlendCubeMap;
#endif
				return EMPCDIShader::Invalid;
			}

		case EMPCDIProfileType::mpcdi_2D:
		case EMPCDIProfileType::mpcdi_3D:
			return EMPCDIShader::WarpAndBlend;

		case EMPCDIProfileType::Invalid:
			return EMPCDIShader::PassThrought;
		};
	}

	return EMPCDIShader::Invalid;
}



enum class EColorOPMode :uint8
{
	Default,
	AddAlpha,
	AddInvAlpha,
};

template<uint32 PixelShaderType>
static bool CompleteWarpTempl(FRHICommandListImmediate& RHICmdList, IMPCDI::FTextureWarpData& TextureWarpData, const IMPCDI::FShaderInputData &ShaderInputData, MPCDI::FMPCDIRegion& MPCDIRegion, EColorOPMode ColorOp)
{
	FMpcdiWarpDrawRectangleParameters VSData;
	GetUniformBufferParameters(TextureWarpData, VSData.DrawRectanglePosScaleBias, VSData.DrawRectangleUVScaleBias, VSData.DrawRectangleInvTargetSizeAndTextureSize);

	{
		//Clear viewport before render
		FIntRect MPCDIViewportRect;
		GetViewportRect(TextureWarpData, MPCDIRegion, MPCDIViewportRect);

		RHICmdList.SetViewport(MPCDIViewportRect.Min.X, MPCDIViewportRect.Min.Y, 0.0f, MPCDIViewportRect.Max.X, MPCDIViewportRect.Max.Y, 1.0f);
		//DrawClearQuad(RHICmdList, FLinearColor::Black);
	}


	// Set the graphic pipeline state.
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Never>::GetRHI();
	
	switch (ColorOp)
	{
	case EColorOPMode::Default:     GraphicsPSOInit.BlendState = TStaticBlendState <>::GetRHI(); break;
	case EColorOPMode::AddAlpha:    GraphicsPSOInit.BlendState = TStaticBlendState <CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_InverseDestAlpha, BF_One>::GetRHI(); break;
	case EColorOPMode::AddInvAlpha: GraphicsPSOInit.BlendState = TStaticBlendState <CW_RGBA, BO_Add, BF_InverseSourceAlpha, BF_SourceAlpha, BO_Add, BF_InverseDestAlpha, BF_One>::GetRHI(); break;
	}

	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();

	TShaderMap<FGlobalShaderType>* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FMpcdiWarpVS>                  VertexShader(ShaderMap);
	TShaderMapRef<TMpcdiWarpBasePS<PixelShaderType> > PixelShader(ShaderMap);

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;// GetVertexDeclarationFVector4();
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;


	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

	VertexShader->SetParameters(RHICmdList, VertexShader->GetVertexShader(), VSData);
	PixelShader->SetParameters(RHICmdList, PixelShader->GetPixelShader(), TextureWarpData.SrcTexture, ShaderInputData, MPCDIRegion);

	{
		// Render quad
		FPixelShaderUtils::DrawFullscreenQuad(RHICmdList, 1);
	}

	return true;
}

bool FMPCDIShader::ApplyWarpBlend(FRHICommandListImmediate& RHICmdList, IMPCDI::FTextureWarpData& TextureWarpData, IMPCDI::FShaderInputData& ShaderInputData, FMPCDIData* MPCDIData)
{
	check(IsInRenderingThread());

	// Map ProfileType to shader type to use
	EMPCDIShader PixelShaderType = GetPixelShaderType(MPCDIData);
	if (EMPCDIShader::Invalid == PixelShaderType)
	{
		//@todo: handle error
		return false;
	}

	FMatrix StereoMatrix;
	GetStereoMatrix(TextureWarpData, StereoMatrix);

	MPCDI::FMPCDIRegion* MPCDIRegion = nullptr;
	if (MPCDIData && MPCDIData->IsValid())
	{
		MPCDIRegion = MPCDIData->GetRegion(ShaderInputData.RegionLocator);
	}

	if (MPCDIRegion == nullptr)
	{
		//Handle error
		return false;
	}
		
	switch (CVarMPCDIShaderType_Value) {
	case (int)EVarMPCDIShaderType::Passthrought:
		PixelShaderType = EMPCDIShader::PassThrought;
		break;
	case (int)EVarMPCDIShaderType::ShowWarpTexture:
		PixelShaderType = EMPCDIShader::ShowWarpTexture;
		break;
	case (int)EVarMPCDIShaderType::Disable:
		return false;
		break;
	};
	
	{
		// Do single warp render pass
		bool bIsRenderSuccess = false;
		FRHIRenderPassInfo RPInfo(TextureWarpData.DstTexture, ERenderTargetActions::Load_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("DisplayClusterMPCDIWarpBlendShader"));
		{
			switch (PixelShaderType)
			{
			case EMPCDIShader::PassThrought:
				ShaderInputData.UVMatrix = StereoMatrix;
				bIsRenderSuccess = CompleteWarpTempl<(int)EMPCDIShader::PassThrought>(RHICmdList, TextureWarpData, ShaderInputData, *MPCDIRegion, EColorOPMode::Default);
				break;
			case EMPCDIShader::ShowWarpTexture:
				ShaderInputData.UVMatrix = ShaderInputData.Frustum.UVMatrix*MPCDIData->GetTextureMatrix()*MPCDIRegion->GetRegionMatrix()*StereoMatrix;
				bIsRenderSuccess = CompleteWarpTempl<(int)EMPCDIShader::WarpAndBlend>(RHICmdList, TextureWarpData, ShaderInputData, *MPCDIRegion, EColorOPMode::Default);
				bIsRenderSuccess = CompleteWarpTempl<(int)EMPCDIShader::ShowWarpTexture>(RHICmdList, TextureWarpData, ShaderInputData, *MPCDIRegion, EColorOPMode::AddAlpha);
				break;

			case EMPCDIShader::WarpAndBlend:
				ShaderInputData.UVMatrix = ShaderInputData.Frustum.UVMatrix*MPCDIData->GetTextureMatrix()*MPCDIRegion->GetRegionMatrix()*StereoMatrix;
				bIsRenderSuccess = CompleteWarpTempl<(int)EMPCDIShader::WarpAndBlend>(RHICmdList, TextureWarpData, ShaderInputData, *MPCDIRegion, EColorOPMode::Default);
				break;

#if 0
				//Cubemap support
			case EMPCDIShader::WarpAndBlendCubeMap:
				//! Not implemented
				break;
#endif
#if 0
				// GPU perfect frustum calc
			case EMPCDIShader::BuildProjectedAABB:
				//! Not implemented
				break;
#endif
			};
		}

		RHICmdList.EndRenderPass();

		if (bIsRenderSuccess)
		{
			return true;
		}
	}

	// Render pass failed, handle error
	return false;
}
