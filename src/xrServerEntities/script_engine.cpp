////////////////////////////////////////////////////////////////////////////
//	Module 		: script_engine.cpp
//	Created 	: 01.04.2004
//  Modified 	: 01.04.2004
//	Author		: Dmitriy Iassenev
//	Description : XRay Script Engine
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "script_engine.h"
#include "ai_space.h"
#include "object_factory.h"
#include "script_process.h"

#ifdef USE_DEBUGGER
#	ifndef USE_LUA_STUDIO
#		include "script_debugger.h"
#	else // #ifndef USE_LUA_STUDIO
#		include "lua_studio.h"
		typedef cs::lua_studio::create_world_function_type			create_world_function_type;
		typedef cs::lua_studio::destroy_world_function_type			destroy_world_function_type;

		static create_world_function_type	s_create_world				= 0;
		static destroy_world_function_type	s_destroy_world				= 0;
		static HMODULE						s_script_debugger_handle	= 0;
		static LogCallback					s_old_log_callback			= 0;
#	endif // #ifndef USE_LUA_STUDIO
#endif

#ifndef XRSE_FACTORY_EXPORTS
#	ifdef DEBUG
#		include "ai_debug.h"
		extern Flags32 psAI_Flags;
#	endif
#endif

void jit_command(lua_State*, LPCSTR);

#if defined(USE_DEBUGGER) && defined(USE_LUA_STUDIO)
static void log_callback			(LPCSTR message)
{
	if (s_old_log_callback)
		s_old_log_callback			(message);

	if (!ai().script_engine().debugger())
		return;

	ai().script_engine().debugger()->add_log_line	(message);
}

static void initialize_lua_studio	( lua_State* state, cs::lua_studio::world*& world, lua_studio_engine*& engine)
{
	engine							= 0;
	world							= 0;

	u32 const old_error_mode		= SetErrorMode(SEM_FAILCRITICALERRORS);
	s_script_debugger_handle		= LoadLibrary(CS_LUA_STUDIO_BACKEND_FILE_NAME);
	SetErrorMode					(old_error_mode);
	if (!s_script_debugger_handle) {
		Msg							("! cannot load %s dynamic library", CS_LUA_STUDIO_BACKEND_FILE_NAME);
		return;
	}

	R_ASSERT2						(s_script_debugger_handle, "can't load script debugger library");

	s_create_world					= (create_world_function_type)
		GetProcAddress(
			s_script_debugger_handle,
			"_cs_lua_studio_backend_create_world@12"
		);
	R_ASSERT2						(s_create_world, "can't find function \"cs_lua_studio_backend_create_world\"");

	s_destroy_world					= (destroy_world_function_type)
		GetProcAddress(
			s_script_debugger_handle,
			"_cs_lua_studio_backend_destroy_world@4"
		);
	R_ASSERT2						(s_destroy_world, "can't find function \"cs_lua_studio_backend_destroy_world\" in the library");

	engine							= xr_new<lua_studio_engine>();
	world							= s_create_world( *engine, false, false );
	VERIFY							(world);

	s_old_log_callback				= SetLogCB(&log_callback);

	jit_command						(state, "debug=2");
	jit_command						(state, "off");

	world->add						(state);
}

static void finalize_lua_studio		( lua_State* state, cs::lua_studio::world*& world, lua_studio_engine*& engine)
{
	world->remove					(state);

	VERIFY							(world);
	s_destroy_world					(world);
	world							= 0;

	VERIFY							(engine);
	xr_delete						(engine);

	FreeLibrary						(s_script_debugger_handle);
	s_script_debugger_handle		= 0;

	SetLogCB						(s_old_log_callback);
}

void CScriptEngine::try_connect_to_debugger		()
{
	if (m_lua_studio_world)
		return;

	initialize_lua_studio			( lua(), m_lua_studio_world, m_lua_studio_engine );
}

void CScriptEngine::disconnect_from_debugger	()
{
	if (!m_lua_studio_world)
		return;

	finalize_lua_studio				( lua(), m_lua_studio_world, m_lua_studio_engine );
}
#endif // #if defined(USE_DEBUGGER) && defined(USE_LUA_STUDIO)

CScriptEngine::CScriptEngine			()
{
	m_stack_level			= 0;
	m_reload_modules		= false;
	m_last_no_file_length	= 0;
	*m_last_no_file			= 0;

#ifdef USE_DEBUGGER
#	ifndef USE_LUA_STUDIO
		m_scriptDebugger	= NULL;
		restartDebugger		();	
#	else // #ifndef USE_LUA_STUDIO
		m_lua_studio_world	= 0;
#	endif // #ifndef USE_LUA_STUDIO
#endif
}

