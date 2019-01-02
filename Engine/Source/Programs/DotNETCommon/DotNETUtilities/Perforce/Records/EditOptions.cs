// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Tools.DotNETCommon.Perforce
{
	/// <summary>
	/// Options for the p4 edit command
	/// </summary>
	[Flags]
	public enum EditOptions
	{
		/// <summary>
		/// No options specified
		/// </summary>
		None = 0,

		/// <summary>
		/// Keep existing workspace files; mark the file as open for edit even if the file is not in the client view. 
		/// </summary>
		KeepWorkspaceFiles = 1,

		/// <summary>
		/// Preview which files would be opened for edit, without actually changing any files or metadata.
		/// </summary>
		PreviewOnly = 2,
	}
}
