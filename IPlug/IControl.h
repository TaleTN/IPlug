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
		_unused(0)
	{}

	virtual ~IControl() {}

	virtual void OnMouseDown(int x, int y, IMouseMod mod) {}
	virtual void OnMouseUp(int x, int y, IMouseMod mod) {}
	virtual void OnMouseDrag(int x, int y, int dX, int dY, IMouseMod mod) {}
	virtual void OnMouseDblClick(int x, int y, IMouseMod mod) {}
	virtual void OnMouseWheel(int x, int y, IMouseMod mod, float d) {}
	virtual void OnKeyDown(int x, int y, int key) {}

	// For efficiency, mouseovers/mouseouts are ignored unless you call IGraphics::HandleMouseOver.
	virtual void OnMouseOver(int x, int y, IMouseMod mod) {}
	virtual void OnMouseOut() {}

	// By default, mouse double click has its own handler. A control can set mDblAsSingleClick to true to change,
	// which maps double click to single click for this control (and also causes the mouse to be
	// captured by the control on double click).
	inline bool MouseDblAsSingleClick() const { return mDblAsSingleClick; }

	virtual void Draw(IGraphics* pGraphics) {}

	// Ask the IGraphics object to open an edit box so the user can enter a value for this control.
	virtual void PromptUserInput();

	inline int ParamIdx() const { return mParamIdx; }
	virtual void SetValueFromPlug(double value) {}
	virtual void SetValueFromUserInput(double value) {}
	virtual double GetValue() const { return 0.0; }

	inline IRECT* GetRECT() { return &mRECT; }        // The draw area for this control.
	virtual IRECT* GetTargetRECT() { return &mRECT; } // The mouse target area (default = draw area).
	virtual void SetTargetArea(const IRECT* pR) {}

	virtual void Hide(const bool hide) { mHide = hide; }
	inline bool IsHidden() const { return mHide; }

	virtual void GrayOut(const bool gray) { mGrayed = gray; }
	inline bool IsGrayed() const { return mGrayed; }

	inline void Reverse(const bool reverse) { mReverse = reverse; }
	inline bool IsReversed() const { return mReverse; }

	// Override if you want the control to be hit only if a visible part of it is hit, or whatever.
	virtual bool IsHit(int x, int y);

	virtual void SetDirty(bool pushParamToPlug = true) { mDirty = 1; }
	virtual void SetClean() { mDirty = mRedraw; mRedraw = 0; }
	inline bool IsDirty() const { return mDirty; }
	virtual void Clamp(double lo = 0.0, double hi = 1.0) {}

	// Disables the right-click manual value entry.
	inline void DisablePrompt(const bool disable) { mDisablePrompt = disable; }
	inline bool IsPromptDisabled() const { return mDisablePrompt; }

	virtual void SetTooltip(const char* tooltip) {}
	virtual const char* GetTooltip() { return NULL; }

	// Sometimes a control changes its state as part of its Draw method.
	// Redraw() prevents the control from being cleaned immediately after drawing.
	inline void Redraw() { mRedraw = 1; }

	// This is an idle call from the GUI thread, as opposed to
	// IPlugBase::OnIdle which is called from the audio processing thread.
	// Only active if IPLUG_USE_IDLE_CALLS is defined.
	#ifdef IPLUG_USE_IDLE_CALLS
	virtual void OnGUIIdle() {}
	#endif

	inline IPlugBase* GetPlug() const { return mPlug; }
	inline IGraphics* GetGUI() const { return mPlug->GetGUI(); }

	virtual void Rescale(IGraphics* pGraphics) {}

protected:
	IPlugBase* mPlug;
	int mParamIdx;
	unsigned int mDirty:1, mRedraw:1, mHide:1, mGrayed:1, mDisablePrompt:1, mDblAsSingleClick:1, mReverse:1, mDirection:1, _unused:24;
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
class IBitmapControl : public IControl
{
public:

	IBitmapControl(IPlugBase* pPlug, int x, int y, int paramIdx, IBitmap* pBitmap,
		IChannelBlend::EBlendMethod blendMethod = IChannelBlend::kBlendNone) 
	:	IControl(pPlug, &IRECT(x, y, pBitmap), paramIdx, blendMethod), mBitmap(*pBitmap) {}