CScriptEngine::~CScriptEngine			()
{
	while (!m_script_processes.empty())
		remove_script_process(m_script_processes.begin()->first);

#ifdef DEBUG
	flush_log					();
#endif // DEBUG

#ifdef USE_DEBUGGER
#	ifndef USE_LUA_STUDIO
		xr_delete				(m_scriptDebugger);
#	else // #ifndef USE_LUA_STUDIO
		disconnect_from_debugger();
#	endif // #ifndef USE_LUA_STUDIO
#endif
}

void CScriptEngine::unload				()
{
	lua_settop				(lua(),m_stack_level);
	m_last_no_file_length	= 0;
	*m_last_no_file			= 0;
}

#include <sstream>

void LogLuaVariable(lua_State* L, const char* Name, int Level, bool bOpenTable, int Index /*= -1*/)
{
	int LuaType = lua_type(L, Index);
	if (LuaType == -1 || LuaType == LUA_TFUNCTION)
		return;

	const char* TypeName = lua_typename(L, LuaType);

	char TabBuffer[32];
	ZeroMemory(TabBuffer, sizeof(TabBuffer));
	std::memset(TabBuffer, '\t', Level);

	auto LuaLogTable = [](lua_State* l, const char* S, int level, int index /*= -1*/) {
		if (lua_istable(l, index))
		{
			lua_pushnil(l);  /* first key */
			while (lua_next(l, index - 1) != 0) {
				char sname[256];
				char sFullName[256];
				sprintf(sname, "%s", lua_tostring(l, index - 1));
				sprintf(sFullName, "%s.%s", S, sname);
				LogLuaVariable(l, sFullName, level + 1, false, index);

				lua_pop(l, 1);  /* removes `value'; keeps `key' for next iteration */
			}
		}
	};

	
	char* lua_types[9] =
	{
		"LUA_TNIL", 
		"LUA_TBOOLEAN", 
		"LUA_TLIGHTUSERDATA",
		"LUA_TNUMBER",
		"LUA_TSTRING", 
		"LUA_TTABLE",
		"LUA_TFUNCTION", 
		"LUA_TUSERDATA", 
		"LUA_TTHREAD" 
	};


	switch (LuaType) 
	{
		case LUA_TNUMBER: {
			Msg("%s %s %s : %.02f", TabBuffer, TypeName, Name, lua_tonumber(L, Index));
			break;
		}
		case LUA_TBOOLEAN: {
			Msg("%s %s %s : %.02f", TabBuffer, TypeName, Name, lua_toboolean(L, Index) == 1 ? TEXT("true") : TEXT("false"));
			break;
		}
		case LUA_TSTRING: {
			Msg("%s %s %s : %s", TabBuffer, TypeName, Name, lua_tostring(L, Index));
			break;
		}
		case LUA_TTABLE: {
			if (bOpenTable) {
				Msg("%s %s %s : Table", TabBuffer, TypeName, Name);

				LuaLogTable(L, Name, Level + 1, Index);
			} else {
				Msg("%s %s %s : Table [...]", TabBuffer, TypeName, Name);
			}
			break;
		}
		case LUA_TUSERDATA: {
			void* UserDataPtr = lua_touserdata(L, Index);
			const luabind::detail::object_rep* obj = static_cast<luabind::detail::object_rep*>(UserDataPtr);
			const luabind::detail::class_rep* objectClass = obj->crep();

			if (objectClass != nullptr) {
				std::stringstream ss;
				ss << UserDataPtr;
				Msg("%s Userdata: %s(%s:0x%s)", TabBuffer, Name, objectClass->name(), ss.str().c_str());
			} else {
				Msg("%s Userdata: %s", TabBuffer, Name);
			}

			return;
			break;
		}
		default: 
		{
			if (LuaType == -1)
				Msg("--- None Type");
			else 
				Msg("[not available]: Type[%s]", lua_types[LuaType]);
			break;
		}
	}
}


