#include "StdAfx.h"
#include "Actor.h"
#include "actor_anim_defs.h"
#include "Inventory.h"
#include "../Include/xrRender/Kinematics.h"


extern int ANIM_SELECTED;

void callbackAnim(CBlend* blend)
{
	CActor* act = (CActor*)blend->CallbackParam;
	if (act)
		act->CanChange = true;
}
  
bool CActor::MpAnimationMODE() const
{
	if (Level().CurrentControlEntity() == this)
	{
		if (!CanChange)
			return true;

		if (!InPlay || !MidPlay || !OutPlay)
			return true;

		return ANIM_SELECTED > 0;
	}
	else
		return !CanChange;
}

void SActorStateAnimation::CreateAnimationsScripted(IKinematicsAnimated* K)
{
	string_path filepath;
	FS.update_path(filepath, "$game_config$", "actor_anims.ltx");
	CInifile* file = xr_new<CInifile>(filepath, true, true);

	if (file && file->section_exist("animations"))
	{
		u32 count = file->r_u32("animations", "count");
		for (int i = 0; i < count; i++)
		{
			string32 tmp = { 0 };
			itoa(i, tmp, 10);
			string32 animation = { 0 };
			xr_strcat(animation, "anim_");
			xr_strcat(animation, tmp);

			if (file->section_exist(animation))
			{
				bool anim_loop = file->r_bool(animation, "anim_loop");
				if (file->line_exist(animation, "anim_snd"))
				{
					shared_str snd = file->r_string(animation, "anim_snd");
					u32 snds = 0;
					if (file->line_exist(animation, "anim_snd_rnd"))
						snds = file->r_u32(animation, "anim_snd_rnd");
					else
						snds = 1;
					for (int snd_i = 1; snd_i <= snds; snd_i++)
					{
						string32 tmp = { 0 };
						itoa(snd_i, tmp, 10);
						string128 path_snd = { 0 };
						xr_strcat(path_snd, snd.c_str());
						xr_strcat(path_snd, tmp);
						m_sound_Animation[i][snd_i].create(path_snd, st_Effect, 0);
					}
					m_rnd_snds[i] = snds;
				}

				m_animation_loop[i] = anim_loop;

				if (file->line_exist(animation, "attached_item"))
					m_animation_attach[i] = file->r_string(animation, "attached_item");

				LPCSTR anims_in = file->r_string(animation, "anim_in");
				LPCSTR anims_out = file->r_string(animation, "anim_out");
				LPCSTR anims_middle = file->r_string(animation, "anim_middle");
				u32 countIN = _GetItemCount(anims_in, ',');
				u32 countOUT = _GetItemCount(anims_out, ',');
				u32 countMID = _GetItemCount(anims_middle, ',');

				in_anims.count[i] = countIN;
				out_anims.count[i] = countOUT;
				middle_anims.count[i] = countMID;

				for (int id = 0; id != countIN; id++)
				{
					string64 anim = { 0 };
					_GetItem(anims_in, id, anim, ',');
					MotionID motionAnim = K->ID_Cycle_Safe(anim);
					in_anims.m_animation_in[i][id] = motionAnim;
				}
				for (int id = 0; id != countOUT; id++)
				{
					string64 anim = { 0 };
					_GetItem(anims_out, id, anim, ',');
					MotionID motionAnim = K->ID_Cycle_Safe(anim);
					out_anims.m_animation_out[i][id] = motionAnim;
				}
				for (int id = 0; id != countMID; id++)
				{
					string64 anim = { 0 };
					_GetItem(anims_middle, id, anim, ',');
					MotionID motionAnim = K->ID_Cycle_Safe(anim);
					middle_anims.m_animation[i][id] = motionAnim;
				}
			}
		}
	}
}


void CActor::script_anim(MotionID Animation, PlayCallback Callback, LPVOID CallbackParam)
{
	IKinematicsAnimated* k = smart_cast<IKinematicsAnimated*>(Visual());
	k->LL_PlayCycle(
		k->LL_GetMotionDef(Animation)->bone_or_part,
		Animation,
		TRUE,
		k->LL_GetMotionDef(Animation)->Accrue(),
		k->LL_GetMotionDef(Animation)->Falloff(),
		k->LL_GetMotionDef(Animation)->Speed(),
		k->LL_GetMotionDef(Animation)->StopAtEnd(),
		Callback, CallbackParam, 0
	);

	CanChange = false;
	SendAnimationToServer(Animation);

}