	IBitmapControl(IPlugBase* pPlug, int x, int y, IBitmap* pBitmap,
        IChannelBlend::EBlendMethod blendMethod = IChannelBlend::kBlendNone)
	:	IControl(pPlug, &IRECT(x, y, pBitmap), -1, blendMethod), mBitmap(*pBitmap) {}

	virtual ~IBitmapControl() {}

	virtual bool Draw(IGraphics* pGraphics);

protected:
	IBitmap mBitmap;
};

// A switch.  Click to cycle through the bitmap states.
class ISwitchControl : public IBitmapControl
{
public:

	ISwitchControl(IPlugBase* pPlug, int x, int y, int paramIdx, IBitmap* pBitmap,
		IChannelBlend::EBlendMethod blendMethod = IChannelBlend::kBlendNone)
	:	IBitmapControl(pPlug, x, y, paramIdx, pBitmap, blendMethod) {}
	~ISwitchControl() {}

	void OnMouseDown(int x, int y, IMouseMod* pMod);
};

// On/off switch that has a target area only.
class IInvisibleSwitchControl : public IControl
{
public:

    IInvisibleSwitchControl(IPlugBase* pPlug, IRECT* pR, int paramIdx);
    ~IInvisibleSwitchControl() {}

    void OnMouseDown(int x, int y, IMouseMod* pMod);

    virtual bool Draw(IGraphics* pGraphics) { return true; }
};

// A set of buttons that maps to a single selection.  Bitmap has 2 states, off and on.
class IRadioButtonsControl : public IControl
{
public:

    IRadioButtonsControl(IPlugBase* pPlug, IRECT* pR, int paramIdx, int nButtons, IBitmap* pBitmap,
        EDirection direction = kVertical);
    ~IRadioButtonsControl() {}

    void OnMouseDown(int x, int y, IMouseMod* pMod);
    bool Draw(IGraphics* pGraphics);

protected:
    WDL_TypedBuf<IRECT> mRECTs;
    IBitmap mBitmap;
};

// A switch that reverts to 0.0 when released.
class IContactControl : public ISwitchControl
{
public:

	IContactControl(IPlugBase* pPlug, int x, int y, int paramIdx, IBitmap* pBitmap)
	:	ISwitchControl(pPlug, x, y, paramIdx, pBitmap) {}
	~IContactControl() {}

	void OnMouseUp(int x, int y, IMouseMod* pMod);
};

// A fader. The bitmap snaps to a mouse click or drag.
class IFaderControl : public IControl
{
public:

	IFaderControl(IPlugBase* pPlug, int x, int y, int len, int paramIdx, IBitmap* pBitmap, 
		EDirection direction = kVertical);	
	~IFaderControl() {}

    int GetLength() const { return mLen; }
    // Size of the handle in pixels.
    int GetHandleHeadroom() const { return mHandleHeadroom; }
    // Size of the handle in terms of the control value.
    double GetHandleValueHeadroom() const { return (double) mHandleHeadroom / (double) mLen; }
    // Where is the handle right now?
    IRECT GetHandleRECT(double value = -1.0) const;  

	virtual void OnMouseDown(int x, int y, IMouseMod* pMod);
	virtual void OnMouseDrag(int x, int y, int dX, int dY, IMouseMod* pMod);

	virtual bool Draw(IGraphics* pGraphics);

protected:
  void SnapToMouse(int x, int y);
  int mLen, mHandleHeadroom;
	IBitmap mBitmap;
	EDirection mDirection;
};

const double DEFAULT_GEARING = 4.0;

// Parent for knobs, to handle mouse action and ballistics.
class IKnobControl : public IControl
{
public:

	IKnobControl(IPlugBase* pPlug, IRECT* pR, int paramIdx, EDirection direction = kVertical,
		double gearing = DEFAULT_GEARING)
	:	IControl(pPlug, pR, paramIdx), mDirection(direction), mGearing(gearing) {}
	virtual ~IKnobControl() {}

    void SetGearing(double gearing) { mGearing = gearing; }
	virtual void OnMouseDrag(int x, int y, int dX, int dY, IMouseMod* pMod);

protected:
	EDirection mDirection;
	double WDL_FIXALIGN mGearing;
} WDL_FIXALIGN;

// A knob that is just a line.
class IKnobLineControl : public IKnobControl
{
public:

