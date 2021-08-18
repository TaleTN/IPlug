#include "IGraphicsCarbon.h"

#ifndef IPLUG_NO_CARBON_SUPPORT

static IRECT GetRegionRect(EventRef const pEvent, const int gfxW, const int gfxH)
{
	RgnHandle pRgn = NULL;
	if (GetEventParameter(pEvent, kEventParamRgnHandle, typeQDRgnHandle, NULL, sizeof(RgnHandle), NULL, &pRgn) == noErr && pRgn)
	{
		Rect rct;
		GetRegionBounds(pRgn, &rct);
		return IRECT(rct.left, rct.top, rct.right, rct.bottom);
	}
	return IRECT(0, 0, gfxW, gfxH);
}

/* static IRECT GetControlRect(EventRef const pEvent, const int gfxW, const int gfxH)
{
	Rect rct;
	if (GetEventParameter(pEvent, kEventParamCurrentBounds, typeQDRectangle, NULL, sizeof(Rect), NULL, &rct) == noErr)
	{
		const int w = rct.right - rct.left;
		const int h = rct.bottom - rct.top;
		if (w > 0 && h > 0)
		{
			return IRECT(0, 0, w, h);
		}
	}
	return IRECT(0, 0, gfxW, gfxH);
} */

// static
pascal OSStatus IGraphicsCarbon::CarbonEventHandler(EventHandlerCallRef const pHandlerCall, EventRef const pEvent, void* const pGraphicsCarbon)
{
	IGraphicsCarbon* const _this = (IGraphicsCarbon*)pGraphicsCarbon;
	IGraphicsMac* const pGraphicsMac = _this->mGraphicsMac;
	const UInt32 eventClass = GetEventClass(pEvent);
	const UInt32 eventKind = GetEventKind(pEvent);
	switch (eventClass)
	{
		case kEventClassControl:
		{
			switch (eventKind)
			{
				case kEventControlDraw:
				{
					const int gfxW = pGraphicsMac->Width() >> kScaleFixed;
					const int gfxH = pGraphicsMac->Height() >> kScaleFixed;
					IRECT r = GetRegionRect(pEvent, gfxW, gfxH);

					CGrafPtr port = NULL;
					if (_this->mIsComposited)
					{
						r.Upscale(kScaleFixed);
						GetEventParameter(pEvent, kEventParamCGContextRef, typeCGContextRef, NULL, sizeof(CGContextRef), NULL, &_this->mCGC);
						CGContextTranslateCTM(_this->mCGC, 0.0f, (CGFloat)gfxH);
						CGContextScaleCTM(_this->mCGC, 1.0f, -1.0f);
						// pGraphicsMac->Draw(&r);
					}
					else
					{
						// Reminder: Not swapping gfx ports in non-composited mode.

						const int ctlH = r.H();
						_this->mContentYOffset = ctlH - gfxH;

						GetEventParameter(pEvent, kEventParamGrafPort, typeGrafPtr, NULL, sizeof(CGrafPtr), NULL, &port);
						QDBeginCGContext(port, &_this->mCGC);
						// Old-style controls drawing, ask the plugin what's dirty rather than relying on the OS.
						r.Clear();
						_this->mGraphicsMac->IsDirty(&r);
					}

					pGraphicsMac->Draw(&r);

					if (port)
					{
						CGContextFlush(_this->mCGC);
						QDEndCGContext(port, &_this->mCGC);
					}

					return noErr;
				}
				case kEventControlBoundsChanged:
				{
					// const int gfxW = pGraphicsMac->Width() >> kScaleFixed;
					// const int gfxH = pGraphicsMac->Height() >> kScaleFixed;
					// IRECT r = GetControlRect(pEvent, gfxW, gfxH);
					// r.Upscale(scale);
					// pGraphicsMac->GetPlug()->UserResizedWindow(&r);
					return noErr;
				}
				case kEventControlDispose:
				{
					// kComponentCloseSelect call should already have done this for us (and deleted mGraphicsMac, for that matter).
					// pGraphicsMac->CloseWindow();
					return noErr;
				}
			}
			break;
		}

		case kEventClassMouse:
		{
			HIPoint hp;
			GetEventParameter(pEvent, kEventParamWindowMouseLocation, typeHIPoint, NULL, sizeof(HIPoint), NULL, &hp);
			HIPointConvert(&hp, kHICoordSpaceWindow, _this->mWindow, kHICoordSpaceView, _this->mView);
			const CGFloat scale = (CGFloat)(1 << kScaleFixed);
			const int x = (int)(hp.x * scale);
			const int y = (int)(hp.y * scale);

			UInt32 mods;
			GetEventParameter(pEvent, kEventParamKeyModifiers, typeUInt32, NULL, sizeof(UInt32), NULL, &mods);
			EventMouseButton button;
			GetEventParameter(pEvent, kEventParamMouseButton, typeMouseButton, NULL, sizeof(EventMouseButton), NULL, &button);
			if (button == kEventMouseButtonPrimary && (mods & cmdKey)) button = kEventMouseButtonSecondary;

			const bool right = button == kEventMouseButtonSecondary, left = !right;
			IMouseMod mmod(left, right, !!(mods & shiftKey), !!(mods & controlKey), !!(mods & optionKey));

			switch (eventKind)
			{
				case kEventMouseDown:
				{
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

					CallNextEventHandler(pHandlerCall, pEvent); // Activates the window, if inactive.

					UInt32 clickCount = 0;
					GetEventParameter(pEvent, kEventParamClickCount, typeUInt32, NULL, sizeof(UInt32), NULL, &clickCount);
					if (clickCount > 1)
					{
						pGraphicsMac->OnMouseDblClick(x, y, mmod);
					}
					else
					{
						pGraphicsMac->OnMouseDown(x, y, mmod);
					}
					return noErr;
				}
				case kEventMouseUp:
				{
					pGraphicsMac->OnMouseUp(x, y, mmod);
					return noErr;
				}
				case kEventMouseMoved:
				{
					pGraphicsMac->OnMouseOver(x, y, mmod);

					if (pGraphicsMac->TooltipsEnabled())
					{
						const int c = pGraphicsMac->GetMouseOver();
						if (c != _this->mTooltipIdx)
						{
							_this->mTooltipIdx = c;
							_this->HideTooltip();
							const char* tooltip = c >= 0 ? pGraphicsMac->GetControl(c)->GetTooltip() : NULL;
							if (tooltip && *tooltip)
							{
								_this->mTooltip = tooltip;
								_this->mTooltipTimer = (pGraphicsMac->FPS() * 3) / 2; // 1.5 seconds
							}
						}
					}
					return noErr;
				}
				case kEventMouseDragged:
				{
					pGraphicsMac->OnMouseDrag(x, y, mmod);
					return noErr;
				}
				case kEventMouseWheelMoved:
				{
					EventMouseWheelAxis axis;
					GetEventParameter(pEvent, kEventParamMouseWheelAxis, typeMouseWheelAxis, NULL, sizeof(EventMouseWheelAxis), NULL, &axis);
					if (axis == kEventMouseWheelAxisY)
					{
						const int canHandle = pGraphicsMac->CanHandleMouseWheel();
						if (canHandle)
						{
							mmod.W = canHandle >= 0;
							const IMouseMod mask(false, false, true, true, true, true);

							if (mmod.Get() & mask.Get())
							{
								SInt32 d;
								GetEventParameter(pEvent, kEventParamMouseWheelDelta, typeSInt32, NULL, sizeof(SInt32), NULL, &d);
								pGraphicsMac->OnMouseWheel(x, y, mmod, (float)d);
							}
						}
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
pascal void IGraphicsCarbon::CarbonTimerHandler(EventLoopTimerRef /* pTimer */, void* const pGraphicsCarbon)
{
	IGraphicsCarbon* const _this = (IGraphicsCarbon*)pGraphicsCarbon;
	IGraphicsMac* const pGraphicsMac = _this->mGraphicsMac;
	IRECT r;
	if (pGraphicsMac->IsDirty(&r))
	{
		if (_this->mIsComposited)
		{
			const int x = r.L >> kScaleFixed;
			const int y = r.T >> kScaleFixed;
			const int w = r.W() >> kScaleFixed;
			const int h = r.H() >> kScaleFixed;
			const HIRect hr = CGRectMake((CGFloat)x, (CGFloat)y, (CGFloat)w, (CGFloat)h);
			HIViewSetNeedsDisplayInRect(_this->mView, &hr, true);
		}
		else
		{
			UpdateControls(_this->mWindow, NULL);
		}
	}

	int timer = _this->mTooltipTimer;
	if (timer && !(_this->mTooltipTimer = timer - 1))
	{
		if (!_this->mShowingTooltip)
		{
			_this->ShowTooltip();
			_this->mTooltipTimer = pGraphicsMac->FPS() * 10; // 10 seconds
		}
		else
		{
			_this->HideTooltip();
		}
	}

	timer = _this->mParamChangeTimer;
	if (timer && !(_this->mParamChangeTimer = timer - 1))
	{
		pGraphicsMac->GetPlug()->EndDelayedInformHostOfParamChange();
	}
}

// static
pascal OSStatus IGraphicsCarbon::CarbonParamEditHandler(EventHandlerCallRef const pHandlerCall, EventRef const pEvent, void* const pGraphicsCarbon)
{
	IGraphicsCarbon* const _this = (IGraphicsCarbon*)pGraphicsCarbon;
	const UInt32 eventClass = GetEventClass(pEvent);
	const UInt32 eventKind = GetEventKind(pEvent);

	_this->HideTooltip();

	if (eventClass == kEventClassKeyboard)
	{
		switch (eventKind)
		{
			case kEventRawKeyDown:
			case kEventRawKeyRepeat:
			{
				char ch;
				GetEventParameter(pEvent, kEventParamKeyMacCharCodes, typeChar, NULL, sizeof(char), NULL, &ch);
				if (ch == 13)
				{
					_this->EndUserInput(true);
					return noErr;
				}
				break;
			}
		}
	}

	return eventNotHandledErr;
}

/* static void ResizeWindow(WindowRef const pWindow, const int w, const int h)
{
	Rect gr; // Screen.
	GetWindowBounds(pWindow, kWindowContentRgn, &gr);
	gr.right = gr.left + w;
	gr.bottom = gr.top + h;
	SetWindowBounds(pWindow, kWindowContentRgn, &gr);
} */

IGraphicsCarbon::IGraphicsCarbon(
	IGraphicsMac* const pGraphicsMac,
	WindowRef const pWindow,
	ControlRef pParentControl
):
	mGraphicsMac(pGraphicsMac),
	mContentYOffset(0),
	mWindow(pWindow),
	mView(NULL),
	mTimer(NULL),
	mControlHandler(NULL),
	mWindowHandler(NULL),
	mCGC(NULL),
	mParamEditView(NULL),
	mParamEditHandler(NULL),
	mEdControl(NULL),
	mEdParam(NULL),
	mShowingTooltip(false),
	mTooltipIdx(-1),
	mTooltipTimer(0),
	mParamChangeTimer(0)
{
	Rect r; // Client.
	r.left = r.top = 0;
	r.bottom = pGraphicsMac->Height() >> kScaleFixed;
	r.right = pGraphicsMac->Width() >> kScaleFixed;

	WindowAttributes winAttrs = 0;
	GetWindowAttributes(pWindow, &winAttrs);
	mIsComposited = !!(winAttrs & kWindowCompositingAttribute);

	UInt32 features =  kControlSupportsFocus | kControlHandlesTracking | kControlSupportsEmbedding;
	if (mIsComposited)
	{
		features |= kHIViewIsOpaque | kHIViewFeatureDoesNotUseSpecialParts;
	}
	CreateUserPaneControl(pWindow, &r, features, &mView);

	static const EventTypeSpec controlEvents[] =
	{
		// { kEventClassControl, kEventControlInitialize },
		// { kEventClassControl, kEventControlGetOptimalBounds },
		// { kEventClassControl, kEventControlHitTest },
		{ kEventClassControl, kEventControlClick },
		// { kEventClassKeyboard, kEventRawKeyDown },
		{ kEventClassControl, kEventControlDraw },
		{ kEventClassControl, kEventControlDispose },
		{ kEventClassControl, kEventControlBoundsChanged }
	};
	InstallControlEventHandler(mView, CarbonEventHandler, GetEventTypeCount(controlEvents), controlEvents, this, &mControlHandler);

	static const EventTypeSpec windowEvents[] =
	{
		{ kEventClassMouse, kEventMouseDown },
		{ kEventClassMouse, kEventMouseUp },
		{ kEventClassMouse, kEventMouseMoved },
		{ kEventClassMouse, kEventMouseDragged },
		{ kEventClassMouse, kEventMouseWheelMoved }
	};
	InstallWindowEventHandler(mWindow, CarbonEventHandler, GetEventTypeCount(windowEvents), windowEvents, this, &mWindowHandler);

	const double t = kEventDurationSecond / (double)pGraphicsMac->FPS();
	OSStatus s = InstallEventLoopTimer(GetMainEventLoop(), 0.0, t, CarbonTimerHandler, this, &mTimer);

	if (mIsComposited)
	{
		if (!pParentControl)
		{
			HIViewRef hvRoot = HIViewGetRoot(pWindow);
			/* s = */ HIViewFindByID(hvRoot, kHIViewWindowContentID, &pParentControl);
		}
		s = HIViewAddSubview(pParentControl, mView);
	}
	else
	{
		if (!pParentControl)
		{
			if (GetRootControl(pWindow, &pParentControl) != noErr)
			{
				CreateRootControl(pWindow, &pParentControl);
			}
		}
		s = EmbedControl(mView, pParentControl);
	}

	if (s == noErr)
	{
		SizeControl(mView, r.right, r.bottom); // offset?
	}
}

IGraphicsCarbon::~IGraphicsCarbon()
{
	// Called from IGraphicsMac::CloseWindow().
	if (mParamEditView)
	{
		RemoveEventHandler(mParamEditHandler);
		mParamEditHandler = NULL;
		HIViewRemoveFromSuperview(mParamEditView);
		mParamEditView = NULL;
		mEdControl = NULL;
		mEdParam = NULL;
	}
	HideTooltip();
	if (mParamChangeTimer) mGraphicsMac->GetPlug()->EndDelayedInformHostOfParamChange();
	RemoveEventLoopTimer(mTimer);
	RemoveEventHandler(mControlHandler);
	RemoveEventHandler(mWindowHandler);
	mTimer = NULL;
	mView = NULL;
}

/* bool IGraphicsCarbon::Resize(int w, int h)
{
	if (mWindow && mView)
	{
		ResizeWindow(mWindow, w >>= kScaleFixed, h >>= kScaleFixed);
		const HIRect hr = CGRectMake(0.0f, 0.0f, (CGFloat)w, (CGFloat)h);
		return HIViewSetFrame(mView, &hr) == noErr;
	}
	return false;
} */

static const int PARAM_EDIT_W = 32;
static const int PARAM_EDIT_H = 14;

bool IGraphicsCarbon::PromptUserInput(IControl* const pControl, IParam* const pParam, const IRECT* pR, const int flags, int fontSize)
{
	if (mParamEditView || !pControl) return false;

	char currentText[IGraphics::kMaxEditLen];
	if (pParam)
	{
		pParam->GetDisplayForHost(currentText, sizeof(currentText));
	}
	else
	{
		pControl->GetTextForUserInput(currentText, sizeof(currentText));
	}

	static const int kPromptCustomHeight = IGraphics::kPromptCustomRect ^ IGraphics::kPromptCustomWidth;
	static const int w = PARAM_EDIT_W, h = PARAM_EDIT_H;

	if (!pR) pR = pControl->GetTargetRECT();

	Rect r;
	if (!(flags & kPromptCustomHeight))
	{
		const int cY = (pR->T + pR->B) >> kScaleFixed;
		r.top = (cY - h) / 2;
		r.bottom = (cY + h) / 2;
	}
	else
	{
		r.top = pR->T >> kScaleFixed;
		r.bottom = pR->B >> kScaleFixed;
	}

	if (!(flags & IGraphics::kPromptCustomWidth))
	{
		const int cX = (pR->L + pR->R) >> kScaleFixed;
		r.left = (cX - w) / 2;
		r.right = (cX + w) / 2;
	}
	else
	{
		r.left = pR->L >> kScaleFixed;
		r.right = pR->R >> kScaleFixed;
	}

	fontSize = fontSize ? fontSize >> kScaleFixed : (PARAM_EDIT_H * 11) / 14;

	ControlRef control = NULL;
	if (CreateEditUnicodeTextControl(NULL, &r, NULL, false, NULL, &control) != noErr) return false;

	HIViewAddSubview(mView, control);

	InstallParamEditHandler(control);
	mParamEditView = control;
	SetParamEditText(currentText);

	ControlFontStyleRec font = { kControlUseJustMask | kControlUseSizeMask | kControlUseFontMask, 0, fontSize, 0, 0, teCenter, 0, 0 };
	font.font = ATSFontFamilyFindFromName(CFSTR("Arial"), kATSOptionFlagsDefault);
	SetControlData(mParamEditView, kControlEditTextPart, kControlFontStyleTag, sizeof(font), &font);

	ShowParamEditView();
	mEdControl = pControl;
	mEdParam = pParam;

	return true;
}

void IGraphicsCarbon::InstallParamEditHandler(ControlRef control)
{
	static const EventTypeSpec events[] =
	{
		{ kEventClassKeyboard, kEventRawKeyDown },
		{ kEventClassKeyboard, kEventRawKeyRepeat }
	};
	InstallControlEventHandler(control, CarbonParamEditHandler, GetEventTypeCount(events), events, this, &mParamEditHandler);
}

void IGraphicsCarbon::SetParamEditText(const char* const txt)
{
	if (txt && *txt)
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

void IGraphicsCarbon::EndUserInput(const bool commit)
{
	RemoveEventHandler(mParamEditHandler);
	mParamEditHandler = NULL;

	if (commit)
	{
		CFStringRef str;
		if (GetControlData(mParamEditView, kControlEditTextPart, kControlEditTextCFStringTag, sizeof(str), &str, NULL) == noErr)
		{
			char txt[IGraphics::kMaxEditLen];
			CFStringGetCString(str, txt, sizeof(txt), kCFStringEncodingUTF8);
			mGraphicsMac->SetFromStringAfterPrompt(mEdControl, mEdParam, txt);
			CFRelease(str);
		}
	}

	HIViewSetVisible(mParamEditView, false);
	HIViewRemoveFromSuperview(mParamEditView);
	if (mIsComposited)
	{
		HIViewSetNeedsDisplay(mView, true);
	}
	else
	{
		mEdControl->SetDirty(false);
		mEdControl->Redraw();
	}
	SetThemeCursor(kThemeArrowCursor);
	SetUserFocusWindow(kUserFocusAuto);

	mParamEditView = NULL;
	mEdControl = NULL;
	mEdParam = NULL;
}

void IGraphicsCarbon::ShowTooltip()
{
	HMHelpContentRec helpTag;
	helpTag.version = kMacHelpVersion;

	helpTag.tagSide = kHMInsideTopLeftCorner;
	const int x = mGraphicsMac->GetMouseX() >> kScaleFixed;
	const int y = mGraphicsMac->GetMouseY() >> kScaleFixed;
	HIRect r = CGRectMake((CGFloat)x, (CGFloat)(y + 23), 1.0f, 1.0f);
	HIRectConvert(&r, kHICoordSpaceView, mView, kHICoordSpaceScreenPixel, NULL);
	helpTag.absHotRect.top = (int)r.origin.y;
	helpTag.absHotRect.left = (int)r.origin.x;
	helpTag.absHotRect.bottom = helpTag.absHotRect.top + (int)r.size.height;
	helpTag.absHotRect.right = helpTag.absHotRect.left + (int)r.size.width;

	helpTag.content[kHMMinimumContentIndex].contentType = kHMCFStringLocalizedContent;
	CFStringRef const str = CFStringCreateWithCString(NULL, mTooltip, kCFStringEncodingUTF8);
	helpTag.content[kHMMinimumContentIndex].u.tagCFString = str;
	helpTag.content[kHMMaximumContentIndex].contentType = kHMNoContent;
	HMDisplayTag(&helpTag);
	CFRelease(str);
	mShowingTooltip = true;
}

void IGraphicsCarbon::HideTooltip()
{
	mTooltipTimer = 0;
	if (mShowingTooltip)
	{
		HMHideTag();
		mShowingTooltip = false;
	}
}

#endif // IPLUG_NO_CARBON_SUPPORT
