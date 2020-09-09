#import <Cocoa/Cocoa.h>
#import <AudioUnit/AUCocoaUIView.h>

#include "IGraphicsCocoa.h"
#include "resource.h" // This is your plugin's resource.h.

static NSString* ToNSString(const char* const cStr)
{
	return [NSString stringWithCString: cStr encoding: NSUTF8StringEncoding];
}

@interface VIEW_CLASS: NSObject <AUCocoaUIBase>
{
	IPlugBase* mPlug;
}
- (id) init;
- (NSView*) uiViewForAudioUnit: (AudioUnit)audioUnit withSize: (NSSize)preferredSize;
- (unsigned int) interfaceVersion;
- (NSString*) description;
@end

@implementation VIEW_CLASS

- (id) init
{
	mPlug = NULL;
	return [super init];
}

- (NSView*) uiViewForAudioUnit: (AudioUnit)audioUnit withSize: (NSSize)preferredSize
{
	mPlug = (IPlugBase*)GetComponentInstanceStorage(audioUnit);
	if (mPlug)
	{
		IGraphics* const pGraphics = mPlug->GetGUI();
		if (pGraphics)
		{
			IGRAPHICS_COCOA* const pView = (IGRAPHICS_COCOA*)pGraphics->OpenWindow(NULL);
			return pView;
		}
	}
	return nil;
}

- (unsigned int) interfaceVersion
{
	return 0;
}

- (NSString*) description
{
	return ToNSString(PLUG_NAME " View");
}

@end