    IKnobLineControl(IPlugBase* pPlug, IRECT* pR, int paramIdx, 
        const IColor* pColor, double innerRadius = 0.0, double outerRadius = 0.0,
        double minAngle = -0.75 * PI, double maxAngle = 0.75 * PI, 
        EDirection direction = kVertical, double gearing = DEFAULT_GEARING);
    ~IKnobLineControl() {}

    bool Draw(IGraphics* pGraphics);

protected:
    IColor mColor;
    float mMinAngle, mMaxAngle, mInnerRadius, mOuterRadius;
};

// A rotating knob.  The bitmap rotates with any mouse drag.
class IKnobRotaterControl : public IKnobControl
{
public:

	IKnobRotaterControl(IPlugBase* pPlug, int x, int y, int paramIdx, IBitmap* pBitmap,
		double minAngle = -0.75 * PI, double maxAngle = 0.75 * PI, int yOffsetZeroDeg = 0, 
		EDirection direction = kVertical, double gearing = DEFAULT_GEARING)
	:	IKnobControl(pPlug, &IRECT(x, y, pBitmap), paramIdx, direction, gearing), 
		mBitmap(*pBitmap), mMinAngle(minAngle), mMaxAngle(maxAngle), mYOffset(yOffsetZeroDeg) {}
	~IKnobRotaterControl() {}

	bool Draw(IGraphics* pGraphics);

protected:
	IBitmap mBitmap;
	double WDL_FIXALIGN mMinAngle, mMaxAngle;
	int mYOffset;
} WDL_FIXALIGN;

// A multibitmap knob.  The bitmap cycles through states as the mouse drags.
class IKnobMultiControl : public IKnobControl
{
public:

	IKnobMultiControl(IPlugBase* pPlug, int x, int y, int paramIdx, IBitmap* pBitmap,
		EDirection direction = kVertical, double gearing = DEFAULT_GEARING)
	:	IKnobControl(pPlug, &IRECT(x, y, pBitmap), paramIdx, direction, gearing), mBitmap(*pBitmap) {}
	~IKnobMultiControl() {}

	bool Draw(IGraphics* pGraphics);

protected:
	IBitmap mBitmap;
};

// A knob that consists of a static base, a rotating mask, and a rotating top.
// The bitmaps are assumed to be symmetrical and identical sizes.
class IKnobRotatingMaskControl : public IKnobControl
{
public:

	IKnobRotatingMaskControl(IPlugBase* pPlug, int x, int y, int paramIdx, 
		IBitmap* pBase, IBitmap* pMask, IBitmap* pTop, 
		double minAngle = -0.75 * PI, double maxAngle = 0.75 * PI, 
		EDirection direction = kVertical, double gearing = DEFAULT_GEARING)
	:	IKnobControl(pPlug, &IRECT(x, y, pBase), paramIdx, direction, gearing),
		mBase(*pBase), mMask(*pMask), mTop(*pTop), mMinAngle(minAngle), mMaxAngle(maxAngle) {}
	~IKnobRotatingMaskControl() {}

	bool Draw(IGraphics* pGraphics);

protected:
	IBitmap mBase, mMask, mTop;
	double WDL_FIXALIGN mMinAngle, mMaxAngle;
} WDL_FIXALIGN;

// Bitmap shows when value = 0, then toggles its target area to the whole bitmap
// and waits for another click to hide itself.
class IBitmapOverlayControl : public ISwitchControl
{
public:

	IBitmapOverlayControl(IPlugBase* pPlug, int x, int y, int paramIdx, IBitmap* pBitmap, IRECT* pTargetArea)
	:	ISwitchControl(pPlug, x, y, paramIdx, pBitmap), mTargetArea(*pTargetArea) {}

	IBitmapOverlayControl(IPlugBase* pPlug, int x, int y, IBitmap* pBitmap, IRECT* pTargetArea)
	:	ISwitchControl(pPlug, x, y, -1, pBitmap), mTargetArea(*pTargetArea) {}

	~IBitmapOverlayControl() {}

	bool Draw(IGraphics* pGraphics);

protected:
	IRECT mTargetArea;	// Keep this around to swap in & out.
};

// Output text to the screen.
class ITextControl : public IControl
{
public:

