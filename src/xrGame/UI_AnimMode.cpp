#include "stdafx.h"
#include "UI_AnimMode.h"

#include "ui/UIScrollView.h"
#include "ui/UIHelper.h"
#include "ui/UIStatic.h"
#include "ui/UIXmlInit.h"
#include "Level.h"
#include "UICursor.h"

extern int ANIM_SELECTED = 0;

bool CUIAMode::OnMouseAction(float x, float y, EUIMessages mouse_action)
{
    if (mouse_action == WINDOW_LBUTTON_DOWN)
    {
        auto child = list_anims->Items();
        u8 id = 0;
        for (auto item : child)
        {
            CUIWindow* w = item;
            CUITextWnd* wind = smart_cast<CUITextWnd*>(w);

            Frect wndRect;
            wind->GetAbsoluteRect(wndRect);
            Fvector2 pos = GetUICursor().GetCursorPosition();

            if (wind && wndRect.in(pos))
            {
                if (wind == text[id])
                {
                    //Msg("Click Anim [%d] [%s] ", id, text[id]->GetText());
                    ANIM_SELECTED = id;
                }
            }
            id += 1;
        }
    }


    return inherited::OnMouseAction(x, y, mouse_action);
}

CUIAMode::CUIAMode()
{
}

CUIAMode::~CUIAMode()
{
}

void CUIAMode::Init()
{
    CUIXml uiXML_Anims;
    uiXML_Anims.Load(CONFIG_PATH, UI_PATH, "ui_MOD.xml");

    frame_animations = UIHelper::CreateStatic(uiXML_Anims, "background_static", this);
    UIHelper::CreateTextWnd(uiXML_Anims, "caption", frame_animations);

    list_anims = xr_new<CUIScrollView>();
    list_anims->SetAutoDelete(true);
    CUIXmlInit::InitScrollView(uiXML_Anims, "list", 0, list_anims);
    frame_animations->AttachChild(list_anims);

    string_path file_path;
    FS.update_path(file_path, "$game_config$", "actor_anims.ltx");

    if (FS.exist(file_path))
    {
        CInifile* file = xr_new<CInifile>(file_path, true, true);
        if (file)
        {
            if (file->section_exist("animations"))
            {
                u32 counts = file->r_u32("animations", "count");
                for (u32 count = 0; count < counts; count++)
                {
                    string32 name = { 0 };
                    string32 tmp;
                    xr_strcat(name, "anim_");
                    xr_strcat(name, itoa(count, tmp, 10));

                    if (file->section_exist(name))
                    {
                        LPCSTR name_desc = file->r_string(name, "anim_descr");
                        text[count] = UIHelper::CreateTextWnd(uiXML_Anims, "caption_text", 0);
                        text[count]->SetText(name_desc);

                        list_anims->AddWindow(text[count], true);
                    }
                }
            }
        }
    }

}

void CUIAMode::Show(bool status)
{
    inherited::Show(status);
}

void CUIAMode::Update()
{
    inherited::Update();
}

bool CUIAMode::OnKeyboardAction(int dik, EUIMessages keyboard_action)
{
    if (is_binded(kAnimationMode, dik))
        HideDialog();

    return inherited::OnKeyboardAction(dik, keyboard_action);
}
