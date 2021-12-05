#include "stdafx.h"
#include "game_sv_freemp.h"
#include "Level.h"
#include "Actor.h"
#include "Inventory.h"
#include "Weapon.h"
#include "CustomDetector.h"

#include <fstream>;
#include "..\jsonxx\jsonxx.h"

#include <fstream>
#include <iostream>


void game_sv_freemp::SavePlayer(game_PlayerState* ps, CInifile* file)
{
	CObject* obj = Level().Objects.net_Find(ps->GameID);
	CActor* actor = smart_cast<CActor*>(obj);
  
	if (actor && actor->g_Alive())
	{
		TIItemContainer items;
		actor->inventory().AddAvailableItems(items, false);
		u32 id = 0;
		
		for (auto item : items)
		{
			if (!xr_strcmp("mp_players_rukzak", item->m_section_id.c_str()))
				continue;

			id += 1;
			string32 itemID;
			xr_sprintf(itemID, "item_%d", id);
 
			file->w_string(itemID, "section", item->m_section_id.c_str());
			file->w_u16(itemID, "slot", item->CurrValue());
			
			if (item->GetCondition() < 1)
				file->w_float(itemID, "condition", item->GetCondition());
			
			if (item->cast_weapon_ammo())
			{
				CWeaponAmmo* ammo = smart_cast<CWeaponAmmo*>(item);
				file->w_u16(itemID, "ammo_count", ammo->m_boxCurr);
			}

			if (item->cast_weapon())
			{
				CWeapon* wpn = smart_cast<CWeapon*>(item);
				file->w_u16(itemID, "ammo_count", u16(wpn->GetAmmoElapsed()));
				file->w_u8(itemID, "ammo_type", wpn->m_ammoType);
				file->w_u8(itemID, "addon_State", wpn->GetAddonsState());
				file->w_u8(itemID, "cur_scope", wpn->m_cur_scope);
			}
		}

		CCustomDetector* detector = smart_cast<CCustomDetector*>(actor->inventory().ItemFromSlot(DETECTOR_SLOT));

		if (detector)
		{
			file->w_string("detector", "section", detector->m_section_id.c_str());
		}

		file->w_u32("actor", "items_count", id);
		file->w_u32("actor", "money", ps->money_for_round);
		file->w_u32("actor", "team", ps->team);
	}
}

bool game_sv_freemp::LoadPlayer(game_PlayerState* ps, CInifile* file)
{
	if (file->section_exist("actor"))
	{
		u32 count = 0;

		if (file->line_exist("actor", "items_count"))
			count = file->r_u32("actor", "items_count");

		if (file->line_exist("actor", "money"))
			ps->money_for_round = file->r_u32("actor", "money");

		if (file->line_exist("actor", "team"))
			ps->team = file->r_u32("actor", "team");

		SpawnItemToActor(ps->GameID, "device_pda");
		SpawnItemToActor(ps->GameID, "device_torch");
		SpawnItemToActor(ps->GameID, "wpn_knife");
		SpawnItemToActor(ps->GameID, "wpn_binoc");

		Msg("[game_sv_freemp] LoadPlayer [%s] items[%d]", ps->getName(), count);

		if (file->section_exist("detector"))
		{
			LPCSTR name = file->r_string("detector", "section");

			CSE_Abstract* E = spawn_begin(name);
			E->ID_Parent = ps->GameID;
			CSE_ALifeInventoryItem* item = smart_cast<CSE_ALifeInventoryItem*>(E);
			spawn_end(E, m_server->GetServerClient()->ID);
		}

		for (u32 id = 1; id != count + 1; id++)
		{
			string32 itemID;
			xr_sprintf(itemID, "item_%d", id);


			if (file->section_exist(itemID))
			{
				LPCSTR name = file->r_string(itemID, "section");

				CSE_Abstract* E = spawn_begin(name);

				E->ID_Parent = ps->GameID;

				CSE_ALifeItem* item = smart_cast<CSE_ALifeItem*>(E);
				CSE_ALifeItemWeapon* wpn = smart_cast<CSE_ALifeItemWeapon*>(item);
				CSE_ALifeItemAmmo* ammo = smart_cast<CSE_ALifeItemAmmo*>(item);

				if (file->line_exist(itemID, "condition"))
				{
					float cond = file->r_float(itemID, "condition");
					item->m_fCondition = cond;
				}

				if (wpn)
				{
					u16 ammo_count = file->r_u16(itemID, "ammo_count");
					u8 ammo_type = file->r_u8(itemID, "ammo_type");
					u8 addon_state = file->r_u8(itemID, "addon_State");
					u8 cur_scope = file->r_u8(itemID, "cur_scope");
					wpn->a_elapsed = ammo_count;
					wpn->ammo_type = ammo_type;
					wpn->m_addon_flags.flags = addon_state;
					wpn->m_cur_scope = cur_scope;
				}

				if (ammo)
				{
					u16 ammo_current = file->r_u16(itemID, "ammo_count");
					ammo->a_elapsed = ammo_current;
				}

				if (file->line_exist(itemID, "slot"))
				{
					u16 slot = file->r_u16(itemID, "slot");
					item->slot = slot;
				}

				spawn_end(E, m_server->GetServerClient()->ID);
			}
		}
		 
		return true;
	}
	else
		return false;
}

