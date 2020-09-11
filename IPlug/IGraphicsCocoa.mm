#include "IGraphicsCocoa.h"

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

static IMouseMod GetMouseMod(const NSEvent* const pEvent, const bool wheel = false)
{
	const int mods = [pEvent modifierFlags], cmd = !!(mods & NSCommandKeyMask);
	return IMouseMod(!cmd, cmd, !!(mods & NSShiftKeyMask), !!(mods & NSControlKeyMask), !!(mods & NSAlternateKeyMask), wheel);
}

static IMouseMod GetRightMouseMod(const NSEvent* const pEvent)
{
	const int mods = [pEvent modifierFlags];
	return IMouseMod(false, true, !!(mods & NSShiftKeyMask), !!(mods & NSControlKeyMask), !!(mods & NSAlternateKeyMask));
}

@implementation IGRAPHICS_COCOA

- (id) init
{
	mGraphics = NULL;
	mTimer = nil;
	mParamChangeTimer = 0;
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

		const int timer = mParamChangeTimer;
		if (timer && !(mParamChangeTimer = timer - 1))
		{
			mGraphics->GetPlug()->EndDelayedInformHostOfParamChange();
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
		if ([pEvent clickCount] > 1)
		{
			mGraphics->OnMouseDblClick(x, y, GetMouseMod(pEvent));
		}
		else
		{
			mGraphics->OnMouseDown(x, y, GetMouseMod(pEvent));
		}
	}
}

- (void) mouseUp: (NSEvent*)pEvent
{
	if (mGraphics)
	{
		int x, y;
		[self getMouseXY: pEvent x: &x y: &y];
		mGraphics->OnMouseUp(x, y, GetMouseMod(pEvent));
	}
}

- (void) mouseDragged: (NSEvent*)pEvent
{
	if (mGraphics)
	{
		int x, y;
		[self getMouseXY: pEvent x: &x y: &y];
		mGraphics->OnMouseDrag(x, y, GetMouseMod(pEvent));
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
		mGraphics->OnMouseDown(x, y, GetRightMouseMod(pEvent));
	}
}

- (void) rightMouseUp: (NSEvent*)pEvent
{
	if (mGraphics)
	{
		int x, y;
		[self getMouseXY: pEvent x: &x y: &y];
		mGraphics->OnMouseUp(x, y, GetRightMouseMod(pEvent));
	}
}

- (void) rightMouseDragged: (NSEvent*)pEvent
{
	if (mGraphics)
	{
		int x, y;
		[self getMouseXY: pEvent x: &x y: &y];
		mGraphics->OnMouseDrag(x, y, GetRightMouseMod(pEvent));
	}
}

- (void) mouseMoved: (NSEvent*)pEvent
{
	if (mGraphics)
	{
		int x, y;
		[self getMouseXY: pEvent x: &x y: &y];
		mGraphics->OnMouseOver(x, y, GetMouseMod(pEvent));
	}
}

- (void) scrollWheel: (NSEvent*)pEvent
{
	int canHandle;
	if (mGraphics && (canHandle = mGraphics->CanHandleMouseWheel()))
	{
		const IMouseMod mod = GetMouseMod(pEvent, canHandle >= 0);
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
		mGraphics = NULL;
		graphics->CloseWindow();
	}
}

- (void) controlTextDidEndEditing: (NSNotification*)aNotification
{
	const char* const txt = (const char*)[[mParamEditView stringValue] UTF8String];
	mGraphics->SetFromStringAfterPrompt(mEdControl, mEdParam, txt);

	[self endUserInput];
	[self viewDidMoveToWindow];
	[self setNeedsDisplay: YES];
}

static const int PARAM_EDIT_W = 42;
static const int PARAM_EDIT_H = 21;

- (void) promptUserInput: (IControl*)pControl param: (IParam*)pParam rect: (const IRECT*)pR size: (int)fontSize
{
	if (mParamEditView || !pControl || !pParam) return;

	char currentText[IGraphics::kMaxParamLen];
	pParam->GetDisplayForHost(currentText);

	static const int kPromptCustomHeight = IGraphics::kPromptCustomRect ^ IGraphics::kPromptCustomWidth;
	static const int scale = IGraphicsMac::kScaleOS;

	const int gh = mGraphics->Height() >> scale;
	if (!pR) pR = pControl->GetTargetRECT();

	int x, y, w, h;
	if (!(fontSize & IGraphics::kPromptCustomWidth))
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

	if (!(fontSize & kPromptCustomHeight))
	{
		const int cY = (pR->T + pR->B) >> scale;
		h = PARAM_EDIT_H;
		y = (cY + h) / 2;
	}
	else
	{
		h = pR->W() >> scale;
		y = (pR->T >> scale) + h;
	}

	fontSize &= ~IGraphics::kPromptCustomRect;
	const CGFloat sz = fontSize ? (CGFloat)(fontSize >> scale) : (CGFloat)PARAM_EDIT_H * (11.0f / 21.0f);

	const NSRect r = NSMakeRect((CGFloat)x, (CGFloat)(gh - y), (CGFloat)w, (CGFloat)h);

	mParamEditView = [[NSTextField alloc] initWithFrame: r];
	[mParamEditView setFont: [NSFont fontWithName: @"Arial" size: sz]];
	[mParamEditView setAlignment: NSCenterTextAlignment];
	[[mParamEditView cell] setLineBreakMode: NSLineBreakByTruncatingTail];
	[mParamEditView setStringValue: ToNSString(currentText)];

	[mParamEditView setDelegate: self];
	[self addSubview: mParamEditView];
	NSWindow* pWindow = [self window];
	[pWindow makeKeyAndOrderFront: nil];
	[pWindow makeFirstResponder: mParamEditView];

	mEdControl = pControl;
	mEdParam = pParam;
}

- (void) endUserInput
{
	[mParamEditView setDelegate: nil];
	[mParamEditView removeFromSuperview];
	mParamEditView = nil;
	mEdControl = NULL;
	mEdParam = NULL;
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

- (void) setParamChangeTimer: (int) ticks
{
  mParamChangeTimer = ticks;
}

- (void) cancelParamChangeTimer
{
  mParamChangeTimer = 0;
}

@end
