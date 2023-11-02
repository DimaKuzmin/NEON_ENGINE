#pragma once

#include "../Include/xrRender/KinematicsAnimated.h"

struct SAnimState
{
	MotionID	legs_fwd;
	MotionID	legs_back;
	MotionID	legs_ls;
	MotionID	legs_rs;
	void		Create								(IKinematicsAnimated* K, LPCSTR base0, LPCSTR base1);

};

struct STorsoWpn{
	enum eMovingState{eIdle, eWalk, eRun, eSprint, eTotal};
	MotionID	moving[eTotal];

	MotionID	zoom;
	MotionID	holster;
	MotionID	draw;
	MotionID	drop;
	MotionID	reload;
	MotionID	reload_1;
	MotionID	reload_2;
	MotionID	attack;
	MotionID	attack_zoom;
	MotionID	fire_idle;
	MotionID	fire_end;

	//анимации для атаки для всего тела (когда мы стоим на месте)
	MotionID	all_attack_0;
	MotionID	all_attack_1;
	MotionID	all_attack_2;
	void		Create								(IKinematicsAnimated* K, LPCSTR base0, LPCSTR base1);
};

#define _total_anim_slots_ 13

struct SActorState
{

	MotionID		legs_idle;
	MotionID		jump_begin;
	MotionID		jump_idle;
	MotionID		landing[2];
	MotionID		legs_turn;
	MotionID		death;
	SAnimState		m_walk;
	SAnimState		m_run;
	STorsoWpn		m_torso[_total_anim_slots_];

	MotionID		m_torso_idle;
	MotionID		m_head_idle;

	MotionID		m_damage[DAMAGE_FX_COUNT];
	void			Create							(IKinematicsAnimated* K, LPCSTR base);
	void			CreateClimb						(IKinematicsAnimated* K);
};

struct SActorSprintState 
{
	//leg anims
	MotionID		legs_fwd;
	MotionID		legs_ls;
	MotionID		legs_rs;
	
	MotionID		legs_jump_fwd;
	MotionID		legs_jump_ls;
	MotionID		legs_jump_rs;

	void Create		(IKinematicsAnimated* K);
};

#define MAX_ANIMS 1024
#define MAX_SNDS 64
#define MAX_SIZE_ANIMS_INOUT 32

struct SScript_AnimInput
{
	MotionID m_animation_in[MAX_ANIMS][MAX_SIZE_ANIMS_INOUT];
	u32 count[MAX_ANIMS];
};

struct SScript_AnimOut
{
	MotionID m_animation_out[MAX_ANIMS][MAX_SIZE_ANIMS_INOUT];
	u32 count[MAX_ANIMS];
};

struct SScript_AnimMiddle
{
	MotionID m_animation[MAX_ANIMS][MAX_SIZE_ANIMS_INOUT];
	u32 count[MAX_ANIMS];
};




struct SActorStateAnimation
{
	SScript_AnimInput  in_anims;
	SScript_AnimOut    out_anims;
	SScript_AnimMiddle middle_anims;

	bool m_animation_loop[MAX_ANIMS];
	u32	 m_rnd_snds[MAX_ANIMS];
	ref_sound m_sound_Animation[MAX_ANIMS][MAX_SNDS];

	shared_str m_animation_attach[MAX_ANIMS];

	void CreateAnimationsScripted(IKinematicsAnimated* K);
};


struct SAnimStateSafe
{
	MotionID	legs_fwd;
	MotionID	legs_back;
	MotionID	legs_ls;
	MotionID	legs_rs;

	void		CreateSafe(IKinematicsAnimated* K, LPCSTR base0, LPCSTR base1);

};

struct SAnimStateSprintSafe
{
	MotionID		legs_fwd;
 	MotionID		legs_ls;
	MotionID		legs_rs;

	MotionID		legs_jump_fwd;
	MotionID		legs_jump_ls;
	MotionID		legs_jump_rs;

	void		CreateSafeSprint(IKinematicsAnimated* K);
};

struct SAnimTorsoStateSafe
{
	enum eMovingState { eIdleSafe, eWalkSafe, eRunSafe, eSprintSafe, eTotal };
	MotionID	torso_moving_safe[eTotal];

	void CreateTorsoSafe(IKinematicsAnimated* K, LPCSTR base0, LPCSTR base1);
};

struct SAnimSafeMotions
{	
	SAnimStateSafe  m_safe_run;
	SAnimStateSafe  m_safe_walk;
	SAnimStateSprintSafe  m_safe_sprint;

	MotionID		m_torso_idle;
	MotionID		m_head_idle;
	MotionID		m_legs_idle;

	SAnimTorsoStateSafe m_safe_torso[_total_anim_slots_];
 
	void CreateSafeAnims(IKinematicsAnimated* K, LPCSTR base);
};

struct SActorMotions
{
	MotionID			m_dead_stop;
	SActorState			m_normal;
	SActorState			m_crouch;
	SActorState			m_climb;
	SActorSprintState	m_sprint;

	SActorStateAnimation m_script;
	SAnimSafeMotions     m_safe_mode;


	void				Create(IKinematicsAnimated* K);
};

//vehicle anims
struct	SVehicleAnimCollection
{
	static const u16 MAX_IDLES = 3;
	u16				idles_num;
	MotionID		idles[MAX_IDLES];
	MotionID		steer_left;
	MotionID		steer_right;
					SVehicleAnimCollection	();
	void			Create				(IKinematicsAnimated* K,u16 num);
};

struct SActorVehicleAnims
{
	static const int TYPES_NUMBER=2;
	SVehicleAnimCollection m_vehicles_type_collections	[TYPES_NUMBER];
						SActorVehicleAnims				();
	void				Create							(IKinematicsAnimated* K);
};


