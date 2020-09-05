#ifndef _IGRAPHICSCARBON_
#define _IGRAPHICSCARBON_

#include <Carbon/Carbon.h>
#include "IGraphicsMac.h"

#ifndef IPLUG_NO_CARBON_SUPPORT

class IGraphicsCarbon
{
public:
  
  IGraphicsCarbon(IGraphicsMac* pGraphicsMac, WindowRef pWindow, ControlRef pParentControl);
  ~IGraphicsCarbon();
  
  ControlRef GetView() { return mView; }
  CGContextRef GetCGContext() { return mCGC; }
  void OffsetContentRect(CGRect* pR);
  bool Resize(int w, int h);
  void PromptUserInput(IControl* pControl, IParam* pParam);
  void PromptUserInput(IEditableTextControl* pControl);

protected:

  void InstallParamEditHandler(ControlRef control);
  void SetParamEditText(const char* txt);
  void ShowParamEditView();
  void EndUserInput(bool commit);

  void ShowTooltip();
  void HideTooltip();
  
  void SetParamChangeTimer(int ticks) { mParamChangeTimer = ticks; }
  void CancelParamChangeTimer() { mParamChangeTimer = 0; }

private:
  
  IGraphicsMac* mGraphicsMac;
  bool mIsComposited;
  int mContentXOffset, mContentYOffset;
  RgnHandle mRgn;
  WindowRef mWindow;
  ControlRef mView; // was HIViewRef
  EventLoopTimerRef mTimer;
  EventHandlerRef mControlHandler, mWindowHandler;
  CGContextRef mCGC;

  ControlRef mParamEditView;
  EventHandlerRef mParamEditHandler;
  // Ed = being edited manually.
  IControl* mEdControl;
  IParam* mEdParam;
  
  bool mShowingTooltip;
  int mTooltipIdx, mTooltipTimer;
  const char* mTooltip;

  int mParamChangeTimer;

public:
  
  static pascal OSStatus CarbonEventHandler(EventHandlerCallRef pHandlerCall, EventRef pEvent, void* pGraphicsCarbon);
  static pascal void CarbonTimerHandler(EventLoopTimerRef pTimer, void* pGraphicsCarbon);
  static pascal OSStatus CarbonParamEditHandler(EventHandlerCallRef pHandlerCall, EventRef pEvent, void* pGraphicsCarbon);

  friend class IGraphicsMac;
};

#endif // IPLUG_NO_CARBON_SUPPORT
#endif // _IGRAPHICSCARBON_
