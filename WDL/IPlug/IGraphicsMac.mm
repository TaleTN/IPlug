#include <Foundation/NSArchiver.h>
#include "IGraphicsMac.h"
#include "IControl.h"
#include "Log.h"
#import "IGraphicsCocoa.h"
#ifndef IPLUG_NO_CARBON_SUPPORT
	#include "IGraphicsCarbon.h"
#endif
#include "../swell/swell-internal.h"
#include <stdlib.h>

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
  NSString* pFile = [[ToNSString(filename) lastPathComponent] stringByDeletingPathExtension];
  if (pBundle && pFile) 
  {
    NSString* pPath = 0;
    if (ispng) pPath = [pBundle pathForResource:pFile ofType:@"png"];  
#ifndef IPLUG_NO_JPEG_SUPPORT
    if (isjpg) pPath = [pBundle pathForResource:pFile ofType:@"jpg"];  
#endif

    if (pPath) 
    {
      const char* resourceFileName = [pPath UTF8String];
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
  r.size.width = mDrawBitmap->getRowSpan();
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

  if (TooltipsEnabled()) {
    IControl** ppControl = mControls.GetList();
    for (int i = 0, n = mControls.GetSize(); i < n; ++i, ++ppControl) {
      IControl* pControl = *ppControl;
      const char* tooltip = pControl->GetTooltip();
      if (tooltip) {
        [(IGRAPHICS_COCOA*) mGraphicsCocoa registerToolTip: pControl->GetTargetRECT()];
      }
    }
  }

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
      pPath->Set([path UTF8String]);
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
      pPath->Set([path UTF8String]);
      pPath->Append("/");
    }
  }
}

// extensions = "txt wav" for example
void IGraphicsMac::PromptForFile(WDL_String* pFilename, EFileAction action, char* dir, char* extensions)
{
  if (!WindowIsOpen()) {
    return;
  }
  CocoaAutoReleasePool pool;

  NSSavePanel* panel = nil;
  switch (action) {
    case kFileSave: {
      panel = [NSSavePanel savePanel];
      break;
    }
    case kFileOpen: {
      panel = [NSOpenPanel openPanel];
      [panel setCanChooseFiles: YES];
      [panel setCanChooseDirectories: NO];
      [panel setResolvesAliases: YES];
      break;
    }
    default:
      return;
  }

  if (CSTR_NOT_EMPTY(extensions)) {
    NSArray* fileTypes = [[NSString stringWithUTF8String:extensions] componentsSeparatedByString: @" "];
    [panel setAllowedFileTypes: fileTypes];
    [panel setAllowsOtherFileTypes: NO];
  }

  // Apple's documentation states that runModalForDirectory is deprecated in
  // Mac OS X v10.6, use setDirectoryURL, setNameFieldStringValue, and
  // runModal instead, which are available in v10.6 and later.
  // http://developer.apple.com/library/mac/documentation/Cocoa/Reference/ApplicationKit/Classes/nssavepanel_Class/

  int result;
  #if MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_6 && MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_6
  if (NSFoundationVersionNumber >= NSFoundationVersionNumber10_6)
  #endif
  #if MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_6
  {
    if (CSTR_NOT_EMPTY(dir)) {
      [panel setDirectoryURL: [NSString stringWithUTF8String: dir]];
    }
    if (pFilename->GetLength()) {
      [panel setNameFieldStringValue: [NSString stringWithUTF8String: pFilename->Get()]];
    }
    result = [panel runModal];
  }
  #endif
  #if MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_6 && MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_6
  else
  #endif
  #if MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_6
  {
    NSString* nsDir = CSTR_NOT_EMPTY(dir) ? [NSString stringWithUTF8String: dir] : nil;
    NSString* nsFile = pFilename->GetLength() ? [NSString stringWithUTF8String: pFilename->Get()] : nil;
    result = [panel runModalForDirectory: nsDir file: nsFile];
  }
  #endif

  if (result == NSOKButton) {
    pFilename->Set([[[panel URL] path] UTF8String]);
  }
  else {
    pFilename->Set("");
  }
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
  CocoaAutoReleasePool pool;

  // http://cocoadev.com/wiki/DeterminingOSVersion
  NSDictionary* dict = [NSDictionary dictionaryWithContentsOfFile:@"/System/Library/CoreServices/SystemVersion.plist"];
  if (!dict) return 0;
  NSString* versionString = [dict objectForKey:@"ProductVersion"];
  if (!versionString) return 0;
  NSArray* versions = [versionString componentsSeparatedByString:@"."];
  int i = atoi([[versions objectAtIndex:0] UTF8String]);
  int ver = (i / 10 * 16 + i % 10) << 8;
  if ([versions count] >= 2)
  {
    i = atoi([[versions objectAtIndex:1] UTF8String]);
    ver |= i < 10 ? (i % 10) << 4 : 0x90;
    if ([versions count] >= 3)
    {
      i = atoi([[versions objectAtIndex:2] UTF8String]);
      ver |= i < 10 ? (i % 10) : 0x9;
    }
  }

  Trace(TRACELOC, "%x", ver);
  return ver;
}

// static
double IGraphicsMac::GetUserFoundationVersion()   // Returns a number like 677.00 (10.5).
{
  return NSFoundationVersionNumber;
}
