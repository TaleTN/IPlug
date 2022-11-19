#include "IGraphicsCocoa.h"

#include "WDL/wdltypes.h"
#include "WDL/swell/swell.h"

// thnx swell-internal.h
#define PREFIX_CLASSNAME3(a, b) a##b
#define PREFIX_CLASSNAME2(a, b) PREFIX_CLASSNAME3(a, b)
#define PREFIX_CLASSNAME(cname) PREFIX_CLASSNAME2(IGRAPHICS_COCOA, cname)

#if MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_12
	#define NSCommandKeyMask      NSEventModifierFlagCommand
	#define NSShiftKeyMask        NSEventModifierFlagShift
	#define NSControlKeyMask      NSEventModifierFlagControl
	#define NSAlternateKeyMask    NSEventModifierFlagOption

	#define NSLeftTextAlignment   NSTextAlignmentLeft
	#define NSCenterTextAlignment NSTextAlignmentCenter
	#define NSRightTextAlignment  NSTextAlignmentRight
#endif

#define ColoredTextField PREFIX_CLASSNAME(_ColoredTextField)

@interface ColoredTextField: NSTextField
{}
- (BOOL) becomeFirstResponder;
@end

static NSRect ToNSRect(const IGraphics* const pGraphics, const IRECT* const pR)
{
	static const int scale = IGraphicsMac::kScaleOS;
	const int B = pGraphics->Height() - pR->B;

	return NSMakeRect((CGFloat)(pR->L >> scale), (CGFloat)(B >> scale),
	(CGFloat)(pR->W() >> scale), (CGFloat)(pR->H() >> scale));
}

static IRECT ToIRECT(const IGraphics* const pGraphics, const NSRect* const pR)
{
	static const int scale = IGraphicsMac::kScaleOS;

	const int x = (int)pR->origin.x << scale, y = (int)pR->origin.y << scale,
	w = (int)pR->size.width << scale, h = (int)pR->size.height << scale;

	const int gh = pGraphics->Height();
	return IRECT(x, gh - (y + h), x + w, gh - y);
}

static NSString* ToNSString(const char* const cStr)
{
	return [NSString stringWithCString: cStr encoding: NSUTF8StringEncoding];
}

static NSColor* ToNSColor(const IColor color)
{
	const CGFloat div = 1.0f / 255.0f;

	const CGFloat r = (CGFloat)color.R * div;
	const CGFloat g = (CGFloat)color.G * div;
	const CGFloat b = (CGFloat)color.B * div;
	const CGFloat a = (CGFloat)color.A * div;

	return [NSColor colorWithRed: r green: g blue: b alpha: a];
}

static IMouseMod GetMouseMod(const NSEvent* const pEvent, const bool left)
{
	const int mods = (int)[pEvent modifierFlags], cmd = !!(mods & NSCommandKeyMask);
	return IMouseMod(!cmd ? left : false, cmd ? left : false, !!(mods & NSShiftKeyMask), !!(mods & NSControlKeyMask), !!(mods & NSAlternateKeyMask));
}

static IMouseMod GetRightMouseMod(const NSEvent* const pEvent, const bool right, const bool wheel = false)
{
	const int mods = (int)[pEvent modifierFlags];
	return IMouseMod(false, right, !!(mods & NSShiftKeyMask), !!(mods & NSControlKeyMask), !!(mods & NSAlternateKeyMask), wheel);
}

@implementation IGRAPHICS_COCOA

- (id) init
{
	mGraphics = NULL;
	mTimer = nil;
	mParamChangeTimer = 0;
	mAutoCommitTimer = 0;
	mAutoCommitDelay = 0;
	return self;
}

- (id) initWithIGraphics: (IGraphicsMac*)pGraphics
{
	mGraphics = pGraphics;
	static const int scale = IGraphicsMac::kScaleOS;
	const int w = pGraphics->Width(), h = pGraphics->Height();
	const NSRect r = NSMakeRect(0.0f, 0.0f, (CGFloat)(w >> scale), (CGFloat)(h >> scale));
	self = [super initWithFrame: r];

	const double sec = 1.0 / (double)pGraphics->FPS();
	mTimer = [NSTimer timerWithTimeInterval: sec target: self selector: @selector(onTimer:) userInfo: nil repeats: YES];
	[[NSRunLoop currentRunLoop] addTimer: mTimer forMode: (NSString*)kCFRunLoopCommonModes];

	return self;
}

- (BOOL) isOpaque
{
	return mGraphics ? YES : NO;
}

- (BOOL) acceptsFirstResponder
{
	return YES;
}

- (BOOL) acceptsFirstMouse: (NSEvent*)pEvent
{
	return YES;
}

