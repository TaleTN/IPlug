#import <Cocoa/Cocoa.h>
#include "IGraphicsMac.h"

// Cocoa objects can be supplied by any existing component,
// so we need to make sure the C++ static lib code gets the
// IGraphicsCocoa that it expects.
#ifndef IGRAPHICS_COCOA
	// You should add IGRAPHICS_COCOA=$(PRODUCT_NAME:identifier)_AU or VST
	// to GCC_PREPROCESSOR_DEFINITIONS under buildSettings in Xcode project.
	#error "IGRAPHICS_COCOA not defined!"

// TN: Dammit Jim, I'm a plugin not an app! so we really shouldn't need
// SWELL_APP_PREFIX, but just to be sure...
#elif !defined(SWELL_APP_PREFIX)
	#define SWELL_APP_PREFIX IGRAPHICS_COCOA
#endif

@interface IGRAPHICS_COCOA: NSView <NSTextFieldDelegate>
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
- (id) initWithIGraphics: (IGraphicsMac*)pGraphics;
- (BOOL) isOpaque;
- (BOOL) acceptsFirstResponder;
- (BOOL) acceptsFirstMouse: (NSEvent*)pEvent;
- (void) viewDidMoveToWindow;
- (void) drawRect: (NSRect)rect;
- (void) onTimer: (NSTimer*)pTimer;
- (void) getMouseXY: (NSEvent*)pEvent x: (int*)pX y: (int*)pY;
- (void) mouseDown: (NSEvent*)pEvent;
- (void) mouseUp: (NSEvent*)pEvent;
- (void) mouseDragged: (NSEvent*)pEvent;
- (void) rightMouseDown: (NSEvent*)pEvent;
- (void) rightMouseUp: (NSEvent*)pEvent;
- (void) rightMouseDragged: (NSEvent*)pEvent;
- (void) mouseMoved: (NSEvent*)pEvent;
- (void) scrollWheel: (NSEvent*)pEvent;
- (void) killTimer;
- (void) removeFromSuperview;
- (void) controlTextDidEndEditing: (NSNotification*)aNotification;
- (void) promptUserInput: (IControl*)pControl param: (IParam*)pParam rect: (const IRECT*)pR size: (int)fontSize;
- (void) endUserInput;
- (NSString*) view: (NSView*)pView stringForToolTip: (NSToolTipTag)tag point: (NSPoint)point userData: (void*)pData;
- (void) registerToolTip: (int) controlIdx rect: (IRECT*) pRECT;
- (void) setParamChangeTimer: (int)ticks;
- (void) cancelParamChangeTimer;
@end
