#include "IGraphicsCocoa.h"

inline NSRect ToNSRect(IGraphics* pGraphics, IRECT* pR) 
{
  int B = pGraphics->Height() - pR->B;
  return NSMakeRect(pR->L, B, pR->W(), pR->H()); 
}

inline IRECT ToIRECT(IGraphics* pGraphics, NSRect* pR) 
{
  int x = pR->origin.x, y = pR->origin.y, w = pR->size.width, h = pR->size.height, gh = pGraphics->Height();
  return IRECT(x, gh - (y + h), x + w, gh - y);
}

NSString* ToNSString(const char* cStr)
{
  return [NSString stringWithCString:cStr encoding:NSUTF8StringEncoding];
}

inline IMouseMod GetMouseMod(NSEvent* pEvent)
{
  int mods = [pEvent modifierFlags];
  return IMouseMod(true, (mods & NSCommandKeyMask), (mods & NSShiftKeyMask), (mods & NSControlKeyMask), (mods & NSAlternateKeyMask));
}

inline IMouseMod GetRightMouseMod(NSEvent* pEvent)
{
  int mods = [pEvent modifierFlags];
  return IMouseMod(false, true, (mods & NSShiftKeyMask), (mods & NSControlKeyMask), (mods & NSAlternateKeyMask));
}

inline void EndUserInput(IGRAPHICS_COCOA* pGraphicsCocoa)
{
  [pGraphicsCocoa->mParamEditView setDelegate: nil];
  [pGraphicsCocoa->mParamEditView removeFromSuperview];
  pGraphicsCocoa->mParamEditView = 0;
  pGraphicsCocoa->mEdControl = 0;
  pGraphicsCocoa->mEdParam = 0;
}

inline int GetMouseOver(IGraphicsMac* pGraphics)
{
	return pGraphics->GetMouseOver();
}

@implementation IGRAPHICS_COCOA

- (id) init
{
  TRACE;
  
  mGraphics = 0;
  mTimer = 0;
  return self;
}

- (id) initWithIGraphics: (IGraphicsMac*) pGraphics
{
  TRACE;
   
  mGraphics = pGraphics;
  NSRect r;
  r.origin.x = r.origin.y = 0.0f;
  r.size.width = (float) pGraphics->Width();
  r.size.height = (float) pGraphics->Height();
  self = [super initWithFrame:r];

  double sec = 1.0 / (double) pGraphics->FPS();
  mTimer = [NSTimer timerWithTimeInterval:sec target:self selector:@selector(onTimer:) userInfo:nil repeats:YES];  
  [[NSRunLoop currentRunLoop] addTimer: mTimer forMode: (NSString*) kCFRunLoopCommonModes];
  
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

- (BOOL) acceptsFirstMouse: (NSEvent*) pEvent
{
  return YES;
}

- (void) viewDidMoveToWindow
{
  NSWindow* pWindow = [self window];
  if (pWindow) {
    [pWindow makeFirstResponder: self];
    [pWindow setAcceptsMouseMovedEvents: YES];
  }
}

- (void) drawRect: (NSRect) rect 
{
  if (mGraphics) mGraphics->Draw(&ToIRECT(mGraphics, &rect));
}

- (void) onTimer: (NSTimer*) pTimer
{
  IRECT r;
  if (pTimer == mTimer && mGraphics && mGraphics->IsDirty(&r)) {
    [self setNeedsDisplayInRect:ToNSRect(mGraphics, &r)];
  }
}

- (void) getMouseXY: (NSEvent*) pEvent x: (int*) pX y: (int*) pY
{
  NSPoint pt = [self convertPoint:[pEvent locationInWindow] fromView:nil];
  *pX = (int) pt.x;
  *pY = mGraphics->Height() - (int) pt.y;
}

- (void) mouseDown: (NSEvent*) pEvent
{
  if (mGraphics)
  {
    if (mParamEditView) EndUserInput(self);
    int x, y;
    [self getMouseXY:pEvent x:&x y:&y];
    if ([pEvent clickCount] > 1) {
      mGraphics->OnMouseDblClick(x, y, &GetMouseMod(pEvent));
    }
    else {
      mGraphics->OnMouseDown(x, y, &GetMouseMod(pEvent));
    }
  }
}

- (void) mouseUp: (NSEvent*) pEvent
{
  if (mGraphics)
  {
    int x, y;
    [self getMouseXY:pEvent x:&x y:&y];
    mGraphics->OnMouseUp(x, y, &GetMouseMod(pEvent));
  }
}

- (void) mouseDragged: (NSEvent*) pEvent
{
  if (mGraphics)
  {
    int x, y;
    [self getMouseXY:pEvent x:&x y:&y];
    mGraphics->OnMouseDrag(x, y, &GetMouseMod(pEvent));
  }
}

- (void) rightMouseDown: (NSEvent*) pEvent
{
  if (mGraphics)
  {
    if (mParamEditView)
    {
      EndUserInput(self);
      return;
    }
    int x, y;
    [self getMouseXY:pEvent x:&x y:&y];
    mGraphics->OnMouseDown(x, y, &GetRightMouseMod(pEvent));
  }
}

- (void) rightMouseUp: (NSEvent*) pEvent
{
  if (mGraphics)
  {
    int x, y;
    [self getMouseXY:pEvent x:&x y:&y];
    mGraphics->OnMouseUp(x, y, &GetRightMouseMod(pEvent));
  }
}

- (void) rightMouseDragged: (NSEvent*) pEvent
{
  if (mGraphics)
  {
    int x, y;
    [self getMouseXY:pEvent x:&x y:&y];
    mGraphics->OnMouseDrag(x, y, &GetRightMouseMod(pEvent));
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
  int c = GetMouseOver(mGraphics);
  if (c < 0) return @"";

  const char* tooltip = mGraphics->GetControl(c)->GetTooltip();
  return CSTR_NOT_EMPTY(tooltip) ? ToNSString((const char*) tooltip) : @"";
}

- (void) registerToolTip: (IRECT*) pRECT
{
  [self addToolTipRect: ToNSRect(mGraphics, pRECT) owner: self userData: nil];
}

@end
