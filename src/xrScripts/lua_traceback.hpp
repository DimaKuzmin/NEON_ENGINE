////////////////////////////////////////////////////////////////////////////
//	Module 		: lua_traceback.h
//  	Created 	: 12.07.2017
//	Author		: ForserX
//	Description : Lua functionality extension
////////////////////////////////////////////////////////////////////////////
#pragma once
#include "xrScripts.h"
SCRIPT_API const char* get_traceback(lua_State *L, int depth);