#include "IGraphics.h"
#include "IControl.h"

#include <assert.h>
#include <math.h>
#include <string.h>

#include "WDL/mutex.h"
#include "WDL/wdltypes.h"

const int IGraphics::kDefaultFPS;

class BitmapStorage
{
public:

  struct BitmapKey
  {
    int id;
    LICE_IBitmap* bitmap;
  };
  
  WDL_PtrList<BitmapKey> m_bitmaps;
  WDL_Mutex m_mutex;

  LICE_IBitmap* Find(int id)
  {
    WDL_MutexLock lock(&m_mutex);
    int i, n = m_bitmaps.GetSize();
    for (i = 0; i < n; ++i)
    {
      BitmapKey* key = m_bitmaps.Get(i);
      if (key->id == id) return key->bitmap;
    }
    return 0;
  }

  void Add(LICE_IBitmap* bitmap, int id = -1)
  {
    WDL_MutexLock lock(&m_mutex);
    BitmapKey* key = m_bitmaps.Add(new BitmapKey);
    key->id = id;
    key->bitmap = bitmap;
  }

  void Remove(LICE_IBitmap* bitmap)
  {
    WDL_MutexLock lock(&m_mutex);
    int i, n = m_bitmaps.GetSize();
    for (i = 0; i < n; ++i)
    {
      if (m_bitmaps.Get(i)->bitmap == bitmap)
      {
        m_bitmaps.Delete(i, true);
        delete(bitmap);
        break;
      }
    }
  }

  ~BitmapStorage()
  {
    int i, n = m_bitmaps.GetSize();
    for (i = 0; i < n; ++i)
    {
      delete(m_bitmaps.Get(i)->bitmap);
    }
    m_bitmaps.Empty(true);
  }
};

static BitmapStorage s_bitmapCache;

class FontStorage
{
public:
	struct FontKey
	{
		WDL_UINT64 style;
		const char* face;
		LICE_IFont* font;
	};

	WDL_PtrList<FontKey> m_fonts;
	WDL_Mutex m_mutex;

	WDL_TypedBuf<int> m_load;

	static WDL_UINT64 PackStyle(const int size, const int style, const int orientation)
	{
		assert(style >= 0 && style < 8);
		assert(orientation >= -268435456 && orientation < 268435456);

		return ((WDL_UINT64)((orientation << 3) | style) << 32) | size;
	}

	LICE_IFont* Find(const WDL_UINT64 style, const char* const face)
	{
		const int n = m_fonts.GetSize();
		for (int i = 0; i < n; ++i)
		{
			const FontKey* const key = m_fonts.Get(i);
			if (key->style == style && (key->face == face || !strcmp(key->face, face)))
			{
				return key->font;
			}
		}
		return NULL;
	}

	LICE_IFont* Find(const IText* const pTxt, const int scale = 0)
	{
		m_mutex.Enter();
		const WDL_UINT64 style = PackStyle(pTxt->mSize >> scale, pTxt->mStyle, pTxt->mOrientation);
		LICE_IFont* const font = Find(style, pTxt->mFont);
		m_mutex.Leave();
		return font;
	}

	LICE_IFont* Add(LICE_IFont* const font, const IText* const pTxt, const int scale = 0)
	{
		m_mutex.Enter();
		const WDL_UINT64 style = PackStyle(pTxt->mSize >> scale, pTxt->mStyle, pTxt->mOrientation);
		LICE_IFont* ret = Find(style, pTxt->mFont);
		if (!ret)
		{
			FontKey* const key = m_fonts.Add(new FontKey);
			key->style = style;
			key->face = pTxt->mFont;
			key->font = ret = font;
		}
		m_mutex.Leave();
		return ret;
	}

	~FontStorage()
	{
		const int n = m_fonts.GetSize();
		for (int i = 0; i < n; ++i)
		{
			delete m_fonts.Get(i)->font;
		}
		m_fonts.Empty(true);
	}
};

static FontStorage s_fontCache;

IGraphics::IGraphics(IPlugBase* pPlug, int w, int h, int refreshFPS)
:	mPlug(pPlug), mWidth(w), mHeight(h), mIdleTicks(0), 
  mMouseCapture(-1), mMouseOver(-1), mMouseX(0), mMouseY(0), mHandleMouseOver(false), mEnableTooltips(false), mStrict(true), mDisplayControlValue(false), mDrawBitmap(0), mTmpBitmap(0)
{
	mFPS = (refreshFPS > 0 ? refreshFPS : DEFAULT_FPS);
}

IGraphics::~IGraphics()
{
    mControls.Empty(true);
	DELETE_NULL(mDrawBitmap);
	DELETE_NULL(mTmpBitmap);
}

