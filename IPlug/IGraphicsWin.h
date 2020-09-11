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

	void HostPath(WDL_String* pPath);
	void PluginPath(WDL_String* pPath);

	bool PromptForFile(WDL_String* pFilename, int action = kFileOpen, const char* dir = NULL, const char* extensions = NULL);
	// bool PromptForColor(IColor* pColor, const char* prompt = NULL);
	void PromptUserInput(IControl* pControl, IParam* pParam, const IRECT* pR = NULL, int fontSize = 0);

	bool OpenURL(const char* url, const char* windowTitle = NULL, const char* confirmMsg = NULL, const char* errMsg = NULL);

  // Return 1 if mouse wheel is processed
  int ProcessMouseWheel(float delta);

  void SetParamChangeTimer(int ticks) { mParamChangeTimer = ticks; }
  void CancelParamChangeTimer() { mParamChangeTimer = 0; }

    // Specialty use!
	void* GetWindow() { return mPlugWnd; }
  HWND GetParentWindow() { return mParentWnd; }
  HWND GetMainWnd();
  void SetMainWndClassName(char* name) { mMainWndClassName.Set(name); }
  void GetMainWndClassName(char* name) { strcpy(name, mMainWndClassName.Get()); }
  IRECT GetWindowRECT();
  void SetWindowTitle(char* str);

protected:
  LICE_IBitmap* OSLoadBitmap(int ID, const char* name);

  void SetTooltip(const char* tooltip);
  void ShowTooltip();
  void HideTooltip();

private:
	HINSTANCE mHInstance;
  HWND mPlugWnd, mTooltipWnd, mParamEditWnd;
	// Ed = being edited manually.
	IControl* mEdControl;
	IParam* mEdParam;
	WNDPROC mDefEditProc;
	int mParamEditMsg;
	bool mShowingTooltip;
	int mTooltipIdx;
	int mParamChangeTimer;
	int mDPI;

	HMODULE mUser32DLL;
	typedef UINT (WINAPI *GDFW)(HWND);
	GDFW mGetDpiForWindow;

  DWORD mPID;
  HWND mParentWnd, mMainWnd;
  WDL_String mMainWndClassName;

	bool mCoInit;

public:

	static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK ParamEditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
  static BOOL CALLBACK FindMainWindow(HWND hWnd, LPARAM lParam);
};
