#include "IGraphicsWin.h"
#include "Hosts.h"

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>
#include <wininet.h>

#include <string.h>
#include "WDL/wdlcstring.h"
#include "WDL/wdlutf8.h"

static int nWndClassReg = 0;
static const WCHAR* const wndClassName = L"IPlugWndClass";

static const int PARAM_EDIT_ID = 99;

static const UINT_PTR IPLUG_TIMER_ID = 2;
static const UINT IPLUG_DEFAULT_DPI = USER_DEFAULT_SCREEN_DPI * 2;

static void ScalePoint(LPPOINT const lpPoint, const int dpi)
{
	lpPoint->x = MulDiv(lpPoint->x, IPLUG_DEFAULT_DPI, dpi);
	lpPoint->y = MulDiv(lpPoint->y, IPLUG_DEFAULT_DPI, dpi);
}

static void ScaleLParamXY(LPPOINT const lpPoint, const LPARAM lParam, const int dpi)
{
	lpPoint->x = GET_X_LPARAM(lParam);
	lpPoint->y = GET_Y_LPARAM(lParam);
	ScalePoint(lpPoint, dpi);
}

static bool WantFocus(IGraphicsWin* const pGraphics)
{
	return pGraphics->GetKeyboardFocus() >= 0 || pGraphics->GetPlug()->GetHost() == kHostMixcraft;
}

static IMouseMod GetMouseMod(const WPARAM wParam, const bool bWheel = false)
{
	static const int mask = MK_LBUTTON | MK_RBUTTON | MK_SHIFT | MK_CONTROL;
	static const int alt = mask + 1, wheel = alt << 1;

	unsigned int bitfield = ((GetKeyState(VK_MENU) >> 11) & alt) | (wParam & mask);
	if (bWheel) bitfield |= wheel;

	IMouseMod mod;
	mod.Set(bitfield);

	return mod;
}

static IMouseMod GetKeyMod(const bool bWheel = false)
{
	return IMouseMod(false, false, GetKeyState(VK_SHIFT) < 0, GetKeyState(VK_CONTROL) < 0, GetKeyState(VK_MENU) < 0, bWheel);
}

static void DeleteParamEditFont(HWND const hWnd)
{
	HFONT const font = (HFONT)SendMessageW(hWnd, WM_GETFONT, 0, 0);
	if (font)
	{
		SendMessageW(hWnd, WM_SETFONT, NULL, FALSE);
		DeleteObject(font);
	}
}

