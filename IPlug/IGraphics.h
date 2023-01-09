#pragma once

#include "IPlugStructs.h"

#include <assert.h>

#include "WDL/lice/lice.h"
#include "WDL/lice/lice_text.h"

#include "WDL/assocarray.h"
#include "WDL/ptrlist.h"

#if defined(__APPLE__) && defined(__LP64__) && !defined(IPLUG_NO_CARBON_SUPPORT)
	#define IPLUG_NO_CARBON_SUPPORT
#endif

class IPlugBase;
class IControl;
class IParam;

class IGraphics
{
public:
	static const int kDefaultFPS = 24;

	#ifdef IPLUG_USE_IDLE_CALLS
	// If not dirty for this many timer ticks, we call OnGUIIDle.
	static const int kIdleTicks = 20;
	#endif

	static const int kMaxParamLen = 32;
	static const int kMaxEditLen = 1024;

	enum EGUIScale { kScaleFull = 0, kScaleHalf = 1 };

	bool PrepDraw(int wantScale); // Recale the draw bitmap.
	bool IsDirty(IRECT* pR); // Ask the plugin what needs to be redrawn.
	void Draw(const IRECT* pR); // The system announces what needs to be redrawn. Ordering and drawing logic.
	virtual void DrawScreen(const IRECT* pR) = 0; // Tells the OS class to put the final bitmap on the screen.

	// So controls can draw only area that will actually be drawn to screen.
	// Guaranteed to be valid in IControl::Draw().
	inline const IRECT* GetDirtyRECT() const { return mDirtyRECT; }

	// Methods for the drawing implementation class. Coordinates, offset,
	// radius, font, etc. are full scale; bitmaps are actual scale.
	void DrawBitmap(const IBitmap* pBitmap, const IRECT* pDest, int srcX, int srcY, float weight = 1.0f);
	void DrawRotatedBitmap(const IBitmap* pBitmap, int destCtrX, int destCtrY, double angle, int yOffsetZeroDeg = 0, float weight = 1.0f);
	// void DrawRotatedMask(const IBitmap* pBase, const IBitmap* pMask, const IBitmap* pTop, int x, int y, double angle, float weight = 1.0f);
	void DrawPoint(IColor color, float x, float y, float weight = 1.0f);
	// Live ammo! Will crash if out of bounds! etc.
	void ForcePixel(IColor color, int x, int y);
	void DrawLine(IColor color, int x1, int y1, int x2, int y2, float weight = 1.0f, bool antiAlias = false);
	void DrawArc(IColor color, float cx, float cy, float r, float minAngle, float maxAngle, float weight = 1.0f, bool antiAlias = false);
	void DrawCircle(IColor color, float cx, float cy, float r, float weight = 1.0f, bool antiAlias = false);
	void RoundRect(IColor color, const IRECT* pR, float weight, int cornerradius, bool aa);
	// Warning: Might not work as expected if color.A < 255 or weight < 1.0f.
	void FillRoundRect(IColor color, const IRECT* pR, float weight, int cornerradius, bool aa);

	void FillIRect(IColor color, const IRECT* pR, float weight = 1.0f);
	void FillCircle(IColor color, float cx, float cy, float r, float weight = 1.0f, bool antiAlias = false);

	bool LoadFont(int ID, const char* name);
	bool UpdateIText(IText* const pTxt) { return !!CacheFont(pTxt, Scale()); }
	static void PrepDrawIText(IText* const pTxt, const int scale = 0) { CacheFont(pTxt, scale); }
	int DrawIText(IText* pTxt, const char* str, const IRECT* pR, int clip = DT_NOCLIP);
	int MeasureIText(IText* pTxt, const char* str, IRECT* pR);

	IColor GetPoint(int x, int y);
	// void* GetData() { return (void*)GetBits(); }

	// Methods for the OS implementation class.
	// virtual void Resize(int w, int h);
	virtual bool WindowIsOpen() const { return !!GetWindow(); }
	enum EPromptFlags { kPromptCustomWidth = 1, kPromptCustomRect = 3, kPromptInline = 4, kPromptMouseClick = 8 };
	virtual bool PromptUserInput(IControl* pControl, IParam* pParam, const IRECT* pR = NULL, int flags = 0, IText* pTxt = NULL, IColor bg = IColor(0), int delay = 0, int x = 0, int y = 0) = 0;
	void SetFromStringAfterPrompt(IControl* pControl, const IParam* pParam, const char* txt);

	virtual bool HostPath(WDL_String* pPath) = 0; // Full path to host executable.
	virtual bool PluginPath(WDL_String* pPath) = 0; // Full path to plugin dll.

	virtual bool UserDataPath(WDL_String* pPath) = 0; // Full path to user data dir.
	bool UserDataPath(WDL_String* pPath, const char* mfrName, const char* plugName = NULL);

