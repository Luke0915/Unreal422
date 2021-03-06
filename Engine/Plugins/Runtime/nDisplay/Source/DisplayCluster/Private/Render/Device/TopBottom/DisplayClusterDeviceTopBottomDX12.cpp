// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render/Device/TopBottom/DisplayClusterDeviceTopBottomDX12.h"

#include "Render/IDisplayClusterRenderManager.h"
#include "Render/Synchronization/IDisplayClusterRenderSyncPolicy.h"

#include "DisplayClusterGlobals.h"
#include "DisplayClusterLog.h"

#include "D3D12Viewport.h"


FDisplayClusterDeviceTopBottomDX12::FDisplayClusterDeviceTopBottomDX12()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

FDisplayClusterDeviceTopBottomDX12::~FDisplayClusterDeviceTopBottomDX12()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}


bool FDisplayClusterDeviceTopBottomDX12::Present(int32& InOutSyncInterval)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	FD3D12Viewport* Viewport = static_cast<FD3D12Viewport*>(MainViewport->GetViewportRHI().GetReference());
	check(Viewport);

#if !WITH_EDITOR
	// Issue frame event
	Viewport->IssueFrameEvent();
	// Wait until GPU finish last frame commands
	Viewport->WaitForFrameEventCompletion();
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

	// Sync policy haven't presented current frame so we do it ourselves
	return PresentImpl(Viewport, InOutSyncInterval);
}