- (void) viewDidMoveToWindow
{
	NSWindow* const pWindow = [self window];
	if (pWindow)
	{
		[pWindow makeFirstResponder: self];
		[pWindow setAcceptsMouseMovedEvents: YES];
	}
}

- (void) drawRect: (NSRect)rect
{
	if (mGraphics)
	{
		const IRECT r = ToIRECT(mGraphics, &rect);
		mGraphics->Draw(&r);
	}
}

- (void) onTimer: (NSTimer*)pTimer
{
	if (pTimer == mTimer && mGraphics)
	{
		if (mGraphics->ScaleNeedsUpdate() && mGraphics->UpdateScale())
		{
			const int w = mGraphics->Width(), h = mGraphics->Height();
			const int scale = mGraphics->Scale();

			const NSSize size = NSMakeSize((CGFloat)(w >> scale), (CGFloat)(h >> scale));
			[self setBoundsSize: size];

			const CGFloat unit = (CGFloat)(1 << (IGraphicsMac::kScaleOS - scale));
			[self scaleUnitSquareToSize: NSMakeSize(unit, unit)];

			mGraphics->SetAllControlsDirty();
			mGraphics->UpdateTooltips();
		}

		IRECT r;
		if (mGraphics->IsDirty(&r))
		{
			[self setNeedsDisplayInRect: ToNSRect(mGraphics, &r)];
		}

		int timer = mParamChangeTimer;
		if (timer && !(mParamChangeTimer = timer - 1))
		{
			mGraphics->GetPlug()->EndDelayedInformHostOfParamChange();
		}

		timer = mAutoCommitTimer;
		if (timer && !(mAutoCommitTimer = timer - 1))
		{
			if (mParamEditView) [self commitUserInput];
		}
	}
}

- (void) getMouseXY: (NSEvent*)pEvent x: (int*)pX y: (int*)pY
{
	const NSPoint pt = [self convertPoint: [pEvent locationInWindow] fromView: nil];
	const CGFloat scale = (CGFloat)(1 << IGraphicsMac::kScaleOS);
	*pX = (int)(pt.x * scale);
	*pY = mGraphics->Height() - (int)(pt.y * scale);
}

- (void) mouseDown: (NSEvent*)pEvent
{
	if (mGraphics)
	{
		if (mParamEditView) [self endUserInput];
		int x, y;
		[self getMouseXY: pEvent x: &x y: &y];
		const IMouseMod mod = GetMouseMod(pEvent, true);
		if ([pEvent clickCount] > 1)
		{
			mGraphics->OnMouseDblClick(x, y, mod);
		}
		else
		{
			mGraphics->OnMouseDown(x, y, mod);
		}
	}
}

- (void) mouseUp: (NSEvent*)pEvent
{
	if (mGraphics)
	{
		int x, y;
		[self getMouseXY: pEvent x: &x y: &y];
		mGraphics->OnMouseUp(x, y, GetMouseMod(pEvent, false));
	}
}

- (void) mouseDragged: (NSEvent*)pEvent
{
	if (mGraphics)
	{
		int x, y;
		[self getMouseXY: pEvent x: &x y: &y];
		mGraphics->OnMouseDrag(x, y, GetMouseMod(pEvent, true));
	}
}

- (void) rightMouseDown: (NSEvent*)pEvent
{
	if (mGraphics)
	{
		if (mParamEditView)
		{
			[self endUserInput];
			return;
		}
		int x, y;
		[self getMouseXY: pEvent x: &x y: &y];
		mGraphics->OnMouseDown(x, y, GetRightMouseMod(pEvent, true));
	}
}

- (void) rightMouseUp: (NSEvent*)pEvent
{
	if (mGraphics)
	{
		int x, y;
		[self getMouseXY: pEvent x: &x y: &y];
		mGraphics->OnMouseUp(x, y, GetRightMouseMod(pEvent, false));
	}
}

- (void) rightMouseDragged: (NSEvent*)pEvent
{
	if (mGraphics)
	{
		int x, y;
		[self getMouseXY: pEvent x: &x y: &y];
		mGraphics->OnMouseDrag(x, y, GetRightMouseMod(pEvent, true));
	}
}

- (void) mouseMoved: (NSEvent*)pEvent
{
	if (mGraphics)
	{
		int x, y;
		[self getMouseXY: pEvent x: &x y: &y];
		mGraphics->OnMouseOver(x, y, GetRightMouseMod(pEvent, false));
	}
}