bool game_sv_freemp::HasSaveFile(game_PlayerState* ps)
{
	if (ps->GameID == get_id(server().GetServerClient()->ID)->GameID)
		return false;

	string_path path;
 	string32 filename;
	xr_strcpy(filename, ps->getName());
	xr_strcat(filename, ".json");

	FS.update_path(path, "$mp_saves$", filename);
	CInifile* file = xr_new<CInifile>(path, true);


	return file->section_exist("actor");
}

void game_sv_freemp::SaveInvBox(CSE_ALifeInventoryBox* box, CInifile* file)
{
	u32 ids = 0;
	for (auto id : box->children)
	{
		ids += 1;
		string32 itemID;
		xr_sprintf(itemID, "item_%d", ids);

		CInventoryItem* item = smart_cast<CInventoryItem*>( Level().Objects.net_Find(id));

		if (!item)
			return;

		file->w_string(itemID, "section", item->m_section_id.c_str());
 
		if (item->GetCondition() < 1)
			file->w_float(itemID, "condition", item->GetCondition());

		if (item->cast_weapon_ammo())
		{
			CWeaponAmmo* ammo = smart_cast<CWeaponAmmo*>(item);
			file->w_u16(itemID, "ammo_count", ammo->m_boxCurr);
		}

		if (item->cast_weapon())
		{
			CWeapon* wpn = smart_cast<CWeapon*>(item);
			file->w_u16(itemID, "ammo_count", u16(wpn->GetAmmoElapsed()));
			file->w_u8(itemID, "ammo_type", wpn->m_ammoType);
			file->w_u8(itemID, "addon_State", wpn->GetAddonsState());
			file->w_u8(itemID, "cur_scope", wpn->m_cur_scope);
		}

		
	}

	file->w_u32("box", "items_count", ids);

}

void game_sv_freemp::LoadInvBox(CSE_ALifeInventoryBox* box, CInifile* file)
{
	if (!file->section_exist("box"))
		return;

	if (!box)
		return;

	u32 count = file->r_u32("box", "items_count");

	// Msg("[game_sv_freemp] LoadInvBox [%d] items[%d]", box->ID, count);
    
	for (u32 id = 1; id != count + 1; id++)
	{
		string32 itemID;
		xr_sprintf(itemID, "item_%d", id);
 
		if (file->section_exist(itemID))
		{
			LPCSTR name = file->r_string(itemID, "section");				
			CSE_Abstract* E = spawn_begin(name);

			E->ID_Parent = box->ID;

			CSE_ALifeItem* item = smart_cast<CSE_ALifeItem*>(E);
			CSE_ALifeItemWeapon* wpn = smart_cast<CSE_ALifeItemWeapon*>(item);
			CSE_ALifeItemAmmo* ammo = smart_cast<CSE_ALifeItemAmmo*>(item);

			if (file->line_exist(itemID, "condition"))
			{
				float cond = file->r_float(itemID, "condition");
				item->m_fCondition = cond;
			}

			if (wpn)
			{
				u16 ammo_count = file->r_u16(itemID, "ammo_count");
				u8 ammo_type = file->r_u8(itemID, "ammo_type");
				u8 addon_state = file->r_u8(itemID, "addon_State");
				u8 cur_scope = file->r_u8(itemID, "cur_scope");
				wpn->a_elapsed = ammo_count;
				wpn->ammo_type = ammo_type;
				wpn->m_addon_flags.flags = addon_state;
				wpn->m_cur_scope = cur_scope;
			}

			if (ammo)
			{
				u16 ammo_current = file->r_u16(itemID, "ammo_count");
				ammo->a_elapsed = ammo_current;
			}

			spawn_end(E, m_server->GetServerClient()->ID);
		}
	}
}


