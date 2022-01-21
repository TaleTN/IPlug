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

	bool HostPath(WDL_String* pPath);
	bool PluginPath(WDL_String* pPath);
	bool UserDataPath(WDL_String* pPath);

	bool PromptForFile(WDL_String* pFilename, int action = kFileOpen, const char* dir = NULL, const char* extensions = NULL);
	// bool PromptForColor(IColor* pColor, const char* prompt = NULL) { return false; }
	bool PromptUserInput(IControl* pControl, IParam* pParam, const IRECT* pR = NULL, int flags = 0, IText* pTxt = NULL, IColor bg = IColor(0), int delay = 0, int x = 0, int y = 0);

	bool OpenURL(const char* url, const char* windowTitle = NULL, const char* confirmMsg = NULL, const char* errMsg = NULL);

	void SetParamChangeTimer(int ticks);
	void CancelParamChangeTimer();

	void* GetWindow() const;

	const char* GetBundleID() const { return mBundleID.Get(); }
	static int GetUserOSVersion(); // Returns a number like 0x1050 (10.5).
	static double GetUserFoundationVersion(); // Returns a number like 677.00 (10.5).

	static const int kAudioUnitProperty_PlugInObject = 0x1a45ffe9;

protected:
	LICE_IBitmap* OSLoadBitmap(int ID, const char* name);
	bool OSLoadFont(int ID, const char* name);

private:
	#ifndef IPLUG_NO_CARBON_SUPPORT
	IGraphicsCarbon* mGraphicsCarbon;
	#endif

	void* mGraphicsCocoa; // Can't forward-declare IGraphicsCocoa because it's an obj-C object.

	WDL_FastString mBundleID;
	int mWantScale, mPrevScale;
};