// static
LRESULT CALLBACK IGraphicsWin::WndProc(HWND const hWnd, const UINT msg, const WPARAM wParam, const LPARAM lParam)
{
	if (msg == WM_CREATE)
	{
		LPCREATESTRUCT const lpcs = (LPCREATESTRUCT)lParam;
		IGraphicsWin* const pGraphics = (IGraphicsWin*)lpcs->lpCreateParams;
		SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LPARAM)pGraphics);

		const int mSec = (int)(1000.0 / pGraphics->FPS());
		SetTimer(hWnd, IPLUG_TIMER_ID, mSec, NULL);

		if (WantFocus(pGraphics)) SetFocus(hWnd);
		return 0;
	}

	IGraphicsWin* const pGraphics = (IGraphicsWin*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

	if (pGraphics && hWnd == pGraphics->mPlugWnd)
	switch (msg)
	{
		case WM_TIMER:
		{
			if (wParam == IPLUG_TIMER_ID)
			{
				IRECT dirtyR;
				if (pGraphics->IsDirty(&dirtyR))
				{
					RECT cR, r;
					GetClientRect(hWnd, &cR);

					const int scale = pGraphics->Scale();

					const int wMul = cR.right - cR.left;
					const int hMul = cR.bottom - cR.top;
					const int wDiv = pGraphics->Width();
					const int hDiv = pGraphics->Height();

					if ((wDiv >> scale) == wMul && (hDiv >> scale) == hMul)
					{
						r.left = dirtyR.L >> scale;
						r.top = dirtyR.T >> scale;
						r.right = dirtyR.R >> scale;
						r.bottom = dirtyR.B >> scale;
					}
					else
					{
						const int x = MulDiv(dirtyR.L, wMul, wDiv);
						const int y = MulDiv(dirtyR.T, hMul, hDiv);
						const int w = MulDiv(dirtyR.R - dirtyR.L, wMul, wDiv);
						const int h = MulDiv(dirtyR.B - dirtyR.T, hMul, hDiv);

						r.left = x - 1;
						r.top = y - 1;
						r.right = x + w + 1;
						r.bottom = y + h + 1;

						r.left = wdl_max(r.left, cR.left);
						r.top = wdl_max(r.top, cR.top);
						r.right = wdl_min(r.right, cR.right);
						r.bottom = wdl_min(r.bottom, cR.bottom);
					}
					InvalidateRect(hWnd, &r, FALSE);

					if (pGraphics->mParamEditWnd)
					{
						GetClientRect(pGraphics->mParamEditWnd, &r);
						MapWindowPoints(pGraphics->mParamEditWnd, hWnd, (LPPOINT)&r, 2);
						ValidateRect(hWnd, &r);
					}
					UpdateWindow(hWnd);
				}

				const int timer = pGraphics->mParamChangeTimer;
				if (timer && !(pGraphics->mParamChangeTimer = timer - 1))
				{
					pGraphics->GetPlug()->EndDelayedInformHostOfParamChange();
				}
			}
			return 0;
		}

		case WM_RBUTTONDOWN:
		{
			if (pGraphics->mParamEditWnd)
			{
				pGraphics->CancelParamEdit();
				return 0;
			}
			// Else fall through.
		}
		case WM_LBUTTONDOWN:
		{
			pGraphics->HideTooltip();
			if (pGraphics->mParamEditWnd) pGraphics->CommitParamEdit();
			SetCapture(hWnd);

			POINT p;
			ScaleLParamXY(&p, lParam, pGraphics->mDPI);

			pGraphics->OnMouseDown(p.x, p.y, GetMouseMod(wParam));
			return 0;
		}

		case WM_MOUSEMOVE:
		{
			POINT p;
			ScaleLParamXY(&p, lParam, pGraphics->mDPI);

			if (!(wParam & (MK_LBUTTON | MK_RBUTTON)))
			{
				if (pGraphics->CanHandleMouseOver())
				{
					pGraphics->OnMouseOver(p.x, p.y, GetMouseMod(wParam));
					TRACKMOUSEEVENT eventTrack = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, HOVER_DEFAULT };
					if (pGraphics->TooltipsEnabled())
					{
						const int c = pGraphics->GetMouseOver();
						if (pGraphics->mTooltipIdx != c)
						{
							pGraphics->mTooltipIdx = c;
							const char* const tooltip = pGraphics->GetTooltip(c);
							if (tooltip)
							{
								const bool hidden = !pGraphics->mTooltipBuf[0];
								pGraphics->SetTooltip(tooltip);
								if (hidden)
									eventTrack.dwFlags |= TME_HOVER;
								else
									pGraphics->ShowTooltip();
							}
							else
							{
								pGraphics->HideTooltip();
							}
						}
					}
					TrackMouseEvent(&eventTrack);
				}
			}
			else if (GetCapture() == hWnd)
			{
				pGraphics->OnMouseDrag(p.x, p.y, GetMouseMod(wParam));
			}
			return 0;
		}

		case WM_MOUSEHOVER:
		{
			pGraphics->ShowTooltip();
			return 0;
		}

		case WM_MOUSELEAVE:
		{
			pGraphics->HideTooltip();
			pGraphics->mTooltipIdx = -1;

			pGraphics->OnMouseOut();
			return 0;
		}

		case WM_LBUTTONUP:
		case WM_RBUTTONUP:
		{
			ReleaseCapture();

			POINT p;
			ScaleLParamXY(&p, lParam, pGraphics->mDPI);

			// TN: Shouldn't this set left/right mouse button flag, so you
			// can detect which button was turned off?
			pGraphics->OnMouseUp(p.x, p.y, GetMouseMod(wParam));
			return 0;
		}

		case WM_LBUTTONDBLCLK:
		case WM_RBUTTONDBLCLK:
		{
			POINT p;
			ScaleLParamXY(&p, lParam, pGraphics->mDPI);

			if (pGraphics->OnMouseDblClick(p.x, p.y, GetMouseMod(wParam)))
			{
				SetCapture(hWnd);
			}
			return 0;
		}

		case WM_MOUSEACTIVATE:
		{
			if (WantFocus(pGraphics)) SetFocus(hWnd);
			break;
		}

		case WM_MOUSEWHEEL:
		{
			const int canHandle = pGraphics->CanHandleMouseWheel();
			if (canHandle)
			{
				pGraphics->HideTooltip();

				const IMouseMod mod = GetMouseMod(wParam, canHandle >= 0);
				const IMouseMod mask(false, false, true, true, true, true);

				if (mod.Get() & mask.Get())
				{
					const float d = (float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA;
					const POINT p = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
					pGraphics->ScaleMouseWheel(hWnd, &p, mod, d);
				}
			}
			return 0;
		}

		case WM_GETDLGCODE:
		{
			return pGraphics->GetKeyboardFocus() >= 0 ? DLGC_WANTALLKEYS : 0;
		}

		case WM_KEYDOWN:
		case WM_KEYUP:
		{
			static const unsigned char vk[12] =
			{
				0, 0, 0, 0, 0xFF, 0x01, // VK_SPACE - VK_DOWN
				0xFF, 0x03,             // 0-9
				0xFE, 0xFF, 0xFF, 0x07  // A-Z
			};

			const int key = (int)wParam;
			if ((unsigned int)key < 96 && !!(vk[(unsigned int)key >> 3] & (1 << (key & 7))))
			{
				POINT p;
				GetCursorPos(&p);
				ScreenToClient(hWnd, &p);
				ScalePoint(&p, pGraphics->mDPI);
				const IMouseMod mod = GetKeyMod();
				const bool ret = msg == WM_KEYDOWN ? pGraphics->OnKeyDown(p.x, p.y, mod, key) : pGraphics->OnKeyUp(p.x, p.y, mod, key);
				if (ret) return 0;
			}

			HWND const hRoot = GetAncestor(hWnd, GA_ROOT);
			if (hRoot) SendNotifyMessageW(hRoot, msg, wParam, lParam);
			return 0;
		}

		case WM_PAINT:
		{
			RECT r;
			if (GetUpdateRect(hWnd, &r, FALSE))
			{
				IRECT ir(r.left, r.top, r.right, r.bottom);
				GetClientRect(hWnd, &r);

				const int wMul = pGraphics->Width();
				const int hMul = pGraphics->Height();
				const int wDiv = r.right - r.left;
				const int hDiv = r.bottom - r.top;

				if (wMul != wDiv || hMul != hDiv)
				{
					const int x = MulDiv(ir.L, wMul, wDiv);
					const int y = MulDiv(ir.T, hMul, hDiv);
					const int w = MulDiv(ir.R - ir.L, wMul, wDiv);
					const int h = MulDiv(ir.B - ir.T, hMul, hDiv);

					ir.L = x;
					ir.T = y;
					ir.R = x + w;
					ir.B = y + h;
				}

				pGraphics->Draw(&ir);
			}
			return 0;
		}

		case WM_CTLCOLOREDIT:
		{
			HBRUSH const brush = pGraphics->mEdBkBrush;
			if (!brush) break;

			HDC const dc = (HDC)wParam;
			SetTextColor(dc, pGraphics->mEdTextColor);
			SetBkColor(dc, pGraphics->mEdBkColor);
			return (INT_PTR)brush;
		}

		case WM_CLOSE:
		{
			pGraphics->CloseWindow();
			return 0;
		}
	}

	return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// static
LRESULT CALLBACK IGraphicsWin::ParamEditProc(HWND const hWnd, const UINT msg, const WPARAM wParam, const LPARAM lParam)
{
	IGraphicsWin* const pGraphics = (IGraphicsWin*)GetWindowLongPtrW(GetParent(hWnd), GWLP_USERDATA);
	static const UINT_PTR timerID = 1;

	if (pGraphics && pGraphics->mParamEditWnd == hWnd)
	{
		pGraphics->HideTooltip();

		switch (msg)
		{
			case WM_TIMER:
			{
				if (wParam == timerID)
				{
					pGraphics->CommitParamEdit(false);
					KillTimer(hWnd, wParam);
				}
				break;
			}

			case WM_KEYDOWN:
			{
				if (wParam == VK_RETURN)
				{
					// Deselect, because in some hosts (FL Studio, VSTHost)
					// selected text gets deleted when processing VK_RETURN.
					SendMessageW(hWnd, EM_SETSEL, -1, 0);
					pGraphics->CommitParamEdit();
					return 0;
				}

				const int delay = pGraphics->mAutoCommitDelay;
				if (delay) SetTimer(hWnd, timerID, delay, NULL);
				break;
			}

			// Handle WM_GETDLGCODE so that we can say that we want the return key message
			// (normally single line edit boxes don't get sent return key messages).
			case WM_GETDLGCODE:
			{
				if (pGraphics->mEdControl) break;
				LPARAM lres;
				// Find out if the original control wants it.
				lres = CallWindowProcW(pGraphics->mDefEditProc, hWnd, WM_GETDLGCODE, wParam, lParam);
				// Add in that we want it if it is a return keydown.
				if (lParam && ((MSG*)lParam)->message == WM_KEYDOWN && wParam == VK_RETURN)
				{
					lres |= DLGC_WANTMESSAGE;
				}
				return lres;
			}
		}

		return CallWindowProcW(pGraphics->mDefEditProc, hWnd, msg, wParam, lParam);
	}

	return DefWindowProcW(hWnd, msg, wParam, lParam);
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
	mEdBkBrush = NULL;
	mEdTextColor = 0;
	mEdBkColor = 0;
	mDefEditProc = NULL;
	mTooltipIdx = -1;
	mParamChangeTimer = 0;
	mAutoCommitDelay = 0;
	mOldKeyboardFocus = -1;
	mDPI = USER_DEFAULT_SCREEN_DPI;

	mUser32DLL = LoadLibrary("USER32.dll");
	mGetDpiForWindow = mUser32DLL ? (GDFW)GetProcAddress(mUser32DLL, "GetDpiForWindow") : NULL;

	// mPID = 0;
	// mMainWnd = NULL;

	mTooltipBuf[0] = 0;
	mForceDPI = 0;
	mCoInit = false;
}

IGraphicsWin::~IGraphicsWin()
{
	CloseWindow();
	if (mUser32DLL) FreeLibrary(mUser32DLL);
	if (mCoInit) CoUninitialize();
}

LICE_IBitmap* IGraphicsWin::OSLoadBitmap(const int ID, const char*)
{
	return LICE_LoadPNGFromResource(mHInstance, MAKEINTRESOURCE(ID));
}

bool IGraphicsWin::OSLoadFont(const int ID, const char*)
{
	HRSRC const hResource = FindResource(mHInstance, MAKEINTRESOURCE(ID), "TTF");
	if (!hResource) return false;

	const DWORD dwSize = SizeofResource(mHInstance, hResource);
	if (!dwSize) return false;

	HGLOBAL const res = LoadResource(mHInstance, hResource);
	if (!res) return false;

	void* const pResourceData = LockResource(res);
	if (!pResourceData) return false;

	DWORD dwNumFonts;
	HANDLE const hFont = AddFontMemResourceEx(pResourceData, dwSize, NULL, &dwNumFonts);

	return !!hFont;
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
	mBackBuf.resize(w, h);
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

	HDC const dcSrc = mBackBuf.getDC();
	const int scale = Scale();

	const int wSrc = Width() >> scale;
	const int hSrc = Height() >> scale;
	const int wDest = r.right - r.left;
	const int hDest = r.bottom - r.top;

	if (wDest == wSrc && hDest == hSrc)
	{
		const int x = pR->L >> scale;
		const int cx = pR->W() >> scale;
		const int y = pR->T >> scale;
		const int cy = pR->H() >> scale;

		BitBlt(dc, x, y, cx, cy, dcSrc, x, y, SRCCOPY);
	}
	else
	{
		const int mode = (wDest % wSrc) || (hDest % hSrc) ? HALFTONE : COLORONCOLOR;

		SetStretchBltMode(dc, mode);
		if (mode == HALFTONE) SetBrushOrgEx(dc, 0, 0, NULL);

		StretchBlt(dc, r.left, r.top, wDest, hDest, dcSrc, 0, 0, wSrc, hSrc, SRCCOPY);
	}

	EndPaint(hWnd, &ps);
}

void* IGraphicsWin::OpenWindow(void* const pParentWnd)
{
	int x = 0, y = 0, w = Width(), h = Height();
	mParentWnd = (HWND)pParentWnd;

	int dpi = mForceDPI;
	if (!dpi) dpi = mGetDpiForWindow ? mGetDpiForWindow(mParentWnd) : USER_DEFAULT_SCREEN_DPI;
	mDPI = dpi;

	const int scale = dpi > USER_DEFAULT_SCREEN_DPI ? kScaleFull : kScaleHalf;
	if (!PrepDraw(scale)) return NULL;

	w = MulDiv(w, dpi, IPLUG_DEFAULT_DPI);
	h = MulDiv(h, dpi, IPLUG_DEFAULT_DPI);
	GetPlug()->ResizeGraphics(w, h);

	if (mPlugWnd)
	{
		RECT pR, cR;
		GetWindowRect(mParentWnd, &pR);
		GetWindowRect(mPlugWnd, &cR);
		CloseWindow();
		x = cR.left - pR.left;
		y = cR.top - pR.top;
	}

	if (nWndClassReg++ == 0)
	{
		const WNDCLASSW wndClass = { CS_DBLCLKS, WndProc, 0, 0, mHInstance, NULL, LoadCursor(NULL, IDC_ARROW), NULL, NULL, wndClassName };
		RegisterClassW(&wndClass);
	}

	mPlugWnd = CreateWindowW(wndClassName, L"IPlug", WS_CHILD | WS_VISIBLE,
		x, y, w, h, (HWND)pParentWnd, NULL, mHInstance, this);

	if (!mPlugWnd && --nWndClassReg == 0)
	{
		UnregisterClassW(wndClassName, mHInstance);
	}
	else
	{
		SetAllControlsDirty();
	}

	if (mPlugWnd && TooltipsEnabled())
	{
		bool ok = false;
		static const INITCOMMONCONTROLSEX iccex = { sizeof(INITCOMMONCONTROLSEX), ICC_TAB_CLASSES };
		if (InitCommonControlsEx(&iccex))
		{
			mTooltipWnd = CreateWindowExW(0, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
				CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, mPlugWnd, NULL, mHInstance, NULL);
			if (mTooltipWnd)
			{
				SetWindowPos(mTooltipWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
				const TOOLINFOW ti = { TTTOOLINFOW_V2_SIZE, TTF_IDISHWND | TTF_SUBCLASS | TTF_TRANSPARENT, mPlugWnd, (UINT_PTR)mPlugWnd };
				SendMessageW(mTooltipWnd, TTM_ADDTOOLW, 0, (LPARAM)&ti);
				ok = true;
			}
		}
		if (!ok) EnableTooltips(ok);
	}

	return mPlugWnd;
}

/* static const int MAX_CLASSNAME_LEN = 128;

static void GetWndClassName(HWND const hWnd, WDL_String* const pStr)
{
	if (pStr->SetLen(MAX_CLASSNAME_LEN - 1))
	{
		GetClassName(hWnd, pStr->Get(), MAX_CLASSNAME_LEN);
	}
	else
	{
		pStr->Set("");
	}
}

BOOL CALLBACK IGraphicsWin::FindMainWindow(HWND const hWnd, const LPARAM lParam)
{
	IGraphicsWin* const pGraphics = (IGraphicsWin*)lParam;
	if (pGraphics)
	{
		DWORD wPID;
		GetWindowThreadProcessId(hWnd, &wPID);
		WDL_String str;
		GetWndClassName(hWnd, &str);
		if (wPID == pGraphics->mPID && !strcmp(str.Get(), pGraphics->mMainWndClassName.Get()))
		{
			pGraphics->mMainWnd = hWnd;
			return FALSE; // Stop enumerating.
		}
	}
	return TRUE;
}

HWND IGraphicsWin::GetMainWnd()
{
	if (!mMainWnd)
	{
		if (mParentWnd)
		{
			HWND parentWnd = mParentWnd;
			while (parentWnd)
			{
				mMainWnd = parentWnd;
				parentWnd = GetParent(mMainWnd);
			}
			GetWndClassName(mMainWnd, &mMainWndClassName);
		}
		else
		{
			const char* const cStr = mMainWndClassName.Get();
			if (cStr && *cStr)
			{
				mPID = GetCurrentProcessId();
				EnumWindows(FindMainWindow, (LPARAM)this);
			}
		}
	}
	return mMainWnd;
}

static const int TOOLWIN_BORDER_W = 6;
static const int TOOLWIN_BORDER_H = 23;

IRECT IGraphicsWin::GetWindowRECT()
{
	if (mPlugWnd)
	{
		RECT r;
		GetWindowRect(mPlugWnd, &r);
		r.right -= TOOLWIN_BORDER_W;
		r.bottom -= TOOLWIN_BORDER_H;
		return IRECT(r.left, r.top, r.right, r.bottom);
	}
	return IRECT();
} */

void IGraphicsWin::SetWindowTitle(const char* const str)
{
	static const int maxLen = 128;
	WCHAR buf[maxLen];
	if (!MultiByteToWideChar(CP_UTF8, 0, str, -1, buf, maxLen)) buf[0] = 0;
	SetWindowTextW(mPlugWnd, buf);
}

void IGraphicsWin::CloseWindow()
{
	if (mPlugWnd)
	{
		if (mParamChangeTimer)
		{
			GetPlug()->EndDelayedInformHostOfParamChange();
		}
		if (mParamEditWnd) DeleteParamEditFont(mParamEditWnd);

		DestroyWindow(mPlugWnd);
		mPlugWnd = NULL;

		if (mTooltipWnd)
		{
			mTooltipWnd = NULL;
			mTooltipIdx = -1;
			mTooltipBuf[0] = 0;
		}

		if (mParamEditWnd)
		{
			DeleteObject(mEdBkBrush);
			mEdBkBrush = NULL;
			mParamEditWnd = NULL;
			mEdParam = NULL;
			mEdControl = NULL;
			mDefEditProc = NULL;
		}

		if (--nWndClassReg == 0)
		{
			UnregisterClassW(wndClassName, mHInstance);
		}
	}
}

static const int PARAM_EDIT_W = 40 * 2;
static const int PARAM_EDIT_H = 16 * 2;

bool IGraphicsWin::PromptUserInput(IControl* const pControl, IParam* const pParam, const IRECT* pR, const int flags, IText* const pTxt, const IColor bg, const int delay, const int x, const int y)
{
	if (mParamEditWnd || !pControl) return false;

	char currentText[kMaxEditLen];
	if (pParam)
	{
		pParam->GetDisplayForHost(currentText, sizeof(currentText));
	}
	else
	{
		pControl->GetTextForUserInput(currentText, sizeof(currentText));
	}

	WCHAR buf[kMaxEditLen];
	if (!MultiByteToWideChar(CP_UTF8, 0, currentText, -1, buf, kMaxEditLen))
	{
		buf[0] = 0;
	}

	static const int kPromptCustomHeight = kPromptCustomRect ^ kPromptCustomWidth;
	static const int w = PARAM_EDIT_W, h = PARAM_EDIT_H;

	if (!pR) pR = pControl->GetTargetRECT();

	int r[7];
	if (!(flags & kPromptCustomWidth))
	{
		r[0] = (pR->L + pR->R - w) / 2;
		r[2] = w;
	}
	else
	{
		r[0] = pR->L;
		r[2] = pR->W();
	}

	if (!(flags & kPromptCustomHeight))
	{
		r[1] = (pR->T + pR->B - h) / 2;
		r[3] = h;
	}
	else
	{
		r[1] = pR->T;
		r[3] = pR->H();
	}

	static const IText kDefaultFont(0, IColor(0));
	const IText* const pFont = pTxt ? pTxt : &kDefaultFont;

	const int fontSize = pFont->mSize;
	r[4] = fontSize ? fontSize : (h * 14) / 16;

	r[5] = x;
	r[6] = y;

	if (mDPI != IPLUG_DEFAULT_DPI)
	{
		const int n = (flags & kPromptMouseClick) ? 7 : 5;
		for (int i = 0; i < n; ++i)
		{
			r[i] = MulDiv(r[i], mDPI, IPLUG_DEFAULT_DPI);
		}
	}

	if (!fontSize && mDPI >= 100)
	{
		if (mDPI < 192)
		{
			static const char tbl[92] =
			{
				14, 14, 14, 14, 14, 15, 15, 15, 15, 15,
				16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
				16, 16, 16, 16, 16, 16, 17, 17, 17, 17,
				17, 17, 17, 17, 17, 17, 18, 18, 18, 18,
				18, 18, 18, 18, 18, 18, 20, 20, 20, 20,
				20, 20, 20, 20, 20, 20, 20, 20, 21, 21,
				21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
				21, 21, 22, 22, 22, 22, 22, 23, 23, 23,
				23, 23, 23, 23, 23, 23, 23, 23, 23, 23,
				23, 24
			};

			r[4] = tbl[mDPI - 100];
		}
		else
		{
			// r[4] = (h * 12 * mDPI) / (16 * 192)
			assert((h * 12) << 3 == 16 * 192);
			r[4] = mDPI >> 3;
		}
	}

	const IColor fg = pFont->mColor;
	if (!fg.Empty() || !bg.Empty())
	{
		mEdTextColor = fg.Empty() ? GetSysColor(COLOR_WINDOWTEXT) : RGB(fg.R, fg.G, fg.B);
		mEdBkColor = bg.Empty() ? GetSysColor(COLOR_WINDOW) : RGB(bg.R, bg.G, bg.B);
		mEdBkBrush = CreateSolidBrush(mEdBkColor);
	}

	mAutoCommitDelay = delay;

	mOldKeyboardFocus = GetKeyboardFocus();
	if (mOldKeyboardFocus >= 0) SetKeyboardFocus(-1);

	static const DWORD align[3] = { ES_LEFT, ES_CENTER, ES_RIGHT };
	assert(pFont->mAlign >= 0 && pFont->mAlign < 3);

	mParamEditWnd = CreateWindowW(L"EDIT", buf,
		align[pFont->mAlign] | WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOHSCROLL,
		r[0], r[1], r[2], r[3], mPlugWnd, (HMENU)(INT_PTR)PARAM_EDIT_ID, mHInstance, NULL);

	SetFocus(mParamEditWnd);

	mDefEditProc = (WNDPROC)SetWindowLongPtrW(mParamEditWnd, GWLP_WNDPROC, (LONG_PTR)ParamEditProc);
	SetWindowLongPtrW(mParamEditWnd, GWLP_USERDATA, 0xdeadf00b);

	HFONT const font = CreateFont(r[4], 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE,
		ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, pFont->mFont);
	SendMessageW(mParamEditWnd, WM_SETFONT, (WPARAM)font, FALSE);

	if (flags & kPromptInline)
	{
		SendMessageW(mParamEditWnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));
	}

	if (flags & kPromptMouseClick)
	{
		ReleaseCapture();
		SendMessageW(mParamEditWnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(r[5] - r[0], r[6] - r[1]));
	}
	else
	{
		SendMessageW(mParamEditWnd, EM_SETSEL, 0, -1);
	}

	mEdControl = pControl;
	mEdParam = pParam;

	return true;
}

void IGraphicsWin::CommitParamEdit(const bool close)
{
	WCHAR buf[kMaxEditLen];
	SendMessageW(mParamEditWnd, WM_GETTEXT, kMaxEditLen, (LPARAM)buf);

	char txt[kMaxEditLen];
	if (WideCharToMultiByte(CP_UTF8, 0, buf, -1, txt, sizeof(txt), NULL, NULL))
	{
		SetFromStringAfterPrompt(mEdControl, mEdParam, txt);
	}

	if (close) CancelParamEdit();
}

void IGraphicsWin::CancelParamEdit()
{
	DeleteParamEditFont(mParamEditWnd);
	SetWindowLongPtrW(mParamEditWnd, GWLP_WNDPROC, (LPARAM)mDefEditProc);
	DestroyWindow(mParamEditWnd);
	DeleteObject(mEdBkBrush);
	SetKeyboardFocus(mOldKeyboardFocus);

	mEdBkBrush = NULL;
	mParamEditWnd = NULL;
	mEdParam = NULL;
	mEdControl = NULL;
	mDefEditProc = NULL;
	mOldKeyboardFocus = -1;
}

static bool GetModulePath(HMODULE const hModule, WDL_String* const pPath)
{
	WCHAR pathStr[MAX_PATH];
	int s = GetModuleFileNameW(hModule, pathStr, MAX_PATH - 1);

	// Windows XP: String is truncated to nSize chars and is not
	// null-terminated.
	// pathStr[MAX_PATH - 1] = 0;

	while (--s >= 0 && pathStr[s] != '\\');
	pathStr[s + 1] = 0;

	if (!(pPath->SetLen(MAX_PATH - 1) &&
		WideCharToMultiByte(CP_UTF8, 0, pathStr, -1, pPath->Get(), MAX_PATH, NULL, NULL)))
	{
		pPath->Set("");
		return false;
	}

	return true;

}

bool IGraphicsWin::HostPath(WDL_String* const pPath)
{
	return GetModulePath(NULL, pPath);
}

bool IGraphicsWin::PluginPath(WDL_String* const pPath)
{
	return GetModulePath(mHInstance, pPath);
}

bool IGraphicsWin::UserDataPath(WDL_String* const pPath)
{
	WCHAR pathStr[MAX_PATH];

	if (!(SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, pathStr)) &&
		pPath->SetLen(MAX_PATH - 1) &&
		WideCharToMultiByte(CP_UTF8, 0, pathStr, -1, pPath->Get(), MAX_PATH, NULL, NULL)))
	{
		pPath->Set("");
		return false;
	}

	return true;
}

