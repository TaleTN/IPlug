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

inline void EndUserInput(IGRAPHICS_COCOA* pGraphicsCocoa)
{
  [pGraphicsCocoa->mParamEditView setDelegate: nil];
  [pGraphicsCocoa->mParamEditView removeFromSuperview];
  pGraphicsCocoa->mParamEditView = 0;
  pGraphicsCocoa->mEdControl = 0;
  pGraphicsCocoa->mEdParam = 0;
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
		if (mParamEditView) EndUserInput(self);
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
			EndUserInput(self);
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

- (void) mouseMoved: (NSEvent*) pEvent
{
  if (mGraphics)
  {
    int x, y;
    [self getMouseXY:pEvent x:&x y:&y];
    mGraphics->OnMouseOver(x, y, &GetMouseMod(pEvent));
  }
}

- (void) scrollWheel: (NSEvent*) pEvent
{
  if (mGraphics)
  {
    int x, y;
    [self getMouseXY:pEvent x:&x y:&y];
    int d = [pEvent deltaY];
    mGraphics->OnMouseWheel(x, y, &GetMouseMod(pEvent), d);
  }
}

- (void) killTimer
{
  [mTimer invalidate];
  mTimer = 0;
}

- (void) removeFromSuperview
{
  if (mParamEditView) EndUserInput(self);
  if (mGraphics)
  {
    IGraphics* graphics = mGraphics;
    mGraphics = 0;
    graphics->CloseWindow();
  }
}

- (void) controlTextDidEndEditing: (NSNotification*) aNotification
{
  char* txt = (char*)[[mParamEditView stringValue] UTF8String];

  NSInteger vi = -1;
  if ([mParamEditView respondsToSelector: @selector(indexOfSelectedItem)] == YES)
    vi = (NSInteger)[mParamEditView indexOfSelectedItem];
  if (vi != -1)
    mEdControl->SetValueFromUserInput(mEdParam->GetNormalized((double)vi));
  else
    mGraphics->SetFromStringAfterPrompt(mEdControl, mEdParam, txt);

  EndUserInput(self);
  [self viewDidMoveToWindow];
  [self setNeedsDisplay: YES];
}

#define PARAM_EDIT_W 42
#define PARAM_EDIT_H 21
#define PARAM_LIST_MIN_W 24
#define PARAM_LIST_W_PER_CHAR 8
#define PARAM_LIST_H 26

- (void) promptUserInput: (IControl*) pControl param: (IParam*) pParam
{
  if (!pControl || !pParam || mParamEditView) return;

  IRECT* pR = pControl->GetRECT();
  int cX = pR->MW(), cY = pR->MH();
  char currentText[MAX_PARAM_LEN];
  pParam->GetDisplayForHost(currentText);

  int n = pParam->GetNDisplayTexts();
  if (n && (pParam->Type() == IParam::kTypeEnum || pParam->Type() == IParam::kTypeBool))
  {
    int i, currentIdx = -1;
    int w = PARAM_LIST_MIN_W, h = PARAM_LIST_H;
    for (i = 0; i < n; ++i)
    {
      const char* str = pParam->GetDisplayText(i);
      w = MAX(w, PARAM_LIST_MIN_W + strlen(str) * PARAM_LIST_W_PER_CHAR);
      if (!strcmp(str, currentText)) currentIdx = i;
    }

    NSRect r = { cX - w/2, mGraphics->Height() - cY - h, w, h };
    mParamEditView = [[NSComboBox alloc] initWithFrame: r];
    [mParamEditView setFont: [NSFont fontWithName: @"Arial" size: 11.]];
    [mParamEditView setNumberOfVisibleItems: n];

    for (i = 0; i < n; ++i)
    {
      const char* str = pParam->GetDisplayText(i);
      [mParamEditView addItemWithObjectValue: ToNSString(str)];
    }
    [mParamEditView selectItemAtIndex: currentIdx];
  }
  else
  {
    const int w = PARAM_EDIT_W, h = PARAM_EDIT_H;
    NSRect r = { cX - w/2, mGraphics->Height() - cY - h/2, w, h };
    mParamEditView = [[NSTextField alloc] initWithFrame: r];
    [mParamEditView setFont: [NSFont fontWithName: @"Arial" size: 11.]];
    [mParamEditView setAlignment: NSCenterTextAlignment];
    [[mParamEditView cell] setLineBreakMode: NSLineBreakByTruncatingTail];
    [mParamEditView setStringValue: ToNSString(currentText)];
  }

  [mParamEditView setDelegate: self];
  [self addSubview: mParamEditView];
  NSWindow* pWindow = [self window];
  [pWindow makeKeyAndOrderFront:nil];
  [pWindow makeFirstResponder: mParamEditView];

  mEdControl = pControl;
  mEdParam = pParam;
}

- (void) promptUserInput: (IEditableTextControl*) pControl
{
  if (!pControl || mParamEditView) return;

  IRECT* pR = pControl->GetRECT();

  NSRect r = { pR->L, mGraphics->Height() - (pR->B + 3), pR->W(), pR->H() + 6 };
  if (pControl->IsSecure())
    mParamEditView = [[NSSecureTextField alloc] initWithFrame: r];
  else
    mParamEditView = [[NSTextField alloc] initWithFrame: r];
  if (!pControl->IsEditable())
  {
    [mParamEditView setEditable: NO]; 
    [mParamEditView setSelectable: YES];
  }

  const IText* txt = pControl->GetIText();
  [mParamEditView setFont: [NSFont fontWithName: ToNSString(txt->mFont) size: AdjustFontSize(txt->mSize)]];
  NSTextAlignment align;
  switch (txt->mAlign)
  {
    case IText::kAlignNear:   align = NSLeftTextAlignment;   break;
    case IText::kAlignFar:    align = NSRightTextAlignment;  break;
    case IText::kAlignCenter:
    default:                  align = NSCenterTextAlignment; break;
  }
  [mParamEditView setAlignment: align];
  [[mParamEditView cell] setLineBreakMode: NSLineBreakByTruncatingTail];
  [mParamEditView setStringValue: ToNSString(pControl->GetText())];

  [mParamEditView setDelegate: self];
  [self addSubview: mParamEditView];
  NSWindow* pWindow = [self window];
  [pWindow makeKeyAndOrderFront:nil];
  [pWindow makeFirstResponder: mParamEditView];

  mEdControl = pControl;
  mEdParam = 0;
}

- (NSString*) view: (NSView*) pView stringForToolTip: (NSToolTipTag) tag point: (NSPoint) point userData: (void*) pData
{
  int c = (long) pData;
  IControl* pControl = mGraphics->GetControl(c);
  const char* tooltip = pControl->GetTooltip();
  return CSTR_NOT_EMPTY(tooltip) && !pControl->IsHidden() && !pControl->IsGrayed() ? ToNSString((const char*) tooltip) : @"";
}

- (void) registerToolTip: (int) controlIdx rect: (IRECT*) pRECT
{
  [self addToolTipRect: ToNSRect(mGraphics, pRECT) owner: self userData: (void*)(long) controlIdx];
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
