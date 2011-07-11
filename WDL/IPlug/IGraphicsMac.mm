#include <Foundation/NSArchiver.h>
#include "IGraphicsMac.h"
#include "IControl.h"
#include "Log.h"
#import "IGraphicsCocoa.h"
#ifndef IPLUG_NO_CARBON_SUPPORT
	#include "IGraphicsCarbon.h"
#endif
#include "../swell/swell-internal.h"

struct CocoaAutoReleasePool
{
  NSAutoreleasePool* mPool;
    
  CocoaAutoReleasePool() 
  {
    mPool = [[NSAutoreleasePool alloc] init];
  }
    
  ~CocoaAutoReleasePool()
  {
    [mPool release];
  }
};

inline NSColor* ToNSColor(IColor* pColor)
{
  double r = (double) pColor->R / 255.0;
  double g = (double) pColor->G / 255.0;
  double b = (double) pColor->B / 255.0;
  double a = (double) pColor->A / 255.0;
  return [NSColor colorWithCalibratedRed:r green:g blue:b alpha:a];
}

IGraphicsMac::IGraphicsMac(IPlugBase* pPlug, int w, int h, int refreshFPS)
:	IGraphics(pPlug, w, h, refreshFPS),
#ifndef IPLUG_NO_CARBON_SUPPORT
	mGraphicsCarbon(0),
#endif
	mGraphicsCocoa(0)
{
  NSApplicationLoad();
  
}

IGraphicsMac::~IGraphicsMac()
{
	CloseWindow();
}

LICE_IBitmap* LoadImgFromResourceOSX(const char* bundleID, const char* filename)
{ 
  if (!filename) return 0;
  CocoaAutoReleasePool pool;
  
  const char* ext = filename+strlen(filename)-1;
  while (ext >= filename && *ext != '.') --ext;
  ++ext;
  
  bool ispng = !stricmp(ext, "png");
#ifdef IPLUG_NO_JPEG_SUPPORT
  if (!ispng) return 0;
#else
  bool isjpg = !stricmp(ext, "jpg");
  if (!isjpg && !ispng) return 0;
#endif
  
  NSBundle* pBundle = [NSBundle bundleWithIdentifier:ToNSString(bundleID)];
  NSString* pFile = [[[NSString stringWithCString:filename] lastPathComponent] stringByDeletingPathExtension];
  if (pBundle && pFile) 
  {
    NSString* pPath = 0;
    if (ispng) pPath = [pBundle pathForResource:pFile ofType:@"png"];  
#ifndef IPLUG_NO_JPEG_SUPPORT
    if (isjpg) pPath = [pBundle pathForResource:pFile ofType:@"jpg"];  
#endif

    if (pPath) 
    {
      const char* resourceFileName = [pPath cString];
      if (CSTR_NOT_EMPTY(resourceFileName))
      {
        if (ispng) return LICE_LoadPNG(resourceFileName);
#ifndef IPLUG_NO_JPEG_SUPPORT
        if (isjpg) return LICE_LoadJPG(resourceFileName);
#endif
      }
    }
  }
  return 0;
}

LICE_IBitmap* IGraphicsMac::OSLoadBitmap(int ID, const char* name)
{
  return LoadImgFromResourceOSX(GetBundleID(), name);
}

bool IGraphicsMac::DrawScreen(IRECT* pR)
{
  CGContextRef pCGC = 0;
  CGRect r = CGRectMake(0, 0, Width(), Height());
  if (mGraphicsCocoa) {
    pCGC = (CGContextRef) [[NSGraphicsContext currentContext] graphicsPort];  // Leak?
    NSGraphicsContext* gc = [NSGraphicsContext graphicsContextWithGraphicsPort: pCGC flipped: YES];
    pCGC = (CGContextRef) [gc graphicsPort];    
  }
#ifndef IPLUG_NO_CARBON_SUPPORT
  else
  if (mGraphicsCarbon) {
    pCGC = mGraphicsCarbon->GetCGContext();
    mGraphicsCarbon->OffsetContentRect(&r);
    // Flipping is handled in IGraphicsCarbon.
  }
#endif
  if (!pCGC) {
    return false;
  }
  
  HDC__ * srcCtx = (HDC__*) mDrawBitmap->getDC();
  CGImageRef img = CGBitmapContextCreateImage(srcCtx->ctx); 
  CGContextDrawImage(pCGC, r, img);
  CGImageRelease(img);
  return true;
}

void* IGraphicsMac::OpenWindow(void* pParent)
{ 
  return OpenCocoaWindow(pParent);
}

#ifndef IPLUG_NO_CARBON_SUPPORT
void* IGraphicsMac::OpenWindow(void* pWindow, void* pControl)
{
  return OpenCarbonWindow(pWindow, pControl);
}
#endif

void* IGraphicsMac::OpenCocoaWindow(void* pParentView)
{
  TRACE;
  CloseWindow();
  mGraphicsCocoa = (IGRAPHICS_COCOA*) [[IGRAPHICS_COCOA alloc] initWithIGraphics: this];
  if (pParentView) {    // Cocoa VST host.
    [(NSView*) pParentView addSubview: (IGRAPHICS_COCOA*) mGraphicsCocoa];
  }
  // Else we are being called by IGraphicsCocoaFactory, which is being called by a Cocoa AU host, 
  // and the host will take care of attaching the view to the window. 
  return mGraphicsCocoa;
}