bool IGraphicsWin::PromptForFile(WDL_String* const pFilename, const int action, const char* dir, const char* const extensions)
{
	if (!WindowIsOpen())
	{
		pFilename->Set("");
		return false;
	}

	WCHAR fnStr[MAX_PATH], dirStr[MAX_PATH];
	const char* const fn = pFilename->Get();
	if (!(*fn && MultiByteToWideChar(CP_UTF8, 0, fn, -1, fnStr, MAX_PATH)))
	{
		fnStr[0] = 0;
	}
	if (dir && !(*dir && MultiByteToWideChar(CP_UTF8, 0, dir, -1, dirStr, MAX_PATH)))
	{
		dir = NULL;
	}

	OPENFILENAMEW ofn;
	memset(&ofn, 0, sizeof(OPENFILENAMEW));

	ofn.lStructSize = sizeof(OPENFILENAMEW);
	ofn.hwndOwner = mPlugWnd;
	ofn.lpstrFile = fnStr;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrInitialDir = dir ? dirStr : NULL;
	ofn.Flags = OFN_PATHMUSTEXIST;

	WCHAR extStr[256];
	WCHAR defExtStr[16];

	if (extensions && *extensions)
	{
		// Extensions should be ANSI.
		assert(WDL_DetectUTF8(extensions) <= 0);

		int p;

		bool seperator = true;
		for (int i = p = 0; extensions[i]; ++i)
		{
			if (seperator)
			{
				if (p) extStr[p++] = ';';
				seperator = false;
				extStr[p++] = '*';
				extStr[p++] = '.';
			}
			if (extensions[i] == ' ')
			{
				seperator = true;
			}
			else
			{
				extStr[p++] = extensions[i];
			}
		}
		extStr[p++] = 0;

		wcscpy(&extStr[p], extStr);
		extStr[p + p] = 0;
		ofn.lpstrFilter = extStr;

		for (int i = p = 0; extensions[i] && extensions[i] != ' '; ++i)
		{
			defExtStr[p++] = extensions[i];
		}
		defExtStr[p++] = 0;
		ofn.lpstrDefExt = defExtStr;
	}

	bool rc = false;
	switch (action)
	{
		case kFileSave:
			ofn.Flags |= OFN_OVERWRITEPROMPT;
			rc = GetSaveFileNameW(&ofn);
			break;

		case kFileOpen:
			ofn.Flags |= OFN_FILEMUSTEXIST;
			rc = GetOpenFileNameW(&ofn);
			break;
	}

	if (rc && pFilename->SetLen(MAX_PATH - 1))
	{
		rc = !!WideCharToMultiByte(CP_UTF8, 0, ofn.lpstrFile, -1, pFilename->Get(), MAX_PATH, NULL, NULL);
	}

	if (!rc) pFilename->Set("");
	return rc;
}

