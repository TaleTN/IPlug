#pragma once

#include "IPlugStructs.h"

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

	// If not dirty for this many timer ticks, we call OnGUIIDle.
	static const int kIdleTicks = 20;

	static const int kMaxParamLen = 32;
	static const int kMaxEditLen = kMaxParamLen;

	enum EGUIScale { kScaleFull = 0, kScaleHalf = 1 };

	bool PrepDraw(int wantScale); // Recale the draw bitmap.
	bool IsDirty(IRECT* pR); // Ask the plugin what needs to be redrawn.
	void Draw(const IRECT* pR); // The system announces what needs to be redrawn. Ordering and drawing logic.
	virtual void DrawScreen(const IRECT* pR) = 0; // Tells the OS class to put the final bitmap on the screen.

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
	static void PrepDrawIText(IText* const pTxt, const int scale = 0) { CacheFont(pTxt, scale); }
	int DrawIText(IText* pTxt, const char* str, const IRECT* pR);
	int MeasureIText(IText* pTxt, const char* str, IRECT* pR);

	IColor GetPoint(int x, int y);
	// void* GetData() { return (void*)GetBits(); }

	// Methods for the OS implementation class.
	// virtual void Resize(int w, int h);
	virtual bool WindowIsOpen() const { return !!GetWindow(); }
	enum EPromptFlags { kPromptCustomWidth = 0x80000000, kPromptCustomRect = 0xC0000000 };
	virtual void PromptUserInput(IControl* pControl, IParam* pParam, const IRECT* pR = NULL, int fontSize = 0) = 0;
	void SetFromStringAfterPrompt(IControl* pControl, const IParam* pParam, const char* txt);
	virtual void HostPath(WDL_String* pPath) = 0; // Full path to host executable.
	virtual void PluginPath(WDL_String* pPath) = 0; // Full path to plugin dll.
	// Run the "open file" or "save file" dialog; extensions = "txt wav" for example.
	enum EFileAction { kFileOpen = 0, kFileSave };
	virtual bool PromptForFile(WDL_String* pFilename, int action = kFileOpen, const char* dir = NULL, const char* extensions = NULL) = 0;
	// virtual bool PromptForColor(IColor* pColor, const char* prompt = NULL) = 0;

	virtual bool OpenURL(const char* url, const char* windowTitle = NULL, const char* confirmMsg = NULL, const char* errMsg = NULL) = 0;

  // Return 1 if mouse wheel is processed
  virtual int ProcessMouseWheel(float delta) { return 0; }

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
	inline int Scale() const { return mScale < 0 ? kScaleFull : mScale; }
	inline int FPS() const { return mFPS; }

	bool PreloadScale(const int scale) { return mScale < 0 ? PrepDraw(scale) : true; }
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
  // Returns the control index of this control (not the number of controls).
	int AttachControl(IControl* pControl);

  IControl* GetControl(int idx) { return mControls.Get(idx); }
  void HideControl(int paramIdx, bool hide);
  void GrayOutControl(int paramIdx, bool gray);
  
  // Normalized means the value is in [0, 1].
  void ClampControl(int paramIdx, double lo, double hi, bool normalized);
  void SetParameterFromPlug(int paramIdx, double value, bool normalized);
  // For setting a control that does not have a parameter associated with it.
  void SetControlFromPlug(int controlIdx, double normalizedValue);

  void SetAllControlsDirty();

  // This is for when the gui needs to change a control value that it can't redraw 
  // for context reasons.  If the gui has redrawn the control, use IPlug::SetParameterFromGUI.
  void SetParameterFromGUI(int paramIdx, double normalizedValue);

  // Convenience wrappers.
	bool DrawBitmap(IBitmap* pBitmap, IRECT* pR, int bmpState = 1, const IChannelBlend* pBlend = 0);
  bool DrawRect(const IColor* pColor, IRECT* pR);
  bool DrawVerticalLine(const IColor* pColor, IRECT* pR, float x);
  bool DrawHorizontalLine(const IColor* pColor, IRECT* pR, float y);
  bool DrawVerticalLine(const IColor* pColor, int xi, int yLo, int yHi);
  bool DrawHorizontalLine(const IColor* pColor, int yi, int xLo, int xHi);
  bool DrawRadialLine(const IColor* pColor, float cx, float cy, float angle, float rMin, float rMax, 
    const IChannelBlend* pBlend = 0, bool antiAlias = false);

	void OnMouseDown(int x, int y, IMouseMod* pMod);
	void OnMouseUp(int x, int y, IMouseMod* pMod);
	void OnMouseDrag(int x, int y, IMouseMod* pMod);
  // Returns true if the control receiving the double click will treat it as a single click
  // (meaning the OS should capture the mouse).
	bool OnMouseDblClick(int x, int y, IMouseMod* pMod);
	void OnMouseWheel(int x, int y, IMouseMod* pMod, int d);
	void OnKeyDown(int x, int y, int key);

  void DisplayControlValue(IControl* pControl);
  
  // For efficiency, mouseovers/mouseouts are ignored unless you explicity say you can handle them.
  void HandleMouseOver(bool canHandle) { mHandleMouseOver = canHandle; }
  bool OnMouseOver(int x, int y, IMouseMod* pMod);   // Returns true if mouseovers are handled.
  void OnMouseOut();
  // Some controls may not need to capture the mouse for dragging, they can call ReleaseCapture when the mouse leaves.
  void ReleaseMouseCapture();

  // Enables/disables tooltips; also enables mouseovers/mouseouts if necessary.
  inline void EnableTooltips(bool enable)
  {
    mEnableTooltips = enable;
    if (enable) mHandleMouseOver = enable;
  }

  // Updates tooltips after (un)hiding controls.
  virtual void UpdateTooltips() = 0;

	// This is an idle call from the GUI thread, as opposed to 
	// IPlug::OnIdle which is called from the audio processing thread.
	void OnGUIIdle();

      LICE_pixel* GetBits();

  // For controls that need to interface directly with LICE.
  inline LICE_SysBitmap* GetDrawBitmap() const { return mDrawBitmap; }
  
  WDL_Mutex mMutex;
  
  struct IMutexLock 
  {
    WDL_Mutex* mpMutex;
    IMutexLock(IGraphics* pGraphics) : mpMutex(&(pGraphics->mMutex)) { mpMutex->Enter(); }
    ~IMutexLock() { mpMutex->Leave(); }
  };
  
protected:
	WDL_PtrList<IControl> mControls;
	IPlugBase* mPlug;

  bool CanHandleMouseOver() { return mHandleMouseOver; }
  inline int GetMouseOver() const { return mMouseOver; }
  inline int GetMouseX() const { return mMouseX; }
  inline int GetMouseY() const { return mMouseY; }
  inline bool TooltipsEnabled() const { return mEnableTooltips; }

	virtual LICE_IBitmap* OSLoadBitmap(int ID, const char* name) = 0;
	virtual bool OSLoadFont(int ID, const char* name) = 0;

	LICE_SysBitmap mDrawBitmap;

	static LICE_CachedFont* CacheFont(IText* pTxt, int scale = 0);

private:
	// LICE_MemBitmap* mTmpBitmap;

	int mWidth, mHeight, mScale, mFPS, mIdleTicks;
	int GetMouseControlIdx(int x, int y);
	int mMouseCapture, mMouseOver, mMouseX, mMouseY;
	bool mHandleMouseOver, mEnableTooltips, mDisplayControlValue;
};