	// Run the "open file" or "save file" dialog; extensions = "txt wav" for example.
	enum EFileAction { kFileOpen = 0, kFileSave };
	virtual bool PromptForFile(WDL_String* pFilename, int action = kFileOpen, const char* dir = NULL, const char* extensions = NULL) = 0;
	// virtual bool PromptForColor(IColor* pColor, const char* prompt = NULL) = 0;

	virtual bool OpenURL(const char* url, const char* windowTitle = NULL, const char* confirmMsg = NULL, const char* errMsg = NULL) = 0;

	// Return 1 if key/mouse wheel is possessed... er, I mean processed.
	virtual int ProcessMouseWheel(float delta) { return 0; }
	virtual int ProcessKey(bool state, IMouseMod mod, int key) { return 0; }

	// Delays mPlug->EndInformHostOfParamChange().
	virtual void SetParamChangeTimer(int ticks) = 0;
	virtual void CancelParamChangeTimer() = 0;

	virtual void* OpenWindow(void* pParentWnd) = 0;
	virtual void CloseWindow() = 0;
	virtual void* GetWindow() const = 0;

	////////////////////////////////////////

	IGraphics(IPlugBase* pPlug, int w, int h, int refreshFPS = 0);
	virtual ~IGraphics();

	inline int Width() const { return mWidth; }
	inline int Height() const { return mHeight; }
	inline int Scale() const { return mScale < 0 ? mDefaultScale : mScale; }
	inline int FPS() const { return mFPS; }

	bool PreloadScale(const int scale) { return mScale < 0 ? PrepDraw(scale) : true; }

	inline void SetDefaultScale(const int scale)
	{
		assert(scale == kScaleFull || scale == kScaleHalf);
		mDefaultScale = scale;
	}

	void Rescale(int scale);

	// Resource wrapper for constructing tables with bitmap resources at
	// different GUI scales. See also IPLUG_RESOURCE macro in IGraphicsWin.h
	// and IGraphicsMac.h.
	struct BitmapResource
	{
		int mID;
		#ifndef _WIN32
		const char* mName;
		#endif

		inline BitmapResource(const int id = 0, const char* const name = NULL)
		: mID(id)
		#ifndef _WIN32
		, mName(name)
		#endif
		{}

		inline int ID() const { return mID & ~1; }
		inline int Scale() const { return mID & 1; }

		#ifndef _WIN32
		inline const char* Name() const { return mName; }
		#else
		static inline const char* Name() { return NULL; }
		#endif
	};

	void LoadBitmapResources(const BitmapResource* pResources);

	inline IPlugBase* GetPlug() { return mPlug; }

	IBitmap LoadIBitmap(int ID, const char* name, int nStates = 1);
	bool UpdateIBitmap(IBitmap* pBitmap);

	void AttachBackground(int ID, const char* name);
	void AttachBackground(IControl* const pControl) { mControls.Insert(0, pControl); }

	// Returns the control index of this control (not the number of controls).
	int AttachControl(IControl* const pControl)
	{
		const int idx = NControls();
		return mControls.Add(pControl) ? idx : -1;
	}

	int AttachControl(IControl* const pControl, const int ID)
	{
		mControlIDs.Insert(ID, pControl);
		return AttachControl(pControl);
	}

	// Returns control index, or -1 if not found.
	int FindControl(const IControl* const pControl) const
	{
		return mControls.Find(pControl);
	}

	IControl* LookupControl(const int ID) const
	{
		return mControlIDs.Get(ID, NULL);
	}

	IControl* GetControl(const int idx) const { return mControls.Get(idx); }
	IControl** GetControls() const { return mControls.GetList(); }

	int NControls() const { return mControls.GetSize(); }
	bool NControls(const int idx) const { return (unsigned int)idx < (unsigned int)NControls(); }

	void HideControl(int paramIdx, bool hide);
	void GrayOutControl(int paramIdx, bool gray);

	// Normalized means the value is in [0, 1].
	void ClampControl(int paramIdx, double lo, double hi, bool normalized);
	void SetParameterFromPlug(int paramIdx, double value, bool normalized);
	// For setting a control that does not have a parameter associated with it.
	void SetControlFromPlug(int controlIdx, double normalizedValue);

	void SetAllControlsDirty();

	// This is for when the GUI needs to change a control value that it can't redraw
	// for context reasons.  If the GUI has redrawn the control, use IPlugBase::SetParameterFromGUI().
	void SetParameterFromGUI(int paramIdx, double normalizedValue);

	// Convenience wrappers.
	void DrawBitmap(const IBitmap* pBitmap, const IRECT* pR, int bmpState = 1, float weight = 1.0f);
	void DrawRect(IColor color, const IRECT* pR);
	void DrawVerticalLine(IColor color, const IRECT* pR, float x);
	void DrawHorizontalLine(IColor color, const IRECT* pR, float y);
	void DrawVerticalLine(IColor color, int xi, int yLo, int yHi);
	void DrawHorizontalLine(IColor color, int yi, int xLo, int xHi);
	void DrawRadialLine(IColor color, float cx, float cy, float angle, float rMin, float rMax, float weight = 1.0f, bool antiAlias = false);