/* static UINT_PTR CALLBACK CCHookProc(HWND const hdlg, const UINT uiMsg, const WPARAM wParam, const LPARAM lParam)
{
	if (uiMsg == WM_INITDIALOG && lParam)
	{
		const CHOOSECOLOR* const cc = (const CHOOSECOLOR*)lParam;
		if (cc && cc->lCustData)
		{
			const char* const str = (const char*)cc->lCustData;
			SetWindowText(hdlg, str);
		}
	}
	return 0;
}

static COLORREF sCustomColorStorage[16] = { 0 };

bool IGraphicsWin::PromptForColor(IColor* const pColor, const char* const prompt)
{
	if (!mPlugWnd) return false;

	CHOOSECOLOR cc;
	memset(&cc, 0, sizeof(CHOOSECOLOR));
	cc.lStructSize = sizeof(CHOOSECOLOR);
	cc.hwndOwner = mPlugWnd;
	cc.rgbResult = RGB(pColor->R, pColor->G, pColor->B);
	cc.lpCustColors = sCustomColorStorage;
	cc.lCustData = (LPARAM)prompt;
	cc.lpfnHook = CCHookProc;
	cc.Flags = CC_RGBINIT | CC_ANYCOLOR | CC_FULLOPEN | CC_SOLIDCOLOR | CC_ENABLEHOOK;

	if (ChooseColor(&cc))
	{
		pColor->R = GetRValue(cc.rgbResult);
		pColor->G = GetGValue(cc.rgbResult);
		pColor->B = GetBValue(cc.rgbResult);
		return true;
	}
	return false;
} */