- (void) scrollWheel: (NSEvent*)pEvent
{
	int canHandle;
	if (mGraphics && (canHandle = mGraphics->CanHandleMouseWheel()))
	{
		const IMouseMod mod = GetRightMouseMod(pEvent, false, canHandle >= 0);
		const IMouseMod mask(false, false, true, true, true, true);

		if (mod.Get() & mask.Get())
		{
			int x, y;
			[self getMouseXY: pEvent x: &x y: &y];
			const CGFloat d = [pEvent deltaY];
			mGraphics->OnMouseWheel(x, y, mod, (float)d);
		}
	}
}

- (void) processKey: (NSEvent*)pEvent state: (BOOL)state
{
	static const char vk[64] =
	{
		// A-Z, 0-9
		0x41, 0x53, 0x44, 0x46, 0x48, 0x47, 0x5A, 0x58, 0x43, 0x56, 0,    0x42, 0x51, 0x57, 0x45, 0x52,
		0x59, 0x54, 0x31, 0x32, 0x33, 0x34, 0x36, 0x35, 0,    0x39, 0x37, 0,    0x38, 0x30, 0,    0x4F,
		0x55, 0,    0x49, 0x50, 0,    0x4C, 0x4A, 0,    0x4B, 0,    0,    0,    0,    0x4E, 0x4D, 0,
		0, VK_SPACE,

		0, VK_HOME, VK_PRIOR, 0, 0, VK_END, 0, VK_NEXT, 0, VK_LEFT, VK_RIGHT, VK_DOWN, VK_UP, 0
	};

	if (mGraphics && mGraphics->GetKeyboardFocus() >= 0)
	{
		int key = [pEvent keyCode];
		if (key >= 50)
		{
			key -= 0x72 - 50;
			key = wdl_max(key, 50);
			key = wdl_min(key, 63);
		}
		key = vk[key];

		if (key)
		{
			int x, y;
			[self getMouseXY: pEvent x: &x y: &y];
			const IMouseMod mod = GetMouseMod(pEvent, false);
			const bool ret = state ? mGraphics->OnKeyDown(x, y, mod, key) : mGraphics->OnKeyUp(x, y, mod, key);
			if (ret) return;
		}
	}

	if (state)
		[super keyDown: pEvent];
	else
		[super keyUp: pEvent];
}

- (void) keyDown: (NSEvent*)pEvent
{
	[self processKey: pEvent state: YES];
}

- (void) keyUp: (NSEvent*)pEvent
{
	[self processKey: pEvent state: NO];
}

- (void) killTimer
{
	[mTimer invalidate];
	mTimer = nil;
}

- (void) removeFromSuperview
{
	if (mParamEditView) [self endUserInput];
	if (mGraphics)
	{
		IGraphics* const graphics = mGraphics;
		graphics->GetPlug()->OnGUIClose();
		mGraphics = NULL;
		graphics->CloseWindow();
	}
}

- (void) controlTextDidChange: (NSNotification*)aNotification
{
	if (mAutoCommitDelay) mAutoCommitTimer = mAutoCommitDelay;
}

- (void) controlTextDidEndEditing: (NSNotification*)aNotification
{
	[self commitUserInput];
	[self endUserInput];
	[self viewDidMoveToWindow];
	[self setNeedsDisplay: YES];
}

static const int PARAM_EDIT_W = 42;
static const int PARAM_EDIT_H = 21;

