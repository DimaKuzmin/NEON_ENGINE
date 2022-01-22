#include "stdafx.h"
#include "UIGameFMP.h"
#include "game_cl_freemp.h"
#include "../xrEngine/xr_input.h"

#include "Actor.h"
#include "level.h"
#include "xr_level_controller.h"

#include "ui/UIStatic.h"
#include "ui/UIXmlInit.h"
#include "UI_AnimMode.h"

BOOL g_cl_draw_mp_statistic = FALSE;

CUIGameFMP::CUIGameFMP()
{
	m_game = NULL;
	m_anims = NULL;
}

CUIGameFMP::~CUIGameFMP()
{
	xr_delete(m_anims);
}

void CUIGameFMP::Init(int stage)
{
	CUIXml uiXml;
	uiXml.Load(CONFIG_PATH, UI_PATH, "ui_game_fmp.xml");

	if (stage == 0)
	{
		//shared
		m_stats = xr_new<CUITextWnd>();
		m_stats->SetAutoDelete(true);

		inherited::Init(stage);

		CUIXmlInit::InitWindow(uiXml, "global", 0, m_window);
		CUIXmlInit::InitTextWnd(uiXml, "stats", 0, m_stats);
	}
	else if (stage == 1)
	{
		//unique

	}
	else if (stage == 2)
	{
		//after
		inherited::Init(stage);
		m_window->AttachChild(m_stats);
	}

	m_anims = xr_new<CUIAMode>();
 	m_anims->Init();
	m_window->AttachChild(m_anims);
}

void CUIGameFMP::SetClGame(game_cl_GameState * g)
{
	inherited::SetClGame(g);
	m_game = smart_cast<game_cl_freemp*>(g);
	R_ASSERT(m_game);
}

void CUIGameFMP::HideShownDialogs()
{
	inherited::HideShownDialogs();
}

void _BCL CUIGameFMP::OnFrame()
{
	inherited::OnFrame();

	if (g_cl_draw_mp_statistic && Level().game->local_player)
	{
		IClientStatistic& stats = Level().GetStatistic();

		string1024 outstr;
		if (UseDirectPlay())
		{
			xr_sprintf(
				outstr,
				"ping: %u/%u\\n"
				"in/out: %.1f/%.2f KB/s\\n"
				"packets dropped: %u\\n"
				"packets retried: %u\\n",
				Level().game->local_player->ping,
				stats.getPing(),
				stats.getReceivedPerSec() / 1000.0f,
				stats.getSendedPerSec() / 1000.0f,
				stats.getDroppedCount(),
				stats.getRetriedCount()
			);
		}
		else
		{
			xr_sprintf(
				outstr,
				"ping: %u/%u\\n"
				"in/out: %.1f/%.2f KB/s\\n"
				"packets in/out: %.0f/%.0f\\n"
				//"queue time: %u\\n"
				//"send rate: %u bps\\n"
				//"pending reliable: %u\\n"
				//"pending unreliable: %u\\n"
				//"sent unacked reliable: %u\\n"
				"quality local: %.2f\\n"
				"quality remote: %.2f\\n",

				Level().game->local_player->ping,
				stats.getPing(),
				stats.getReceivedPerSec() / 1000.0f,
				stats.getSendedPerSec() / 1000.0f,
				stats.getPacketsInPerSec(),
				stats.getPacketsOutPerSec(),
				//stats.getQueueTime(),
				//stats.getSendRateBytesPerSecond(),
				//stats.getPendingReliable(),
				//stats.getPendingUnreliable(),
				//stats.getSentUnackedReliable(),
				stats.getQualityLocal(),
				stats.getQualityRemote()
			);
		}

		m_stats->SetTextST(outstr);
		m_stats->Enable(true);
	}
	else if (m_stats->IsEnabled())
	{
		m_stats->SetTextST("");
		m_stats->Enable(false);
	}
}

bool CUIGameFMP::IR_UIOnKeyboardPress(int dik)
{
	if (inherited::IR_UIOnKeyboardPress(dik)) return true;
	if (Device.Paused()) return false;

	CInventoryOwner* pInvOwner = smart_cast<CInventoryOwner*>(Level().CurrentEntity());
	if (!pInvOwner)			return false;

	CEntityAlive* EA = smart_cast<CEntityAlive*>(Level().CurrentEntity());
	if (!EA || !EA->g_Alive())	return false;

	CActor *pActor = smart_cast<CActor*>(pInvOwner);
	if (!pActor)
		return false;

	if (!pActor->g_Alive())
		return false;

	switch (get_binded_action(dik))
	{
		case kACTIVE_JOBS:
			{
				if (!pActor->inventory_disabled())
					ShowPdaMenu();			
			} break;
		case kINVENTORY:
			{
				if (!pActor->inventory_disabled())
					ShowActorMenu();			
			} break;

		case kAnimationMode:
		{
			if (!m_anims->IsShown())
				m_anims->ShowDialog(false);

		}break;

		default:
			break;
	}
	return false;
}