void printLuaTraceback(lua_State* L)
{
	lua_Debug lua_debug;
	int level = 0;
	Msg("--- CallStack: ");
	const char* LocalVarName;

	while (lua_getstack(L, level, &lua_debug))
	{
		lua_getinfo(L, "Slnt", &lua_debug);

		std::string functionName = lua_debug.name ? lua_debug.name : "(unknown)";
		std::string source = lua_debug.source ? lua_debug.source : "(unknown)";
		int line = lua_debug.currentline;

		Msg("[%d] %s (%s : %d)", level, functionName.c_str(), source.c_str(), line);

		level++;		
	}
   
	level=0;

	while(lua_getstack(L, level, &lua_debug))
	{
		bool bPringName = true;
		int Iter = 1;
	 
		//if (false)
		while ( (LocalVarName = lua_getlocal(L, &lua_debug, Iter++)) != NULL) 
		{
			if (bPringName) 
			{
				Msg("Local variables[%d]:", level);
				bPringName = false;
			}
			LogLuaVariable(L, LocalVarName, 1, true, -1);

			lua_pop(L, 1);  
		}

		level++;
	}


	Msg("--- Callstack End");
}


int CScriptEngine::lua_panic			(lua_State *L)
{
	Msg("--- Callstack for lua_panic");
	printLuaTraceback(L);
	print_output	(L,"PANIC",LUA_ERRRUN);
	return			(0);
}


void CScriptEngine::lua_error			(lua_State *L)
{
	Msg("--- Callstack for lua_error");
	printLuaTraceback(L);

	print_output			(L,"",LUA_ERRRUN);
	ai().script_engine().on_error	(L);

#if !XRAY_EXCEPTIONS
	Debug.fatal				(DEBUG_INFO,"LUA error: %s",lua_tostring(L,-1));
#else
	throw					lua_tostring(L,-1);
#endif
}

int  CScriptEngine::lua_pcall_failed	(lua_State *L)
{
	Msg("--- Callstack for lua_pcall_failed");
	printLuaTraceback(L);

	print_output			(L,"",LUA_ERRRUN);
	ai().script_engine().on_error	(L);

#if !XRAY_EXCEPTIONS
	Debug.fatal				(DEBUG_INFO,"LUA error: %s",lua_isstring(L,-1) ? lua_tostring(L,-1) : "");
#endif
	if (lua_isstring(L,-1))
		lua_pop				(L,1);
	return					(LUA_ERRRUN);
}

void lua_cast_failed					(lua_State *L, LUABIND_TYPE_INFO info)
{
	Msg("--- Callstack for lua_cast_failed");
	printLuaTraceback(L);

	CScriptEngine::print_output	(L,"",LUA_ERRRUN);

	Debug.fatal				(DEBUG_INFO,"LUA error: cannot cast lua value to %s",info->name());
}

void CScriptEngine::setup_callbacks		()
{
#ifdef USE_DEBUGGER
#	ifndef USE_LUA_STUDIO
		if( debugger() )
			debugger()->PrepareLuaBind	();
#	endif // #ifndef USE_LUA_STUDIO
#endif

#ifdef USE_DEBUGGER
#	ifndef USE_LUA_STUDIO
		if (!debugger() || !debugger()->Active() ) 
#	endif // #ifndef USE_LUA_STUDIO
#endif
	{
#if !XRAY_EXCEPTIONS
//		luabind::set_error_callback		(CScriptEngine::lua_error);
#endif

//#ifndef MASTER_GOLD
		luabind::set_pcall_callback		(CScriptEngine::lua_pcall_failed);
//#endif // MASTER_GOLD
	}

//#if !XRAY_EXCEPTIONS
//	luabind::set_cast_failed_callback	(lua_cast_failed);
//#endif
	lua_atpanic							(lua(),CScriptEngine::lua_panic);
}

#ifdef DEBUG
#	include "script_thread.h"
void CScriptEngine::lua_hook_call		(lua_State *L, lua_Debug *dbg)
{
	if (ai().script_engine().current_thread())
		ai().script_engine().current_thread()->script_hook(L,dbg);
	else
		ai().script_engine().m_stack_is_ready	= true;
}
#endif

int auto_load				(lua_State *L)
{
	if ((lua_gettop(L) < 2) || !lua_istable(L,1) || !lua_isstring(L,2)) {
		lua_pushnil	(L);
		return		(1);
	}

	ai().script_engine().process_file_if_exists(lua_tostring(L,2),false);
	lua_rawget		(L,1);
	return			(1);
}