void IGraphics::Resize(int w, int h)
{
  // The OS implementation class has to do all the work, then call up to here.
  mWidth = w;
  mHeight = h;
  ReleaseMouseCapture();
  mControls.Empty(true);
  mPlug->ResizeGraphics(w, h);
}

void IGraphics::SetFromStringAfterPrompt(IControl* pControl, IParam* pParam, char *txt)
{
	if (pParam)
	{
		double v;
		bool mapped = pParam->GetNDisplayTexts();
		if (mapped)
		{
			int vi;
			mapped = pParam->MapDisplayText(txt, &vi);
			if (mapped) v = (double)vi;
		}
		if (!mapped)
		{
			v = atof(txt);
			if (pParam->DisplayIsNegated()) v = -v;
		}
		int paramIdx = pControl->ParamIdx();
		if (paramIdx >= 0) {
			mPlug->BeginInformHostOfParamChange(paramIdx);
		}
    	pControl->SetValueFromUserInput(pParam->GetNormalized(v));
		if (paramIdx >= 0) {
			mPlug->EndInformHostOfParamChange(paramIdx);
		}
	}
	else // if (pControl)
	{
		if (((IEditableTextControl*)pControl)->IsEditable()) ((ITextControl*)pControl)->SetTextFromPlug(txt);
	}
}

void IGraphics::AttachBackground(int ID, const char* name)
{
  IBitmap bg = LoadIBitmap(ID, name);
  IControl* pBG = new IBitmapControl(mPlug, 0, 0, -1, &bg, IChannelBlend::kBlendClobber);
  mControls.Insert(0, pBG);
}

int IGraphics::AttachControl(IControl* pControl)
{
	mControls.Add(pControl);
  return mControls.GetSize() - 1;
}

void IGraphics::HideControl(int paramIdx, bool hide)
{
  int i, n = mControls.GetSize();
  IControl** ppControl = mControls.GetList();
	for (i = 0; i < n; ++i, ++ppControl) {
		IControl* pControl = *ppControl;
		if (pControl->ParamIdx() == paramIdx) {
      pControl->Hide(hide);
    }
    // Could be more than one, don't break until we check them all.
  }
}

void IGraphics::GrayOutControl(int paramIdx, bool gray)
{
  int i, n = mControls.GetSize();
  IControl** ppControl = mControls.GetList();
	for (i = 0; i < n; ++i, ++ppControl) {
    IControl* pControl = *ppControl;
    if (pControl->ParamIdx() == paramIdx) {
      pControl->GrayOut(gray);
    }
    // Could be more than one, don't break until we check them all.
  }
}

void IGraphics::ClampControl(int paramIdx, double lo, double hi, bool normalized)
{
  if (!normalized) {
    IParam* pParam = mPlug->GetParam(paramIdx);
    lo = pParam->GetNormalized(lo);
    hi = pParam->GetNormalized(hi);
  }  
  int i, n = mControls.GetSize();
  IControl** ppControl = mControls.GetList();
	for (i = 0; i < n; ++i, ++ppControl) {
    IControl* pControl = *ppControl;
    if (pControl->ParamIdx() == paramIdx) {
      pControl->Clamp(lo, hi);
    }
    // Could be more than one, don't break until we check them all.
  }
}

void IGraphics::SetParameterFromPlug(int paramIdx, double value, bool normalized)
{
  if (!normalized) {
    IParam* pParam = mPlug->GetParam(paramIdx);
    value = pParam->GetNormalized(value);
  }  
  int i, n = mControls.GetSize();
  IControl** ppControl = mControls.GetList();
	for (i = 0; i < n; ++i, ++ppControl) {
    IControl* pControl = *ppControl;
    if (pControl->ParamIdx() == paramIdx) {
      //WDL_MutexLock lock(&mMutex);
      pControl->SetValueFromPlug(value);
      // Could be more than one, don't break until we check them all.
    }
  }
}

void IGraphics::SetControlFromPlug(int controlIdx, double normalizedValue)
{
  if (controlIdx >= 0 && controlIdx < mControls.GetSize()) {
    //WDL_MutexLock lock(&mMutex);
    mControls.Get(controlIdx)->SetValueFromPlug(normalizedValue);
  }
}

void IGraphics::SetAllControlsDirty()
{
  int i, n = mControls.GetSize();
  IControl** ppControl = mControls.GetList();
	for (i = 0; i < n; ++i, ++ppControl) {
    IControl* pControl = *ppControl;
    pControl->SetDirty(false);
  }
}

