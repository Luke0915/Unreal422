// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render/Device/SidebySide/DisplayClusterDeviceSideBySideOpenGL.h"
#include "Render/Device/DisplayClusterDeviceInternals.h"

#include "Render/IDisplayClusterRenderManager.h"
#include "Render/Synchronization/IDisplayClusterRenderSyncPolicy.h"

#include "DisplayClusterGlobals.h"
#include "DisplayClusterLog.h"


FDisplayClusterDeviceSideBySideOpenGL::FDisplayClusterDeviceSideBySideOpenGL()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

FDisplayClusterDeviceSideBySideOpenGL::~FDisplayClusterDeviceSideBySideOpenGL()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Windows implementation
//////////////////////////////////////////////////////////////////////////////////////////////
#if PLATFORM_WINDOWS
bool FDisplayClusterDeviceSideBySideOpenGL::Present(int32& InOutSyncInterval)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	const int srcX1 = 0;
	const int srcY1 = 0;
	const int srcX2 = BackBuffSize.X;
	const int srcY2 = BackBuffSize.Y;

	const int dstX1 = 0;
	const int dstY1 = BackBuffSize.Y;
	const int dstX2 = BackBuffSize.X;
	const int dstY2 = 0;

	FOpenGLViewport* pOglViewport = static_cast<FOpenGLViewport*>(MainViewport->GetViewportRHI().GetReference());
	check(pOglViewport);
	FPlatformOpenGLContext* const pContext = pOglViewport->GetGLContext();
	check(pContext && pContext->DeviceContext);

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, pContext->ViewportFramebuffer);
	glReadBuffer(GL_COLOR_ATTACHMENT0);

	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("Blit framebuffer: [%d,%d - %d,%d] -> [%d,%d - %d,%d]"), srcX1, srcY1, srcX2, srcY2, dstX1, dstY1, dstX2, dstY2);
	glDrawBuffer(GL_BACK);
	glBlitFramebuffer(
		srcX1, srcY1, srcX2, srcY2,
		dstX1, dstY1, dstX2, dstY2,
		GL_COLOR_BUFFER_BIT,
		GL_NEAREST);

#if !WITH_EDITOR
	pOglViewport->IssueFrameEvent();
	pOglViewport->WaitForFrameEventCompletion();
#endif

	// Update sync value with nDisplay value
	InOutSyncInterval = GetSwapInt();

	TSharedPtr<IDisplayClusterRenderSyncPolicy> SyncPolicy = GDisplayCluster->GetRenderMgr()->GetCurrentSynchronizationPolicy();
	if (SyncPolicy.IsValid())
	{
		// False results means we don't need to present current frame, the sync object already presented it
		if (!SyncPolicy->SynchronizeClusterRendering(InOutSyncInterval))
		{
			return false;
		}
	}

	::SwapBuffers(pOglViewport->GetGLContext()->DeviceContext);
	REPORT_GL_END_BUFFER_EVENT_FOR_FRAME_DUMP();

	return false;
}
#endif // PLATFORM_WINDOWS


//////////////////////////////////////////////////////////////////////////////////////////////
// IStereoRenderTargetManager
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterDeviceSideBySideOpenGL::UpdateViewport(bool bUseSeparateRenderTarget, const class FViewport& Viewport, class SViewport* ViewportWidget)
{
	FDisplayClusterDeviceBase::UpdateViewport(bUseSeparateRenderTarget, Viewport, ViewportWidget);

	// Since the GL approach performs blit in the Present method, we have to store the back buffer size for further output computations
	BackBuffSize = Viewport.GetRenderTargetTextureSizeXY();
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Linux implementation
//////////////////////////////////////////////////////////////////////////////////////////////
#if PLATFORM_LINUX
bool FDisplayClusterDeviceSideBySideOpenGL::Present(int32& InOutSyncInterval)
{
	// Forward to default implementation (should be a black screen)
	return FDisplayClusterDeviceBase::Present(InOutSyncInterval);
}
#endif