//Json Save File
using namespace jsonxx;

void game_sv_freemp::SaveJson(game_PlayerState* ps, shared_str name)
{ 
	CActor* pActor = smart_cast<CActor*>(Level().Objects.net_Find(ps->GameID));
	if (!pActor || !pActor->g_Alive()) return;

	TIItemContainer items;
	pActor->inventory().AddAvailableItems(items, false);

 	Array jsonInInventory;
	Object jsonMAIN;

	for (auto item : items)
	{
		if (!xr_strcmp(item->m_section_id.c_str(), "mp_players_rukzak"))
			continue;

		Object table;
		table << "section" << String(item->m_section_id.c_str());
		table << "slot" << Number(item->CurrValue());
		table << "condition" << Number(item->GetCondition());

		if (item->cast_weapon_ammo())
		{
			CWeaponAmmo* ammo = smart_cast<CWeaponAmmo*>(item);
			table << "ammo_box_count" << Number(ammo->m_boxCurr);
		}

		if (item->cast_weapon())
		{
			CWeapon* wpn = smart_cast<CWeapon*>(item);
			table << "ammo_count" << Number(wpn->GetAmmoElapsed());
			table << "ammo_type" << Number(wpn->m_ammoType);
			table << "addon_state" << Number(wpn->GetAddonsState());
			table << "cur_scope" << Number(wpn->m_cur_scope);
		}

		jsonInInventory << table;
	}

	CCustomDetector* detector = smart_cast<CCustomDetector*>(pActor->inventory().ItemFromSlot(DETECTOR_SLOT));

	if (detector)
		jsonMAIN << "Detector" << String(detector->m_section_id.c_str());

	jsonMAIN << "Items" << jsonInInventory;
	jsonMAIN << "X" << pActor->Position().x;
	jsonMAIN << "Y" << pActor->Position().y;
	jsonMAIN << "Z" << pActor->Position().z;

	jsonMAIN << "money" << Number(ps->money_for_round);
	jsonMAIN << "team" << Number(ps->team);
	jsonMAIN << "admin" << Number(ps->testFlag(GAME_PLAYER_HAS_ADMIN_RIGHTS));


	string_path path_name;

	string256 file;
	xr_strcat(file, ps->getName());
	xr_strcat(file, ".json");

	if (FS.path_exist("$mp_saves_file$"))
	{
		FS.update_path(path_name, "$mp_saves_file$", file);
	}
	else
	{
		return;
	}

	IWriter* file_W = FS.w_open(path_name);
	if (file_W)
		file_W->w_string(jsonMAIN.json().c_str());
	FS.w_close(file_W);

	/*
	std::ofstream ofile(path_name);

	if (ofile.is_open())
		ofile << jsonMAIN.json().c_str();
	ofile.close();
	*/
}