void IGraphics::SetParameterFromGUI(int paramIdx, double normalizedValue)
{
  int i, n = mControls.GetSize();
  IControl** ppControl = mControls.GetList();
	for (i = 0; i < n; ++i, ++ppControl) {
    IControl* pControl = *ppControl;
    if (pControl->ParamIdx() == paramIdx) {
      pControl->SetValueFromUserInput(normalizedValue);
      // Could be more than one, don't break until we check them all.
    }
	}
}

IBitmap IGraphics::LoadIBitmap(int ID, const char* name, int nStates)
{
  LICE_IBitmap* lb = s_bitmapCache.Find(ID); 
  if (!lb)
  {
    lb = OSLoadBitmap(ID, name);
    bool imgResourceFound = (lb);
    assert(imgResourceFound); // Protect against typos in resource.h and .rc files.
    s_bitmapCache.Add(lb, ID);
  }
  return IBitmap(lb, lb->getWidth(), lb->getHeight(), nStates);
}

void IGraphics::RetainBitmap(IBitmap* pBitmap)
{
  s_bitmapCache.Add((LICE_IBitmap*)pBitmap->mData);
}

void IGraphics::ReleaseBitmap(IBitmap* pBitmap)
{
  s_bitmapCache.Remove((LICE_IBitmap*)pBitmap->mData);
}

bool IGraphics::PrepDraw(const int wantScale)
{
	if (wantScale != mScale && mPlug->OnGUIRescale(wantScale))
	{
		const int n = mControls.GetSize();
		IControl* const* const ppControl = mControls.GetList();
		for (int i = 0; i < n; ++i)
		{
			IControl* const pControl = ppControl[i];
			pControl->Rescale(this);
		}
	}
	return !!mDrawBitmap.getBits();
}

void IGraphics::DrawBitmap(const IBitmap* const pIBitmap, const IRECT* const pDest, int srcX, int srcY, const float weight)
{
	LICE_IBitmap* const pLB = (LICE_IBitmap*)pIBitmap->mData;
	IRECT r = *pDest;

	const int scale = Scale();
	if (scale) { r.Downscale(scale); srcX >>= scale; srcY >>= scale; }

	LICE_Blit(&mDrawBitmap, pLB, r.L, r.T, srcX, srcY, r.W(), r.H(), weight, IChannelBlend::kBlendNone);
}

void IGraphics::DrawRotatedBitmap(const IBitmap* const pIBitmap, const int destCtrX, const int destCtrY, const double angle,
	const int yOffsetZeroDeg, const float weight)
{
	LICE_IBitmap* const pLB = (LICE_IBitmap*)pIBitmap->mData;

	/* const double dA = angle * M_PI / 180.0;
	// Can't figure out what LICE_RotatedBlit is doing for irregular bitmaps exactly.
	const double w = (double)bitmap.W;
	const double h = (double)bitmap.H;
	const double sinA = fabs(sin(dA));
	const double cosA = fabs(cos(dA));
	const int W = (int)(h * sinA + w * cosA);
	const int H = (int)(h * cosA + w * sinA); */

	const int scale = Scale();

	const int W = pIBitmap->W;
	const int H = pIBitmap->H;
	const int destX = (destCtrX >> scale) - W / 2;
	const int destY = (destCtrY >> scale) - H / 2;

	LICE_RotatedBlit(&mDrawBitmap, pLB, destX, destY, W, H, 0.0f, 0.0f, (float)W, (float)H, (float)angle,
		false, weight, IChannelBlend::kBlendNone | LICE_BLIT_FILTER_BILINEAR, 0.0f, (float)(yOffsetZeroDeg >> scale));
}

/* void IGraphics::DrawRotatedMask(const IBitmap* const pIBase, const IBitmap* const pIMask, const IBitmap* const pITop,
	const int x, const int y, const double angle, const float weight)
{
	LICE_IBitmap* const pBase = (LICE_IBitmap*)pIBase->mData;
	LICE_IBitmap* const pMask = (LICE_IBitmap*)pIMask->mData;
	LICE_IBitmap* const pTop = (LICE_IBitmap*)pITop->mData;

	const double dA = angle * M_PI / 180.0;
	const int W = pIBase->W;
	const int H = pIBase->H;
	const float xOffs = (W & 1) ? -0.5f : 0.0f;

	if (!mTmpBitmap) mTmpBitmap = new LICE_MemBitmap();
	LICE_Copy(mTmpBitmap, pBase);
	LICE_ClearRect(mTmpBitmap, 0, 0, W, H, LICE_RGBA(255, 255, 255, 0));

	LICE_RotatedBlit(mTmpBitmap, pMask, 0, 0, W, H, 0.0f, 0.0f, (float)W, (float)H, (float)dA,
		true, 1.0f, LICE_BLIT_MODE_ADD | LICE_BLIT_FILTER_BILINEAR | LICE_BLIT_USE_ALPHA, xOffs, 0.0f);
	LICE_RotatedBlit(mTmpBitmap, pTop, 0, 0, W, H, 0.0f, 0.0f, (float)W, (float)H, (float)dA,
		true, 1.0f, LICE_BLIT_MODE_COPY | LICE_BLIT_FILTER_BILINEAR | LICE_BLIT_USE_ALPHA, xOffs, 0.0f);

	const int scale = Scale();
	LICE_Blit(&mDrawBitmap, mTmpBitmap, x >> scale, y >> scale, 0, 0, W, H, weight, IChannelBlend::kBlendNone);
} */

