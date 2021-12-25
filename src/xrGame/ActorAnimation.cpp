#include "stdafx.h"
#include "Actor.h"
#include "ActorAnimation.h"
#include "actor_anim_defs.h"
#include "weapon.h"
#include "inventory.h"
#include "missile.h"
#include "level.h"
#ifdef DEBUG
#include "PHDebug.h"
#include "ui_base.h"
#endif
#include "hit.h"
#include "PHDestroyable.h"
#include "Car.h"
#include "../Include/xrRender/Kinematics.h"
#include "ai_object_location.h"
#include "game_cl_base.h"
#include "../xrEngine/motion.h"
#include "artefact.h"
#include "IKLimbsController.h"
#include "player_hud.h"

static const float y_spin0_factor		= 0.0f;
static const float y_spin1_factor		= 0.4f;
static const float y_shoulder_factor	= 0.4f;
static const float y_head_factor		= 0.2f;
static const float p_spin0_factor		= 0.0f;
static const float p_spin1_factor		= 0.2f;
static const float p_shoulder_factor	= 0.7f;
static const float p_head_factor		= 0.1f;
static const float r_spin0_factor		= 0.3f;
static const float r_spin1_factor		= 0.3f;
static const float r_shoulder_factor	= 0.2f;
static const float r_head_factor		= 0.2f;

CBlend	*PlayMotionByParts(IKinematicsAnimated* sa, MotionID motion_ID, BOOL bMixIn, PlayCallback Callback, LPVOID CallbackParam);

void  CActor::Spin0Callback(CBoneInstance* B)
{
	CActor*	A			= static_cast<CActor*>(B->callback_param());	VERIFY	(A);

	Fmatrix				spin;
	float				bone_yaw	= angle_normalize_signed(A->r_torso.yaw - A->r_model_yaw - A->r_model_yaw_delta)*y_spin0_factor;
	float				bone_pitch	= angle_normalize_signed(A->r_torso.pitch)*p_spin0_factor;
	float				bone_roll	= angle_normalize_signed(A->r_torso.roll)*r_spin0_factor;
	Fvector c			= B->mTransform.c;
	spin.setXYZ			(-bone_pitch,bone_yaw,bone_roll);
	B->mTransform.mulA_43(spin);
	B->mTransform.c		= c;
}
void  CActor::Spin1Callback(CBoneInstance* B)
{
	CActor*	A			= static_cast<CActor*>(B->callback_param());	VERIFY	(A);

	Fmatrix				spin;
	float				bone_yaw	= angle_normalize_signed(A->r_torso.yaw - A->r_model_yaw - A->r_model_yaw_delta)*y_spin1_factor;
	float				bone_pitch	= angle_normalize_signed(A->r_torso.pitch)*p_spin1_factor;
	float				bone_roll	= angle_normalize_signed(A->r_torso.roll)*r_spin1_factor;
	Fvector c			= B->mTransform.c;
	spin.setXYZ			(-bone_pitch,bone_yaw,bone_roll);
	B->mTransform.mulA_43(spin);
	B->mTransform.c		= c;
}
void  CActor::ShoulderCallback(CBoneInstance* B)
{
	CActor*	A			= static_cast<CActor*>(B->callback_param());	VERIFY	(A);
	Fmatrix				spin;
	float				bone_yaw	= angle_normalize_signed(A->r_torso.yaw - A->r_model_yaw - A->r_model_yaw_delta)*y_shoulder_factor;
	float				bone_pitch	= angle_normalize_signed(A->r_torso.pitch)*p_shoulder_factor;
	float				bone_roll	= angle_normalize_signed(A->r_torso.roll)*r_shoulder_factor;
	Fvector c			= B->mTransform.c;
	spin.setXYZ			(-bone_pitch,bone_yaw,bone_roll);
	B->mTransform.mulA_43(spin);
	B->mTransform.c		= c;
}
void  CActor::HeadCallback(CBoneInstance* B)
{
	CActor*	A			= static_cast<CActor*>(B->callback_param());	VERIFY	(A);
	Fmatrix				spin;
	float				bone_yaw	= angle_normalize_signed(A->r_torso.yaw - A->r_model_yaw - A->r_model_yaw_delta)*y_head_factor;
	float				bone_pitch	= angle_normalize_signed(A->r_torso.pitch)*p_head_factor;
	float				bone_roll	= angle_normalize_signed(A->r_torso.roll)*r_head_factor;
	Fvector c			= B->mTransform.c;
	spin.setXYZ			(-bone_pitch,bone_yaw,bone_roll);
	B->mTransform.mulA_43(spin);
	B->mTransform.c		= c;
}

