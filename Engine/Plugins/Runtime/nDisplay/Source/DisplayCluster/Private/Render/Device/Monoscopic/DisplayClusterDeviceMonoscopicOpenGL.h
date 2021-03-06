// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Device/DisplayClusterDeviceMonoscopicBase.h"
#include "Render/Device/DisplayClusterDeviceInternals.h"


/**
 * Monoscopic render device (OpenGL3, OpenGL4)
 */
class FDisplayClusterDeviceMonoscopicOpenGL
	: public FDisplayClusterDeviceMonoscopicBase
{
public:
	FDisplayClusterDeviceMonoscopicOpenGL();
	virtual ~FDisplayClusterDeviceMonoscopicOpenGL();

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// FRHICustomPresent
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Present(int32& InOutSyncInterval) override;

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IStereoRenderTargetManager
	//////////////////////////////////////////////////////////////////////////////////////////////
	void UpdateViewport(bool bUseSeparateRenderTarget, const class FViewport& Viewport, class SViewport* ViewportWidget) override;

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// FDisplayClusterDeviceBase
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void CopyTextureToBackBuffer_RenderThread(FRHICommandListImmediate& RHICmdList, FTexture2DRHIParamRef BackBuffer, FTexture2DRHIParamRef SrcTexture, FVector2D WindowSize) const override
	{
		// Forbid the default FDisplayClusterDeviceBase::CopyTextureToBackBuffer_RenderThread implementation. The OpenGL copies data in the Present method.
	}

private:
	// Set up swap interval for upcoming buffer swap
	void UpdateSwapInterval(int32 swapInt) const;

private:
	FIntPoint BackBuffSize = { 0, 0 };
};
