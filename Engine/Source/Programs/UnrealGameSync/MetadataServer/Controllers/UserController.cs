// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Data.SQLite;
using System.Web.Http;
using MetadataServer.Connectors;
using MetadataServer.Models;

namespace MetadataServer.Controllers
{
	public class UserController : ApiController
	{
		public object Get(string Name)
		{
			long UserId = SqlConnector.FindOrAddUserId(Name);
			return new { Id = UserId };
		}
	}
}