void  CActor::VehicleHeadCallback(CBoneInstance* B)
{
	CActor*	A			= static_cast<CActor*>(B->callback_param());	VERIFY	(A);
	Fmatrix				spin;
	float				bone_yaw	= angle_normalize_signed(A->r_torso.yaw)*0.75f;
	float				bone_pitch	= angle_normalize_signed(A->r_torso.pitch)*0.75f;
	float				bone_roll	= angle_normalize_signed(A->r_torso.roll)*r_head_factor;
	Fvector c			= B->mTransform.c;
	spin.setHPB			(bone_yaw,bone_pitch,-bone_roll);
	B->mTransform.mulA_43(spin);
	B->mTransform.c		= c;
}

void STorsoWpn::Create(IKinematicsAnimated* K, LPCSTR base0, LPCSTR base1)
{
	char			buf[128];
	moving[eIdle]	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_aim_1"));
	moving[eWalk]	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_aim_2"));
	moving[eRun]	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_aim_3"));
	moving[eSprint]	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_escape_0"));
	zoom			= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_aim_0"));
	holster			= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_holster_0"));
	draw			= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_draw_0"));
	reload			= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_reload_0"));
	reload_1		= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_reload_1"));
	reload_2		= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_reload_2"));
	drop			= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_drop_0"));
	attack			= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_attack_1"));
	attack_zoom		= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_attack_0"));
	fire_idle		= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_attack_1"));
	fire_end		= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_attack_2"));
	all_attack_0	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_all",base1,"_attack_0"));
	all_attack_1	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_all",base1,"_attack_1"));
	all_attack_2	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_all",base1,"_attack_2"));
}
void SAnimState::Create(IKinematicsAnimated* K, LPCSTR base0, LPCSTR base1)
{
	char			buf[128];
	legs_fwd		= K->ID_Cycle(strconcat(sizeof(buf),buf,base0,base1,"_fwd_0"));
	legs_back		= K->ID_Cycle(strconcat(sizeof(buf),buf,base0,base1,"_back_0"));
	legs_ls			= K->ID_Cycle(strconcat(sizeof(buf),buf,base0,base1,"_ls_0"));
	legs_rs			= K->ID_Cycle(strconcat(sizeof(buf),buf,base0,base1,"_rs_0"));
}


void SActorStateAnimation::CreateAnimationsScripted(IKinematicsAnimated* K)
{
	//Msg("Register ActorAnimation");

	string_path filepath;
	FS.update_path(filepath, "$game_config$", "actor_anims.ltx");
	CInifile* file = xr_new<CInifile>(filepath, true, true);
	
	if (file && file->section_exist("animations"))
	{
		//Msg("Register ActorAnimation Finded");

		u32 count = file->r_u32("animations", "count");

		for (int i = 0; i < count; i++)
		{
			string32 tmp = {0};
			itoa(i, tmp, 10);
			string32 animation = {0}; 
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
					{
						snds = file->r_u32(animation, "anim_snd_rnd");
						m_rnd_snds[i] = snds;
					}
					else
					{
						snds = 1;
					}

					for (int snd_i = 1; snd_i <= snds; snd_i++)
					{
						string32 tmp = {0};
						itoa(snd_i, tmp, 10);
						
						string128 path_snd = {0};
						xr_strcat(path_snd, snd.c_str());
						xr_strcat(path_snd, tmp);

						m_sound_Animation[i][snd_i].create(path_snd, st_Effect, 0);
					//	Msg("Register Snd [%d] -> sndID[%d] path[%s]", i, snd_i, path_snd);
					}
				}


				m_animation_loop[i] = anim_loop;

				if (file->line_exist(animation, "attached_item"))
				{
					m_animation_attach[i] = file->r_string(animation, "attached_item");
				}

				LPCSTR anims_in = file->r_string(animation, "anim_in");
				LPCSTR anims_out = file->r_string(animation, "anim_out");
				LPCSTR anims_middle = file->r_string(animation, "anim_middle");
				u32 countIN  =  _GetItemCount(anims_in, ',');
				u32 countOUT =  _GetItemCount(anims_out, ',');
				u32 countMID =  _GetItemCount(anims_middle, ',');

				for (int id = 0; id != countIN; id++ )
				{
					string64 anim = { 0 };
					_GetItem(anims_in, id, anim, ',');

					MotionID motionAnim = K->ID_Cycle_Safe(anim);

					in_anims.m_animation_in[i][id] = motionAnim;
					//Msg("Register ActorAnimation IN[%d] -> [%d][%s]", i, id, anim);
				}

				in_anims.count[i] = countIN;  
				//Msg("CountIN [%d]", countIN);

				for (int id = 0; id != countOUT; id++)
				{
					string64 anim = {0};
					_GetItem(anims_out, id, anim, ',');

					MotionID motionAnim = K->ID_Cycle_Safe(anim);

					out_anims.m_animation_out[i][id] = motionAnim;
					//Msg("Register ActorAnimation OUT[%d] -> [%d][%s]", i, id, anim);
				}

				out_anims.count[i] = countOUT;
				//Msg("CountOUT [%d]", countOUT);

				for (int id = 0; id != countMID; id++)
				{
					string64 anim = { 0 };
					_GetItem(anims_middle, id, anim, ',');

					MotionID motionAnim = K->ID_Cycle_Safe(anim);

					middle_anims.m_animation[i][id] = motionAnim;
					//Msg("Register ActorAnimation MIDDLE[%d] -> [%d][%s]", i, id, anim);
				}

				

				middle_anims.count[i] = countMID;
				//Msg("CountMID [%d]", countMID);
			}
		}
	}

}


