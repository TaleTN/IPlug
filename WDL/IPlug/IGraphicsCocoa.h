#import <Cocoa/Cocoa.h>
#include <Carbon/Carbon.h>
#include <AudioUnit/AudioUnit.h>
#import <AudioUnit/AUCocoaUIView.h>
#include "IGraphicsMac.h"

// Cocoa objects can be supplied by any existing component, 
// so we need to make sure the C++ static lib code gets the 
// IGraphicsCocoa that it expects.
#define IGRAPHICS_COCOA IGraphicsCocoa_v1002_C599B068

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
@end
