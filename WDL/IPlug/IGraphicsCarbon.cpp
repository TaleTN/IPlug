#include "IGraphicsCarbon.h"
#ifndef IPLUG_NO_CARBON_SUPPORT

IRECT GetRegionRect(EventRef pEvent, int gfxW, int gfxH)
{
  RgnHandle pRgn = 0;
  if (GetEventParameter(pEvent, kEventParamRgnHandle, typeQDRgnHandle, 0, sizeof(RgnHandle), 0, &pRgn) == noErr && pRgn) {
    Rect rct;
    GetRegionBounds(pRgn, &rct);
    return IRECT(rct.left, rct.top, rct.right, rct.bottom); 
  }
  return IRECT(0, 0, gfxW, gfxH);
}

IRECT GetControlRect(EventRef pEvent, int gfxW, int gfxH)
{
  Rect rct;
  if (GetEventParameter(pEvent, kEventParamCurrentBounds, typeQDRectangle, 0, sizeof(Rect), 0, &rct) == noErr) {
    int w = rct.right - rct.left;
    int h = rct.bottom - rct.top;
    if (w > 0 && h > 0) {
      return IRECT(0, 0, w, h);
    }
  }  
  return IRECT(0, 0, gfxW, gfxH);
}

// static 
pascal OSStatus IGraphicsCarbon::CarbonEventHandler(EventHandlerCallRef pHandlerCall, EventRef pEvent, void* pGraphicsCarbon)
{
  IGraphicsCarbon* _this = (IGraphicsCarbon*) pGraphicsCarbon;
  IGraphicsMac* pGraphicsMac = _this->mGraphicsMac;
  UInt32 eventClass = GetEventClass(pEvent);
  UInt32 eventKind = GetEventKind(pEvent);
  switch (eventClass) {      
    case kEventClassControl: {
      switch (eventKind) {          
        case kEventControlDraw: {
          
          int gfxW = pGraphicsMac->Width(), gfxH = pGraphicsMac->Height();
          IRECT r = GetRegionRect(pEvent, gfxW, gfxH);  
          
          CGrafPtr port = 0;
          if (_this->mIsComposited) {
            GetEventParameter(pEvent, kEventParamCGContextRef, typeCGContextRef, 0, sizeof(CGContextRef), 0, &(_this->mCGC));         
            CGContextTranslateCTM(_this->mCGC, 0, gfxH);
            CGContextScaleCTM(_this->mCGC, 1.0, -1.0);     
            //pGraphicsMac->Draw(&r);
          }
          else {
            #pragma REMINDER("not swapping gfx ports in non-composited mode")
            
            int ctlH = r.H();
            _this->mContentYOffset = ctlH - gfxH;
            
            //Rect rct;          
            //GetWindowBounds(_this->mWindow, kWindowContentRgn, &rct);
            //int wndH = rct.bottom - rct.top;
            //if (wndH > gfxH) {
            //  int yOffs = wndH - gfxH;
            //  _this->mContentYOffset = yOffs;
            // }
            
            GetEventParameter(pEvent, kEventParamGrafPort, typeGrafPtr, 0, sizeof(CGrafPtr), 0, &port);
            QDBeginCGContext(port, &(_this->mCGC));        
            // Old-style controls drawing, ask the plugin what's dirty rather than relying on the OS.
            r.R = r.T = r.R = r.B = 0;
            _this->mGraphicsMac->IsDirty(&r);         
          }       
          
          pGraphicsMac->Draw(&r);
            
          if (port) {
            CGContextFlush(_this->mCGC);
            QDEndCGContext(port, &(_this->mCGC));
          }
                     
          return noErr;
        }  
        case kEventControlBoundsChanged: {        
          int gfxW = pGraphicsMac->Width(), gfxH = pGraphicsMac->Height();
          IRECT r = GetControlRect(pEvent, gfxW, gfxH);
          //pGraphicsMac->GetPlug()->UserResizedWindow(&r);
          return noErr;
        }        
        case kEventControlDispose: {
          // kComponentCloseSelect call should already have done this for us (and deleted mGraphicsMac, for that matter).
          // pGraphicsMac->CloseWindow();
          return noErr;
        }
      }
      break;
    }
    case kEventClassMouse: {
      HIPoint hp;
      GetEventParameter(pEvent, kEventParamWindowMouseLocation, typeHIPoint, 0, sizeof(HIPoint), 0, &hp);
      HIPointConvert(&hp, kHICoordSpaceWindow, _this->mWindow, kHICoordSpaceView, _this->mView);
      int x = (int) hp.x;
      int y = (int) hp.y;

      UInt32 mods;
      GetEventParameter(pEvent, kEventParamKeyModifiers, typeUInt32, 0, sizeof(UInt32), 0, &mods);
      EventMouseButton button;
      GetEventParameter(pEvent, kEventParamMouseButton, typeMouseButton, 0, sizeof(EventMouseButton), 0, &button);
      if (button == kEventMouseButtonPrimary && (mods & cmdKey)) button = kEventMouseButtonSecondary;
      IMouseMod mmod(true, button == kEventMouseButtonSecondary, (mods & shiftKey), (mods & controlKey), (mods & optionKey));
      
      switch (eventKind) {
        case kEventMouseDown: {
          _this->HideTooltip();
          if (_this->mParamEditView)
          {
            HIViewRef view;
            HIViewGetViewForMouseEvent(_this->mView, pEvent, &view);
            if (view == _this->mParamEditView) break;
            if (button == kEventMouseButtonSecondary)
            {
              _this->EndUserInput(false);
              break;
            }
            _this->EndUserInput(true);
          }

          CallNextEventHandler(pHandlerCall, pEvent);   // Activates the window, if inactive.
          
          UInt32 clickCount = 0;
          GetEventParameter(pEvent, kEventParamClickCount, typeUInt32, 0, sizeof(UInt32), 0, &clickCount);
          if (clickCount > 1) {
            pGraphicsMac->OnMouseDblClick(x, y, &mmod);
          }
          else {          
            pGraphicsMac->OnMouseDown(x, y, &mmod);
          }
          return noErr;
        }
        case kEventMouseUp: {
          pGraphicsMac->OnMouseUp(x, y, &mmod);
          return noErr;
        }
        case kEventMouseMoved: {
          pGraphicsMac->OnMouseOver(x, y, &mmod);

          if (pGraphicsMac->TooltipsEnabled()) {
            int c = pGraphicsMac->GetMouseOver();
            if (c != _this->mTooltipIdx) {
              _this->mTooltipIdx = c;
              _this->HideTooltip();
              const char* tooltip = c >= 0 ? pGraphicsMac->GetControl(c)->GetTooltip() : NULL;
              if (CSTR_NOT_EMPTY(tooltip)) {
                _this->mTooltip = tooltip;
                _this->mTooltipTimer = pGraphicsMac->FPS() * 3 / 2; // 1.5 seconds
              }
            }
          }
          return noErr;
        }
        case kEventMouseDragged: {
          pGraphicsMac->OnMouseDrag(x, y, &mmod);
          return noErr; 
        }
        case kEventMouseWheelMoved: {
          EventMouseWheelAxis axis;
          GetEventParameter(pEvent, kEventParamMouseWheelAxis, typeMouseWheelAxis, 0, sizeof(EventMouseWheelAxis), 0, &axis);
          if (axis == kEventMouseWheelAxisY) {
            int d;
            GetEventParameter(pEvent, kEventParamMouseWheelDelta, typeSInt32, 0, sizeof(SInt32), 0, &d);
            pGraphicsMac->OnMouseWheel(x, y, &mmod, d);
            return noErr;
          }
        }   
      }
      break;    
    }
  }
  return eventNotHandledErr;
}    

