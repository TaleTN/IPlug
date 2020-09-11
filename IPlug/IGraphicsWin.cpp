#include "IGraphicsWin.h"
#include "Hosts.h"

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
#include <windowsx.h>
#include <wininet.h>

#include <string.h>
#include "WDL/wdlcstring.h"

static int nWndClassReg = 0;
static const char* wndClassName = "IPlugWndClass";
static double sFPS = 0.0;

#define PARAM_EDIT_ID 99

enum EParamEditMsg {
	kNone,
	kEditing,
	kCancel,
	kCommit
};

#define IPLUG_TIMER_ID 2

inline void SetMouseWheelFocus(HWND hWnd, IGraphicsWin* pGraphics)
{
	switch (pGraphics->GetPlug()->GetHost())
	{
		case kHostMixcraft:
			SetFocus(hWnd);
	}
}

inline IMouseMod GetMouseMod(WPARAM wParam)
{
	return IMouseMod((wParam & MK_LBUTTON), (wParam & MK_RBUTTON), 
        (wParam & MK_SHIFT), (wParam & MK_CONTROL), GetKeyState(VK_MENU) < 0);
}

// static
LRESULT CALLBACK IGraphicsWin::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  if (msg == WM_CREATE) {
    LPCREATESTRUCT lpcs = (LPCREATESTRUCT) lParam;
    SetWindowLongPtr(hWnd, GWLP_USERDATA, (LPARAM) (lpcs->lpCreateParams));
		int mSec = int(1000.0 / sFPS);
		SetTimer(hWnd, IPLUG_TIMER_ID, mSec, NULL);
		SetMouseWheelFocus(hWnd, (IGraphicsWin*) (lpcs->lpCreateParams));
		return 0;
	}

	IGraphicsWin* pGraphics = (IGraphicsWin*) GetWindowLongPtr(hWnd, GWLP_USERDATA);
	char txt[MAX_EDIT_LEN];

	if (!pGraphics || hWnd != pGraphics->mPlugWnd) {
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	if (pGraphics->mParamEditWnd && pGraphics->mParamEditMsg == kEditing) {
		if (msg == WM_RBUTTONDOWN) {
			pGraphics->mParamEditMsg = kCancel;
			return 0;
		}
	}

	switch (msg) {

		case WM_TIMER: {
			if (wParam == IPLUG_TIMER_ID) {
				if (pGraphics->mParamEditWnd && pGraphics->mParamEditMsg != kNone) {
					switch (pGraphics->mParamEditMsg) {
            case kCommit: {
							SendMessage(pGraphics->mParamEditWnd, WM_GETTEXT, MAX_EDIT_LEN, (LPARAM) txt);
							pGraphics->SetFromStringAfterPrompt(pGraphics->mEdControl, pGraphics->mEdParam, txt);
							// Fall through.
            }
            case kCancel:
			      {
							SetWindowLongPtr(pGraphics->mParamEditWnd, GWLP_WNDPROC, (LPARAM) pGraphics->mDefEditProc);
							DestroyWindow(pGraphics->mParamEditWnd);
							pGraphics->mParamEditWnd = 0;
							pGraphics->mEdParam = 0;
							pGraphics->mEdControl = 0;
							pGraphics->mDefEditProc = 0;
            }
            break;            
          }
					pGraphics->mParamEditMsg = kNone;
					//return 0;
				}

        IRECT dirtyR;
        if (pGraphics->IsDirty(&dirtyR)) {
          RECT r = { dirtyR.L, dirtyR.T, dirtyR.R, dirtyR.B };
          InvalidateRect(hWnd, &r, FALSE);
          if (pGraphics->mParamEditWnd) {
            GetClientRect(pGraphics->mParamEditWnd, &r);
            MapWindowPoints(pGraphics->mParamEditWnd, hWnd, (LPPOINT)&r, 2);
            ValidateRect(hWnd, &r);
          }
          UpdateWindow(hWnd);
        }

        if (pGraphics->mParamChangeTimer && !--pGraphics->mParamChangeTimer) {
          pGraphics->GetPlug()->EndDelayedInformHostOfParamChange();
        }
      }
      return 0;
    }
    case WM_RBUTTONDOWN: {
			if (pGraphics->mParamEditWnd) {
				pGraphics->mParamEditMsg = kCancel;
				return 0;
			}
			// Else fall through.
    }
    case WM_LBUTTONDOWN: {
			pGraphics->HideTooltip();
			if (pGraphics->mParamEditWnd) pGraphics->mParamEditMsg = kCommit;
			SetCapture(hWnd);
			pGraphics->OnMouseDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &GetMouseMod(wParam));
			return 0;
    }
    case WM_MOUSEMOVE: {
			if (!(wParam & (MK_LBUTTON | MK_RBUTTON))) { 
        if (pGraphics->OnMouseOver(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &GetMouseMod(wParam))) {
          TRACKMOUSEEVENT eventTrack = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, HOVER_DEFAULT };
          if (pGraphics->TooltipsEnabled()) {
            int c = pGraphics->GetMouseOver();
            if (c != pGraphics->mTooltipIdx) {
              if (c >= 0) eventTrack.dwFlags |= TME_HOVER;
              pGraphics->mTooltipIdx = c;
              pGraphics->HideTooltip();
            }
          }
          TrackMouseEvent(&eventTrack);
        }
			}
      else
			if (GetCapture() == hWnd) {
				pGraphics->OnMouseDrag(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &GetMouseMod(wParam));
			}
			return 0;
    }
	case WM_MOUSEHOVER: {
		pGraphics->ShowTooltip();
		return 0;
	}
    case WM_MOUSELEAVE: {
      pGraphics->HideTooltip();
      pGraphics->OnMouseOut();
      return 0;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONUP: {
      ReleaseCapture();
			pGraphics->OnMouseUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &GetMouseMod(wParam));
			return 0;
    }
    case WM_LBUTTONDBLCLK: {
      if (pGraphics->OnMouseDblClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &GetMouseMod(wParam))) {
        SetCapture(hWnd);
      }
			return 0;
    }
		case WM_MOUSEACTIVATE: {
			SetMouseWheelFocus(hWnd, pGraphics);
			return MA_ACTIVATE;
 		}
		case WM_MOUSEWHEEL: {
			int d = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
			int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
			RECT r;
			GetWindowRect(hWnd, &r);
			pGraphics->OnMouseWheel(x - r.left, y - r.top, &GetMouseMod(wParam), d);
			return 0;
		}

    case WM_KEYDOWN:
    {
      bool ok = true;
      int key;     

      if (wParam == VK_SPACE) key = KEY_SPACE;
      else if (wParam == VK_UP) key = KEY_UPARROW;
      else if (wParam == VK_DOWN) key = KEY_DOWNARROW;
      else if (wParam == VK_LEFT) key = KEY_LEFTARROW;
      else if (wParam == VK_RIGHT) key = KEY_RIGHTARROW;
      else if (wParam >= '0' && wParam <= '9') key = KEY_DIGIT_0+wParam-'0';
      else if (wParam >= 'A' && wParam <= 'Z') key = KEY_ALPHA_A+wParam-'A';
      else if (wParam >= 'a' && wParam <= 'z') key = KEY_ALPHA_A+wParam-'a';
      else ok = false;

      if (ok)
      {
        POINT p;
        GetCursorPos(&p); 
        ScreenToClient(hWnd, &p);
        pGraphics->OnKeyDown(p.x, p.y, key);
      }
    }
    return 0;


		case WM_PAINT: {
      RECT r;
      if (GetUpdateRect(hWnd, &r, FALSE)) {
        IRECT ir(r.left, r.top, r.right, r.bottom);
        pGraphics->Draw(&ir);
      }
			return 0;
		}

		//case WM_CTLCOLOREDIT: {
		//	// An edit control just opened.
		//	HDC dc = (HDC) wParam;
		//	SetTextColor(dc, ///);
		//	return 0;
		//}

		case WM_CLOSE: {
			pGraphics->CloseWindow();
			return 0;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

// static 
LRESULT CALLBACK IGraphicsWin::ParamEditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	IGraphicsWin* pGraphics = (IGraphicsWin*) GetWindowLongPtr(GetParent(hWnd), GWLP_USERDATA);

	if (pGraphics && pGraphics->mParamEditWnd && pGraphics->mParamEditWnd == hWnd) 
  {
		pGraphics->HideTooltip();

		switch (msg) {
			case WM_KEYDOWN: {
				if (wParam == VK_RETURN) {
					// Deselect, because in some hosts (FL Studio, VSTHost)
					// selected text gets deleted when processing VK_RETURN.
					char className[5];
					if (GetClassName(pGraphics->mParamEditWnd, className, sizeof(className)) == 4 && !stricmp(className, "EDIT")) {
						SendMessage(pGraphics->mParamEditWnd, EM_SETSEL, -1, 0);
					}
					pGraphics->mParamEditMsg = kCommit;
					return 0;
				}
				// Fall through.
			}
			case WM_SETFOCUS: {
				pGraphics->mParamEditMsg = kEditing;
				break;
			}
			case WM_KILLFOCUS: {
				pGraphics->mParamEditMsg = kNone;
				break;
			}
			// handle WM_GETDLGCODE so that we can say that we want the return key message
			//  (normally single line edit boxes don't get sent return key messages)
			case WM_GETDLGCODE: {
				if (pGraphics->mEdParam) break;
				LPARAM lres;
				// find out if the original control wants it
				lres = CallWindowProc(pGraphics->mDefEditProc, hWnd, WM_GETDLGCODE, wParam, lParam);
				// add in that we want it if it is a return keydown
				if (lParam  &&	 ((MSG*)lParam)->message == WM_KEYDOWN  &&  wParam == VK_RETURN) {
					lres |= DLGC_WANTMESSAGE;
				}
				return lres;
			}
			case WM_COMMAND: {
				switch HIWORD(wParam) {
					case CBN_SELCHANGE: {
						if (pGraphics->mParamEditWnd) {
							pGraphics->mParamEditMsg = kCommit;
							return 0;
						}
					}
				
				}
				break;	// Else let the default proc handle it.
			}
		}
		return CallWindowProc(pGraphics->mDefEditProc, hWnd, msg, wParam, lParam);
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

IGraphicsWin::IGraphicsWin(
	IPlugBase* const pPlug,
	const int w,
	const int h,
	const int refreshFPS
):
	IGraphics(pPlug, w, h, refreshFPS)
{
	mHInstance = NULL;
	mParentWnd = NULL;
	mPlugWnd = NULL;
	mTooltipWnd = NULL;
	mParamEditWnd = NULL;
	mEdControl = NULL;
	mEdParam = NULL;
	mDefEditProc = NULL;
	mParamEditMsg = kNone;
	mShowingTooltip = false;
	mTooltipIdx = -1;
	mParamChangeTimer = 0;

	mPID = 0;
	mMainWnd = NULL;
}

IGraphicsWin::~IGraphicsWin()
{
	CloseWindow();
  FREE_NULL(mCustomColorStorage);
}

LICE_IBitmap* IGraphicsWin::OSLoadBitmap(int ID, const char* name)
{
#ifndef IPLUG_NO_JPEG_SUPPORT
  const char* ext = name+strlen(name)-1;
  while (ext > name && *ext != '.') --ext;
  ++ext;

  if (!stricmp(ext, "png"))
#endif
  return _LICE::LICE_LoadPNGFromResource(mHInstance, MAKEINTRESOURCE(ID), 0);
#ifndef IPLUG_NO_JPEG_SUPPORT
  if (!stricmp(ext, "jpg") || !stricmp(ext, "jpeg")) return _LICE::LICE_LoadJPGFromResource(mHInstance, MAKEINTRESOURCE(ID), 0);
  return 0;
#endif
}

/* static void GetWindowSize(HWND const pWnd, int* const pW, int* const pH)
{
	if (pWnd)
	{
		RECT r;
		GetWindowRect(pWnd, &r);
		*pW = r.right - r.left;
		*pH = r.bottom - r.top;
	}
	else
	{
		*pW = *pH = 0;
	}
}

static bool IsChildWindow(HWND const pWnd)
{
	if (pWnd)
	{
		const LONG style = GetWindowLong(pWnd, GWL_STYLE);
		const LONG exStyle = GetWindowLong(pWnd, GWL_EXSTYLE);
		return (style & WS_CHILD) && !(exStyle & WS_EX_MDICHILD);
	}
	return false;
}

static const UINT SETPOS_FLAGS = SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE;

void IGraphicsWin::Resize(int w, int h)
{
	const int dw = w - Width(), dh = h - Height();
	IGraphics::Resize(w, h);
	mDrawBitmap.resize(w, h);
	if (WindowIsOpen())
	{
		HWND pParent = NULL, pGrandparent = NULL;
		int parentW = 0, parentH = 0, grandparentW = 0, grandparentH = 0;
		GetWindowSize(mPlugWnd, &w, &h);
		if (IsChildWindow(mPlugWnd))
		{
			pParent = GetParent(mPlugWnd);
			GetWindowSize(pParent, &parentW, &parentH);
			if (IsChildWindow(pParent))
			{
				pGrandparent = GetParent(pParent);
				GetWindowSize(pGrandparent, &grandparentW, &grandparentH);
			}
		}
		SetWindowPos(mPlugWnd, 0, 0, 0, w + dw, h + dh, SETPOS_FLAGS);
		if (pParent)
		{
			SetWindowPos(pParent, 0, 0, 0, parentW + dw, parentH + dh, SETPOS_FLAGS);
		}
		if (pGrandparent)
		{
			SetWindowPos(pGrandparent, 0, 0, 0, grandparentW + dw, grandparentH + dh, SETPOS_FLAGS);
		}

		const RECT r = { 0, 0, w, h };
		InvalidateRect(mPlugWnd, &r, FALSE);
	}
} */

void IGraphicsWin::DrawScreen(const IRECT* const pR)
{
	HWND const hWnd = (HWND)GetWindow();

	PAINTSTRUCT ps;
	HDC const dc = BeginPaint(hWnd, &ps);

	RECT r;
	GetClientRect(hWnd, &r);

	HDC const dcSrc = mDrawBitmap.getDC();
	const int scale = Scale();

	const int wDiv = Width() >> scale;
	const int hDiv = Height() >> scale;
	const int wMul = r.right - r.left;
	const int hMul = r.bottom - r.top;

	if (wMul == wDiv && hMul == hDiv)
	{
		const int x = pR->L >> scale;
		const int cx = pR->W() >> scale;
		const int y = pR->T >> scale;
		const int cy = pR->H() >> scale;

		BitBlt(dc, x, y, cx, cy, dcSrc, x, y, SRCCOPY);
	}
	else
	{
		SetStretchBltMode(dc, HALFTONE);
		SetBrushOrgEx(dc, 0, 0, NULL);

		const int wSrc = pR->W();
		const int xDest = MulDiv(pR->L, wMul, wDiv);
		const int wDest = MulDiv(wSrc, wMul, wDiv);

		const int hSrc = pR->H();
		const int yDest = MulDiv(pR->T, hMul, hDiv);
		const int hDest = MulDiv(hSrc, hMul, hDiv);

		StretchBlt(dc, xDest, yDest, wDest, hDest, dcSrc, pR->L, pR->T, wSrc, hSrc, SRCCOPY);
	}

	EndPaint(hWnd, &ps);
}

void* IGraphicsWin::OpenWindow(void* pParentWnd)
{
  int x = 0, y = 0, w = Width(), h = Height();
  mParentWnd = (HWND) pParentWnd;

	if (mPlugWnd) {
		RECT pR, cR;
		GetWindowRect((HWND) pParentWnd, &pR);
		GetWindowRect(mPlugWnd, &cR);
		CloseWindow();
		x = cR.left - pR.left;
		y = cR.top - pR.top;
		w = cR.right - cR.left;
		h = cR.bottom - cR.top;
	}

	if (nWndClassReg++ == 0) {
		WNDCLASS wndClass = { CS_DBLCLKS, WndProc, 0, 0, mHInstance, 0, LoadCursor(NULL, IDC_ARROW), 0, 0, wndClassName };
		RegisterClass(&wndClass);
	}

  sFPS = FPS();
  mPlugWnd = CreateWindow(wndClassName, "IPlug", WS_CHILD | WS_VISIBLE, // | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
		x, y, w, h, (HWND) pParentWnd, 0, mHInstance, this);
	//SetWindowLong(mPlugWnd, GWL_USERDATA, (LPARAM) this);

	if (!mPlugWnd && --nWndClassReg == 0) {
		UnregisterClass(wndClassName, mHInstance);
	}
  else {
    SetAllControlsDirty();
  }  

	if (mPlugWnd && TooltipsEnabled())
	{
		bool ok = false;
		static const INITCOMMONCONTROLSEX iccex = { sizeof(INITCOMMONCONTROLSEX), ICC_TAB_CLASSES };
		if (InitCommonControlsEx(&iccex))
		{
			mTooltipWnd = CreateWindowEx(0, TOOLTIPS_CLASS, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
				CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, mPlugWnd, NULL, mHInstance, NULL);
			if (mTooltipWnd)
			{
				SetWindowPos(mTooltipWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
				TOOLINFO ti = { TTTOOLINFOA_V2_SIZE, TTF_IDISHWND | TTF_SUBCLASS, mPlugWnd, (UINT_PTR)mPlugWnd };
				ti.lpszText = (LPTSTR)NULL;
				SendMessage(mTooltipWnd, TTM_ADDTOOL, 0, (LPARAM)&ti);
				ok = true;
			}
		}
		if (!ok) EnableTooltips(ok);
	}

	return mPlugWnd;
}

#define MAX_CLASSNAME_LEN 128
void GetWndClassName(HWND hWnd, WDL_String* pStr)
{
    char cStr[MAX_CLASSNAME_LEN];
    cStr[0] = '\0';
    GetClassName(hWnd, cStr, MAX_CLASSNAME_LEN);
    pStr->Set(cStr);
}

BOOL CALLBACK IGraphicsWin::FindMainWindow(HWND hWnd, LPARAM lParam)
{
    IGraphicsWin* pGraphics = (IGraphicsWin*) lParam;
    if (pGraphics) {
        DWORD wPID;
        GetWindowThreadProcessId(hWnd, &wPID);
        WDL_String str;
        GetWndClassName(hWnd, &str);
        if (wPID == pGraphics->mPID && !strcmp(str.Get(), pGraphics->mMainWndClassName.Get())) {
            pGraphics->mMainWnd = hWnd;
            return FALSE;   // Stop enumerating.
        }
    }
    return TRUE;
}

HWND IGraphicsWin::GetMainWnd()
{
    if (!mMainWnd) {
        if (mParentWnd) {
            HWND parentWnd = mParentWnd;
            while (parentWnd) {
                mMainWnd = parentWnd;
                parentWnd = GetParent(mMainWnd);
            }
            GetWndClassName(mMainWnd, &mMainWndClassName);
        }
        else
        if (CSTR_NOT_EMPTY(mMainWndClassName.Get())) {
            mPID = GetCurrentProcessId();
            EnumWindows(FindMainWindow, (LPARAM) this);
        }
    }
    return mMainWnd;
}

#define TOOLWIN_BORDER_W 6
#define TOOLWIN_BORDER_H 23

IRECT IGraphicsWin::GetWindowRECT()
{
    if (mPlugWnd) {
        RECT r;
        GetWindowRect(mPlugWnd, &r);
        r.right -= TOOLWIN_BORDER_W;
        r.bottom -= TOOLWIN_BORDER_H;
        return IRECT(r.left, r.top, r.right, r.bottom);
    }
    return IRECT();
}

void IGraphicsWin::SetWindowTitle(char* str)
{
    SetWindowText(mPlugWnd, str);
}

void IGraphicsWin::CloseWindow()
{
	if (mPlugWnd) {
		if (mTooltipWnd) {
			DestroyWindow(mTooltipWnd);
			mTooltipWnd = 0;
			mShowingTooltip = false;
			mTooltipIdx = -1;
		}

		if (mParamChangeTimer) {
			GetPlug()->EndDelayedInformHostOfParamChange();
		}

		DestroyWindow(mPlugWnd);
		mPlugWnd = 0;

		if (mParamEditWnd)
		{
			mParamEditWnd = 0;
			mEdParam = 0;
			mEdControl = 0;
			mDefEditProc = 0;
		}

		if (--nWndClassReg == 0) {
			UnregisterClass(wndClassName, mHInstance);
		}
	}
}

#define PARAM_EDIT_W 36
#define PARAM_EDIT_H 16
#define PARAM_EDIT_H_PER_ENUM 36
#define PARAM_LIST_MIN_W 24
#define PARAM_LIST_W_PER_CHAR 8

void IGraphicsWin::PromptUserInput(IControl* pControl, IParam* pParam)
{
	if (!pControl || !pParam || mParamEditWnd) {
		return;
	}

	IRECT* pR = pControl->GetRECT();
	int cX = int(pR->MW()), cY = int(pR->MH());
  char currentText[MAX_PARAM_LEN];
  pParam->GetDisplayForHost(currentText);

  int n = pParam->GetNDisplayTexts();
  if (n && (pParam->Type() == IParam::kTypeEnum || pParam->Type() == IParam::kTypeBool)) {
    int i, currentIdx = -1;
		int w = PARAM_LIST_MIN_W, h = PARAM_EDIT_H_PER_ENUM * (n + 1);
		for (i = 0; i < n; ++i) {
      const char* str = pParam->GetDisplayText(i);
      w = MAX(w, PARAM_LIST_MIN_W + strlen(str) * PARAM_LIST_W_PER_CHAR);
			if (!strcmp(str, currentText)) {
				currentIdx = i;
			}
		}			

		mParamEditWnd = CreateWindow("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
			cX - w/2, cY, w, h, mPlugWnd, (HMENU) PARAM_EDIT_ID, mHInstance, 0);

		for (i = 0; i < n; ++i) {
      const char* str = pParam->GetDisplayText(i);
			SendMessage(mParamEditWnd, CB_ADDSTRING, 0, (LPARAM) str);
		}
		SendMessage(mParamEditWnd, CB_SETCURSEL, (WPARAM) currentIdx, 0);
	}
	else {
		int w = PARAM_EDIT_W, h = PARAM_EDIT_H;
		mParamEditWnd = CreateWindow("EDIT", currentText, WS_CHILD | WS_VISIBLE | ES_CENTER | ES_MULTILINE,
			cX - w/2, cY - h/2, w, h, mPlugWnd, (HMENU) PARAM_EDIT_ID, mHInstance, 0);
		SendMessage(mParamEditWnd, EM_SETSEL, 0, -1);
	}
	SetFocus(mParamEditWnd);

	mDefEditProc = (WNDPROC) SetWindowLongPtr(mParamEditWnd, GWLP_WNDPROC, (LONG_PTR) ParamEditProc);
  SetWindowLongPtr(mParamEditWnd, GWLP_USERDATA, 0xdeadf00b);

  IText txt;
	HFONT font = CreateFont(txt.mSize, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, txt.mFont);
	SendMessage(mParamEditWnd, WM_SETFONT, (WPARAM) font, 0);
	//DeleteObject(font);	

	mEdControl = pControl;
	mEdParam = pParam;	
}

void IGraphicsWin::PromptUserInput(IEditableTextControl* pControl)
{
	if (!pControl || mParamEditWnd) return;

	IRECT* pR = pControl->GetRECT();

	const IText* txt = pControl->GetIText();
	DWORD editStyle;
	switch (txt->mAlign)
	{
		case IText::kAlignNear:   editStyle = ES_LEFT;   break;
		case IText::kAlignFar:    editStyle = ES_RIGHT;  break;
		case IText::kAlignCenter:
		default:                  editStyle = ES_CENTER; break;
	}
	if (!pControl->IsEditable()) editStyle |= ES_READONLY;
	if (pControl->IsSecure())
		editStyle |= ES_PASSWORD;
	else
		editStyle |= ES_MULTILINE;
	mParamEditWnd = CreateWindow("EDIT", pControl->GetText(), WS_CHILD | WS_VISIBLE | editStyle,
		pR->L, pR->T, pR->W(), pR->H(), mPlugWnd, (HMENU) PARAM_EDIT_ID, mHInstance, 0);
	SendMessage(mParamEditWnd, EM_SETSEL, 0, -1);
	SetFocus(mParamEditWnd);

	mDefEditProc = (WNDPROC) SetWindowLongPtr(mParamEditWnd, GWLP_WNDPROC, (LONG_PTR) ParamEditProc);
  SetWindowLongPtr(mParamEditWnd, GWLP_USERDATA, 0xdeadf00b);

	HFONT font = CreateFont(txt->mSize, 0, 0, 0, txt->mStyle == IText::kStyleBold ? FW_BOLD : 0, txt->mStyle == IText::kStyleItalic ? TRUE : 0, 0, 0, 0, 0, 0, 0, 0, txt->mFont);
	SendMessage(mParamEditWnd, WM_SETFONT, (WPARAM) font, 0);
	//DeleteObject(font);	

	mEdControl = pControl;
	mEdParam = 0;
}

#define MAX_PATH_LEN 256

void GetModulePath(HMODULE hModule, WDL_String* pPath) 
{
  pPath->Set("");
	char pathCStr[MAX_PATH_LEN];
	pathCStr[0] = '\0';
	if (GetModuleFileName(hModule, pathCStr, MAX_PATH_LEN)) {
    int s = -1;
    for (int i = 0; i < strlen(pathCStr); ++i) {
      if (pathCStr[i] == '\\') {
        s = i;
      }
    }
    if (s >= 0 && s + 1 < strlen(pathCStr)) {
      pPath->Set(pathCStr, s + 1);
    }
  }
}

void IGraphicsWin::HostPath(WDL_String* pPath)
{
  GetModulePath(0, pPath);
}

void IGraphicsWin::PluginPath(WDL_String* pPath)
{
  GetModulePath(mHInstance, pPath);
}

void IGraphicsWin::PromptForFile(WDL_String* pFilename, EFileAction action, char* dir, char* extensions)
{
	if (!WindowIsOpen()) { 
		pFilename->Set("");
		return;
	}

  WDL_String pathStr;
	char fnCStr[MAX_PATH_LEN], dirCStr[MAX_PATH_LEN];
	strncpy(fnCStr, pFilename->Get(), MAX_PATH_LEN - 1);
	fnCStr[MAX_PATH_LEN - 1] = '\0';
	dirCStr[0] = '\0';
	if (CSTR_NOT_EMPTY(dir)) {
  pathStr.Set(dir);
		strcpy(dirCStr, dir);
	}
  else {
    HostPath(&pathStr);
  }
	
	OPENFILENAME ofn;
	memset(&ofn, 0, sizeof(OPENFILENAME));

	ofn.lStructSize = sizeof(OPENFILENAME);
	ofn.hwndOwner = mPlugWnd;
	ofn.lpstrFile = fnCStr;
	ofn.nMaxFile = MAX_PATH_LEN - 1;
	ofn.lpstrInitialDir = dirCStr;
	ofn.Flags = OFN_PATHMUSTEXIST;

	if (CSTR_NOT_EMPTY(extensions)) {
		char extStr[256];
		char defExtStr[16];
		int i, p, n = strlen(extensions);

		bool seperator = true;
		for (i = 0, p = 0; i < n; ++i) {
			if (seperator) {
				if (p) {
					extStr[p++] = ';';
				}
				seperator = false;
				extStr[p++] = '*';
				extStr[p++] = '.';
			}
			if (extensions[i] == ' ') {
				seperator = true;
			}
			else {
				extStr[p++] = extensions[i];
			}
		}
		extStr[p++] = '\0';

		strcpy(&extStr[p], extStr);
		extStr[p + p] = '\0';
		ofn.lpstrFilter = extStr;

		for (i = 0, p = 0; i < n && extensions[i] != ' '; ++i) {
			defExtStr[p++] = extensions[i];
		}
		defExtStr[p++] = '\0';
		ofn.lpstrDefExt = defExtStr;
	}

    bool rc = false;
    switch (action) {
        case kFileSave:
            ofn.Flags |= OFN_OVERWRITEPROMPT;
            rc = GetSaveFileName(&ofn);
            break;

        case kFileOpen:     
        default:
            ofn.Flags |= OFN_FILEMUSTEXIST;
    	    rc = GetOpenFileName(&ofn);
            break;
    }

    if (rc) {
        pFilename->Set(ofn.lpstrFile);
    }
    else {
        pFilename->Set("");
    }
}

UINT_PTR CALLBACK CCHookProc(HWND hdlg, UINT uiMsg, WPARAM wParam, LPARAM lParam)
{
    if (uiMsg == WM_INITDIALOG && lParam) {
        CHOOSECOLOR* cc = (CHOOSECOLOR*) lParam;
        if (cc && cc->lCustData) {
            char* str = (char*) cc->lCustData;
            SetWindowText(hdlg, str);
        }
    }
    return 0;
}

bool IGraphicsWin::PromptForColor(IColor* pColor, char* prompt)
{
    if (!mPlugWnd) {
        return false;
    }
    if (!mCustomColorStorage) {
        mCustomColorStorage = (COLORREF*) calloc(16, sizeof(COLORREF));
    }
    CHOOSECOLOR cc;
    memset(&cc, 0, sizeof(CHOOSECOLOR));
    cc.lStructSize = sizeof(CHOOSECOLOR);
    cc.hwndOwner = mPlugWnd;
    cc.rgbResult = RGB(pColor->R, pColor->G, pColor->B);
    cc.lpCustColors = mCustomColorStorage;
    cc.lCustData = (LPARAM) prompt;
    cc.lpfnHook = CCHookProc;
    cc.Flags = CC_RGBINIT | CC_ANYCOLOR | CC_FULLOPEN | CC_SOLIDCOLOR | CC_ENABLEHOOK;
    
    if (ChooseColor(&cc)) {
        pColor->R = GetRValue(cc.rgbResult);
        pColor->G = GetGValue(cc.rgbResult);
        pColor->B = GetBValue(cc.rgbResult);
        return true;
    }
    return false;
}

#define MAX_INET_ERR_CODE 32
bool IGraphicsWin::OpenURL(const char* url, 
  const char* msgWindowTitle, const char* confirmMsg, const char* errMsgOnFailure)
{
  if (confirmMsg && MessageBox(mPlugWnd, confirmMsg, msgWindowTitle, MB_YESNO) != IDYES) {
    return false;
  }
  DWORD inetStatus = 0;
  if (InternetGetConnectedState(&inetStatus, 0)) {
    if ((INT_PTR) ShellExecute(mPlugWnd, "open", url, 0, 0, SW_SHOWNORMAL) > MAX_INET_ERR_CODE) {
      return true;
    }
  }
  if (errMsgOnFailure) {
    MessageBox(mPlugWnd, errMsgOnFailure, msgWindowTitle, MB_OK);
  }
  return false;
}

int IGraphicsWin::ProcessMouseWheel(float delta)
{
  POINT p;
  GetCursorPos(&p);
  HWND hWnd = WindowFromPoint(p);
  if (hWnd == mPlugWnd) {
    int d = int(delta);
    RECT r;
    GetWindowRect(hWnd, &r);
    OnMouseWheel(p.x - r.left, p.y - r.top, &IMouseMod(false, false, GetKeyState(VK_SHIFT) < 0, GetKeyState(VK_CONTROL) < 0, GetKeyState(VK_MENU) < 0), d);
    return 1;
  }
  return 0;
}

void IGraphicsWin::SetTooltip(const char* tooltip)
{
	TOOLINFO ti = { TTTOOLINFOA_V2_SIZE, 0, mPlugWnd, (UINT_PTR)mPlugWnd };
	ti.lpszText = (LPTSTR)tooltip;
	SendMessage(mTooltipWnd, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
}

void IGraphicsWin::ShowTooltip()
{
	mTooltipIdx = TooltipsEnabled() ? GetMouseOver() : -1;
	if (mTooltipIdx < 0) return;

	const char* tooltip = GetControl(mTooltipIdx)->GetTooltip();
	if (tooltip)
	{
		assert(strlen(tooltip) < 80);
		SetTooltip(tooltip);
		mShowingTooltip = true;
	}
}

void IGraphicsWin::HideTooltip()
{
	if (mShowingTooltip)
	{
		SetTooltip(NULL);
		mShowingTooltip = false;
	}
}