void SActorState::CreateClimb(IKinematicsAnimated* K)
{
	string128		buf,buf1;
	string16		base;
	
	//climb anims
	xr_strcpy(base,"cl");
	legs_idle		= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_idle_1"));
	m_torso_idle	= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_torso_0_aim_0"));
	m_walk.Create	(K,base,"_run");
	m_run.Create	(K,base,"_run");

	//norm anims
	xr_strcpy(base,"norm");
	legs_turn		= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_turn"));
	death			= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_death_0"));
	m_torso[0].Create(K,base,"_1");
	m_torso[1].Create(K,base,"_2");
	m_torso[2].Create(K,base,"_3");
	m_torso[3].Create(K,base,"_4");
	m_torso[4].Create(K,base,"_5");
	m_torso[5].Create(K,base,"_6");
	m_torso[6].Create(K,base,"_7");
	m_torso[7].Create(K,base,"_8");
	m_torso[8].Create(K,base,"_9");
	m_torso[9].Create(K,base,"_10");
	m_torso[10].Create(K,base,"_11");
	m_torso[11].Create(K,base,"_12");
	m_torso[12].Create(K,base,"_13");


	m_head_idle.invalidate();///K->ID_Cycle("head_idle_0");
	jump_begin		= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_jump_begin"));
	jump_idle		= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_jump_idle"));
	landing[0]		= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_jump_end"));
	landing[1]		= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_jump_end_1"));

	for (int k=0; k<12; ++k)
		m_damage[k]	= K->ID_FX(strconcat(sizeof(buf),buf,base,"_damage_",itoa(k,buf1,10)));
}


void SActorState::Create(IKinematicsAnimated* K, LPCSTR base)
{
	string128		buf,buf1;
	legs_turn		= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_turn"));
	legs_idle		= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_idle_0"));
	death			= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_death_0"));
	
	m_walk.Create	(K,base,"_walk");
	m_run.Create	(K,base,"_run");

	m_torso[0].Create(K,base,"_1");
	m_torso[1].Create(K,base,"_2");
	m_torso[2].Create(K,base,"_3");
	m_torso[3].Create(K,base,"_4");
	m_torso[4].Create(K,base,"_5");
	m_torso[5].Create(K,base,"_6");
	m_torso[6].Create(K,base,"_7");
	m_torso[7].Create(K,base,"_8");
	m_torso[8].Create(K,base,"_9");
	m_torso[9].Create(K,base,"_10");
	m_torso[10].Create(K,base,"_11");
	m_torso[11].Create(K,base,"_12");
	m_torso[12].Create(K,base,"_13");
	
	m_torso_idle	= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_torso_0_aim_0"));
	m_head_idle		= K->ID_Cycle("head_idle_0");
	jump_begin		= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_jump_begin"));
	jump_idle		= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_jump_idle"));
	landing[0]		= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_jump_end"));
	landing[1]		= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_jump_end_1"));

	for (int k=0; k<12; ++k)
		m_damage[k]	= K->ID_FX(strconcat(sizeof(buf),buf,base,"_damage_",itoa(k,buf1,10)));
}

void SActorSprintState::Create(IKinematicsAnimated* K)
{
	//leg anims
	legs_fwd=K->ID_Cycle("norm_escape_00");
	legs_ls=K->ID_Cycle("norm_escape_ls_00");
	legs_rs=K->ID_Cycle("norm_escape_rs_00");

	legs_jump_fwd	=K->ID_Cycle("norm_escape_jump_00");
	legs_jump_ls	=K->ID_Cycle("norm_escape_ls_jump_00");
	legs_jump_rs	=K->ID_Cycle("norm_escape_rs_jump_00");
}

void SActorMotions::Create(IKinematicsAnimated* V)
{
	m_dead_stop				= V->ID_Cycle("norm_dead_stop_0");

	m_normal.Create	(V,"norm");
	m_crouch.Create	(V,"cr");
	//m_climb.Create	(V,"cr");
	m_climb.CreateClimb(V);
	m_sprint.Create(V);

	m_script.CreateAnimationsScripted(V);
}

SActorVehicleAnims::SActorVehicleAnims()
{
	
}
void SActorVehicleAnims::Create(IKinematicsAnimated* V)
{
	for(u16 i=0;TYPES_NUMBER>i;++i) m_vehicles_type_collections[i].Create(V,i);
}

