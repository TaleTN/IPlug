#pragma once

#include "IPlugBase.h"
#include "IGraphics.h"

#include <assert.h>
#include "WDL/wdltypes.h"

// A control is anything on the GUI, it could be a static bitmap, or
// something that moves or changes. The control could manipulate
// canned bitmaps or do run-time vector drawing, or whatever.
//
// Some controls respond to mouse actions, either by moving a bitmap,
// transforming a bitmap, or cycling through a set of bitmaps.
// Other controls are readouts only.

class IControl
{
public:
	enum EDirection { kVertical = 0, kHorizontal = 1 };
	static const float kGrayedAlpha;

	// If paramIdx is > -1, this control will be associated with a plugin parameter.
	IControl(
		IPlugBase* const pPlug,
		const IRECT* const pR = NULL,
		const int paramIdx = -1
	):
		mPlug(pPlug),
		mParamIdx(paramIdx),
		mDirty(1),
		mRedraw(0),
		mHide(0),
		mGrayed(0),
		mDisablePrompt(0),
		mDblAsSingleClick(0),
		mReverse(0),
		mDirection(0),
		mAutoUpdate(0),
		mReadOnly(0),
		mBypass(0),
		_unused(0)
	{
		if (pR) mRECT = *pR;
	}

	virtual ~IControl() {}

	virtual void OnMouseDown(int x, int y, IMouseMod mod) {}
	virtual void OnMouseUp(int x, int y, IMouseMod mod) {}
	virtual void OnMouseDrag(int x, int y, int dX, int dY, IMouseMod mod) {}
	virtual void OnMouseDblClick(int x, int y, IMouseMod mod) {}
	virtual void OnMouseWheel(int x, int y, IMouseMod mod, float d) {}

	virtual bool OnKeyDown(int x, int y, IMouseMod mod, int key) { return false; }
	virtual bool OnKeyUp(int x, int y, IMouseMod mod, int key) { return false; }

	// For efficiency, mouseovers/mouseouts are ignored unless you call IGraphics::HandleMouseOver.
	virtual void OnMouseOver(int x, int y, IMouseMod mod) {}
	virtual void OnMouseOut() {}

	// By default, mouse double click has its own handler. A control can set mDblAsSingleClick to true to change,
	// which maps double click to single click for this control (and also causes the mouse to be
	// captured by the control on double click).
	inline void SetMouseDblAsSingleClick(const bool single) { mDblAsSingleClick = single; }
	inline bool MouseDblAsSingleClick() const { return mDblAsSingleClick; }

	virtual void Draw(IGraphics* pGraphics) {}

	// Ask the IGraphics object to open an edit box so the user can enter a value for this control.
	virtual void PromptUserInput();

	inline int ParamIdx() const { return mParamIdx; }
	inline void SetParamIdx(const int paramIdx) { mParamIdx = paramIdx; }

	virtual void SetValueFromPlug(double value);
	virtual void SetValueFromUserInput(double value);

	virtual void SetValue(double value) {}
	virtual double GetValue() const { return 0.0; }

	inline IRECT* GetRECT() { return &mRECT; }        // The draw area for this control.
	virtual IRECT* GetTargetRECT() { return &mRECT; } // The mouse target area (default = draw area).
	virtual void SetTargetArea(const IRECT* pR) {}

	virtual void Hide(bool hide);
	inline bool IsHidden() const { return mHide; }

	virtual void GrayOut(bool gray);
	inline bool IsGrayed() const { return mGrayed; }

	inline void Reverse(const bool reverse) { mReverse = reverse; }
	inline bool IsReversed() const { return mReverse; }

	// Automatically update other controls with same paramIdx.
	inline void AutoUpdate(const bool update) { mAutoUpdate = update; }
	inline bool DoesAutoUpdate() const { return mAutoUpdate; }

	inline void ReadOnly(const bool readOnly) { mReadOnly = readOnly; }
	inline bool IsReadOnly() const { return mReadOnly; }

	inline void Bypass(const bool bypass) { mBypass = bypass; }
	inline bool IsBypassed() const { return mBypass; }

	// Override if you want the control to be hit only if a visible part of it is hit, or whatever.
	virtual bool IsHit(int x, int y);