// static 
pascal void IGraphicsCarbon::CarbonTimerHandler(EventLoopTimerRef pTimer, void* pGraphicsCarbon)
{
  IGraphicsCarbon* _this = (IGraphicsCarbon*) pGraphicsCarbon;
  IRECT r;
  if (_this->mGraphicsMac->IsDirty(&r)) {
    if (_this->mIsComposited) {
      HIViewSetNeedsDisplayInRect(_this->mView, &CGRectMake(r.L, r.T, r.W(), r.H()), true);
    }
    else {
      int h = _this->mGraphicsMac->Height();
      SetRectRgn(_this->mRgn, r.L, h - r.B, r.R, h - r.T);
      UpdateControls(_this->mWindow, 0);// _this->mRgn);
    }
  } 

  if (_this->mTooltipTimer) {
    if (!--_this->mTooltipTimer) {
      if (!_this->mShowingTooltip) {
        _this->ShowTooltip();
        _this->mTooltipTimer = _this->mGraphicsMac->FPS() * 10; // 10 seconds
      }
      else {
        _this->HideTooltip();
      }
    }
  }
}

// static
pascal OSStatus IGraphicsCarbon::CarbonParamEditHandler(EventHandlerCallRef pHandlerCall, EventRef pEvent, void* pGraphicsCarbon)
{
  IGraphicsCarbon* _this = (IGraphicsCarbon*) pGraphicsCarbon;
  UInt32 eventClass = GetEventClass(pEvent);
  UInt32 eventKind = GetEventKind(pEvent);

  _this->HideTooltip();

  switch (eventClass)
  {
    case kEventClassKeyboard:
    {
      switch (eventKind)
      {
        case kEventRawKeyDown:
        case kEventRawKeyRepeat:
        {
          char ch;
          GetEventParameter(pEvent, kEventParamKeyMacCharCodes, typeChar, NULL, sizeof(ch), NULL, &ch);
          if (ch == 13)
          {
            _this->EndUserInput(true);
            return noErr;
          }
          break;
        }
      }
      break;
    }
  }
  return eventNotHandledErr;
}