	ITextControl(IPlugBase* pPlug, IRECT* pR, IText* pText, const char* str = "")
	:	IControl(pPlug, pR), mText(*pText)
  {
    mStr.Set(str);
  }
	~ITextControl() {}

	// GetText returns a pointer to a 0 char if the text has zero length
	// (see wdlstring.h).
	const char *GetText() { return mStr.Get(); }
	void SetTextFromPlug(char* str);
	void ClearTextFromPlug() { SetTextFromPlug(""); }

	const IText* GetIText() const { return &mText; }

	bool Draw(IGraphics* pGraphics);

protected:
	IText mText;
	WDL_String mStr;
};

// If paramIdx is specified, the text is automatically set to the output
// of Param::GetDisplayForHost().  If showParamLabel = true, Param::GetLabelForHost() is appended.
class ICaptionControl : public ITextControl
{
public:

    ICaptionControl(IPlugBase* pPlug, IRECT* pR, int paramIdx, IText* pText, bool showParamLabel = true);
    ~ICaptionControl() {}

    bool Draw(IGraphics* pGraphics);

protected:
    bool mShowParamLabel;
};

// An editable text control, single click starts the editing.
// Setting editable false means the text can't be changed, but can be
// selected and copied to the clipboard.
// Setting secure true means the text isn't displayed, eg for password entry.
// Call SetBGColor() if you want it to draw a filled rectangle behind
// the text.
// Note: there's no dummy parameter so if you need notification when the 
// text is editted you'll have to use the observer interface in ISubject.h
class IEditableTextControl: public ITextControl
{
public:
	IEditableTextControl(IPlugBase* pPlug, IRECT* pR, IText* pText, const char* str = "", bool editable = true, bool secure = false):
	ITextControl(pPlug, pR, pText, str), mBGColor(0), mEditable(editable), mSecure(secure) { mDblAsSingleClick = true; }
	~IEditableTextControl() {}

	inline bool IsEditable() const { return mEditable; }
	inline bool IsSecure() const { return mSecure; }

	void SetBGColor(const IColor *pBGColor);

	void OnMouseDown(int x, int y, IMouseMod* pMod);

	bool Draw(IGraphics* pGraphics);

protected:
	IColor mBGColor;
	bool mEditable;
	bool mSecure;
};

#define MAX_URL_LEN 256
#define MAX_NET_ERR_MSG_LEN 1024

class IURLControl : public IControl
{
public:

  IURLControl(IPlugBase* pPlug, IRECT* pR, const char* url, const char* backupURL = 0, const char* errMsgOnFailure = 0);
  ~IURLControl() {}

  void OnMouseDown(int x, int y, IMouseMod* pMod);
  bool Draw(IGraphics* pGraphics) { return true; }

protected:
  char mURL[MAX_URL_LEN], mBackupURL[MAX_URL_LEN], mErrMsg[MAX_NET_ERR_MSG_LEN];
};

// This is a weird control for a few reasons.
// - Although its numeric mValue is not meaningful, it needs to be associated with a plugin parameter
// so it can inform the plug when the file selection has changed. If the associated plugin parameter is
// declared after kNumParams in the EParams enum, the parameter will be a dummy for this purpose only.
// - Because it puts up a modal window, it needs to redraw itself twice when it's dirty, 
// because moving the modal window will clear the first dirty state.
class IFileSelectorControl : public IControl
{
public:

  enum EFileSelectorState { kFSNone, kFSSelecting, kFSDone };

  IFileSelectorControl(IPlugBase* pPlug, IRECT* pR, int paramIdx, IBitmap* pBitmap, 
    IGraphics::EFileAction action, char* dir = "", char* extensions = "")     // extensions = "txt wav" for example.
	:	IControl(pPlug, pR, paramIdx), mBitmap(*pBitmap), 
    mFileAction(action), mDir(dir), mExtensions(extensions), mState(kFSNone) {}
	~IFileSelectorControl() {}

	void OnMouseDown(int x, int y, IMouseMod* pMod);

	void GetLastSelectedFileForPlug(WDL_String* pStr);
	void SetLastSelectedFileFromPlug(char* file);

  bool Draw(IGraphics* pGraphics);
	bool IsDirty();

protected:
  IBitmap mBitmap;
	WDL_String mDir, mFile, mExtensions;
  IGraphics::EFileAction mFileAction;
	EFileSelectorState mState;
};