void IGraphics::DrawPoint(const IColor color, float x, float y, const float weight)
{
	const int scale = Scale();
	if (scale)
	{
		// const float mul = 1.0f / (float)(1 << scale);
		assert(scale == 1); static const float mul = 0.5f;
		x *= mul; y *= mul;
	}
	LICE_PutPixel(&mDrawBitmap, (int)(x + 0.5f), (int)(y + 0.5f), color.Get(), weight, IChannelBlend::kBlendNone);
}

void IGraphics::ForcePixel(const IColor color, const int x, const int y)
{
	const int scale = Scale();
	LICE_pixel* px = mDrawBitmap.getBits();
	px += (x >> scale) + (y >> scale) * mDrawBitmap.getRowSpan();
	*px = color.Get();
}

void IGraphics::DrawLine(const IColor color, const int x1, const int y1, const int x2, const int y2,
	const float weight, const bool antiAlias)
{
	const int scale = Scale();
	LICE_Line(&mDrawBitmap, x1 >> scale, y1 >> scale, x2 >> scale, y2 >> scale, color.Get(), weight,
		IChannelBlend::kBlendNone, antiAlias);
}

void IGraphics::DrawArc(const IColor color, float cx, float cy, float r, const float minAngle, const float maxAngle,
	const float weight, const bool antiAlias)
{
	const int scale = Scale();
	if (scale)
	{
		// const float mul = 1.0f / (float)(1 << scale);
		assert(scale == 1); static const float mul = 0.5f;
		cx *= mul; cy *= mul; r *= mul;
	}
	LICE_Arc(&mDrawBitmap, cx, cy, r, minAngle, maxAngle, color.Get(), weight, IChannelBlend::kBlendNone, antiAlias);
}

void IGraphics::DrawCircle(const IColor color, float cx, float cy, float r, const float weight, const bool antiAlias)
{
	const int scale = Scale();
	if (scale)
	{
		// const float mul = 1.0f / (float)(1 << scale);
		assert(scale == 1); static const float mul = 0.5f;
		cx *= mul; cy *= mul; r *= mul;
	}
	LICE_Circle(&mDrawBitmap, cx, cy, r, color.Get(), weight, IChannelBlend::kBlendNone, antiAlias);
}

void IGraphics::RoundRect(const IColor color, const IRECT* const pR, const float weight, int cornerradius,
	const bool aa)
{
	IRECT r = *pR;
	const int scale = Scale();
	if (scale) { r.Downscale(scale); cornerradius >>= scale; }

	LICE_RoundRect(&mDrawBitmap, (float)r.L, (float)r.T, (float)r.W(), (float)r.H(), cornerradius,
		color.Get(), weight, IChannelBlend::kBlendNone, aa);
}

void IGraphics::FillRoundRect(const IColor color, const IRECT* const pR, const float weight, int cornerradius,
	const bool aa)
{
	// assert(!(color.A < 255 || weight < 1.0f));

	IRECT r = *pR;
	const int scale = Scale();
	if (scale) { r.Downscale(scale); cornerradius >>= scale; }

	const int x1 = r.L;
	const int y1 = r.T;
	const int h = r.H();
	const int w = r.W();

	static const int mode = IChannelBlend::kBlendNone;
	const LICE_pixel col = color.Get();

	LICE_FillRect(&mDrawBitmap, x1 + cornerradius, y1, w - 2 * cornerradius, h, col, weight, mode);
	LICE_FillRect(&mDrawBitmap, x1, y1 + cornerradius, cornerradius, h - 2 * cornerradius, col, weight, mode);
	LICE_FillRect(&mDrawBitmap, x1 + w - cornerradius, y1 + cornerradius, cornerradius, h - 2 * cornerradius, col, weight, mode);

	LICE_FillCircle(&mDrawBitmap, (float)(x1 + cornerradius), (float)(y1 + cornerradius), (float)cornerradius, col, weight, mode, aa);
	LICE_FillCircle(&mDrawBitmap, (float)(x1 + w - cornerradius - 1), (float)(y1 + h - cornerradius - 1), (float)cornerradius, col, weight, mode, aa);
	LICE_FillCircle(&mDrawBitmap, (float)(x1 + w - cornerradius - 1), (float)(y1 + cornerradius), (float)cornerradius, col, weight, mode, aa);
	LICE_FillCircle(&mDrawBitmap, (float)(x1 + cornerradius), (float)(y1 + h - cornerradius - 1), (float)cornerradius, col, weight, mode, aa);
}