void ResizeWindow(WindowRef pWindow, int w, int h)
{
  Rect gr;  // Screen.
  GetWindowBounds(pWindow, kWindowContentRgn, &gr);
  gr.right = gr.left + w;
  gr.bottom = gr.top + h;
  SetWindowBounds(pWindow, kWindowContentRgn, &gr); 
}

IGraphicsCarbon::IGraphicsCarbon(IGraphicsMac* pGraphicsMac, WindowRef pWindow, ControlRef pParentControl)
: mGraphicsMac(pGraphicsMac), mWindow(pWindow), mView(0), mTimer(0), mControlHandler(0), mWindowHandler(0), mCGC(0),
  mContentXOffset(0), mContentYOffset(0), mParamEditView(0), mParamEditHandler(0), mEdControl(0), mEdParam(0),
  mShowingTooltip(false), mTooltipIdx(-1), mTooltipTimer(0)
{ 
  TRACE;
  
  Rect r;   // Client.
  r.left = r.top = 0;
  r.right = pGraphicsMac->Width();
  r.bottom = pGraphicsMac->Height();   
  //ResizeWindow(pWindow, r.right, r.bottom);

  WindowAttributes winAttrs = 0;
  GetWindowAttributes(pWindow, &winAttrs);
  mIsComposited = (winAttrs & kWindowCompositingAttribute);
  mRgn = NewRgn();  
  
  UInt32 features =  kControlSupportsFocus | kControlHandlesTracking | kControlSupportsEmbedding;
  if (mIsComposited) {
    features |= kHIViewIsOpaque | kHIViewFeatureDoesNotUseSpecialParts;
  }
  CreateUserPaneControl(pWindow, &r, features, &mView);    
  
  const EventTypeSpec controlEvents[] = {	
    //{ kEventClassControl, kEventControlInitialize },
    //{kEventClassControl, kEventControlGetOptimalBounds},    
    //{ kEventClassControl, kEventControlHitTest },
    { kEventClassControl, kEventControlClick },
    //{ kEventClassKeyboard, kEventRawKeyDown },
    { kEventClassControl, kEventControlDraw },
    { kEventClassControl, kEventControlDispose },
    { kEventClassControl, kEventControlBoundsChanged }
  };
  InstallControlEventHandler(mView, CarbonEventHandler, GetEventTypeCount(controlEvents), controlEvents, this, &mControlHandler);
  
  const EventTypeSpec windowEvents[] = {
    { kEventClassMouse, kEventMouseDown },
    { kEventClassMouse, kEventMouseUp },
    { kEventClassMouse, kEventMouseMoved },
    { kEventClassMouse, kEventMouseDragged },
    { kEventClassMouse, kEventMouseWheelMoved }
  };
  InstallWindowEventHandler(mWindow, CarbonEventHandler, GetEventTypeCount(windowEvents), windowEvents, this, &mWindowHandler);  
  
  double t = kEventDurationSecond/(double)pGraphicsMac->FPS();
  OSStatus s = InstallEventLoopTimer(GetMainEventLoop(), 0.0, t, CarbonTimerHandler, this, &mTimer);
  
  if (mIsComposited) {
    if (!pParentControl) {
      HIViewRef hvRoot = HIViewGetRoot(pWindow);
      s = HIViewFindByID(hvRoot, kHIViewWindowContentID, &pParentControl); 
    }  
    s = HIViewAddSubview(pParentControl, mView);
  }
  else {
    if (!pParentControl) {
      if (GetRootControl(pWindow, &pParentControl) != noErr) {
        CreateRootControl(pWindow, &pParentControl);
      }
    }
    s = EmbedControl(mView, pParentControl); 
  }

  if (s == noErr) {
    SizeControl(mView, r.right, r.bottom);  // offset?
  }
}