#ifndef IPLUG_NO_CARBON_SUPPORT
void* IGraphicsMac::OpenCarbonWindow(void* pParentWnd, void* pParentControl)
{
  TRACE;
  CloseWindow();
  WindowRef pWnd = (WindowRef) pParentWnd;
  ControlRef pControl = (ControlRef) pParentControl;
  // On 10.5 or later we could have used HICocoaViewCreate, but for 10.4 we have to support Carbon explicitly.
  mGraphicsCarbon = new IGraphicsCarbon(this, pWnd, pControl);
  return mGraphicsCarbon->GetView();
}
#endif

void IGraphicsMac::CloseWindow()
{
#ifndef IPLUG_NO_CARBON_SUPPORT
  if (mGraphicsCarbon) 
  {
    DELETE_NULL(mGraphicsCarbon);
  }
  else  
#endif
	if (mGraphicsCocoa) 
  {
    IGRAPHICS_COCOA* graphicscocoa = (IGRAPHICS_COCOA*)mGraphicsCocoa;
    [graphicscocoa killTimer];
    mGraphicsCocoa = 0;
    if (graphicscocoa->mGraphics)
    {
      graphicscocoa->mGraphics = 0;
      [graphicscocoa removeFromSuperview];   // Releases.
    }

	}
}

bool IGraphicsMac::WindowIsOpen()
{
#ifndef IPLUG_NO_CARBON_SUPPORT
	return (mGraphicsCarbon || mGraphicsCocoa);
#else
	return mGraphicsCocoa;
#endif
}

void IGraphicsMac::Resize(int w, int h)
{
  IGraphics::Resize(w, h);
  if (mDrawBitmap) {
    mDrawBitmap->resize(w, h);
  } 
  
#ifndef IPLUG_NO_CARBON_SUPPORT
  if (mGraphicsCarbon) {
    mGraphicsCarbon->Resize(w, h);
  }
  else
#endif
  if (mGraphicsCocoa) {
    NSSize size = { w, h };
    [(IGRAPHICS_COCOA*) mGraphicsCocoa setFrameSize: size ];
  }  
}

void IGraphicsMac::HostPath(WDL_String* pPath)
{
  CocoaAutoReleasePool pool;
  NSBundle* pBundle = [NSBundle bundleWithIdentifier: ToNSString(GetBundleID())];
  if (pBundle) {
    NSString* path = [pBundle executablePath];
    if (path) {
      pPath->Set([path cString]);
    }
  }
}

void IGraphicsMac::PluginPath(WDL_String* pPath)
{
  CocoaAutoReleasePool pool;
  NSBundle* pBundle = [NSBundle bundleWithIdentifier: ToNSString(GetBundleID())];
  if (pBundle) {
    NSString* path = [[pBundle bundlePath] stringByDeletingLastPathComponent]; 
    if (path) {
      pPath->Set([path cString]);
      pPath->Append("/");
    }
  }
}

// extensions = "txt wav" for example
void IGraphicsMac::PromptForFile(WDL_String* pFilename, int action, char* dir, char* extensions)
{
}

bool IGraphicsMac::PromptForColor(IColor* pColor, char* prompt)
{
	return false;
}

void IGraphicsMac::PromptUserInput(IControl* pControl, IParam* pParam)
{
  if (mGraphicsCocoa)
    [mGraphicsCocoa promptUserInput: pControl param: pParam];
#ifndef IPLUG_NO_CARBON_SUPPORT
  else if (mGraphicsCarbon)
    mGraphicsCarbon->PromptUserInput(pControl, pParam);
#endif
}

void IGraphicsMac::PromptUserInput(IEditableTextControl* pControl)
{
  if (mGraphicsCocoa)
    [mGraphicsCocoa promptUserInput: pControl];
#ifndef IPLUG_NO_CARBON_SUPPORT
  else if (mGraphicsCarbon)
    mGraphicsCarbon->PromptUserInput(pControl);
#endif
}

bool IGraphicsMac::OpenURL(const char* url,
  const char* msgWindowTitle, const char* confirmMsg, const char* errMsgOnFailure)
{
#pragma REMINDER("Warning and error messages for OpenURL not implemented")
  NSURL* pURL = 0;
  if (strstr(url, "http")) {
    pURL = [NSURL URLWithString:ToNSString(url)];
  }
  else {
    pURL = [NSURL fileURLWithPath:ToNSString(url)];
  }    
  if (pURL) {
    bool ok = ([[NSWorkspace sharedWorkspace] openURL:pURL]);
  // [pURL release];
    return ok;
  }
  return true;
}

void* IGraphicsMac::GetWindow()
{
	return mGraphicsCocoa;
}

// static
int IGraphicsMac::GetUserOSVersion()   // Returns a number like 0x1050 (10.5).
{
  SInt32 ver = 0;
  Gestalt(gestaltSystemVersion, &ver);
  Trace(TRACELOC, "%x", ver);
  return ver;
}
