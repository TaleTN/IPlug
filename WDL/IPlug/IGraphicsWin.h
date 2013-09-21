#ifndef _IGRAPHICSWIN_
#define _IGRAPHICSWIN_

#include "IControl.h"
#include "IGraphics.h"

#include <windows.h>
#include <windowsx.h>
#include <winuser.h>

#ifndef IPLUG_NO_JPEG_SUPPORT
	#define IPLUG_PNG_RESOURCE(id, name) (id), ".png"
	#define IPLUG_JPEG_RESOURCE(id, name) (id), ".jpg"
#else
	#define IPLUG_PNG_RESOURCE(id, name) (id), NULL
#endif

class IGraphicsWin : public IGraphics
{
public:

	IGraphicsWin(IPlugBase* pPlug, int w, int h, int refreshFPS = 0);
	virtual ~IGraphicsWin();

  void SetHInstance(HINSTANCE hInstance) { mHInstance = hInstance; }
  
  void Resize(int w, int h);
  bool DrawScreen(IRECT* pR);  
  
	void* OpenWindow(void* pParentWnd);
	void CloseWindow();
	bool WindowIsOpen() { return (mPlugWnd); }

	inline void UpdateTooltips() { if (!TooltipsEnabled()) HideTooltip(); }

	void HostPath(WDL_String* pPath); 
  void PluginPath(WDL_String* pPath);

	void PromptForFile(WDL_String* pFilename, EFileAction action = kFileOpen, char* dir = "",
    char* extensions = "");   // extensions = "txt wav" for example.

  bool PromptForColor(IColor* pColor, char* prompt = "");
	void PromptUserInput(IControl* pControl, IParam* pParam);
	void PromptUserInput(IEditableTextControl* pControl);

  bool OpenURL(const char* url, 
    const char* msgWindowTitle = 0, const char* confirmMsg = 0, const char* errMsgOnFailure = 0);

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
	COLORREF* mCustomColorStorage;

  DWORD mPID;
  HWND mParentWnd, mMainWnd;
  WDL_String mMainWndClassName;

public:

	static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK ParamEditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
  static BOOL CALLBACK FindMainWindow(HWND hWnd, LPARAM lParam);
};

////////////////////////////////////////

#endif