#pragma once

#include "IControl.h"
#include "IGraphics.h"

#define IPLUG_RESOURCE(xxx) (xxx##_ID), (xxx##_FN)
#define IPLUG_PNG_RESOURCE(id, name) (id), (name)

#ifndef IPLUG_NO_CARBON_SUPPORT
	class IGraphicsCarbon;
#endif

class IGraphicsMac: public IGraphics
{
public:
	static const int kScaleOS = kScaleHalf;

	IGraphicsMac(IPlugBase* pPlug, int w, int h, int refreshFPS = 0);
	~IGraphicsMac();

	void SetBundleID(const char* const bundleID) { mBundleID.Set(bundleID); }

	void DrawScreen(const IRECT* pR);

	bool InitScale();
	bool UpdateScale();

	inline bool ScaleNeedsUpdate() const { return mWantScale != mPrevScale; }

	void* OpenWindow(void* const pWindow) { return OpenCocoaWindow(pWindow); }
	void* OpenCocoaWindow(void* pParentView);

	#ifndef IPLUG_NO_CARBON_SUPPORT
	void* OpenCarbonWindow(void* pParentWnd, void* pParentControl = NULL);
	#endif
  
	void CloseWindow();
	bool WindowIsOpen() const;
	// void Resize(int w, int h);

  void UpdateTooltips();

	void HostPath(WDL_String* pPath); 
  void PluginPath(WDL_String* pPath);

	void PromptForFile(WDL_String* pFilename, EFileAction action = kFileOpen, char* dir = "",
    char* extensions = "");   // extensions = "txt wav" for example.
  bool PromptForColor(IColor* pColor, char* prompt = "");
	void PromptUserInput(IControl* pControl, IParam* pParam);
	void PromptUserInput(IEditableTextControl* pControl);
  bool OpenURL(const char* url, const char* msgWindowTitle = 0, const char* confirmMsg = 0, const char* errMsgOnFailure = 0);

  void SetParamChangeTimer(int ticks);
  void CancelParamChangeTimer();

	void* GetWindow();

	const char* GetBundleID() const { return mBundleID.Get(); }
  static int GetUserOSVersion();   // Returns a number like 0x1050 (10.5).
  static double GetUserFoundationVersion();   // Returns a number like 677.00 (10.5).
  
protected:
  
  virtual LICE_IBitmap* OSLoadBitmap(int ID, const char* name);
  
private:
	#ifndef IPLUG_NO_CARBON_SUPPORT
	IGraphicsCarbon* mGraphicsCarbon;
	#endif

	void* mGraphicsCocoa; // Can't forward-declare IGraphicsCocoa because it's an obj-C object.

	WDL_FastString mBundleID;
	int mWantScale, mPrevScale;
};

inline int AdjustFontSize(int size)
{
	return int(0.75 * (double)size);
}
