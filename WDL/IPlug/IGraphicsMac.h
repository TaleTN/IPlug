#ifndef _IGRAPHICSMAC_
#define _IGRAPHICSMAC_

#include "IControl.h"
#include "IGraphics.h"
#include "../swell/swell.h"
#include <Carbon/Carbon.h>

#define IPLUG_PNG_RESOURCE(id, name) (id), (name)

#ifndef IPLUG_NO_JPEG_SUPPORT
#define IPLUG_JPEG_RESOURCE(id, name) (id), (name)
#endif

#ifndef IPLUG_NO_CARBON_SUPPORT
	class IGraphicsCarbon;
#endif

class IGraphicsMac : public IGraphics
{
public:

	IGraphicsMac(IPlugBase* pPlug, int w, int h, int refreshFPS = 0);
	virtual ~IGraphicsMac();

  void SetBundleID(const char* bundleID) { mBundleID.Set(bundleID); }
  
  bool DrawScreen(IRECT* pR);

  void* OpenWindow(void* pWindow);
#ifndef IPLUG_NO_CARBON_SUPPORT
  void* OpenWindow(void* pWindow, void* pControl);
#endif
  
	void* OpenCocoaWindow(void* pParentView);  
#ifndef IPLUG_NO_CARBON_SUPPORT
  void* OpenCarbonWindow(void* pParentWnd, void* pParentControl);
#endif
  
	void CloseWindow();
	bool WindowIsOpen();
  void Resize(int w, int h);
  
  void UpdateTooltips();

	void HostPath(WDL_String* pPath); 
  void PluginPath(WDL_String* pPath);

	void PromptForFile(WDL_String* pFilename, EFileAction action = kFileOpen, char* dir = "",
    char* extensions = "");   // extensions = "txt wav" for example.
  bool PromptForColor(IColor* pColor, char* prompt = "");
	void PromptUserInput(IControl* pControl, IParam* pParam);
	void PromptUserInput(IEditableTextControl* pControl);
  bool OpenURL(const char* url, const char* msgWindowTitle = 0, const char* confirmMsg = 0, const char* errMsgOnFailure = 0);

	void* GetWindow();

	int mIdleTicks;

  const char* GetBundleID()  { return mBundleID.Get(); }
  static int GetUserOSVersion();   // Returns a number like 0x1050 (10.5).
  static double GetUserFoundationVersion();   // Returns a number like 677.00 (10.5).
  
protected:
  
  virtual LICE_IBitmap* OSLoadBitmap(int ID, const char* name);
  
private:
  
#ifndef IPLUG_NO_CARBON_SUPPORT
  IGraphicsCarbon* mGraphicsCarbon; 
#endif
  void* mGraphicsCocoa;   // Can't forward-declare IGraphicsCocoa because it's an obj-C object.
  
  WDL_String mBundleID;

  friend class IGraphicsCarbon;
  friend int GetMouseOver(IGraphicsMac* pGraphics);
};

inline CFStringRef MakeCFString(const char* cStr)
{
  return CFStringCreateWithCString(0, cStr, kCFStringEncodingUTF8); 
}

struct CFStrLocal 
{
  CFStringRef mCFStr;
  CFStrLocal(const char* cStr) 
  {
    mCFStr = MakeCFString(cStr); 
  }
  ~CFStrLocal() 
  {
    CFRelease(mCFStr); 
  }
};

struct CStrLocal
{
  char* mCStr;
  CStrLocal(CFStringRef cfStr) 
  {
    int n = CFStringGetLength(cfStr) + 1;
    mCStr = (char*) malloc(n);
    CFStringGetCString(cfStr, mCStr, n, kCFStringEncodingUTF8);
  }
  ~CStrLocal() 
  {
    FREE_NULL(mCStr); 
  }
};

inline int AdjustFontSize(int size)
{
	return int(0.75 * (double)size);
}

#endif