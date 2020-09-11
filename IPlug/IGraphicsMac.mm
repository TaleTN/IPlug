#include "IGraphicsMac.h"
#import "IGraphicsCocoa.h"

#ifndef IPLUG_NO_CARBON_SUPPORT
	#include "IGraphicsCarbon.h"
#endif

#include "WDL/swell/swell.h"

#include <string.h>
#import <objc/runtime.h>

// TN: Would like to use SWELL_AutoReleaseHelper instead, but can't because
// that would require much more than just SWELL GDI...
struct CocoaAutoReleasePool
{
	NSAutoreleasePool* const mPool;
	CocoaAutoReleasePool(): mPool([[NSAutoreleasePool alloc] init]) {}
	~CocoaAutoReleasePool() { [mPool release]; }
};

static NSString* ToNSString(const char* const cStr)
{
	return [NSString stringWithCString: cStr encoding: NSUTF8StringEncoding];
}

IGraphicsMac::IGraphicsMac(
	IPlugBase* const pPlug,
	const int w,
	const int h,
	const int refreshFPS
):
	IGraphics(pPlug, w, h, refreshFPS),

	#ifndef IPLUG_NO_CARBON_SUPPORT
	mGraphicsCarbon(NULL),
	#endif

	mGraphicsCocoa(NULL),
	mWantScale(-1)
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
  
#ifndef IPLUG_NO_JPEG_SUPPORT
  const char* ext = filename+strlen(filename)-1;
  while (ext >= filename && *ext != '.') --ext;
  ++ext;
  
  bool ispng = !stricmp(ext, "png");
  bool isjpg = !stricmp(ext, "jpg");
  if (!isjpg && !ispng) return 0;