void CScriptEngine::setup_auto_load		()
{
	luaL_newmetatable					(lua(),"XRAY_AutoLoadMetaTable");
	lua_pushstring						(lua(),"__index");
	lua_pushcfunction					(lua(), auto_load);
	lua_settable						(lua(),-3);
	lua_pushstring 						(lua(),"_G"); 
	lua_gettable 						(lua(),LUA_GLOBALSINDEX); 
	luaL_getmetatable					(lua(),"XRAY_AutoLoadMetaTable");
	lua_setmetatable					(lua(),-2);
	//. ??????????
	// lua_settop							(lua(),-0);
}

extern void export_classes(lua_State *L);

void CScriptEngine::init				()
{
#ifdef USE_LUA_STUDIO
	bool lua_studio_connected = !!m_lua_studio_world;
	if (lua_studio_connected)
		m_lua_studio_world->remove		(lua());
#endif // #ifdef USE_LUA_STUDIO

	CScriptStorage::reinit				();

#ifdef USE_LUA_STUDIO
	if (m_lua_studio_world || strstr(Core.Params, "-lua_studio")) {
		if (!lua_studio_connected)
			try_connect_to_debugger		();
		else {
			jit_command					(lua(), "debug=2");
			jit_command					(lua(), "off");
			m_lua_studio_world->add		(lua());
		}
	}
#endif // #ifdef USE_LUA_STUDIO

	luabind::open						(lua());
	setup_callbacks						();
	export_classes						(lua());
	setup_auto_load						();

#ifdef DEBUG
	m_stack_is_ready					= true;
#endif

#ifndef USE_LUA_STUDIO
#	ifdef DEBUG
#		if defined(USE_DEBUGGER) && !defined(USE_LUA_STUDIO)
			if( !debugger() || !debugger()->Active()  )
#		endif // #if defined(USE_DEBUGGER) && !defined(USE_LUA_STUDIO)
				lua_sethook					(lua(),lua_hook_call,	LUA_MASKLINE|LUA_MASKCALL|LUA_MASKRET,	0);
#	endif // #ifdef DEBUG
#endif // #ifndef USE_LUA_STUDIO
//	lua_sethook							(lua(), lua_hook_call,	LUA_MASKLINE|LUA_MASKCALL|LUA_MASKRET,	0);

	bool								save = m_reload_modules;
	m_reload_modules					= true;
	process_file_if_exists				("_G",false);
	m_reload_modules					= save;

	register_script_classes				();
	object_factory().register_script	();

#ifdef XRGAME_EXPORTS
	load_common_scripts					();
#endif
	m_stack_level						= lua_gettop(lua());
}

void CScriptEngine::remove_script_process	(const EScriptProcessors &process_id)
{
	CScriptProcessStorage::iterator	I = m_script_processes.find(process_id);
	if (I != m_script_processes.end()) {
		xr_delete						((*I).second);
		m_script_processes.erase		(I);
	}
}

void CScriptEngine::load_common_scripts()
{
#ifdef DBG_DISABLE_SCRIPTS
	return;
#endif
	string_path		S;
	FS.update_path	(S,"$game_config$","script.ltx");
	CInifile		*l_tpIniFile = xr_new<CInifile>(S);
	R_ASSERT		(l_tpIniFile);
	if (!l_tpIniFile->section_exist("common")) {
		xr_delete			(l_tpIniFile);
		return;
	}

	if (l_tpIniFile->line_exist("common","script")) {
		LPCSTR			caScriptString = l_tpIniFile->r_string("common","script");
		u32				n = _GetItemCount(caScriptString);
		string256		I;
		for (u32 i=0; i<n; ++i) {
			process_file(_GetItem(caScriptString,i,I));
			xr_strcat	(I,"_initialize");
			if (object("_G",I,LUA_TFUNCTION)) {
//				lua_dostring			(lua(),xr_strcat(I,"()"));
				luabind::functor<void>	f;
				R_ASSERT				(functor(I,f));
				f						();
			}
		}
	}

	xr_delete			(l_tpIniFile);
}