void CActor::ReciveAnimationPacket(NET_Packet& packet)
{
	MotionID motion;
	packet.r(&motion, sizeof(motion));

	if (motion.valid())
	{
		IKinematicsAnimated* k = smart_cast<IKinematicsAnimated*>(Visual());
		k->LL_PlayCycle(
			k->LL_GetMotionDef(motion)->bone_or_part,
			motion,
			TRUE,
			k->LL_GetMotionDef(motion)->Accrue(),
			k->LL_GetMotionDef(motion)->Falloff(),
			k->LL_GetMotionDef(motion)->Speed(),
			k->LL_GetMotionDef(motion)->StopAtEnd(),
			callbackAnim, this, 0
		);

		CanChange = false;
	}

}

void CActor::ReciveActivateItem(NET_Packet& packet)
{
	shared_str item;
	packet.r_stringZ(item);
	bool activate = packet.r_u8();

	Msg("Activate Item [%s] -> [%d]", item.c_str(), activate);

	CInventoryItem* inv_item = this->inventory().GetItemFromInventory(item.c_str());
	CAttachableItem* item_attach = smart_cast<CAttachableItem*>(inv_item);

	if (item_attach)
	{
		if (this->can_attach(inv_item) && activate)
		{
			this->attach(inv_item);
		}

		if (activate)
		{
			item_attach->enable(true);
		}
		else
		{
			item_attach->enable(false);
		}
	}
}

void CActor::ReciveSoundPlay(NET_Packet packet)
{
	u32 snd_id, selectedID;
	packet.r_u32(selectedID);
	packet.r_u32(snd_id);
	bool activate = packet.r_u8();

	Msg("Recive SND Packet [%d] / act[%d]", snd_id, activate);

	if (activate)
	{
		if (selected._p)
			if (!selected._feedback())
			{
				selected.stop();
			}

		ref_sound snd_sel = m_anims->m_script.m_sound_Animation[selectedID][snd_id];

		if (!snd_sel._feedback())
		{
			snd_sel.play_at_pos(this, Position(), false, 0);
			selected = snd_sel;
		}
	}
	else
	{
		if (selected._p)
			if (selected._feedback())
			{
				selected.stop();
			}
	}

}

void CActor::SendAnimationToServer(MotionID motion)
{
	NET_Packet packet;
	u_EventGen(packet, GE_ACTOR_ANIMATION_SCRIPT, this->ID());
	packet.w(&motion, sizeof(motion));
	u_EventSend(packet, net_flags(true, true));
}

void CActor::SendActivateItem(shared_str item, bool activate)
{
	NET_Packet packet;
	u_EventGen(packet, GE_ACTOR_ITEM_ACTIVATE, this->ID());
	packet.w_stringZ(item);
	packet.w_u8(activate ? 1 : 0);
	u_EventSend(packet, net_flags(true, true));
}

void CActor::SendSoundPlay(u32 ID, bool Activate)
{
	NET_Packet packet;
	u_EventGen(packet, GE_ACTOR_SND_ACTIVATE, this->ID());
	packet.w_u32(selectedID);
	packet.w_u32(ID);
	packet.w_u8(Activate);
	u_EventSend(packet, net_flags(true, true));
}

void CActor::soundPlay()
{
	if (MidPlay || !InPlay || m_anims->m_script.m_rnd_snds[selectedID] == 0)
		return;

	u32 rnd = Random.randI(1, m_anims->m_script.m_rnd_snds[selectedID]);

	sSndID = rnd;

	if (!start_sel)
	{
		ref_sound snd_sel = m_anims->m_script.m_sound_Animation[selectedID][sSndID];
		if (!snd_sel._feedback())
		{
			snd_sel.play_at_pos(this, Position(), false, 0);
			selected = snd_sel;
			SendSoundPlay(sSndID, 1);
		}
		start_sel = true;
	}

	if (!selected._feedback())
	{
		selected.stop();
		start_sel = false;
	}
}

