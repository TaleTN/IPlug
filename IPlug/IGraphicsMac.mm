#include "IGraphicsMac.h"
#import "IGraphicsCocoa.h"

#ifndef IPLUG_NO_CARBON_SUPPORT
	#include "IGraphicsCarbon.h"
#endif

#include "WDL/swell/swell.h"

// TN: I guess "undocumented" see WDL/swell/swell-misc.mm, but it works!
#define SWELL_IMPLEMENT_GETOSXVERSION
#include "WDL/swell/swell-internal.h"

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

static const char* FindResourceOSX(const char* const bundleID, const char* const filename, const char* const type)
{
	if (!filename) return NULL;
	// const CocoaAutoReleasePool pool;

	NSBundle* const pBundle = [NSBundle bundleWithIdentifier: ToNSString(bundleID)];
	NSString* const pFile = [[ToNSString(filename) lastPathComponent] stringByDeletingPathExtension];
	if (pBundle && pFile)
	{
		NSString* const pPath = [pBundle pathForResource: pFile ofType: ToNSString(type)];

		if (pPath)
		{
			const char* const resourceFileName = [pPath UTF8String];
			if (resourceFileName && *resourceFileName)
			{
				return resourceFileName;
			}
		}
	}
	return NULL;
}

LICE_IBitmap* IGraphicsMac::OSLoadBitmap(int /* ID */, const char* const name)
{
	const CocoaAutoReleasePool pool;
	const char* const resourceFileName = FindResourceOSX(GetBundleID(), name, "png");
	return resourceFileName ? LICE_LoadPNG(resourceFileName) : NULL;
}

bool IGraphicsMac::OSLoadFont(int /* ID */, const char* const name)
{
	const CocoaAutoReleasePool pool;
	const char* const resourceFileName = FindResourceOSX(GetBundleID(), name, "ttf");
	return resourceFileName ? !!AddFontResourceEx(resourceFileName, FR_PRIVATE, NULL) : NULL;
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
		delete mGraphicsCarbon;
		mGraphicsCarbon = NULL;
	}
	else
	#endif
	if (mGraphicsCocoa)
	{
		IGRAPHICS_COCOA* const graphicscocoa = (IGRAPHICS_COCOA*)mGraphicsCocoa;
		[graphicscocoa removeAllToolTips];
		GetPlug()->EndDelayedInformHostOfParamChange();
		[graphicscocoa killTimer];
		mGraphicsCocoa = NULL;

		IGraphicsMac* pGraphicsMac;
		object_getInstanceVariable(graphicscocoa, "_mGraphics", (void**)&pGraphicsMac);
		if (pGraphicsMac)
		{
			object_setInstanceVariable(graphicscocoa, "_mGraphics", NULL);
			[graphicscocoa removeFromSuperview]; // Releases.
		}
	}
}

bool IGraphicsMac::WindowIsOpen() const
{
	return
	#ifndef IPLUG_NO_CARBON_SUPPORT
	mGraphicsCarbon ||
	#endif
	mGraphicsCocoa;
}

/* void IGraphicsMac::Resize(const int w, const int h)
{
	IGraphics::Resize(w, h);
    mDrawBitmap.resize(w, h);

	#ifndef IPLUG_NO_CARBON_SUPPORT
	if (mGraphicsCarbon)
	{
		mGraphicsCarbon->Resize(w, h);
	}
	else
	#endif
	if (mGraphicsCocoa)
	{
		const NSSize size = NSMakeSize((CGFloat)(w >> kScaleOS), (CGFloat)(h >> kScaleOS));
		[(IGRAPHICS_COCOA*)mGraphicsCocoa setFrameSize: size];
	}
} */

void IGraphicsMac::UpdateTooltips()
{
	#ifndef IPLUG_NO_CARBON_SUPPORT
	if (mGraphicsCarbon)
	{
		if (!TooltipsEnabled()) mGraphicsCarbon->HideTooltip();
		return;
	}
	#endif

	if (!mGraphicsCocoa) return;
	const CocoaAutoReleasePool pool;

	[(IGRAPHICS_COCOA*)mGraphicsCocoa removeAllToolTips];
	if (!TooltipsEnabled()) return;

	IControl* const* const ppControl = mControls.GetList();
	for (int i = mControls.GetSize() - 1; i >= 0; --i)
	{
		IControl* const pControl = ppControl[i];
		if (!pControl->IsHidden())
		{
			const IRECT* const pR = pControl->GetTargetRECT();
			if (!pR->Empty())
			{
				[(IGRAPHICS_COCOA*)mGraphicsCocoa registerToolTip: i rect: pR];
			}
		}
	}
}

bool IGraphicsMac::HostPath(WDL_String* const pPath)
{
	const CocoaAutoReleasePool pool;
	NSBundle* const pBundle = [NSBundle bundleWithIdentifier: ToNSString(GetBundleID())];
	if (pBundle)
	{
		NSString* const path = [pBundle executablePath];
		if (path)
		{
			pPath->Set([path UTF8String]);
			return true;
		}
	}
	pPath->Set("");
	return false;
}

bool IGraphicsMac::PluginPath(WDL_String* const pPath)
{
	const CocoaAutoReleasePool pool;
	NSBundle* const pBundle = [NSBundle bundleWithIdentifier: ToNSString(GetBundleID())];
	if (pBundle)
	{
		NSString* const path = [[pBundle bundlePath] stringByDeletingLastPathComponent];
		if (path)
		{
			pPath->Set([path UTF8String]);
			pPath->Append("/");
			return true;
		}
	}
	pPath->Set("");
	return false;
}