void IGraphics::FillIRect(const IColor color, const IRECT* const pR, const float weight)
{
	IRECT r = *pR;
	const int scale = Scale();
	if (scale) r.Downscale(scale);

	LICE_FillRect(&mDrawBitmap, r.L, r.T, r.W(), r.H(), color.Get(), weight, IChannelBlend::kBlendNone);
}

void IGraphics::FillCircle(const IColor color, float cx, float cy, float r, const float weight, const bool antiAlias)
{
	const int scale = Scale();
	if (scale)
	{
		// const float mul = 1.0f / (float)(1 << scale);
		assert(scale == 1); static const float mul = 0.5f;
		cx *= mul; cy *= mul; r *= mul;
	}
	LICE_FillCircle(&mDrawBitmap, cx, cy, r, color.Get(), weight, IChannelBlend::kBlendNone, antiAlias);
}

int IGraphics::DrawIText(IText* const pTxt, const char* const str, const IRECT* const pR)
{
	const int scale = Scale();

	LICE_IFont* font = pTxt->mCached;
	if (!font)
	{
		font = CacheFont(pTxt, scale);
		if (!font) return 0;
	}

	if (!str || !*str) return font->GetLineHeight();

	const LICE_pixel color = pTxt->mColor.Get();
	font->SetTextColor(color);

	static const UINT align[3] = { DT_LEFT, DT_CENTER, DT_RIGHT };
	assert(pTxt->mAlign >= 0 && pTxt->mAlign < 3);

	UINT fmt = align[pTxt->mAlign] | DT_NOCLIP;
	if (LICE_GETA(color) < 255) fmt |= LICE_DT_USEFGALPHA;

	RECT R = { pR->L, pR->T, pR->R, pR->B };

	if (scale)
	{
		R.left >>= scale;
		R.top >>= scale;
		R.right >>= scale;
		R.bottom >>= scale;
	}
 
	const int h = font->DrawText(&mDrawBitmap, str, -1, &R, fmt) << scale;

	return h;
}

int IGraphics::MeasureIText(IText* const pTxt, const char* const str, IRECT* const pR)
{
	const int scale = Scale();

	LICE_IFont* font = pTxt->mCached;
	if (!font)
	{
		font = CacheFont(pTxt, scale);
		if (!font) return 0;
	}

	if (!str || !*str)
	{
		const int lh = font->GetLineHeight() << scale;
		pR->R = pR->L;
		pR->B = pR->T + lh;
		return lh;
	}

	const UINT fmt = DT_CALCRECT | DT_NOCLIP | DT_LEFT;
	// if (LICE_GETA(color) < 255) fmt |= LICE_DT_USEFGALPHA;

	RECT R = { 0 };
	int h = font->DrawText(&mDrawBitmap, str, -1, &R, fmt);

	if (scale)
	{
		h <<= scale;
		R.left <<= scale;
		R.top <<= scale;
		R.right <<= scale;
		R.bottom <<= scale;
	}

	pR->L += R.left;
	pR->T += R.top;
	pR->R = pR->L + R.right - R.left;
	pR->B = pR->T + R.bottom - R.top;

	return h;
}

bool IGraphics::LoadFont(const int ID, const char* const name)
{
	s_fontCache.m_mutex.Enter();
	bool ret = s_fontCache.m_load.Find(ID) >= 0;
	if (!ret)
	{
		ret = OSLoadFont(ID, name);
		if (ret) s_fontCache.m_load.Add(ID);
	}
	s_fontCache.m_mutex.Leave();
	return ret;
}