void CScriptEngine::process_file_if_exists	(LPCSTR file_name, bool warn_if_not_exist)
{
	u32						string_length = xr_strlen(file_name);
	if (!warn_if_not_exist && no_file_exists(file_name,string_length))
		return;

	string_path				S,S1;
	if (m_reload_modules || (*file_name && !namespace_loaded(file_name))) {
		FS.update_path		(S,"$game_scripts$",strconcat(sizeof(S1),S1,file_name,".script"));
		if (!warn_if_not_exist && !FS.exist(S)) {
#ifdef DEBUG
#	ifndef XRSE_FACTORY_EXPORTS
			if (psAI_Flags.test(aiNilObjectAccess))
#	endif
			{
				print_stack			();
				Msg					("* trying to access variable %s, which doesn't exist, or to load script %s, which doesn't exist too",file_name,S1);
				m_stack_is_ready	= true;
			}
#endif
			add_no_file		(file_name,string_length);
			return;
		}
#ifndef MASTER_GOLD
		Msg					("* loading script %s",S1);
#endif // MASTER_GOLD
		m_reload_modules	= false;
		load_file_into_namespace(S,*file_name ? file_name : "_G");
	}
}

void CScriptEngine::process_file	(LPCSTR file_name)
{
	process_file_if_exists	(file_name,true);
}

void CScriptEngine::process_file	(LPCSTR file_name, bool reload_modules)
{
	m_reload_modules		= reload_modules;
	process_file_if_exists	(file_name,true);
	m_reload_modules		= false;
}

void CScriptEngine::register_script_classes		()
{
#ifdef DBG_DISABLE_SCRIPTS
	return;
#endif
	string_path					S;
	FS.update_path				(S,"$game_config$","script.ltx");
	CInifile					*l_tpIniFile = xr_new<CInifile>(S);
	R_ASSERT					(l_tpIniFile);

	if (!l_tpIniFile->section_exist("common")) {
		xr_delete				(l_tpIniFile);
		return;
	}

	m_class_registrators		= READ_IF_EXISTS(l_tpIniFile,r_string,"common","class_registrators","");
	xr_delete					(l_tpIniFile);

	u32							n = _GetItemCount(*m_class_registrators);
	string256					I;
	for (u32 i=0; i<n; ++i) {
		_GetItem				(*m_class_registrators,i,I);
		luabind::functor<void>	result;
		if (!functor(I,result)) {
			script_log			(eLuaMessageTypeError,"Cannot load class registrator %s!",I);
			continue;
		}
		result					(const_cast<CObjectFactory*>(&object_factory()));
	}
}

bool CScriptEngine::function_object(LPCSTR function_to_call, luabind::object &object, int type)
{
	if (!xr_strlen(function_to_call))
		return				(false);

	string256				name_space, function;

	parse_script_namespace	(function_to_call,name_space,sizeof(name_space),function,sizeof(function));
	if (xr_strcmp(name_space,"_G")) {
		LPSTR				file_name = strchr(name_space,'.');
		if (!file_name)
			process_file	(name_space);
		else {
			*file_name		= 0;
			process_file	(name_space);
			*file_name		= '.';
		}
	}

	if (!this->object(name_space,function,type))
		return				(false);

	luabind::object			lua_namespace	= this->name_space(name_space);
	object					= lua_namespace[function];
	return					(true);
}

#if defined(USE_DEBUGGER) && !defined(USE_LUA_STUDIO)
void CScriptEngine::stopDebugger				()
{
	if (debugger()){
		xr_delete	(m_scriptDebugger);
		Msg			("Script debugger succesfully stoped.");
	}
	else
		Msg			("Script debugger not present.");
}

void CScriptEngine::restartDebugger				()
{
	if(debugger())
		stopDebugger();

	m_scriptDebugger = xr_new<CScriptDebugger>();
	debugger()->PrepareLuaBind();
	Msg				("Script debugger succesfully restarted.");
}
#endif // #if defined(USE_DEBUGGER) && !defined(USE_LUA_STUDIO)

bool CScriptEngine::no_file_exists	(LPCSTR file_name, u32 string_length)
{
	if (m_last_no_file_length != string_length)
		return				(false);

	return					(!memcmp(m_last_no_file,file_name,string_length*sizeof(char)));
}

void CScriptEngine::add_no_file		(LPCSTR file_name, u32 string_length)
{
	m_last_no_file_length	= string_length;
	CopyMemory				(m_last_no_file,file_name,(string_length+1)*sizeof(char));
}

void CScriptEngine::collect_all_garbage	()
{
	lua_gc					(lua(),LUA_GCCOLLECT,0);
	lua_gc					(lua(),LUA_GCCOLLECT,0);
}

void CScriptEngine::on_error			(lua_State* state)
{
#if defined(USE_DEBUGGER) && defined(USE_LUA_STUDIO)
	if (!debugger())
		return;

	debugger()->on_error	( state );
#endif // #if defined(USE_DEBUGGER) && defined(USE_LUA_STUDIO)
}