- (BOOL) promptUserInput: (IControl*)pControl param: (IParam*)pParam rect: (const IRECT*)pR flags: (int)flags font: (IText*)pTxt background: (IColor)bg delay: (int)delay x: (int)mouseX y: (int)mouseY
{
	// To-do: Implement kPromptInline, kPromptMouseClick (see IGraphicsWin.cpp).

	if (mParamEditView || !pControl) return NO;

	char currentText[IGraphics::kMaxEditLen];
	if (pParam)
	{
		pParam->GetDisplayForHost(currentText, sizeof(currentText));
	}
	else
	{
		pControl->GetTextForUserInput(currentText, sizeof(currentText));
	}

	static const int kPromptCustomHeight = IGraphics::kPromptCustomRect ^ IGraphics::kPromptCustomWidth;
	static const int scale = IGraphicsMac::kScaleOS;

	const int gh = mGraphics->Height() >> scale;
	if (!pR) pR = pControl->GetTargetRECT();

	int x, y, w, h;
	if (!(flags & IGraphics::kPromptCustomWidth))
	{
		const int cX = (pR->L + pR->R) >> scale;
		w = PARAM_EDIT_W;
		x = (cX - w) / 2;
	}
	else
	{
		w = pR->W() >> scale;
		x = pR->L >> scale;
	}

	if (!(flags  & kPromptCustomHeight))
	{
		const int cY = (pR->T + pR->B) >> scale;
		h = PARAM_EDIT_H;
		y = (cY + h) / 2;
	}
	else
	{
		h = pR->H() >> scale;
		y = (pR->T >> scale) + h;
	}

	static const IText kDefaultFont(0, IColor(0));
	const IText* const pFont = pTxt ? pTxt : &kDefaultFont;

	int fontSize = pFont->mSize >> scale;
	const CGFloat sz = fontSize ? (CGFloat)fontSize : (CGFloat)PARAM_EDIT_H * (11.0f / 21.0f);

	NSFont* font = [NSFont fontWithName: ToNSString(pFont->mFont) size: sz];
	if (fontSize)
	{
		// See DrawText() in WDL/swell/swell-gdi.mm.
		int lineHeight = (int)([font ascender] - [font descender] + [font leading] + 1.5f);
		if (lineHeight && ++lineHeight != fontSize)
		{
			// See IGraphics::CacheFont().
			fontSize = (int)((double)(fontSize * fontSize) / (double)lineHeight + 0.5);
			font = [NSFont fontWithDescriptor: [font fontDescriptor] size: (CGFloat)fontSize];
		}
	}

	mAutoCommitTimer = 0;
	mAutoCommitDelay = (int)((double)(mGraphics->FPS() * delay) * 0.001);

	static const NSTextAlignment align[3] = { NSLeftTextAlignment, NSCenterTextAlignment, NSRightTextAlignment };
	assert(pFont->mAlign >= 0 && pFont->mAlign < 3);

	IColor fg = pFont->mColor;

	// TN: Force default black on white, or else Logic Pro X/GarageBand 10
	// will use transparent background instead of default system colors.
	fg = fg.Empty() ? IColor(255, 0, 0, 0) : fg;
	bg = bg.Empty() ? IColor(255, 255, 255, 255) : bg;

	const NSRect r = NSMakeRect((CGFloat)x, (CGFloat)(gh - y), (CGFloat)w, (CGFloat)h);

	NSTextField* const textField = /* fg.Empty() ? [NSTextField alloc] : */ [ColoredTextField alloc];
	mParamEditView = [textField initWithFrame: r];

	[mParamEditView setFont: font];
	[mParamEditView setAlignment: align[pFont->mAlign]];
	[[mParamEditView cell] setLineBreakMode: NSLineBreakByTruncatingTail];
	[mParamEditView setStringValue: ToNSString(currentText)];

	// if (!fg.Empty())
	{
		[mParamEditView setTextColor: ToNSColor(fg)];
	}

	// if (!bg.Empty())
	{
		[mParamEditView setBackgroundColor: ToNSColor(bg)];
		[mParamEditView setDrawsBackground: YES];
	}

	[mParamEditView setDelegate: self];
	[self addSubview: mParamEditView];
	NSWindow* pWindow = [self window];
	[pWindow makeKeyAndOrderFront: nil];
	[pWindow makeFirstResponder: mParamEditView];

	mEdControl = pControl;
	mEdParam = pParam;

	return YES;
}

- (void) commitUserInput
{
	const char* const txt = (const char*)[[mParamEditView stringValue] UTF8String];
	mGraphics->SetFromStringAfterPrompt(mEdControl, mEdParam, txt);
}

- (void) endUserInput
{
	[mParamEditView setDelegate: nil];
	[mParamEditView removeFromSuperview];
	mParamEditView = nil;
	mEdControl = NULL;
	mEdParam = NULL;
	mAutoCommitTimer = 0;
	mAutoCommitDelay = 0;
}

- (NSString*) view: (NSView*)pView stringForToolTip: (NSToolTipTag)tag point: (NSPoint)point userData: (void*)pData
{
	const int c = /* (long)pData */ mGraphics->GetMouseOver();
	IControl* const pControl = mGraphics->GetControl(c);
	if (!pControl) return @"";

	const char* const tooltip = pControl->GetTooltip();
	return tooltip && *tooltip ? ToNSString(tooltip) : @"";
}

- (void) registerToolTip: (int)controlIdx rect: (const IRECT*)pRECT
{
	[self addToolTipRect: ToNSRect(mGraphics, pRECT) owner: self userData: (void*)(long)controlIdx];
}

- (void) setParamChangeTimer: (int)ticks
{
	mParamChangeTimer = ticks;
}

- (void) cancelParamChangeTimer
{
	mParamChangeTimer = 0;
}

@end

@implementation ColoredTextField

// Source: https://stackoverflow.com/questions/2258300/nstextfield-white-text-on-black-background-but-black-cursor
- (BOOL) becomeFirstResponder
{
	if (![super becomeFirstResponder]) return NO;

	NSTextView* const textField = (NSTextView*)[self currentEditor];
	if ([textField respondsToSelector: @selector(setInsertionPointColor:)])
	{
		[textField setInsertionPointColor: [self textColor]];
	}

	return YES;
}

@end
