#pragma once
#include "ui\UIDialogWnd.h"


class CUIStatic;
class CUIScrollView;
class CUITextWnd;

class CUIAMode : public CUIDialogWnd
{
	typedef CUIWindow	inherited;
	virtual bool	OnMouseAction(float x, float y, EUIMessages mouse_action);
	CUIScrollView* list_anims;
	CUITextWnd* text[32];

public:
	CUIAMode();
	~CUIAMode();
	CUIStatic* frame_animations;


	void			Init();
	virtual void 	Show(bool status);
	virtual void	Update();
	virtual bool	OnKeyboardAction(int dik, EUIMessages keyboard_action);
};

