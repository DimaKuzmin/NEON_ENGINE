#pragma once

#include "game_sv_mp.h"
#include "../xrEngine/pure_relcase.h"

#define MP_SAVE_JSON

class game_sv_freemp : public game_sv_mp, private pure_relcase
{

	typedef game_sv_mp inherited;

protected:
	xr_vector<u16> inventory_boxes;
	xr_map<u16, u32> inv_box;

public:


									game_sv_freemp();
	virtual							~game_sv_freemp();
	
	virtual		void				Create(shared_str &options);


	virtual		bool				UseSKin() const { return false; }

	virtual		LPCSTR				type_name() const { return "freemp"; };
	void __stdcall		net_Relcase(CObject* O) {};

	// helper functions
	void									AddMoneyToPlayer(game_PlayerState* ps, s32 amount);
	void									SpawnItemToActor(u16 actorId, LPCSTR name);

	virtual		void				OnPlayerReady(ClientID id_who);
	virtual		void				OnPlayerConnect(ClientID id_who);
	virtual		void				OnPlayerConnectFinished(ClientID id_who);
	virtual		void				OnPlayerDisconnect(ClientID id_who, LPSTR Name, u16 GameID);

	virtual		void				OnPlayerKillPlayer(game_PlayerState* ps_killer, game_PlayerState* ps_killed, KILL_TYPE KillType, SPECIAL_KILL_TYPE SpecialKillType, CSE_Abstract* pWeaponA);

	virtual		void				OnEvent(NET_Packet &tNetPacket, u16 type, u32 time, ClientID sender);

	virtual		void				Update();

	virtual		BOOL				OnTouch(u16 eid_who, u16 eid_what, BOOL bForced = FALSE);

	virtual		void				on_death(CSE_Abstract* e_dest, CSE_Abstract* e_src);

	virtual		void				OnPlayerTrade(NET_Packet &P, ClientID const & clientID);
	virtual		void				OnTransferMoney(NET_Packet &P, ClientID const & clientID);

	virtual		void				RespawnPlayer(ClientID id_who, bool NoSpectator);

	virtual		bool				HasSaveFile(game_PlayerState* ps);

#ifndef MP_SAVE_JSON
	virtual     void				SavePlayer(game_PlayerState* ps, CInifile* file);
	virtual     bool				LoadPlayer(game_PlayerState* ps, CInifile* file);


	virtual		void				SaveInvBox(CSE_ALifeInventoryBox* box, CInifile* file);
	virtual		void				LoadInvBox(CSE_ALifeInventoryBox* box, CInifile* file);
#else
	virtual		void				SaveJson(game_PlayerState* ps);
	virtual		bool				LoadJson(game_PlayerState* ps);

	virtual		void				SaveInventory(CSE_ALifeInventoryBox* box, string_path name);
	virtual		void				LoadInventory(CSE_ALifeInventoryBox* box, string_path name);

	virtual		bool				LoadPlayerPosition(game_PlayerState* ps, Fvector& position, Fvector& angle, float& health);

	virtual void					assign_RP(CSE_Abstract* E, game_PlayerState* ps_who);
#endif


};