#endif
  
  NSBundle* pBundle = [NSBundle bundleWithIdentifier:ToNSString(bundleID)];
  NSString* pFile = [[ToNSString(filename) lastPathComponent] stringByDeletingPathExtension];
  if (pBundle && pFile) 
  {
    NSString* pPath = 0;
#ifndef IPLUG_NO_JPEG_SUPPORT
    if (ispng)
#endif
    pPath = [pBundle pathForResource:pFile ofType:@"png"];  
#ifndef IPLUG_NO_JPEG_SUPPORT
    if (isjpg) pPath = [pBundle pathForResource:pFile ofType:@"jpg"];  
#endif

    if (pPath) 
    {
      const char* resourceFileName = [pPath UTF8String];
      if (CSTR_NOT_EMPTY(resourceFileName))
      {
#ifndef IPLUG_NO_JPEG_SUPPORT
        if (ispng)
#endif
        return LICE_LoadPNG(resourceFileName);
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

void IGraphicsMac::DrawScreen(const IRECT* /* pR */)
{
	CGContextRef pCGC = NULL;
	CGRect r = CGRectMake(0.0f, 0.0f, (CGFloat)(Width() >> kScaleOS), (CGFloat)(Height() >> kScaleOS));

	if (mGraphicsCocoa)
	{
		pCGC = (CGContextRef)[[NSGraphicsContext currentContext] graphicsPort]; // Leak?
		NSGraphicsContext* const gc = [NSGraphicsContext graphicsContextWithGraphicsPort: pCGC flipped: YES];
		pCGC = (CGContextRef)[gc graphicsPort];

		const CGSize retina = CGContextConvertSizeToDeviceSpace(pCGC, CGSizeMake(1.0f, 1.0f));
		const int wantScale = retina.width > 1.0f ? kScaleFull : kScaleHalf;
		if (wantScale != mWantScale) mWantScale = wantScale;
	}
	#ifndef IPLUG_NO_CARBON_SUPPORT
	else if (mGraphicsCarbon)
	{
		pCGC = mGraphicsCarbon->GetCGContext();
		mGraphicsCarbon->OffsetContentRect(&r);
		// Flipping is handled in IGraphicsCarbon.
	}
	#endif
	if (!pCGC) return;

	CGContextRef const srcCtx = (CGContextRef)SWELL_GetCtxGC(mDrawBitmap.getDC());
	CGImageRef img = srcCtx ? CGBitmapContextCreateImage(srcCtx) : NULL;

	// TN: If GUI width is multiple of 8, then getRowSpan() == getWidth() at
	// both kScaleFull and kScaleHalf, so clipping isn't necessary. Do note
	// that after downsizing GUI clipping will always be necessary, because
	// LICE_SysBitmap doesn't actually resize down.
	const int w = mDrawBitmap.getWidth();
	if (mDrawBitmap.getRowSpan() > w)
	{
		const int h = mDrawBitmap.getHeight();
		const int scale = kScaleOS - Scale();
		const CGRect clip = CGRectMake(0.0f, 0.0f, (CGFloat)(w >> scale), (CGFloat)(h >> scale));
		if (img)
		{
			CGImageRef const tmp = CGImageCreateWithImageInRect(img, clip);
			CGImageRelease(img);
			img = tmp;
		}
	}
	if (!img) return;

	CGContextDrawImage(pCGC, r, img);
	CGImageRelease(img);
}

bool IGraphicsMac::InitScale()
{
	if (mWantScale < 0)
	{
		if (!PreloadScale(kScaleOS)) return false;
		mWantScale = mPrevScale = kScaleOS;
	}
	return true;
}

bool IGraphicsMac::UpdateScale()
{
	mPrevScale = mWantScale;
	return PrepDraw(mWantScale);
}

void* IGraphicsMac::OpenCocoaWindow(void* const pParentView)
{
	CloseWindow();
	InitScale();

	mGraphicsCocoa = (IGRAPHICS_COCOA*)[[IGRAPHICS_COCOA alloc] initWithIGraphics: this];
	if (pParentView) // Cocoa VST host.
	{
		[(NSView*)pParentView addSubview: (IGRAPHICS_COCOA*)mGraphicsCocoa];
	}
	// Else we are being called by IGraphicsCocoaFactory, which is being called by a Cocoa AU host,
	// and the host will take care of attaching the view to the window.

	UpdateTooltips();

	return mGraphicsCocoa;
}

#ifndef IPLUG_NO_CARBON_SUPPORT
void* IGraphicsMac::OpenCarbonWindow(void* const pParentWnd, void* const pParentControl)
{
	CloseWindow();
	InitScale();

	WindowRef const pWnd = (WindowRef)pParentWnd;
	ControlRef const pControl = (ControlRef)pParentControl;
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
    [graphicscocoa removeAllToolTips];
    GetPlug()->EndDelayedInformHostOfParamChange();
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

void IGraphicsMac::UpdateTooltips()
{
#ifndef IPLUG_NO_CARBON_SUPPORT
  if (mGraphicsCarbon) {
    if (!TooltipsEnabled()) mGraphicsCarbon->HideTooltip();
    return;
  }
#endif

  if (!mGraphicsCocoa) return;
  CocoaAutoReleasePool pool;

  [(IGRAPHICS_COCOA*) mGraphicsCocoa removeAllToolTips];
  if (!TooltipsEnabled()) return;

  IControl** ppControl = mControls.GetList();
  for (int i = 0, n = mControls.GetSize(); i < n; ++i, ++ppControl) {
    IControl* pControl = *ppControl;
    const char* tooltip = pControl->GetTooltip();
    if (tooltip && !pControl->IsHidden()) {
      IRECT* pR = pControl->GetTargetRECT();
      if (!pControl->GetTargetRECT()->Empty()) {
        [(IGRAPHICS_COCOA*) mGraphicsCocoa registerToolTip: i rect: pR];
      }
    }
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
    pFilename->Set("");
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
      [panel setDirectoryURL: [NSURL fileURLWithPath: [NSString stringWithUTF8String: dir]]];
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

void IGraphicsMac::SetParamChangeTimer(int ticks)
{
  if (mGraphicsCocoa)
    [mGraphicsCocoa setParamChangeTimer: ticks];
#ifndef IPLUG_NO_CARBON_SUPPORT
  else if (mGraphicsCarbon)
    mGraphicsCarbon->SetParamChangeTimer(ticks);
#endif
}

void IGraphicsMac::CancelParamChangeTimer()
{
  if (mGraphicsCocoa)
    [mGraphicsCocoa cancelParamChangeTimer];
#ifndef IPLUG_NO_CARBON_SUPPORT
  else if (mGraphicsCarbon)
    mGraphicsCarbon->CancelParamChangeTimer();
#endif
}