static const int MAX_INET_ERR_CODE = 32;

bool IGraphicsWin::OpenURL(const char* const url, const char* const windowTitle,
	const char* const confirmMsg, const char* const errMsg)
{
	if (confirmMsg && MessageBox(mPlugWnd, confirmMsg, windowTitle, MB_YESNO) != IDYES)
	{
		return false;
	}
	DWORD inetStatus = 0;
	if (InternetGetConnectedState(&inetStatus, 0))
	{
		if (!mCoInit)
		{
			const HRESULT hres = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
			mCoInit = SUCCEEDED(hres);
		}
		if (mCoInit && (int)(INT_PTR)ShellExecute(mPlugWnd, "open", url, 0, 0, SW_SHOWNORMAL) > MAX_INET_ERR_CODE)
		{
			return true;
		}
	}
	if (errMsg)
	{
		MessageBox(mPlugWnd, errMsg, windowTitle, MB_OK);
	}
	return false;
}

int IGraphicsWin::ProcessMouseWheel(const float delta)
{
	const int canHandle = CanHandleMouseWheel();
	if (canHandle)
	{
		POINT p;
		GetCursorPos(&p);
		HWND const hWnd = WindowFromPoint(p);
		if (hWnd == mPlugWnd)
		{
			HideTooltip();

			const IMouseMod mod = GetKeyMod(canHandle >= 0);
			const IMouseMod mask(false, false, true, true, true, true);

			if (mod.Get() & mask.Get())
			{
				ScaleMouseWheel(hWnd, &p, mod, delta);
				return 1;
			}
		}
	}
	return 0;
}