	virtual void SetDirty(bool pushParamToPlug = true);
	virtual void SetClean() { mDirty = mRedraw = 0; }
	inline bool IsDirty() const { return mDirty | mRedraw; }
	virtual void Clamp(double lo = 0.0, double hi = 1.0) {}

	// Disables the right-click manual value entry.
	inline void DisablePrompt(const bool disable) { mDisablePrompt = disable; }
	inline bool IsPromptDisabled() const { return mDisablePrompt; }

	virtual void SetTooltip(const char* tooltip) {}
	virtual const char* GetTooltip() { return NULL; }

	// Sometimes a control changes its state as part of its Draw method.
	inline void Redraw() { mDirty = mRedraw = 1; }

	// This is an idle call from the GUI thread, as opposed to
	// IPlugBase::OnIdle which is called from the audio processing thread.
	// Only active if IPLUG_USE_IDLE_CALLS is defined.
	#ifdef IPLUG_USE_IDLE_CALLS
	virtual void OnGUIIdle() {}
	#endif

	inline IPlugBase* GetPlug() const { return mPlug; }
	inline IGraphics* GetGUI() const { return mPlug->GetGUI(); }

	virtual void Rescale(IGraphics* pGraphics) {}

	// For pure text edit controls (i.e. no value, paramIdx < 0).
	virtual void SetTextFromUserInput(const char* txt) {}
	virtual char* GetTextForUserInput(char* buf, int bufSize = 128);

protected:
	IPlugBase* mPlug;
	int mParamIdx;
	unsigned int mDirty:1, mRedraw:1, mHide:1, mGrayed:1, mDisablePrompt:1, mDblAsSingleClick:1, mReverse:1, mDirection:1, mAutoUpdate:1, mReadOnly:1, mBypass:1, _unused:21;
	IRECT mRECT;
};

// Draws the background bitmap.
class IBackgroundControl: public IControl
{
public:
	IBackgroundControl(
		IPlugBase* pPlug,
		const IBitmap* pBitmap
	);

	void Draw(IGraphics* pGraphics);
	void Rescale(IGraphics* pGraphics);

protected:
	IBitmap mBitmap;
};

// Draws a bitmap, or one frame of a stacked bitmap depending on the current value.
class IBitmapControl: public IControl
{
public:
	IBitmapControl(
		IPlugBase* pPlug,
		int x = 0,
		int y = 0,
		int paramIdx = -1,
		const IBitmap* pBitmap = NULL
	);

	IBitmapControl(
		IPlugBase* pPlug,
		int x,
		int y,
		const IBitmap* pBitmap
	);

	void Draw(IGraphics* pGraphics);

	void SetValue(double value);
	double GetValue() const { return mValue; }

	IRECT* GetTargetRECT() { return &mTargetRECT; }
	void SetTargetArea(const IRECT* pR);

	bool IsHit(int x, int y);

	void Clamp(double lo = 0.0, double hi = 1.0);

	void SetTooltip(const char* const tooltip) { mTooltip = tooltip; }
	const char* GetTooltip() { return mTooltip; }

	void Rescale(IGraphics* pGraphics);

protected:
	void UpdateValue(double value);

	IRECT mTargetRECT;
	IBitmap mBitmap;
	const char* mTooltip;
	double WDL_FIXALIGN mValue;
}
WDL_FIXALIGN;

// A switch. Click to toggle/cycle through the bitmap states.
class ISwitchControl: public IBitmapControl
{
public:
	ISwitchControl(
		IPlugBase* pPlug,
		int x,
		int y,
		int paramIdx = -1,
		const IBitmap* pBitmap = NULL
	);

	void OnMouseDown(int x, int y, IMouseMod mod);
	void Draw(IGraphics* pGraphics);
};

// On/off switch that has a target area only.
class IInvisibleSwitchControl: public IControl
{
public:
	IInvisibleSwitchControl(
		IPlugBase* pPlug,
		const IRECT* pR = NULL,
		int paramIdx = -1
	);

	void OnMouseDown(int x, int y, IMouseMod mod);

	void SetValue(double value);
	double GetValue() const { return mValue; }

	IRECT* GetTargetRECT() { return &mTargetRECT; }
	void SetTargetArea(const IRECT* pR);