SVehicleAnimCollection::SVehicleAnimCollection()
{
	for(u16 i=0;MAX_IDLES>i;++i) idles[i].invalidate();
	idles_num = 0;
	steer_left.invalidate();
	steer_right.invalidate();
}

void SVehicleAnimCollection::Create(IKinematicsAnimated* V,u16 num)
{
	string128 buf,buff1,buff2;
	strconcat(sizeof(buff1),buff1,itoa(num,buf,10),"_");
	steer_left=	V->ID_Cycle(strconcat(sizeof(buf),buf,"steering_idle_",buff1,"ls"));
	steer_right=V->ID_Cycle(strconcat(sizeof(buf),buf,"steering_idle_",buff1,"rs"));

	for(int i=0;MAX_IDLES>i;++i){
		idles[i]=V->ID_Cycle_Safe(strconcat(sizeof(buf),buf,"steering_idle_",buff1,itoa(i,buff2,10)));
		if(idles[i]) idles_num++;
		else break;
	}
}

void CActor::steer_Vehicle(float angle)	
{
	if(!m_holder)		return;
/*
	CCar*	car			= smart_cast<CCar*>(m_holder);
	u16 anim_type       = car->DriverAnimationType();
	SVehicleAnimCollection& anims=m_vehicle_anims->m_vehicles_type_collections[anim_type];
	if(angle==0.f) 		smart_cast<IKinematicsAnimated*>	(Visual())->PlayCycle(anims.idles[0]);
	else if(angle>0.f)	smart_cast<IKinematicsAnimated*>	(Visual())->PlayCycle(anims.steer_right);
	else				smart_cast<IKinematicsAnimated*>	(Visual())->PlayCycle(anims.steer_left);
*/
}

void legs_play_callback		(CBlend *blend)
{
	CActor					*object = (CActor*)blend->CallbackParam;
	VERIFY					(object);
	object->m_current_legs.invalidate();
}

void CActor::g_SetSprintAnimation( u32 mstate_rl,MotionID &head,MotionID &torso,MotionID &legs)
{
	SActorSprintState& sprint			= m_anims->m_sprint;
	
	bool jump = (mstate_rl&mcFall)		||
				(mstate_rl&mcLanding)	||
				(mstate_rl&mcLanding)	||
				(mstate_rl&mcLanding2)	||
				(mstate_rl&mcJump)		;
	
	if		(mstate_rl & mcFwd)		legs = (!jump) ? sprint.legs_fwd	: sprint.legs_jump_fwd;
	else if (mstate_rl & mcLStrafe) legs = (!jump) ? sprint.legs_ls		: sprint.legs_jump_ls;
	else if (mstate_rl & mcRStrafe)	legs = (!jump) ? sprint.legs_rs		: sprint.legs_jump_rs;	
}

CMotion*        FindMotionKeys(MotionID motion_ID,IRenderVisual* V)
{
	IKinematicsAnimated* VA = smart_cast<IKinematicsAnimated*>(V);
	return (VA && motion_ID.valid())?VA->LL_GetRootMotion(motion_ID):0;
}

#ifdef DEBUG
BOOL	g_ShowAnimationInfo = TRUE;
#endif // DEBUG
char* mov_state[] ={
	"idle",
	"walk",
	"run",
	"sprint",
};


extern int AnimCurrent;

void callbackAnim(CBlend* blend)
{
	CActor* act = (CActor*) blend->CallbackParam;
	if (act)
	{
		act->CanChange = true;
		//Msg("Unfreaze Animation change [%d]", act->ID());
	}
}