void IGraphicsWin::ScaleMouseWheel(HWND const hWnd, const POINT* const pPoint, const IMouseMod mod, const float delta)
{
	if (mParamEditWnd) return;

	RECT r;
	GetWindowRect(hWnd, &r);

	r.left = pPoint->x - r.left;
	r.top = pPoint->y - r.top;
	ScalePoint((LPPOINT)&r, mDPI);

	OnMouseWheel(r.left, r.top, mod, delta);
}

int IGraphicsWin::ProcessKey(const bool state, const IMouseMod mod, const int key)
{
	POINT p;
	GetCursorPos(&p);
	ScreenToClient(mPlugWnd, &p);
	ScalePoint(&p, mDPI);
	return state ? OnKeyDown(p.x, p.y, mod, key) : OnKeyUp(p.x, p.y, mod, key);
}

void IGraphicsWin::SetKeyboardFocus(const int controlIdx)
{
	IGraphics::SetKeyboardFocus(controlIdx);
	if (controlIdx >= 0 && mPlugWnd) SetFocus(mPlugWnd);
}

const char* IGraphicsWin::GetTooltip(const int controlIdx)
{
	return controlIdx >= 0 ? GetControl(controlIdx)->GetTooltip() : NULL;
}

void IGraphicsWin::SetTooltip(const char* const tooltip)
{
	if (!MultiByteToWideChar(CP_UTF8, 0, tooltip, -1, mTooltipBuf, kMaxTooltipLen))
	{
		mTooltipBuf[0] = 0;
	}
}

void IGraphicsWin::ShowTooltip()
{
	TOOLINFOW ti = { TTTOOLINFOW_V2_SIZE, TTF_IDISHWND, mPlugWnd, (UINT_PTR)mPlugWnd };
	ti.lpszText = mTooltipBuf;
	SendMessageW(mTooltipWnd, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
}

void IGraphicsWin::HideTooltip()
{
	if (mTooltipBuf[0])
	{
		mTooltipBuf[0] = 0;
		ShowTooltip();
	}
}

#ifndef NDEBUG
void IPlugDebugLog(const char* const str)
{
	OutputDebugString(str);
}
#endif