LICE_CachedFont* IGraphics::CacheFont(IText* const pTxt, const int scale)
{
	LICE_CachedFont* font = (LICE_CachedFont*)s_fontCache.Find(pTxt, scale);
	if (!font)
	{
		font = new LICE_CachedFont;
		int h = pTxt->mSize >> scale;
		const int esc = 10 * pTxt->mOrientation;
		const int wt = pTxt->mStyle & IText::kStyleBold ? FW_BOLD : FW_NORMAL;
		const int it = pTxt->mStyle & IText::kStyleItalic ? TRUE : FALSE;

		static const int quality[4] =
		{
			DEFAULT_QUALITY,
			NONANTIALIASED_QUALITY,
			ANTIALIASED_QUALITY,

			#ifdef CLEARTYPE_QUALITY
			CLEARTYPE_QUALITY
			#else
			ANTIALIASED_QUALITY
			#endif
		};

		assert(pTxt->mQuality >= 0 && pTxt->mQuality < 4);
		const int q = quality[pTxt->mQuality];

		#ifdef __APPLE__
		bool resized = false;
		for (;;)
		{
			if (h < 2) h = 2;
			#endif
			HFONT hFont = CreateFont(h, 0, esc, esc, wt, it, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, q, DEFAULT_PITCH, pTxt->mFont);
			if (!hFont)
			{
				delete font;
				return NULL;
			}
			font->SetFromHFont(hFont, LICE_FONT_FLAG_OWNS_HFONT | LICE_FONT_FLAG_FORCE_NATIVE);
			#ifdef __APPLE__
			int l;
			if (resized || (l = font->GetLineHeight()) == h) break;

			h = (int)((double)(h * h) / (double)l + 0.5);
			resized = true;
		}
		#endif
		LICE_CachedFont* const cached = (LICE_CachedFont*)s_fontCache.Add(font, pTxt, scale);
		if (cached != font)
		{
			delete font;
			font = cached;
		}
	}
	pTxt->mCached = font;
	return font;
}

IColor IGraphics::GetPoint(const int x, const int y)
{
	const int scale = Scale();
	const LICE_pixel pix = LICE_GetPixel(&mDrawBitmap, x >> scale, y >> scale);
	return IColor(LICE_GETA(pix), LICE_GETR(pix), LICE_GETG(pix), LICE_GETB(pix));
}

bool IGraphics::DrawVerticalLine(const IColor* pColor, int xi, int yLo, int yHi)
{
  _LICE::LICE_Line(mDrawBitmap, (float)xi, (float)yLo, (float)xi, (float)yHi, LiceColor(pColor), 1.0f, LICE_BLIT_MODE_COPY, false);
  return true;
}

bool IGraphics::DrawHorizontalLine(const IColor* pColor, int yi, int xLo, int xHi)
{
  _LICE::LICE_Line(mDrawBitmap, (float)xLo, (float)yi, (float)xHi, (float)yi, LiceColor(pColor), 1.0f, LICE_BLIT_MODE_COPY, false);
  return true;
}

IBitmap IGraphics::ScaleBitmap(IBitmap* pIBitmap, int destW, int destH)
{
  LICE_IBitmap* pSrc = (LICE_IBitmap*) pIBitmap->mData;
  LICE_MemBitmap* pDest = new LICE_MemBitmap(destW, destH);
  _LICE::LICE_ScaledBlit(pDest, pSrc, 0, 0, destW, destH, 0.0f, 0.0f, (float) pIBitmap->W, (float) pIBitmap->H, 1.0f, 
    LICE_BLIT_MODE_COPY | LICE_BLIT_FILTER_BILINEAR);

  IBitmap bmp(pDest, destW, destH, pIBitmap->N);
  RetainBitmap(&bmp);
  return bmp;
}

IBitmap IGraphics::CropBitmap(IBitmap* pIBitmap, IRECT* pR)
{
  int destW = pR->W(), destH = pR->H();
  LICE_IBitmap* pSrc = (LICE_IBitmap*) pIBitmap->mData;
  LICE_MemBitmap* pDest = new LICE_MemBitmap(destW, destH);
  _LICE::LICE_Blit(pDest, pSrc, 0, 0, pR->L, pR->T, destW, destH, 1.0f, LICE_BLIT_MODE_COPY);

  IBitmap bmp(pDest, destW, destH, pIBitmap->N);
  RetainBitmap(&bmp);
  return bmp;
}

LICE_pixel* IGraphics::GetBits()
{
  return mDrawBitmap->getBits();
}

bool IGraphics::DrawBitmap(IBitmap* pBitmap, IRECT* pR, int bmpState, const IChannelBlend* pBlend)
{
	int srcY = 0;
	if (pBitmap->N > 1 && bmpState > 1) {
		srcY = int(0.5 + (double) pBitmap->H * (double) (bmpState - 1) / (double) pBitmap->N);
	}
	return DrawBitmap(pBitmap, pR, 0, srcY, pBlend);    
}