	void OnMouseDown(int x, int y, IMouseMod mod);
	void OnMouseUp(int x, int y, IMouseMod mod);
	void OnMouseDrag(int x, int y, IMouseMod mod);
	// Returns true if the control receiving the double click will treat it as a single click
	// (meaning the OS should capture the mouse).
	bool OnMouseDblClick(int x, int y, IMouseMod mod);

	// Enable/disable mouse wheel, or handle only when combined with modifier key.
	enum EHandleMouseWheel { kMouseWheelEnable = 1, kMouseWheelDisable = 0, kMouseWheelModKey = -1 };
	inline void HandleMouseWheel(const int canHandle) { mHandleMouseWheel = canHandle; }
	void OnMouseWheel(int x, int y, IMouseMod mod, float d);

	// For efficiency, mouseovers/mouseouts are ignored unless you explicity say you can handle them.
	void HandleMouseOver(const bool canHandle) { mHandleMouseOver = canHandle; }
	void OnMouseOver(int x, int y, IMouseMod mod);
	void OnMouseOut();

	inline void SetMouseCapture(const int controlIdx)
	{
		mMouseCapture = controlIdx;
	}

	void SetMouseCapture(const IControl* const pControl)
	{
		SetMouseCapture(FindControl(pControl));
	}

	// Some controls may not need to capture the mouse for dragging, they can call ReleaseCapture when the mouse leaves.
	void ReleaseMouseCapture()
	{
		const int c = mMouseCapture;
		mMouseY = mMouseX = mMouseCapture = -1;
		if (c >= 0) EndInformHostOfParamChange(c);
	}

	inline int GetMouseCapture() const { return mMouseCapture; }
	inline int GetMouseOver() const { return mMouseOver; }
	inline int GetMouseX() const { return mMouseX; }
	inline int GetMouseY() const { return mMouseY; }
	inline bool CanHandleMouseOver() const { return mHandleMouseOver; }
	inline bool TooltipsEnabled() const { return mEnableTooltips; }
	inline int CanHandleMouseWheel() const { return mHandleMouseWheel; }

	bool OnKeyDown(int x, int y, IMouseMod mod, int key);
	bool OnKeyUp(int x, int y, IMouseMod mod, int key);

	// Sets index of control that will receive OnKeyDown(). If -1, then
	// OnKeyDown() is disabled.
	virtual void SetKeyboardFocus(const int controlIdx)
	{
		mKeyboardFocus = controlIdx;
	}

	void SetKeyboardFocus(const IControl* const pControl)
	{
		SetKeyboardFocus(FindControl(pControl));
	}

	inline int GetKeyboardFocus() const { return mKeyboardFocus; }

	// Enables/disables tooltips; also enables mouseovers/mouseouts if necessary.
	void EnableTooltips(const bool enable)
	{
		if (enable) mHandleMouseOver = enable;
		mEnableTooltips = enable;
	}

	// Updates tooltips after (un)hiding controls.
	virtual void UpdateTooltips() = 0;

	// This is an idle call from the GUI thread, as opposed to
	// IPlug::OnIdle which is called from the audio processing thread.
	#ifdef IPLUG_USE_IDLE_CALLS
	void OnGUIIdle();
	#endif

	// For controls that need to interface directly with LICE.
	LICE_pixel* GetBits() { return mDrawBitmap->getBits(); }
	inline LICE_SysBitmap* GetDrawBitmap() const { return mDrawBitmap; }

	// Returns previous backbuffer. SetDrawBitmap(NULL) restores main backbuffer.
	LICE_SysBitmap* SetDrawBitmap(LICE_SysBitmap* const pBackbuf)
	{
		LICE_SysBitmap* const pOldBuf = mDrawBitmap;
		mDrawBitmap = pBackbuf ? pBackbuf : &mBackBuf;
		return pOldBuf;
	}

protected:
	WDL_IntKeyedArray<IControl*> mControlIDs;
	WDL_PtrList<IControl> mControls;
	IPlugBase* mPlug;

	virtual LICE_IBitmap* OSLoadBitmap(int ID, const char* name) = 0;
	virtual bool OSLoadFont(int ID, const char* name) = 0;

	LICE_SysBitmap mBackBuf;
	LICE_SysBitmap* mDrawBitmap;

	static LICE_CachedFont* CacheFont(IText* pTxt, int scale = 0);

private:
	// LICE_MemBitmap* mTmpBitmap;

	const IRECT* mDirtyRECT;
	int mWidth, mHeight, mScale, mDefaultScale, mFPS;

	int GetMouseControlIdx(int x, int y);
	void EndInformHostOfParamChange(int controlIdx);

	int mMouseCapture, mMouseOver, mMouseX, mMouseY;
	int mKeyboardFocus;
	bool mHandleMouseOver, mEnableTooltips;
	signed char mHandleMouseWheel;

	#ifdef IPLUG_USE_IDLE_CALLS
	int mIdleTicks;
	#endif
};

#ifndef NDEBUG
void IPlugDebugLog(const char* str);
#endif