	bool IsHit(int x, int y);

	void SetTooltip(const char* const tooltip) { mTooltip = tooltip; }
	const char* GetTooltip() { return mTooltip; }

protected:
	IRECT mTargetRECT;
	const char* mTooltip;
	double WDL_FIXALIGN mValue;
}
WDL_FIXALIGN;

// A switch that reverts to 0.0 when released.
class IContactControl: public ISwitchControl
{
public:
	IContactControl(
		IPlugBase* pPlug,
		int x,
		int y,
		int paramIdx = -1,
		const IBitmap* pBitmap = NULL
	);

	void OnMouseUp(int x, int y, IMouseMod mod);
};

// A fader. The bitmap snaps/moves to a mouse click or drag.
class IFaderControl: public IBitmapControl
{
public:
	IFaderControl(
		IPlugBase* pPlug,
		int x,
		int y,
		int len,
		int paramIdx = -1,
		const IBitmap* pBitmap = NULL,
		int direction = kVertical
	);

	inline int GetLength() const { return mLen; }
	// Size of the handle in pixels.
	int GetHandleHeadroom() const { return mDirection == kVertical ? mHandleRECT.H() : mHandleRECT.W(); }
	// Size of the handle in terms of the control value.
	double GetHandleValueHeadroom() const { return (double)GetHandleHeadroom() / (double)mLen; }
	// Where is the handle right now?
	inline IRECT* GetHandleRECT() { return &mHandleRECT; }

	void OnMouseDown(int x, int y, IMouseMod mod);
	void OnMouseDrag(int x, int y, int dX, int dY, IMouseMod mod);
	void OnMouseDblClick(int x, int y, IMouseMod mod);
	void OnMouseWheel(int x, int y, IMouseMod mod, float d);

	void Draw(IGraphics* pGraphics);
	void PromptUserInput();

	void SetValueFromPlug(double value);

	inline double GetDefaultValue() const { return mDefaultValue; }

	inline void SetDefaultValue(const double value)
	{
		assert(value >= 0.0 && value <= 1.0);
		mDefaultValue = value;
	}

	void Rescale(IGraphics* pGraphics);

protected:
	void SnapToMouse(int x, int y);
	void UpdateHandleRECT();

	double WDL_FIXALIGN mDefaultValue;
	IRECT mHandleRECT;
	int mLen;
}
WDL_FIXALIGN;

// A multibitmap knob. The bitmap cycles through states as the mouse drags.
class IKnobMultiControl: public IBitmapControl
{
public:
	static const double kDefaultGearing;

	IKnobMultiControl(
		IPlugBase* pPlug,
		int x,
		int y,
		int paramIdx = -1,
		const IBitmap* pBitmap = NULL
		// int direction = kVertical
	);

	inline void SetGearing(const double gearing) { mGearing = gearing; }
	inline double GetGearing() const { return mGearing; }

	void OnMouseDown(int x, int y, IMouseMod mod);
	void OnMouseDrag(int x, int y, int dX, int dY, IMouseMod mod);
	void OnMouseDblClick(int x, int y, IMouseMod mod);
	void OnMouseWheel(int x, int y, IMouseMod mod, float d);

	void SetValueFromPlug(double value);

	inline double GetDefaultValue() const { return mDefaultValue; }

	inline void SetDefaultValue(const double value)
	{
		assert(value >= 0.0 && value <= 1.0);
		mDefaultValue = value;
	}

protected:
	double WDL_FIXALIGN mGearing, mDefaultValue;
}
WDL_FIXALIGN;

// Output text to the screen.
class ITextControl: public IControl
{
public:
	ITextControl(
		IPlugBase* pPlug,
		const IRECT* pR = NULL,
		const IText* pFont = NULL,
		const char* str = NULL
	);

	const char* GetText() const { return mStr.Get(); }
	void SetTextFromPlug(const char* str);
	void ClearTextFromPlug() { SetTextFromPlug(""); }

	inline IText* GetFont() { return &mFont; }

	void Draw(IGraphics* pGraphics);
	bool IsHit(int x, int y);
	void Rescale(IGraphics* pGraphics);

protected:
	IText mFont;
	WDL_FastString mStr;
};