// extensions = "txt wav" for example
bool IGraphicsMac::PromptForFile(WDL_String* const pFilename, const int action, const char* const dir, const char* const extensions)
{
	if (!WindowIsOpen())
	{
		pFilename->Set("");
		return false;
	}
	const CocoaAutoReleasePool pool;

	NSSavePanel* panel = nil;
	switch (action)
	{
		case kFileSave:
		{
			panel = [NSSavePanel savePanel];
			break;
		}
		case kFileOpen:
		{
			NSOpenPanel* const openPanel = [NSOpenPanel openPanel];
			panel = openPanel;
			[openPanel setCanChooseFiles: YES];
			[openPanel setCanChooseDirectories: NO];
			[openPanel setResolvesAliases: YES];
			break;
		}
		default:
		{
			pFilename->Set("");
			return false;
		}
	}

	if (extensions && *extensions)
	{
		NSArray* const fileTypes = [[NSString stringWithUTF8String: extensions] componentsSeparatedByString: @" "];
		[panel setAllowedFileTypes: fileTypes];
		[panel setAllowsOtherFileTypes: NO];
	}

	// Apple's documentation states that runModalForDirectory is deprecated
	// in Mac OS X v10.6, use setDirectoryURL, setNameFieldStringValue, and
	// runModal instead, which are available in v10.6 and later.
	// http://developer.apple.com/library/mac/documentation/Cocoa/Reference/ApplicationKit/Classes/nssavepanel_Class/

	int result;
	#if MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_6 && MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_6
	if (NSFoundationVersionNumber >= NSFoundationVersionNumber10_6)
	#endif
	#if MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_6
	{
		if (dir && *dir)
		{
			[panel setDirectoryURL: [NSURL fileURLWithPath: [NSString stringWithUTF8String: dir]]];
		}
		if (pFilename->GetLength())
		{
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
		NSString* const nsDir = dir && *dir ? [NSString stringWithUTF8String: dir] : nil;
		NSString* const nsFile = pFilename->GetLength() ? [NSString stringWithUTF8String: pFilename->Get()] : nil;
		result = [panel runModalForDirectory: nsDir file: nsFile];
	}
	#endif

	const bool ok = result == NSOKButton;
	pFilename->Set(ok ? [[[panel URL] path] UTF8String] : "");

	return ok;
}

void IGraphicsMac::PromptUserInput(IControl* const pControl, IParam* const pParam, const IRECT* const pR, const int fontSize)
{
	if (mGraphicsCocoa)
	{
		[(IGRAPHICS_COCOA*)mGraphicsCocoa promptUserInput: pControl param: pParam rect: pR size: fontSize];
	}
	#ifndef IPLUG_NO_CARBON_SUPPORT
	else if (mGraphicsCarbon)
	{
		mGraphicsCarbon->PromptUserInput(pControl, pParam, pR, fontSize);
	}
	#endif
}

bool IGraphicsMac::OpenURL(const char* const url, const char* /* windowTitle */,
	const char* /* confirmMsg */, const char* /* errMsg */)
{
	// Reminder: Warning and error messages for OpenURL not implemented.

	const CocoaAutoReleasePool pool;
	NSURL* pURL = nil;
	if (!strncmp(url, "http", 4))
	{
		pURL = [NSURL URLWithString: ToNSString(url)];
	}
	else
	{
		pURL = [NSURL fileURLWithPath: ToNSString(url)];
	}
	if (pURL)
	{
		const bool ok = [[NSWorkspace sharedWorkspace] openURL: pURL];
		return ok;
	}
	return true;
}

void* IGraphicsMac::GetWindow() const
{
	#ifndef IPLUG_NO_CARBON_SUPPORT
	if (mGraphicsCarbon) return mGraphicsCarbon->GetView();
	#endif

	return mGraphicsCocoa;
}

// static
int IGraphicsMac::GetUserOSVersion() // Returns a number like 0x1050 (10.5).
{
	return SWELL_GetOSXVersion();
}

// static
double IGraphicsMac::GetUserFoundationVersion() // Returns a number like 677.00 (10.5).
{
	return NSFoundationVersionNumber;
}

void IGraphicsMac::SetParamChangeTimer(const int ticks)
{
	if (mGraphicsCocoa)
	{
		[(IGRAPHICS_COCOA*)mGraphicsCocoa setParamChangeTimer: ticks];
	}
	#ifndef IPLUG_NO_CARBON_SUPPORT
	else if (mGraphicsCarbon)
	{
		mGraphicsCarbon->SetParamChangeTimer(ticks);
	}
	#endif
}

void IGraphicsMac::CancelParamChangeTimer()
{
	if (mGraphicsCocoa)
	{
		[(IGRAPHICS_COCOA*)mGraphicsCocoa cancelParamChangeTimer];
	}
	#ifndef IPLUG_NO_CARBON_SUPPORT
	else if (mGraphicsCarbon)
	{
		mGraphicsCarbon->CancelParamChangeTimer();
	}
	#endif
}

#ifndef NDEBUG
void IPlugDebugLog(const char* const str)
{
	NSLog(@"%s", str);
}
#endif