bool game_sv_freemp::LoadJson(game_PlayerState* ps, shared_str name)
{
	string_path path;
	
	string256 file;
	xr_strcat(file, ps->getName());
	xr_strcat(file, ".json");

	FS.update_path(path, "$mp_saves_file$", file);

	if (!FS.path_exist(path))
		return false;

	Object json;

	std::ifstream ifile(path);

	if (ifile.is_open())
	{
		std::string str((std::istreambuf_iterator<char>(ifile)), std::istreambuf_iterator<char>());
		json.parse(str);
 	}

	ifile.close();

	if (json.has<Number>("money"))
	{
		int money = json.get<Number>("money");

		ps->money_for_round = money;
		
	}

	if (json.has<Number>("team"))
		ps->team = json.get<Number>("team");

	if (json.has<Number>("admin"))
	{
		bool admin = json.get<Number>("admin");
		if (admin)
			ps->setFlag(GAME_PLAYER_HAS_ADMIN_RIGHTS);
	}

	signal_Syncronize();

	if (json.has<Array>("Items"))
	{
		Array items = json.get<Array>("Items");

		for (int i = 0; i != items.size(); i++)
		{
			if (items.has<Object>(i))
			{
				Object item = items.get<Object>(i);

				std::string section = { 0 };
				u16 slot, ammo_count, ammo_box_count = 0;
				float					   condition = 0;
				u8 ammo_type, addon_state, cur_scope = 0;

				if (item.has<Number>("condition"))
					condition = item.get<Number>("condition");
				if (item.has<Number>("slot"))
					slot = item.get<Number>("slot");
				if (item.has<Number>("ammo_box_count"))
					ammo_box_count = item.get<Number>("ammo_box_count");
				if (item.has<Number>("ammo_count"))
					ammo_count = item.get<Number>("ammp_count");
				if (item.has<Number>("ammo_type"))
					ammo_type = item.get<Number>("ammo_type");
				if (item.has<Number>("addon_state"))
					addon_state = item.get<Number>("addon_state");
				if (item.has<Number>("cur_scope"))
					cur_scope = item.get<Number>("cur_scope");


				if (pSettings->section_exist(section.c_str()))
				{
					CSE_Abstract* E = spawn_begin(section.c_str());

					E->ID_Parent = ps->GameID;

					CSE_ALifeItem* item = smart_cast<CSE_ALifeItem*>(E);
					CSE_ALifeItemWeapon* wpn = smart_cast<CSE_ALifeItemWeapon*>(item);
					CSE_ALifeItemAmmo* ammo = smart_cast<CSE_ALifeItemAmmo*>(item);

					if (item)
						item->slot = slot;

					if (ammo)
						ammo->a_elapsed = ammo_box_count;

					if (wpn)
					{
						wpn->a_elapsed = ammo_count;
						wpn->ammo_type = ammo_type;
						wpn->m_addon_flags.flags = addon_state;
						wpn->m_cur_scope = cur_scope;
					}

					spawn_end(E, m_server->GetServerClient()->ID);

				}
			}
		}
	}
 
	return true;
}

void game_sv_freemp::SaveInventory(game_PlayerState* ps, shared_str name)
{

}

void game_sv_freemp::LoadInventory(game_PlayerState* ps, shared_str name)
{

}


bool game_sv_freemp::LoadPlayerPosition(game_PlayerState* ps, Fvector& position, Fvector& angle)
{
	string_path path;
	FS.update_path(path, "$mp_saves_file$", ps->getName());
	if (!FS.path_exist(path))
		return false;

	Object json;

	std::ifstream ifile(path);

	if (ifile.is_open())
	{
		std::string str((std::istreambuf_iterator<char>(ifile)), std::istreambuf_iterator<char>());
		json.parse(str);
	}

	ifile.close();
	
	if (json.has<Number>("X") && json.has<Number>("Y") && json.has<Number>("Z"))
	{
		position.x = json.get<Number>("X");
		position.y = json.get<Number>("Y");
		position.z = json.get<Number>("Z");
		angle.set(0,0,0);
		return true;
	}
	
	return false;
}

void game_sv_freemp::assign_RP(CSE_Abstract* E, game_PlayerState* ps_who)
{
	Fvector angle, pos;
	
	if (LoadPlayerPosition(ps_who, angle, pos) && !ps_who->testFlag(GAME_PLAYER_MP_SAVE_LOADED))
	{
		E->position().set(pos);
		E->angle().set(angle);
	}
	else
		return inherited::assign_RP(E, ps_who);
}