IGraphicsCarbon::~IGraphicsCarbon()
{
  // Called from IGraphicsMac::CloseWindow.
  if (mParamEditView)
  {
    RemoveEventHandler(mParamEditHandler);
    mParamEditHandler = 0;
    HIViewRemoveFromSuperview(mParamEditView);
    mParamEditView = 0;
    mEdControl = 0;
    mEdParam = 0;
  }
  HideTooltip();
  RemoveEventLoopTimer(mTimer);
  RemoveEventHandler(mControlHandler);
  RemoveEventHandler(mWindowHandler);
  mTimer = 0;
  mView = 0;
  DisposeRgn(mRgn);
}

void IGraphicsCarbon::OffsetContentRect(CGRect* pR)
{
  *pR = CGRectOffset(*pR, (float) mContentXOffset, (float) mContentYOffset);
}

bool IGraphicsCarbon::Resize(int w, int h)
{
  if (mWindow && mView) {
    ResizeWindow(mWindow, w, h);
    return (HIViewSetFrame(mView, &CGRectMake(0, 0, w, h)) == noErr);
  }
  return false;
}

#define PARAM_EDIT_W 32
#define PARAM_EDIT_H 14
#define PARAM_LIST_MIN_W 24
#define PARAM_LIST_W_PER_CHAR 8
#define PARAM_LIST_H 22

