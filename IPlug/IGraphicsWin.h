#pragma once

#include "IControl.h"
#include "IGraphics.h"

#include <windows.h>

#define IPLUG_RESOURCE(xxx) (xxx##_ID), NULL
#define IPLUG_PNG_RESOURCE(id, name) (id), NULL

class IGraphicsWin: public IGraphics
{
public:
	IGraphicsWin(IPlugBase* pPlug, int w, int h, int refreshFPS = 0);
	~IGraphicsWin();

	inline void SetHInstance(HINSTANCE const hInstance) { mHInstance = hInstance; }

	// void Resize(int w, int h);
	void DrawScreen(const IRECT* pR);

	void* OpenWindow(void* pParentWnd);
	void CloseWindow();
	bool WindowIsOpen() const { return !!mPlugWnd; }

	void UpdateTooltips() { if (!TooltipsEnabled()) HideTooltip(); }

	bool HostPath(WDL_String* pPath);
	bool PluginPath(WDL_String* pPath);
	bool UserDataPath(WDL_String* pPath);

	bool PromptForFile(WDL_String* pFilename, int action = kFileOpen, const char* dir = NULL, const char* extensions = NULL);
	// bool PromptForColor(IColor* pColor, const char* prompt = NULL);
	bool PromptUserInput(IControl* pControl, IParam* pParam, const IRECT* pR = NULL, int flags = 0, IText* pTxt = NULL, IColor bg = IColor(0), int delay = 0, int x = 0, int y = 0);

	bool OpenURL(const char* url, const char* windowTitle = NULL, const char* confirmMsg = NULL, const char* errMsg = NULL);

	// Return 1 if key/mouse wheel is processed.
	int ProcessMouseWheel(float delta);
	int ProcessKey(bool state, IMouseMod mod, int key);

	void SetKeyboardFocus(int controlIdx);

	void SetParamChangeTimer(const int ticks) { mParamChangeTimer = ticks; }
	void CancelParamChangeTimer() { mParamChangeTimer = 0; }

    // Specialty use!
	void* GetWindow() const { return mPlugWnd; }
	inline HWND GetParentWindow() const { return mParentWnd; }
	// HWND GetMainWnd();
	// void SetMainWndClassName(const char* const name) { mMainWndClassName.Set(name); }
	// void GetMainWndClassName(char* const name) { strcpy(name, mMainWndClassName.Get()); }
	// IRECT GetWindowRECT();
	void SetWindowTitle(const char* str);

	// Defaults to 0, which means auto detect via GetDpiForWindow().
	inline void ForceDPI(const int dpi) { mForceDPI = dpi; }

protected:
	LICE_IBitmap* OSLoadBitmap(int ID, const char* name);
	bool OSLoadFont(int ID, const char* name);

	void ScaleMouseWheel(HWND hWnd, const POINT* pPoint, IMouseMod mod, float delta);

	const char* GetTooltip(int controlIdx);
	void SetTooltip(const char* tooltip);
	void ShowTooltip();
	void HideTooltip();

	void CommitParamEdit(bool close = true);
	void CancelParamEdit();

private:
	HINSTANCE mHInstance;
	HWND mParentWnd, mPlugWnd, mTooltipWnd, mParamEditWnd;
	// Ed = being edited manually.
	IControl* mEdControl;
	IParam* mEdParam;
	HBRUSH mEdBkBrush;
	COLORREF mEdTextColor, mEdBkColor;
	WNDPROC mDefEditProc;
	int mTooltipIdx;
	int mParamChangeTimer;
	int mAutoCommitDelay;
	int mOldKeyboardFocus;
	int mDPI;

	HMODULE mUser32DLL;
	typedef UINT (WINAPI *GDFW)(HWND);
	GDFW mGetDpiForWindow;

	// DWORD mPID;
	// HWND mMainWnd;
	// WDL_String mMainWndClassName;

	static const int kMaxTooltipLen = 80;
	WCHAR mTooltipBuf[kMaxTooltipLen];

	int mForceDPI;
	bool mCoInit;

public:
	static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK ParamEditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	// static BOOL CALLBACK FindMainWindow(HWND hWnd, LPARAM lParam);
};