bool IGraphics::DrawRect(const IColor* pColor, IRECT* pR)
{
  bool rc = DrawHorizontalLine(pColor, pR->T, pR->L, pR->R);
  rc &= DrawHorizontalLine(pColor, pR->B, pR->L, pR->R);
  rc &= DrawVerticalLine(pColor, pR->L, pR->T, pR->B);
  rc &= DrawVerticalLine(pColor, pR->R, pR->T, pR->B);
  return rc;
}

bool IGraphics::DrawVerticalLine(const IColor* pColor, IRECT* pR, float x)
{
  x = BOUNDED(x, 0.0f, 1.0f);
  int xi = pR->L + int(x * (float) (pR->R - pR->L));
  return DrawVerticalLine(pColor, xi, pR->T, pR->B);
}

bool IGraphics::DrawHorizontalLine(const IColor* pColor, IRECT* pR, float y)
{
  y = BOUNDED(y, 0.0f, 1.0f);
  int yi = pR->B - int(y * (float) (pR->B - pR->T));
  return DrawHorizontalLine(pColor, yi, pR->L, pR->R);
}

bool IGraphics::DrawRadialLine(const IColor* pColor, float cx, float cy, float angle, float rMin, float rMax, 
  const IChannelBlend* pBlend, bool antiAlias)
{
  float sinV = sin(angle);
  float cosV = cos(angle);
  float xLo = cx + rMin * sinV;
  float xHi = cx + rMax * sinV;
  float yLo = cy - rMin * cosV;
  float yHi = cy - rMax * cosV;
  return DrawLine(pColor, xLo, yLo, xHi, yHi, pBlend, antiAlias);
}

bool IGraphics::IsDirty(IRECT* const pR)
{
	bool dirty = false;
	const int n = mControls.GetSize();
	IControl* const* const ppControl = mControls.GetList();
	for (int i = 0; i < n; ++i)
	{
		IControl* const pControl = ppControl[i];
		if (pControl->IsDirty())
		{
			*pR = pR->Union(pControl->GetRECT());
			dirty = true;
		}
	}

	const int scale = Scale();
	if (scale)
	{
		// const int mask = ~((1 << scale) - 1);
		// pR->L &= mask;
		// pR->T &= mask;
		// pr->R = (((pr->R - 1) >> scale) + 1) << scale;
		// pr->B = (((pr->B - 1) >> scale) + 1) << scale;

		assert(scale == 1);
		static const int mask = ~1;
		pR->L &= mask;
		pR->T &= mask;
		pR->R += pR->R & 1;
		pR->B += pR->B & 1;
	}

	#ifdef IPLUG_USE_IDLE_CALLS
	if (dirty)
	{
		mIdleTicks = 0;
	}
	else if (++mIdleTicks > kIdleTicks)
	{
		OnGUIIdle();
		mIdleTicks = 0;
	}
	#endif

	return dirty;
}

void IGraphics::DisplayControlValue(IControl* pControl)
{
//  char str[32];
//  int paramIdx = pControl->ParamIdx();
//  if (paramIdx >= 0) {
//    IParam* pParam = mPlug->GetParam(paramIdx);
//    pParam->GetDisplayForHost(str);
//    IRECT r = *(pControl->GetRECT());
//    r.L = r.MW() - 10;
//    r.R = r.L + 20;
//    r.T = r.MH() - 5;
//    r.B = r.T + 10;    
//    DrawIText(&IText(), str, &r);
//  }  
}  
                         
// The OS is announcing what needs to be redrawn,
// which may be a larger area than what is strictly dirty.
void IGraphics::Draw(const IRECT* const pR)
{
	const int n = mControls.GetSize();
	IControl* const* const ppControl = mControls.GetList();
	for (int i = 0; i < n; ++i)
	{
		IControl* const pControl = ppControl[i];
		if (!(pControl->IsHidden()) && pR->Intersects(pControl->GetRECT()))
		{
			pControl->Draw(this);
		}
		pControl->SetClean();
	}

	DrawScreen(pR);
}

void IGraphics::OnMouseDown(int x, int y, IMouseMod* pMod)
{
	ReleaseMouseCapture();
  int c = GetMouseControlIdx(x, y);
	if (c >= 0) {
		mMouseCapture = c;
		mMouseX = x;
		mMouseY = y;
    mDisplayControlValue = (pMod->R);
    IControl* pControl = mControls.Get(c);
    int paramIdx = pControl->ParamIdx();
    if (paramIdx >= 0) {
      mPlug->BeginInformHostOfParamChange(paramIdx);
    }    
    pControl->OnMouseDown(x, y, pMod);
  }
}