void IGraphicsCarbon::PromptUserInput(IControl* pControl, IParam* pParam)
{
  if (!pControl || !pParam || mParamEditView) return;

  IRECT* pR = pControl->GetRECT();
  int cX = pR->MW(), cY = pR->MH();
  char currentText[MAX_PARAM_LEN];
  pParam->GetDisplayForHost(currentText);

  ControlRef control = 0;
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

    HIRect r = CGRectMake(cX - w/2, cY, w, h);
    if (HIComboBoxCreate(&r, NULL, NULL, NULL, kHIComboBoxStandardAttributes, &control) != noErr) return;

    for (i = 0; i < n; ++i)
    {
      CFStringRef str = CFStringCreateWithCString(NULL, pParam->GetDisplayText(i), kCFStringEncodingUTF8);
      if (str)
      {
        HIComboBoxAppendTextItem(control, str, NULL);
        CFRelease(str);
      }
    }
  }
  else
  {
    const int w = PARAM_EDIT_W, h = PARAM_EDIT_H;
    Rect r = { cY - h/2, cX - w/2, cY + h/2, cX + w/2 };
    if (CreateEditUnicodeTextControl(NULL, &r, NULL, false, NULL, &control) != noErr) return;
  }
  HIViewAddSubview(mView, control);

  InstallParamEditHandler(control);
  mParamEditView = control;
  SetParamEditText(currentText);

  ControlFontStyleRec font = { kControlUseJustMask | kControlUseSizeMask | kControlUseFontMask, 0, 11, 0, 0, teCenter, 0, 0 };
  font.font = ATSFontFamilyFindFromName(CFSTR("Arial"), kATSOptionFlagsDefault);
  SetControlData(mParamEditView, kControlEditTextPart, kControlFontStyleTag, sizeof(font), &font);

  ShowParamEditView();
  mEdControl = pControl;
  mEdParam = pParam;
}

void IGraphicsCarbon::PromptUserInput(IEditableTextControl* pControl)
{
  if (!pControl || mParamEditView) return;

  IRECT* pR = pControl->GetRECT();

  ControlRef control = 0;
  Rect r = { pR->T, pR->L, pR->B, pR->R };
  if (CreateEditUnicodeTextControl(NULL, &r, NULL, pControl->IsSecure(), NULL, &control) != noErr) return;
  HIViewAddSubview(mView, control);

  InstallParamEditHandler(control);
  mParamEditView = control;
  SetParamEditText(pControl->GetText());
  if (!pControl->IsEditable())
  {
    const Boolean locked = true;
    SetControlData(mParamEditView, kControlEntireControl, kControlEditTextLockedTag, sizeof(locked), &locked);
  }

  const IText* txt = pControl->GetIText();
  //SInt16 style;
  //switch (txt->mStyle)
  //{
  //  case IText::kStyleBold:   style = 1; break;
  //  case IText::kStyleItalic: style = 2; break;
  //  case IText::kStyleNormal:
  //  default:                  style = 0; break;
  //}
  SInt16 just;
  switch (txt->mAlign)
  {
    case IText::kAlignNear:   just = teFlushLeft;  break;
    case IText::kAlignFar:    just = teFlushRight; break;
    case IText::kAlignCenter:
    default:                  just = teCenter;     break;
  }
  ControlFontStyleRec font = { kControlUseJustMask | kControlUseSizeMask | kControlUseFontMask, 0, AdjustFontSize(txt->mSize), 0, 0, just, 0, 0 };
  CFStringRef str = CFStringCreateWithCString(NULL, txt->mFont, kCFStringEncodingUTF8);
  font.font = ATSFontFamilyFindFromName(str ? str : CFSTR("Arial"), kATSOptionFlagsDefault);
  SetControlData(mParamEditView, kControlEditTextPart, kControlFontStyleTag, sizeof(font), &font);
  if (str) CFRelease(str);

  ShowParamEditView();
  mEdControl = pControl;
  mEdParam = 0;
}

void IGraphicsCarbon::InstallParamEditHandler(ControlRef control)
{
  const EventTypeSpec events[] = {
    { kEventClassKeyboard, kEventRawKeyDown },
    { kEventClassKeyboard, kEventRawKeyRepeat }
  };
  InstallControlEventHandler(control, CarbonParamEditHandler, GetEventTypeCount(events), events, this, &mParamEditHandler);
}