void CActor::g_SetAnimation( u32 mstate_rl )
{


	if (!g_Alive()) {
		if (m_current_legs||m_current_torso){
			SActorState*				ST = 0;
			if (mstate_rl&mcCrouch)		ST = &m_anims->m_crouch;
			else						ST = &m_anims->m_normal;
			mstate_real					= 0;
			m_current_legs.invalidate	();
			m_current_torso.invalidate	();

			//smart_cast<IKinematicsAnimated*>(Visual())->PlayCycle(m_anims->m_dead_stop);
		}

		return;
	}
	STorsoWpn::eMovingState	moving_idx 		= STorsoWpn::eIdle;
	SActorState*					ST 		= 0;
	SAnimState*						AS 		= 0;
	
	if		(mstate_rl&mcCrouch)	
		ST 		= &m_anims->m_crouch;
	else if	(mstate_rl&mcClimb)		
		ST 		= &m_anims->m_climb;
	else							
		ST 		= &m_anims->m_normal;

	bool bAccelerated = isActorAccelerated(mstate_rl, IsZoomAimingMode());
	if ( bAccelerated )
	{
		AS							= &ST->m_run;
	}else{
		AS							= &ST->m_walk;
	}
	if(mstate_rl&mcAnyMove)
	{
		if( bAccelerated )
			moving_idx				= STorsoWpn::eRun;
		else
			moving_idx				= STorsoWpn::eWalk;
	}
	// анимации
	MotionID 						M_legs;
	MotionID 						M_torso;
	MotionID 						M_head;

	//если мы просто стоим на месте
	bool is_standing = false;

	// Legs
	if		(mstate_rl&mcLanding)	M_legs	= ST->landing[0];
	else if (mstate_rl&mcLanding2)	M_legs	= ST->landing[1];
	else if ((mstate_rl&mcTurn)&&
			!(mstate_rl&mcClimb))	M_legs	= ST->legs_turn;
	else if (mstate_rl&mcFall)		M_legs	= ST->jump_idle;
	else if (mstate_rl&mcJump)		M_legs	= ST->jump_begin;
	else if (mstate_rl&mcFwd)		M_legs	= AS->legs_fwd;
	else if (mstate_rl&mcBack)		M_legs	= AS->legs_back;
	else if (mstate_rl&mcLStrafe)	M_legs	= AS->legs_ls;
	else if (mstate_rl&mcRStrafe)	M_legs	= AS->legs_rs;
	else is_standing = true;

	if(mstate_rl&mcSprint)
	{
		g_SetSprintAnimation			(mstate_rl,M_head,M_torso,M_legs);
		moving_idx						= STorsoWpn::eSprint;
	}

	if (this == Level().CurrentViewEntity())
	{	
		if ((mstate_rl&mcSprint) != (mstate_old&mcSprint))
		{
			g_player_hud->OnMovementChanged(mcSprint);
		}else
		if ((mstate_rl&mcAnyMove) != (mstate_old&mcAnyMove))
		{
			g_player_hud->OnMovementChanged(mcAnyMove);
		}
	};

	//-----------------------------------------------------------------------
	// Torso
	if(mstate_rl&mcClimb)
	{
		if		(mstate_rl&mcFwd)		M_torso	= AS->legs_fwd;
		else if (mstate_rl&mcBack)		M_torso	= AS->legs_back;
		else if (mstate_rl&mcLStrafe)	M_torso	= AS->legs_ls;
		else if (mstate_rl&mcRStrafe)	M_torso	= AS->legs_rs;
	}
	
	if (!CanChange)
	{
		if (!OutPlay && Level().CurrentControlEntity() == this)
 			soundPlay();

 		return;
	}

	if (!MpAnimationMODE())
	{
		if (!OutPlay)
		{
			if (Level().CurrentControlEntity() == this)
				SelectScriptAnimation();
			return;
		}
		else
		{
 			InputAnim = 0;
			OutAnim = 0;
			MidAnim = 0;
		}
	}
	else
	{
		if (Level().CurrentControlEntity() == this)
			SelectScriptAnimation();
		return;
	}

	if(!M_torso)
	{
		CInventoryItem* _i = inventory().ActiveItem();
		CHudItem		*H = smart_cast<CHudItem*>(_i);
		CWeapon			*W = smart_cast<CWeapon*>(_i);
		CMissile		*M = smart_cast<CMissile*>(_i);
		CArtefact		*A = smart_cast<CArtefact*>(_i);
					
		if (H) {
			VERIFY(H->animation_slot() <= _total_anim_slots_);
			STorsoWpn* TW			= &ST->m_torso[H->animation_slot() - 1];
			if (!b_DropActivated&&!fis_zero(f_DropPower))
			{
				M_torso					= TW->drop;
				if (!M_torso)	
				{
					Msg("! drop animation for %s", *(H->object().cName()));
					M_torso = ST->m_torso_idle;
				};
				m_bAnimTorsoPlayed		= TRUE;
			}
			else
			{
				if (!m_bAnimTorsoPlayed) 
				{
					if (W) 
					{
						bool K	=inventory().GetActiveSlot() == KNIFE_SLOT;
						bool R3 = W->IsTriStateReload();
						
						if(K)
						{
							switch (W->GetState())
							{
							case CWeapon::eIdle:		M_torso	= TW->moving[moving_idx];		break;
							
							case CWeapon::eFire:	
								if(is_standing)
														M_torso = M_legs = M_head = TW->all_attack_0;
								else
														M_torso	= TW->attack_zoom;
								break;

							case CWeapon::eFire2:
								if(is_standing)
														M_torso = M_legs = M_head = TW->all_attack_1;
								else
														M_torso	= TW->fire_idle;
								break;

							case CWeapon::eReload:		M_torso	= TW->reload;					break;
							case CWeapon::eShowing:		M_torso	= TW->draw;						break;
							case CWeapon::eHiding:		M_torso	= TW->holster;					break;
							default				 :  	M_torso	= TW->moving[moving_idx];		break;
							}
						}
						else
						{
							switch (W->GetState())
							{
							case CWeapon::eIdle:		M_torso	= W->IsZoomed()?TW->zoom:TW->moving[moving_idx];	break;
							case CWeapon::eFire:		M_torso	= W->IsZoomed()?TW->attack_zoom:TW->attack;				break;
							case CWeapon::eFire2:		M_torso	= W->IsZoomed()?TW->attack_zoom:TW->attack;				break;
							case CWeapon::eReload:	
								if(!R3)
									M_torso	= TW->reload;
								else
								{
									CWeapon::EWeaponSubStates sub_st = W->GetReloadState();
									switch (sub_st)
									{
										case CWeapon::eSubstateReloadBegin:			M_torso	= TW->reload;	break;
										case CWeapon::eSubstateReloadInProcess:		M_torso	= TW->reload_1; break;
										case CWeapon::eSubstateReloadEnd:			M_torso	= TW->reload_2; break;
										default:									M_torso	= TW->reload;	break;
									}
								}break;

							case CWeapon::eShowing:	M_torso	= TW->draw;					break;
							case CWeapon::eHiding:	M_torso	= TW->holster;				break;
							default				 :  M_torso	= TW->moving[moving_idx];	break;
							}
						}
					}
					else if (M) 
					{
						if(is_standing)
						{
							switch (M->GetState())
							{
							case CMissile::eShowing		:		M_torso	= TW->draw;			break;
							case CMissile::eHiding		:		M_torso	= TW->holster;		break;
							case CMissile::eIdle		:		M_torso	= TW->moving[moving_idx];		break;
							case CMissile::eThrowStart	:		M_torso = M_legs = M_head = TW->all_attack_0;	break;
							case CMissile::eReady		:		M_torso = M_legs = M_head = TW->all_attack_1;	break;
							case CMissile::eThrow		:		M_torso = M_legs = M_head = TW->all_attack_2;	break;
							case CMissile::eThrowEnd	:		M_torso = M_legs = M_head = TW->all_attack_2;	break;
							default						:		M_torso	= TW->draw;			break; 
							}
						}
						else
						{
							switch (M->GetState())
							{
							case CMissile::eShowing		:		M_torso	= TW->draw;						break;
							case CMissile::eHiding		:		M_torso	= TW->holster;					break;
							case CMissile::eIdle		:		M_torso	= TW->moving[moving_idx];		break;
							case CMissile::eThrowStart	:		M_torso	= TW->attack_zoom;				break;
							case CMissile::eReady		:		M_torso	= TW->fire_idle;				break;
							case CMissile::eThrow		:		M_torso	= TW->fire_end;					break;
							case CMissile::eThrowEnd	:		M_torso	= TW->fire_end;					break;
							default						:		M_torso	= TW->draw;						break; 
							}
						}
					}
					else if (A)
					{
							switch(A->GetState())
							{
								case CArtefact::eIdle		: M_torso	= TW->moving[moving_idx];	break; 
								case CArtefact::eShowing	: M_torso	= TW->draw;					break; 
								case CArtefact::eHiding		: M_torso	= TW->holster;				break; 
								case CArtefact::eActivating : M_torso	= TW->zoom;					break; 
								default						: M_torso	= TW->moving[moving_idx];
							}
					
					}
				}
			}
		}
	}
  
	MotionID		mid = smart_cast<IKinematicsAnimated*>(Visual())->ID_Cycle("norm_idle_0");

	if (!M_legs)
	{
		if((mstate_rl&mcCrouch)&&!isActorAccelerated(mstate_rl, IsZoomAimingMode()))//!(mstate_rl&mcAccel))
		{
			M_legs=smart_cast<IKinematicsAnimated*>(Visual())->ID_Cycle("cr_idle_1");
		}
		else 
			M_legs	= ST->legs_idle;
	}
	if (!M_head)					
		M_head	= ST->m_head_idle;

	if (!M_torso)
	{
		if (m_bAnimTorsoPlayed)		
			M_torso	= m_current_torso;
		else						
			M_torso = ST->m_torso_idle;
	}
	
	// есть анимация для всего - запустим / иначе запустим анимацию по частям
	if (m_current_torso!=M_torso)
	{
		if (m_bAnimTorsoPlayed)		
			m_current_torso_blend = smart_cast<IKinematicsAnimated*>(Visual())->PlayCycle(M_torso,TRUE,AnimTorsoPlayCallBack,this);
		else						
			m_current_torso_blend = smart_cast<IKinematicsAnimated*>(Visual())->PlayCycle(M_torso);

		m_current_torso=M_torso;
	}
	
	if(m_current_head!=M_head)
	{
		if(M_head)
			smart_cast<IKinematicsAnimated*>(Visual())->PlayCycle(M_head);

		m_current_head=M_head;
	}

	if (m_current_legs!=M_legs)
	{
		float pos					= 0.f;
		VERIFY						(!m_current_legs_blend || !fis_zero(m_current_legs_blend->timeTotal));
		if ((mstate_real&mcAnyMove)&&(mstate_old&mcAnyMove)&&m_current_legs_blend)
			pos						= fmod(m_current_legs_blend->timeCurrent,m_current_legs_blend->timeTotal)/m_current_legs_blend->timeTotal;


		IKinematicsAnimated* ka		= smart_cast<IKinematicsAnimated*>(Visual());
		m_current_legs_blend		= PlayMotionByParts(ka, M_legs, TRUE, legs_play_callback, this);
//		m_current_legs_blend		= smart_cast<IKinematicsAnimated*>(Visual())->PlayCycle(M_legs,TRUE,legs_play_callback,this);


		if ((!(mstate_old&mcAnyMove))&&(mstate_real&mcAnyMove))
		{
			pos						= 0.5f;//0.5f*Random.randI(2);
		}
		if (m_current_legs_blend)
			m_current_legs_blend->timeCurrent = m_current_legs_blend->timeTotal*pos;

		m_current_legs				= M_legs;

		CStepManager::on_animation_start(M_legs, m_current_legs_blend);
	}



#ifdef DEBUG
	if(bDebug && g_ShowAnimationInfo)
	{
		UI().Font().pFontStat->OutSetI	(0,0);
		UI().Font().pFontStat->OutNext("[%s]",mov_state[moving_idx]);
		IKinematicsAnimated* KA = smart_cast<IKinematicsAnimated*>(Visual());
		if(M_torso)
			UI().Font().pFontStat->OutNext("torso [%s]",KA->LL_MotionDefName_dbg(M_torso).first);
		if(M_head)
			UI().Font().pFontStat->OutNext("head [%s]",KA->LL_MotionDefName_dbg(M_head).first);
		if(M_legs)
			UI().Font().pFontStat->OutNext("legs [%s]",KA->LL_MotionDefName_dbg(M_legs).first);
	}
#endif

#ifdef DEBUG
	if ((Level().CurrentControlEntity() == this) && g_ShowAnimationInfo) {
		string128 buf;
		xr_strcpy(buf,"");
		if (isActorAccelerated(mstate_rl, IsZoomAimingMode()))		xr_strcat(buf,"Accel ");
		if (mstate_rl&mcCrouch)		xr_strcat(buf,"Crouch ");
		if (mstate_rl&mcFwd)		xr_strcat(buf,"Fwd ");
		if (mstate_rl&mcBack)		xr_strcat(buf,"Back ");
		if (mstate_rl&mcLStrafe)	xr_strcat(buf,"LStrafe ");
		if (mstate_rl&mcRStrafe)	xr_strcat(buf,"RStrafe ");
		if (mstate_rl&mcJump)		xr_strcat(buf,"Jump ");
		if (mstate_rl&mcFall)		xr_strcat(buf,"Fall ");
		if (mstate_rl&mcTurn)		xr_strcat(buf,"Turn ");
		if (mstate_rl&mcLanding)	xr_strcat(buf,"Landing ");
		if (mstate_rl&mcLLookout)	xr_strcat(buf,"LLookout ");
		if (mstate_rl&mcRLookout)	xr_strcat(buf,"RLookout ");
		if (m_bJumpKeyPressed)		xr_strcat(buf,"+Jumping ");
		UI().Font().pFontStat->OutNext	("MSTATE:     [%s]",buf);
/*
		switch (m_PhysicMovementControl->Environment())
		{
		case CPHMovementControl::peOnGround:	xr_strcpy(buf,"ground");			break;
		case CPHMovementControl::peInAir:		xr_strcpy(buf,"air");				break;
		case CPHMovementControl::peAtWall:		xr_strcpy(buf,"wall");				break;
		}
		UI().Font().pFontStat->OutNext	(buf);
		UI().Font().pFontStat->OutNext	("Accel     [%3.2f, %3.2f, %3.2f]",VPUSH(NET_SavedAccel));
		UI().Font().pFontStat->OutNext	("V         [%3.2f, %3.2f, %3.2f]",VPUSH(m_PhysicMovementControl->GetVelocity()));
		UI().Font().pFontStat->OutNext	("vertex ID   %d",ai_location().level_vertex_id());
		
		Game().m_WeaponUsageStatistic->Draw();
		*/
	};
#endif

	if (!m_current_torso_blend)
		return;

	IKinematicsAnimated		*skeleton_animated = smart_cast<IKinematicsAnimated*>(Visual());

	CMotionDef				*motion0 = skeleton_animated->LL_GetMotionDef(m_current_torso);
	VERIFY					(motion0);
	if (!(motion0->flags & esmSyncPart))
		return;

	if (!m_current_legs_blend)
		return;

	CMotionDef				*motion1 = skeleton_animated->LL_GetMotionDef(m_current_legs);
	VERIFY					(motion1);
	if (!(motion1->flags & esmSyncPart))
		return;


	m_current_torso_blend->timeCurrent	= m_current_legs_blend->timeCurrent/m_current_legs_blend->timeTotal*m_current_torso_blend->timeTotal;
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

#include "script_sound.h"
#include "ai_sounds.h"

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
	u_EventGen(packet,GE_ACTOR_ANIMATION_SCRIPT, this->ID());
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
	if (!InPlay)
	{
		return;
	}
	
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
	if (oldAnim != AnimCurrent)
	{
		if (InPlay && MidPlay && OutPlay)
		{
			oldAnim = AnimCurrent;
			InputAnim = 0;
			OutAnim = 0;
			MidAnim = 0;
		}
  
		Msg("Clear Anim");
	}

	u32 selectedAnimation = oldAnim;
	selectedID = selectedAnimation;
	
	u32 countIN = m_anims->m_script.in_anims.count[selectedAnimation];

	if (false)
	if (m_anims->m_script.m_animation_attach[selectedAnimation].size() == 0)
	{
		for (auto item : this->attached_objects())
		{
			item->enable(false);
			SendActivateItem(item->item().m_section_id.c_str(), false);
			
		}
	}

	MidPlay = false;
	OutPlay = false;
	InPlay	= false;
	
	//Msg("Anim counts INPUT[%d]", countIN);

	MotionID script_BODY;
	
	if (countIN == 0)
		InPlay = true;
	else
	{
		if (InputAnim >= countIN)
		{
			InPlay = true;
		}
		else
		{
			InPlay = false;
		}
	}
	
	//Msg("Anim in Play[%s]", InPlay ? "true" : "false");

	if (!InPlay)
	{
		script_BODY = m_anims->m_script.in_anims.m_animation_in[selectedAnimation][InputAnim];
		script_anim(script_BODY, callbackAnim, this);
		InputAnim += 1;

		if (m_anims->m_script.m_animation_attach[selectedAnimation].size() > 0)
		{
			shared_str attach = m_anims->m_script.m_animation_attach[selectedAnimation];

			CInventoryItem* inv_item = this->inventory().GetItemFromInventory(attach.c_str());
			CAttachableItem* item = smart_cast<CAttachableItem*>(inv_item);

			//Msg("Find Anim Attach Item [%s]", attach.c_str());

			if (item)
			{
				item->enable(true);
				SendActivateItem(attach, true);

				bool att = this->can_attach(inv_item);
				
				if (att)
				{		   
					//Msg("Anim Attach Item [%s] Attached END", attach.c_str());
					this->attach(inv_item);

				}
				else
				{
					//Msg("Anim Attach Item [%s] Cant ATTACH", attach.c_str());
				}
				  
			}
		}

		//Msg("Anim InputAnim[%d]", InputAnim);
	}
  
	if (!InPlay)
	{
		return;
	}

	//Msg("InPlay");

	u32 countMid = m_anims->m_script.middle_anims.count[selectedAnimation];

	if (MidAnim >= countMid)
	{
		if (!MpAnimationMODE())
		{
			MidPlay = true;
		}
		else
		{
			bool valid = selectedAnimation != AnimCurrent;
			if (m_anims->m_script.m_animation_loop[selectedAnimation] && !valid)
			{
				MidAnim = 0;
			}
			else
			{
				MidPlay = true;
 			}
		}
	}
	else
	{
		if (countMid == 0)
		{
			MidPlay = true;
		}
		else
			MidPlay = false;
	}

	if (!MidPlay)
	{
		script_BODY = m_anims->m_script.middle_anims.m_animation[selectedAnimation][MidAnim];
		script_anim(script_BODY, callbackAnim, this);
		MidAnim += 1; 

		//Msg("Anim middle [%d][%d]", selectedAnimation, MidAnim);
	}

	if (!MidPlay)
	{
		return;
	}

	//Msg("MidPlay");

	u32 countOUT = m_anims->m_script.out_anims.count[selectedAnimation];

	if (countOUT == 0)
		OutPlay = true;

	if (OutAnim >= countOUT)
	{
		OutPlay = true;
	}
	else
	{
		OutPlay = false;
	}

	if (OutPlay != true)
	{
		script_BODY = m_anims->m_script.out_anims.m_animation_out[selectedAnimation][OutAnim];
		script_anim(script_BODY, callbackAnim, this);
		OutAnim += 1;
 
		//Msg("Anim Out[%d]", OutAnim);
	} 

	if (OutPlay)
	{
		//Msg("OutPlay");

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

		for (auto snd : m_anims->m_script.m_sound_Animation[selectedID])
		{
			if (snd._feedback())
			{
				snd.stop();
				SendSoundPlay(selectedID, 0);
			}
		}
	}

	if (!OutPlay)
	{
		//Msg("!OutPlay");
		return;
	}
}