void IGraphics::OnMouseUp(int x, int y, IMouseMod* pMod)
{
	int c = GetMouseControlIdx(x, y);
	mMouseCapture = mMouseX = mMouseY = -1;
  mDisplayControlValue = false;
	if (c >= 0) {
    IControl* pControl = mControls.Get(c);
		pControl->OnMouseUp(x, y, pMod);
    int paramIdx = pControl->ParamIdx();
    if (paramIdx >= 0) {
      mPlug->EndInformHostOfParamChange(paramIdx);
    }    
	}
}

bool IGraphics::OnMouseOver(int x, int y, IMouseMod* pMod)
{
  if (mHandleMouseOver) {
    int c = GetMouseControlIdx(x, y);
    if (c >= 0) {
	    mMouseX = x;
	    mMouseY = y;
	    mControls.Get(c)->OnMouseOver(x, y, pMod);
      if (mMouseOver >= 0 && mMouseOver != c) {
        mControls.Get(mMouseOver)->OnMouseOut();
      }
      mMouseOver = c;
    }
  }
  return mHandleMouseOver;
}

void IGraphics::OnMouseOut()
{
  int i, n = mControls.GetSize();
  IControl** ppControl = mControls.GetList();
	for (i = 0; i < n; ++i, ++ppControl) {
		IControl* pControl = *ppControl;
		pControl->OnMouseOut();
	}
  mMouseOver = -1;
}

void IGraphics::OnMouseDrag(int x, int y, IMouseMod* pMod)
{
  int c = mMouseCapture;
  if (c >= 0) {
	  int dX = x - mMouseX;
	  int dY = y - mMouseY;
    if (dX != 0 || dY != 0) {
      mMouseX = x;
      mMouseY = y;
      mControls.Get(c)->OnMouseDrag(x, y, dX, dY, pMod);
    }
  }
}

bool IGraphics::OnMouseDblClick(int x, int y, IMouseMod* pMod)
{
	ReleaseMouseCapture();
  bool newCapture = false;
	int c = GetMouseControlIdx(x, y);
	if (c >= 0) {
    IControl* pControl = mControls.Get(c);
    int paramIdx = pControl->ParamIdx();
    if (paramIdx >= 0) {
      mPlug->BeginInformHostOfParamChange(paramIdx);
    }
    if (pControl->MouseDblAsSingleClick()) {
      mMouseCapture = c;
      mMouseX = x;
      mMouseY = y;
      pControl->OnMouseDown(x, y, pMod);
      newCapture = true;
    }
    else {
		  pControl->OnMouseDblClick(x, y, pMod);
    }
    // OnMouseUp() will call EndInformHostOfParamChange().
	}
  return newCapture;
}

void IGraphics::OnMouseWheel(int x, int y, IMouseMod* pMod, int d)
{	
	int c = GetMouseControlIdx(x, y);
	if (c >= 0) {
		IControl* pControl = mControls.Get(c);
		int paramIdx = pControl->ParamIdx();
		if (paramIdx >= 0) {
			mPlug->BeginDelayedInformHostOfParamChange(paramIdx);
		}
		pControl->OnMouseWheel(x, y, pMod, d);
		if (paramIdx >= 0) {
			mPlug->DelayEndInformHostOfParamChange(paramIdx);
		}
	}
}

void IGraphics::ReleaseMouseCapture()
{
	mMouseCapture = mMouseX = mMouseY = -1;
}

void IGraphics::OnKeyDown(int x, int y, int key)
{
	int c = GetMouseControlIdx(x, y);
	if (c >= 0) {
		mControls.Get(c)->OnKeyDown(x, y, key);
	}
}

int IGraphics::GetMouseControlIdx(int x, int y)
{
	if (mMouseCapture >= 0) {
		return mMouseCapture;
	}
	// The BG is a control and will catch everything, so assume the programmer
	// attached the controls from back to front, and return the frontmost match.
  int i = mControls.GetSize() - 1;
  IControl** ppControl = mControls.GetList() + i;
	for (/* */; i >= 0; --i, --ppControl) {
    IControl* pControl = *ppControl;
    if (!pControl->IsHidden() && !pControl->IsGrayed() && pControl->IsHit(x, y)) {
      return i;
    }
	}
	return -1;
}

void IGraphics::OnGUIIdle()
{
  int i, n = mControls.GetSize();
  IControl** ppControl = mControls.GetList();
	for (i = 0; i < n; ++i, ++ppControl) {
		IControl* pControl = *ppControl;
		pControl->OnGUIIdle();
	}
}