void CActor::SelectScriptAnimation()
{
	if (!CanChange)
		return;

	//Msg("In [%d] [%s]", InputAnim, InPlay ? "true" : "false");
	//Msg("Mid [%d] [%s]", MidAnim, MidPlay ? "true" : "false");
	//Msg("Out [%d] [%s]", OutAnim, OutPlay ? "true" : "false");

	if (oldAnim != ANIM_SELECTED)
	{
		if (InPlay && MidPlay && OutPlay)
		{
			oldAnim = ANIM_SELECTED;
			InputAnim = 0;
			OutAnim = 0;
			MidAnim = 0;
		}
	}

	//Msg("Selected [%d] UI_SEL [%d]", oldAnim, ANIM_SELECTED);

	u32 selectedAnimation = oldAnim;

	selectedID = selectedAnimation;


	u32 countIN = m_anims->m_script.in_anims.count[selectedAnimation];

	MidPlay = false;
	OutPlay = false;
	InPlay = false;

	MotionID script_BODY;

	if (countIN == 0)
		InPlay = true;
	else
		if (InputAnim >= countIN)
			InPlay = true;
		else
			InPlay = false;


	if (!InPlay)
	{
		script_BODY = m_anims->m_script.in_anims.m_animation_in[selectedAnimation][InputAnim];
		script_anim(script_BODY, callbackAnim, this);
		InputAnim += 1;
		NEED_EXIT = true;

		if (m_anims->m_script.m_animation_attach[selectedAnimation].size() > 0)
		{
			shared_str attach = m_anims->m_script.m_animation_attach[selectedAnimation];

			CInventoryItem* inv_item = this->inventory().GetItemFromInventory(attach.c_str());
			CAttachableItem* item = smart_cast<CAttachableItem*>(inv_item);

			if (item)
			{
				item->enable(true);
				SendActivateItem(attach, true);

				bool att = this->can_attach(inv_item);

				if (att)
					this->attach(inv_item);
			}
		}

		NET_Packet packet;
		u_EventGen(packet, GE_ACTOR_HIDE_ALL_STATE, this->ID());
		u_EventSend(packet, net_flags(true, true));
	}

	if (!InPlay)
		return;

	u32 countMid = m_anims->m_script.middle_anims.count[selectedAnimation];

	if (MidAnim >= countMid)
	{
		if (ANIM_SELECTED == 0)
		{
			MidPlay = true;
		}
		else
		{
			bool valid = selectedAnimation != ANIM_SELECTED;
			if (m_anims->m_script.m_animation_loop[selectedAnimation] && !valid)
				MidAnim = 0;
			else
				MidPlay = true;

		}
	}
	else
	{
		if (countMid == 0)
			MidPlay = true;
		else
			MidPlay = false;
	}

	if (!MidPlay)
	{
		script_BODY = m_anims->m_script.middle_anims.m_animation[selectedAnimation][MidAnim];
		script_anim(script_BODY, callbackAnim, this);
		MidAnim += 1;

		soundPlay();
	}

	if (!MidPlay)
		return;

	u32 countOUT = m_anims->m_script.out_anims.count[selectedAnimation];

	if (countOUT == 0)
		OutPlay = true;

	if (OutAnim >= countOUT)
		OutPlay = true;
	else
		OutPlay = false;

	if (!OutPlay)
	{
		script_BODY = m_anims->m_script.out_anims.m_animation_out[selectedAnimation][OutAnim];
		script_anim(script_BODY, callbackAnim, this);
		OutAnim += 1;
	}

	if (OutPlay)
	{
		if (selected._feedback())
			selected.stop();
		NEED_EXIT = false;
		if (m_anims->m_script.m_animation_attach[selectedAnimation].size() > 0)
		{
			shared_str attach = m_anims->m_script.m_animation_attach[selectedAnimation];
			CInventoryItem* inv_item = this->inventory().GetItemFromInventory(attach.c_str());
			CAttachableItem* item = smart_cast<CAttachableItem*>(inv_item);

			if (item)
			{
				item->enable(false);
				SendActivateItem(attach, false);
			}
		}
	}
}

void CActor::StopAllSNDs()
{
	OutPlay = true;
	CanChange = true;
	NEED_EXIT = false;
	ANIM_SELECTED = 0;
}

