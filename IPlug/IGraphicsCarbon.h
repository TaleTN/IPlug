#pragma once

#include "IGraphicsMac.h"

#ifndef IPLUG_NO_CARBON_SUPPORT

#include <Carbon/Carbon.h>

class IGraphicsCarbon
{
public:
	static const int kScaleFixed = IGraphicsMac::kScaleOS;

	IGraphicsCarbon(IGraphicsMac* pGraphicsMac, WindowRef pWindow, ControlRef pParentControl);
	~IGraphicsCarbon();

	inline ControlRef GetView() const { return mView; }
	inline CGContextRef GetCGContext() const { return mCGC; }

	void OffsetContentRect(CGRect* const pR) const
	{
		*pR = CGRectOffset(*pR, 0.0f, (CGFloat)mContentYOffset);
	}

	// bool Resize(int w, int h);
	bool PromptUserInput(IControl* pControl, IParam* pParam, const IRECT* pR = NULL, int flags = 0, int fontSize = 0);

protected:
	void InstallParamEditHandler(ControlRef control);
	void SetParamEditText(const char* txt);
	void ShowParamEditView();
	void EndUserInput(bool commit);

	void ShowTooltip();
	void HideTooltip();

	void SetParamChangeTimer(const int ticks) { mParamChangeTimer = ticks; }
	void CancelParamChangeTimer() { mParamChangeTimer = 0; }

private:
	IGraphicsMac* mGraphicsMac;
	bool mIsComposited;
	int mContentYOffset;
	WindowRef mWindow;
	ControlRef mView;
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
