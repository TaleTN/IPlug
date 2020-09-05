#import <Cocoa/Cocoa.h>
#include <Carbon/Carbon.h>
#include <AudioUnit/AudioUnit.h>
#import <AudioUnit/AUCocoaUIView.h>
#include "IGraphicsMac.h"

// Cocoa objects can be supplied by any existing component, 
// so we need to make sure the C++ static lib code gets the 
// IGraphicsCocoa that it expects.
#ifndef IGRAPHICS_COCOA
	#define IGRAPHICS_COCOA IGraphicsCocoa_xxxxxxx
#endif

#if MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_5
  #if __LP64__ || NS_BUILD_32_LIKE_64
    typedef long NSInteger;
    typedef unsigned long NSUInteger;
  #else
    typedef int NSInteger;
    typedef unsigned int NSUInteger;
  #endif
#endif

NSString* ToNSString(const char* cStr);

inline CGRect ToCGRect(int h, IRECT* pR)
{
  int B = h - pR->B;
  return CGRectMake(pR->L, B, pR->W(), B + pR->H()); 
}

@interface IGRAPHICS_COCOA : NSView
{
  IGraphicsMac* mGraphics;
  NSTimer* mTimer;
  NSTextField* mParamEditView;
  // Ed = being edited manually.
  IControl* mEdControl;
  IParam* mEdParam;
  int mParamChangeTimer;
}
- (id) init;
- (id) initWithIGraphics: (IGraphicsMac*) pGraphics;
- (BOOL) isOpaque;
- (BOOL) acceptsFirstResponder;
- (BOOL) acceptsFirstMouse: (NSEvent*) pEvent;
- (void) viewDidMoveToWindow;
- (void) drawRect: (NSRect) rect;
- (void) onTimer: (NSTimer*) pTimer;
- (void) getMouseXY: (NSEvent*) pEvent x: (int*) pX y: (int*) pY;
- (void) mouseDown: (NSEvent*) pEvent;
- (void) mouseUp: (NSEvent*) pEvent;
- (void) mouseDragged: (NSEvent*) pEvent;
- (void) rightMouseDown: (NSEvent*) pEvent;
- (void) rightMouseUp: (NSEvent*) pEvent;
- (void) rightMouseDragged: (NSEvent*) pEvent;
- (void) mouseMoved: (NSEvent*) pEvent;
- (void) scrollWheel: (NSEvent*) pEvent;
- (void) killTimer;
- (void) controlTextDidChange: (NSNotification *) aNotification;
- (void) promptUserInput: (IControl*) pControl param: (IParam*) pParam;
- (void) promptUserInput: (IEditableTextControl*) pControl;
- (NSString*) view: (NSView*) pView stringForToolTip: (NSToolTipTag) tag point: (NSPoint) point userData: (void*) pData;
- (void) registerToolTip: (int) controlIdx rect: (IRECT*) pRECT;
- (void) setParamChangeTimer: (int) ticks;
- (void) cancelParamChangeTimer;
@end
