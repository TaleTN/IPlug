#ifndef _IGRAPHICS_
#define _IGRAPHICS_

#include "IPlugStructs.h"
#include "IControl.h"
#include "../lice/lice.h"

// Specialty stuff for calling in to Reaper for Lice functionality.
#ifdef REAPER_SPECIAL
  #include "../IPlugExt/ReaperExt.h"
  #define _LICE ReaperExt
#else
  #define _LICE
#endif

#define MAX_PARAM_LEN 32
#define MAX_EDIT_LEN  1000

class IPlugBase;
class IControl;
class IEditableTextControl;
class IParam;

class IGraphics
{
public:
  
  void PrepDraw();    // Called once, when the IGraphics class is attached to the IPlug class.
	bool IsDirty(IRECT* pR);        // Ask the plugin what needs to be redrawn.
  bool Draw(IRECT* pR);           // The system announces what needs to be redrawn.  Ordering and drawing logic.
  virtual bool DrawScreen(IRECT* pR) = 0;  // Tells the OS class to put the final bitmap on the screen.

  // Methods for the drawing implementation class.
	bool DrawBitmap(IBitmap* pBitmap, IRECT* pDest, int srcX, int srcY,
		const IChannelBlend* pBlend = 0); 
	bool DrawRotatedBitmap(IBitmap* pBitmap, int destCtrX, int destCtrY, double angle, int yOffsetZeroDeg = 0,
		const IChannelBlend* pBlend = 0); 
	bool DrawRotatedMask(IBitmap* pBase, IBitmap* pMask, IBitmap* pTop, int x, int y, double angle,
    const IChannelBlend* pBlend = 0); 
	bool DrawPoint(const IColor* pColor, float x, float y, 
		const IChannelBlend* pBlend = 0, bool antiAlias = false);
  // Live ammo!  Will crash if out of bounds!  etc.
  bool ForcePixel(const IColor* pColor, int x, int y);
	bool DrawLine(const IColor* pColor, float x1, float y1, float x2, float y2,
		const IChannelBlend* pBlend = 0, bool antiAlias = false);
	bool DrawArc(const IColor* pColor, float cx, float cy, float r, float minAngle, float maxAngle, 
		const IChannelBlend* pBlend = 0, bool antiAlias = false);
	bool DrawCircle(const IColor* pColor, float cx, float cy, float r,
		const IChannelBlend* pBlend = 0, bool antiAlias = false);
  bool FillIRect(const IColor* pColor, IRECT* pR, const IChannelBlend* pBlend = 0);
	virtual bool DrawIText(IText* pTxt, char* str, IRECT* pR) = 0;
  IColor GetPoint(int x, int y);
  void* GetData() { return GetBits(); }

	// Methods for the OS implementation class.  
  virtual void Resize(int w, int h);
	virtual bool WindowIsOpen() { return (GetWindow()); }
	virtual void PromptUserInput(IControl* pControl, IParam* pParam) = 0;
	virtual void PromptUserInput(IEditableTextControl* pControl) = 0;
	void SetFromStringAfterPrompt(IControl* pControl, IParam* pParam, char *txt);
	virtual void HostPath(WDL_String* pPath) = 0;   // Full path to host executable.
  virtual void PluginPath(WDL_String* pPath) = 0; // Full path to plugin dll.
	// Run the "open file" or "save file" dialog.  Default to host executable path.
  enum EFileAction { kFileOpen, kFileSave }; // See IFileSelectorControl::EFileAction.
	virtual void PromptForFile(WDL_String* pFilename, int action = kFileOpen, char* dir = 0,
        char* extensions = 0) = 0;  // extensions = "txt wav" for example.
  virtual bool PromptForColor(IColor* pColor, char* prompt = 0) = 0;

  virtual bool OpenURL(const char* url, 
    const char* msgWindowTitle = 0, const char* confirmMsg = 0, const char* errMsgOnFailure = 0) = 0;
  
  // Strict (default): draw everything within the smallest rectangle that contains everything dirty.
  // Every control is guaranteed to get no more than one Draw() call per cycle.
  // Fast: draw only controls that intersect something dirty.
  // If there are overlapping controls, fast drawing can generate multiple Draw() calls per cycle
  // (a control may be asked to draw multiple parts of itself, if it intersects with something dirty.)
  void SetStrictDrawing(bool strict);
  
  virtual void* OpenWindow(void* pParentWnd) = 0;
  virtual void* OpenWindow(void* pParentWnd, void* pParentControl) { return 0; }  // For OSX Carbon hosts ... ugh.
	virtual void CloseWindow() = 0;  
	virtual void* GetWindow() = 0;

	////////////////////////////////////////

	IGraphics(IPlugBase* pPlug, int w, int h, int refreshFPS = 0);
	virtual ~IGraphics();
  
  int Width() { return mWidth; }
  int Height() { return mHeight; }
  int FPS() { return mFPS; }
  
  IPlugBase* GetPlug() { return mPlug; }
  
	IBitmap LoadIBitmap(int ID, const char* name, int nStates = 1);
  IBitmap ScaleBitmap(IBitmap* pSrcBitmap, int destW, int destH);
  IBitmap CropBitmap(IBitmap* pSrcBitmap, IRECT* pR);
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

	// This is an idle call from the GUI thread, as opposed to 
	// IPlug::OnIdle which is called from the audio processing thread.
	void OnGUIIdle();

  void RetainBitmap(IBitmap* pBitmap);
  void ReleaseBitmap(IBitmap* pBitmap);
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
  IRECT mDrawRECT;

  bool CanHandleMouseOver() { return mHandleMouseOver; }

  virtual LICE_IBitmap* OSLoadBitmap(int ID, const char* name) = 0;
	LICE_SysBitmap* mDrawBitmap;

private:

	LICE_MemBitmap* mTmpBitmap;

	int mWidth, mHeight, mFPS, mIdleTicks;
	int GetMouseControlIdx(int x, int y);
	int mMouseCapture, mMouseOver, mMouseX, mMouseY;
  bool mHandleMouseOver, mStrict, mDisplayControlValue;
};

#endif