void IGraphicsCarbon::SetParamEditText(const char* txt)
{
  if (txt && txt[0] != '\0')
  {
    CFStringRef str = CFStringCreateWithCString(NULL, txt, kCFStringEncodingUTF8);
    if (str)
    {
      SetControlData(mParamEditView, kControlEditTextPart, kControlEditTextCFStringTag, sizeof(str), &str);
      CFRelease(str);
    }
    ControlEditTextSelectionRec sel;
    sel.selStart = 0;
    sel.selEnd = strlen(txt);
    SetControlData(mParamEditView, kControlEditTextPart, kControlEditTextSelectionTag, sizeof(sel), &sel);
  }
}

void IGraphicsCarbon::ShowParamEditView()
{
  HIViewSetVisible(mParamEditView, true);
  HIViewAdvanceFocus(mParamEditView, 0);
  SetKeyboardFocus(mWindow, mParamEditView, kControlEditTextPart);
  SetUserFocusWindow(mWindow);
}

void IGraphicsCarbon::EndUserInput(bool commit)
{
  RemoveEventHandler(mParamEditHandler);
  mParamEditHandler = 0;

  if (commit)
  {
    CFStringRef str;
    if (GetControlData(mParamEditView, kControlEditTextPart, kControlEditTextCFStringTag, sizeof(str), &str, NULL) == noErr)
    {
      char txt[MAX_EDIT_LEN];
      CFStringGetCString(str, txt, MAX_EDIT_LEN, kCFStringEncodingUTF8);
      mGraphicsMac->SetFromStringAfterPrompt(mEdControl, mEdParam, txt);
      CFRelease(str);
    }
  }

  HIViewSetVisible(mParamEditView, false);
  HIViewRemoveFromSuperview(mParamEditView);
  if (mIsComposited)
  {
    //IRECT* pR = mEdControl->GetRECT();
    //HIViewSetNeedsDisplayInRect(mView, &CGRectMake(pR->L, pR->T, pR->W(), pR->H()), true);
    HIViewSetNeedsDisplay(mView, true);
  }
  else
  {
    mEdControl->SetDirty(false);
    mEdControl->Redraw();
  }
  SetThemeCursor(kThemeArrowCursor);
  SetUserFocusWindow(kUserFocusAuto);

  mParamEditView = 0;
  mEdControl = 0;
  mEdParam = 0;
}

void IGraphicsCarbon::ShowTooltip()
{
  HMHelpContentRec helpTag;
  helpTag.version = kMacHelpVersion;

  helpTag.tagSide = kHMInsideTopLeftCorner;
  HIRect r = CGRectMake(mGraphicsMac->GetMouseX(), mGraphicsMac->GetMouseY() + mGraphicsMac->GetMouseCursorYOffset() + 3, 1, 1);
  HIRectConvert(&r, kHICoordSpaceView, mView, kHICoordSpaceScreenPixel, NULL);
  helpTag.absHotRect.top = (int)r.origin.y;
  helpTag.absHotRect.left = (int)r.origin.x;
  helpTag.absHotRect.bottom = helpTag.absHotRect.top + (int)r.size.height;
  helpTag.absHotRect.right = helpTag.absHotRect.left + (int)r.size.width;

  helpTag.content[kHMMinimumContentIndex].contentType = kHMCFStringLocalizedContent;
  helpTag.content[kHMMinimumContentIndex].u.tagCFString = CFStringCreateWithCString(NULL, mTooltip, kCFStringEncodingUTF8);
  helpTag.content[kHMMaximumContentIndex].contentType = kHMNoContent;
  HMDisplayTag(&helpTag);
  mShowingTooltip = true;
}

void IGraphicsCarbon::HideTooltip()
{
  mTooltipTimer = 0;
  if (mShowingTooltip) {
    HMHideTag();
    mShowingTooltip = false;
  }
}

#endif // IPLUG_NO_CARBON_SUPPORT
