#include "IGraphics.h"
#include "IControl.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "WDL/mutex.h"
#include "WDL/wdltypes.h"

const int IGraphics::kDefaultFPS;

class BitmapStorage
{
public:
	WDL_IntKeyedArray<LICE_IBitmap*> m_bitmaps;
	WDL_Mutex m_mutex;

	BitmapStorage(): m_bitmaps(Dispose) {}

	LICE_IBitmap* Find(const int id)
	{
		m_mutex.Enter();
		LICE_IBitmap* const bitmap = m_bitmaps.Get(id, NULL);
		m_mutex.Leave();
		return bitmap;
	}

	void Add(LICE_IBitmap* const bitmap, const int id)
	{
		m_mutex.Enter();
		m_bitmaps.Insert(id, bitmap);
		m_mutex.Leave();
	}

	/* void Remove(LICE_IBitmap* const bitmap)
	{
		m_mutex.Enter();
		const int* const id = m_bitmaps.ReverseLookupPtr(bitmap);
		if (id)
		{
			m_bitmaps.Delete(*id);
			delete bitmap;
		}
		m_mutex.Leave();
	} */

	static void Dispose(LICE_IBitmap* const bitmap)
	{
		delete bitmap;
	}

	static const LICE_WrapperBitmap kEmptyBitmap;
};

const LICE_WrapperBitmap BitmapStorage::kEmptyBitmap(NULL, 0, 0, 0, false);

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

IGraphics::IGraphics(
	IPlugBase* const pPlug,
	const int w,
	const int h,
	const int refreshFPS
):
	mPlug(pPlug),
	mDrawBitmap(&mBackBuf),
	// mTmpBitmap(NULL),
	mDirtyRECT(NULL),
	mWidth(w),
	mHeight(h),
	mScale(-1),
	mDefaultScale(kScaleFull),
	mFPS(refreshFPS > 0 ? refreshFPS : kDefaultFPS),
	mMouseCapture(-1),
	mMouseOver(-1),
	mMouseX(0),
	mMouseY(0),
	mKeyboardFocus(-1),
	mHandleMouseOver(false),
	mEnableTooltips(false),
	mHandleMouseWheel(kMouseWheelEnable)

	#ifdef IPLUG_USE_IDLE_CALLS
	, mIdleTicks(0)
	#endif
{
}

IGraphics::~IGraphics()
{
	mControls.Empty(true);
	// delete mTmpBitmap;
}

/* void IGraphics::Resize(const int w, const int h)
{
	// The OS implementation class has to do all the work, then call up to here.
	mWidth = w;
	mHeight = h;
	ReleaseMouseCapture();
	mControls.Empty(true);
	mPlug->ResizeGraphics(w, h);
} */

void IGraphics::SetFromStringAfterPrompt(IControl* const pControl, const IParam* const pParam, const char* const txt)
{
	if (pParam)
	{
		double v;
		const bool mapped = pParam->MapDisplayText(txt, &v);
		if (!mapped)
		{
			v = strtod(txt, NULL);
			if (pParam->DisplayIsNegated()) v = -v;
			v = pParam->GetNormalized(v);
		}
		const int paramIdx = pControl->ParamIdx();
		if (paramIdx >= 0)
		{
			mPlug->BeginInformHostOfParamChange(paramIdx);
		}
		pControl->SetValueFromUserInput(v);
		if (paramIdx >= 0)
		{
			mPlug->EndInformHostOfParamChange(paramIdx);
		}
	}
	else
	{
		pControl->SetTextFromUserInput(txt);
	}
}

bool IGraphics::UserDataPath(WDL_String* const pPath, const char* const mfrName, const char* const plugName)
{
	const bool ok = UserDataPath(pPath);
	if (ok)
	{
		pPath->Append(WDL_DIRCHAR_STR);
		pPath->Append(mfrName);

		if (plugName)
		{
			pPath->Append(WDL_DIRCHAR_STR);
			pPath->Append(plugName);
		}
	}
	return ok;
}

void IGraphics::AttachBackground(const int ID, const char* const name)
{
	const IBitmap bg = LoadIBitmap(ID, name);
	IControl* const pBG = new IBackgroundControl(mPlug, &bg);
	mControls.Insert(0, pBG);
}

void IGraphics::HideControl(const int paramIdx, const bool hide)
{
	const int n = mControls.GetSize();
	IControl* const* const ppControl = mControls.GetList();
	for (int i = 0; i < n; ++i)
	{
		IControl* const pControl = ppControl[i];
		if (pControl->ParamIdx() == paramIdx)
		{
			pControl->Hide(hide);
		}
		// Could be more than one, don't break until we check them all.
	}
}

void IGraphics::GrayOutControl(const int paramIdx, const bool gray)
{
	const int n = mControls.GetSize();
	IControl* const* const ppControl = mControls.GetList();
	for (int i = 0; i < n; ++i)
	{
		IControl* const pControl = ppControl[i];
		if (pControl->ParamIdx() == paramIdx)
		{
			pControl->GrayOut(gray);
		}
		// Could be more than one, don't break until we check them all.
	}
}

void IGraphics::ClampControl(const int paramIdx, double lo, double hi, const bool normalized)
{
	if (!normalized)
	{
		const IParam* const pParam = mPlug->GetParam(paramIdx);
		lo = pParam->GetNormalized(lo);
		hi = pParam->GetNormalized(hi);
	}
	const int n = mControls.GetSize();
	IControl* const* const ppControl = mControls.GetList();
	for (int i = 0; i < n; ++i)
	{
		IControl* const pControl = ppControl[i];
		if (pControl->ParamIdx() == paramIdx)
		{
			pControl->Clamp(lo, hi);
			pControl->SetDirty();
		}
		// Could be more than one, don't break until we check them all.
	}
}

void IGraphics::SetParameterFromPlug(const int paramIdx, double value, const bool normalized)
{
	if (!normalized)
	{
		const IParam* const pParam = mPlug->GetParam(paramIdx);
		value = pParam->GetNormalized(value);
	}
	const int n = mControls.GetSize();
	IControl* const* const ppControl = mControls.GetList();
	for (int i = 0; i < n; ++i)
	{
		IControl* const pControl = ppControl[i];
		if (pControl->ParamIdx() == paramIdx)
		{
			pControl->SetValueFromPlug(value);
			// Could be more than one, don't break until we check them all.
		}
	}
}

void IGraphics::SetControlFromPlug(const int controlIdx, const double normalizedValue)
{
	IControl* const pControl = mControls.Get(controlIdx);
	if (pControl) pControl->SetValueFromPlug(normalizedValue);
}

void IGraphics::SetAllControlsDirty()
{
	const int n = mControls.GetSize();
	IControl* const* const ppControl = mControls.GetList();
	for (int i = 0; i < n; ++i)
	{
		IControl* const pControl = ppControl[i];
		pControl->SetDirty(false);
	}
}

void IGraphics::SetParameterFromGUI(const int paramIdx, const double normalizedValue)
{
	const int n = mControls.GetSize();
	IControl* const* const ppControl = mControls.GetList();
	for (int i = 0; i < n; ++i)
	{
		IControl* const pControl = ppControl[i];
		if (pControl->ParamIdx() == paramIdx)
		{
			pControl->SetValueFromUserInput(normalizedValue);
			// Could be more than one, don't break until we check them all.
		}
	}
}

void IGraphics::LoadBitmapResources(const BitmapResource* const pResources)
{
	for (int i = 0; pResources[i].mID; ++i)
	{
		LoadIBitmap(pResources[i].mID, pResources[i].Name());
	}
}

IBitmap IGraphics::LoadIBitmap(const int ID, const char* const name, const int nStates)
{
	LICE_IBitmap* lb = s_bitmapCache.Find(ID);
	if (!lb)
	{
		lb = OSLoadBitmap(ID, name);

		#ifndef NDEBUG
		{
			// Protect against typos in resource.h and .rc files.
			// Also, don't forget to add image files to Xcode project,
			// and beware that on macOS filenames are case-sensitive.
			const bool imgResourceNotFound = !!lb;
			assert(imgResourceNotFound);
		}
		#endif

		s_bitmapCache.Add(lb, ID);
	}
	return IBitmap(lb, lb->getWidth(), lb->getHeight() / nStates, nStates);
}

bool IGraphics::UpdateIBitmap(IBitmap* const pBitmap)
{
	LICE_IBitmap* const empty = (LICE_IBitmap*)&BitmapStorage::kEmptyBitmap;
	LICE_IBitmap* lb = (LICE_IBitmap*)pBitmap->mData;

	const int ID = pBitmap->ID() | Scale();
	if (pBitmap->mID == ID && lb) return lb != empty;

	lb = s_bitmapCache.Find(ID);
	if (!lb) lb = empty;

	pBitmap->mData = lb;
	pBitmap->W = lb->getWidth();
	pBitmap->H = lb->getHeight() / pBitmap->N;
	pBitmap->mID = ID;

	return lb != empty;
}

void IGraphics::Rescale(const int scale)
{
	assert(scale == kScaleFull || scale == kScaleHalf);

	const int w = mWidth >> scale;
	const int h = mHeight >> scale;
	mScale = scale;

	mBackBuf.resize(w, h);
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
	return !!mBackBuf.getBits();
}

void IGraphics::DrawBitmap(const IBitmap* const pIBitmap, const IRECT* const pDest, int srcX, int srcY, const float weight)
{
	LICE_IBitmap* const pLB = (LICE_IBitmap*)pIBitmap->mData;
	IRECT r = *pDest;

	const int scale = Scale();
	if (scale) { r.Downscale(scale); srcX >>= scale; srcY >>= scale; }

	LICE_Blit(mDrawBitmap, pLB, r.L, r.T, srcX, srcY, r.W(), r.H(), weight, IChannelBlend::kBlendNone);
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

	LICE_RotatedBlit(mDrawBitmap, pLB, destX, destY, W, H, 0.0f, 0.0f, (float)W, (float)H, (float)angle,
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
	LICE_Blit(mDrawBitmap, mTmpBitmap, x >> scale, y >> scale, 0, 0, W, H, weight, IChannelBlend::kBlendNone);
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
	LICE_PutPixel(mDrawBitmap, (int)(x + 0.5f), (int)(y + 0.5f), color.Get(), weight, IChannelBlend::kBlendNone);
}

void IGraphics::ForcePixel(const IColor color, const int x, const int y)
{
	const int scale = Scale();
	LICE_pixel* px = mDrawBitmap->getBits();
	px += (x >> scale) + (y >> scale) * mDrawBitmap->getRowSpan();
	*px = color.Get();
}

void IGraphics::DrawLine(const IColor color, const int x1, const int y1, const int x2, const int y2,
	const float weight, const bool antiAlias)
{
	const int scale = Scale();
	LICE_Line(mDrawBitmap, x1 >> scale, y1 >> scale, x2 >> scale, y2 >> scale, color.Get(), weight,
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
	LICE_Arc(mDrawBitmap, cx, cy, r, minAngle, maxAngle, color.Get(), weight, IChannelBlend::kBlendNone, antiAlias);
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
	LICE_Circle(mDrawBitmap, cx, cy, r, color.Get(), weight, IChannelBlend::kBlendNone, antiAlias);
}

void IGraphics::RoundRect(const IColor color, const IRECT* const pR, const float weight, int cornerradius,
	const bool aa)
{
	IRECT r = *pR;
	const int scale = Scale();
	if (scale) { r.Downscale(scale); cornerradius >>= scale; }

	LICE_RoundRect(mDrawBitmap, (float)r.L, (float)r.T, (float)r.W(), (float)r.H(), cornerradius,
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

	LICE_FillRect(mDrawBitmap, x1 + cornerradius, y1, w - 2 * cornerradius, h, col, weight, mode);
	LICE_FillRect(mDrawBitmap, x1, y1 + cornerradius, cornerradius, h - 2 * cornerradius, col, weight, mode);
	LICE_FillRect(mDrawBitmap, x1 + w - cornerradius, y1 + cornerradius, cornerradius, h - 2 * cornerradius, col, weight, mode);

	LICE_FillCircle(mDrawBitmap, (float)(x1 + cornerradius), (float)(y1 + cornerradius), (float)cornerradius, col, weight, mode, aa);
	LICE_FillCircle(mDrawBitmap, (float)(x1 + w - cornerradius - 1), (float)(y1 + h - cornerradius - 1), (float)cornerradius, col, weight, mode, aa);
	LICE_FillCircle(mDrawBitmap, (float)(x1 + w - cornerradius - 1), (float)(y1 + cornerradius), (float)cornerradius, col, weight, mode, aa);
	LICE_FillCircle(mDrawBitmap, (float)(x1 + cornerradius), (float)(y1 + h - cornerradius - 1), (float)cornerradius, col, weight, mode, aa);
}

void IGraphics::FillIRect(const IColor color, const IRECT* const pR, const float weight)
{
	IRECT r = *pR;
	const int scale = Scale();
	if (scale) r.Downscale(scale);

	LICE_FillRect(mDrawBitmap, r.L, r.T, r.W(), r.H(), color.Get(), weight, IChannelBlend::kBlendNone);
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
	LICE_FillCircle(mDrawBitmap, cx, cy, r, color.Get(), weight, IChannelBlend::kBlendNone, antiAlias);
}

int IGraphics::DrawIText(IText* const pTxt, const char* const str, const IRECT* const pR, const int clip)
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
		return lh;
	}

	const LICE_pixel color = pTxt->mColor.Get();
	font->SetTextColor(color);

	static const UINT align[3] = { DT_LEFT, DT_CENTER, DT_RIGHT };
	assert(pTxt->mAlign >= 0 && pTxt->mAlign < 3);

	UINT fmt = align[pTxt->mAlign] | clip;
	if (LICE_GETA(color) < 255) fmt |= LICE_DT_USEFGALPHA;

	RECT R = { pR->L, pR->T, pR->R, pR->B };

	if (scale)
	{
		R.left >>= scale;
		R.top >>= scale;
		R.right >>= scale;
		R.bottom >>= scale;
	}

	// TN: Quick patch to fix vertical offset.
	#ifdef __APPLE__
	R.top--;
	R.top = wdl_max(R.top, 0);
	#endif

	const int h = font->DrawText(mDrawBitmap, str, -1, &R, fmt) << scale;

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
	int h = font->DrawText(mDrawBitmap, str, -1, &R, fmt);

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
	const LICE_pixel pix = LICE_GetPixel(mDrawBitmap, x >> scale, y >> scale);
	return IColor(LICE_GETA(pix), LICE_GETR(pix), LICE_GETG(pix), LICE_GETB(pix));
}

void IGraphics::DrawVerticalLine(const IColor color, int xi, int yLo, int yHi)
{
	const int scale = Scale();
	if (scale) { xi >>= scale; yLo >>= scale; yHi >>= scale; }

	LICE_Line(mDrawBitmap, xi, yLo, xi, yHi, color.Get(), 1.0f, LICE_BLIT_MODE_COPY, false);
}

void IGraphics::DrawHorizontalLine(const IColor color, int yi, int xLo, int xHi)
{
	const int scale = Scale();
	if (scale) { yi >>= scale; xLo >>= scale; xHi >>= scale; }

	LICE_Line(mDrawBitmap, xLo, yi, xHi, yi, color.Get(), 1.0f, LICE_BLIT_MODE_COPY, false);
}

void IGraphics::DrawBitmap(const IBitmap* const pBitmap, const IRECT* const pR, const int bmpState, const float weight)
{
	LICE_IBitmap* const pLB = (LICE_IBitmap*)pBitmap->mData;
	const int srcY = pBitmap->N > 1 && bmpState > 1 ? (bmpState - 1) * pBitmap->H : 0;

	IRECT r = *pR;
	const int scale = Scale();
	if (scale) r.Downscale(scale);

	LICE_Blit(mDrawBitmap, pLB, r.L, r.T, 0, srcY, r.W(), r.H(), weight, IChannelBlend::kBlendNone);
}

void IGraphics::DrawRect(const IColor color, const IRECT* const pR)
{
	DrawHorizontalLine(color, pR->T, pR->L, pR->R);
	DrawHorizontalLine(color, pR->B, pR->L, pR->R);
	DrawVerticalLine(color, pR->L, pR->T, pR->B);
	DrawVerticalLine(color, pR->R, pR->T, pR->B);
}

void IGraphics::DrawVerticalLine(const IColor color, const IRECT* const pR, float x)
{
	x = wdl_max(x, 0.0f);
	x = wdl_min(x, 1.0f);

	const int xi = pR->L + (int)(x * (float)pR->W());
	DrawVerticalLine(color, xi, pR->T, pR->B);
}

void IGraphics::DrawHorizontalLine(const IColor color, const IRECT* const pR, float y)
{
	y = wdl_max(y, 0.0f);
	y = wdl_min(y, 1.0f);

	const int yi = pR->B - (int)(y * (float)pR->H());
	DrawHorizontalLine(color, yi, pR->L, pR->R);
}

void IGraphics::DrawRadialLine(const IColor color, const float cx, const float cy, const float angle, const float rMin, const float rMax,
	const float weight, const bool antiAlias)
{
	const float sinV = sinf(angle);
	const float cosV = cosf(angle);
	const int xLo = (int)(cx + rMin * sinV);
	const int xHi = (int)(cx + rMax * sinV);
	const int yLo = (int)(cy - rMin * cosV);
	const int yHi = (int)(cy - rMax * cosV);
	DrawLine(color, xLo, yLo, xHi, yHi, weight, antiAlias);
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
			pControl->SetClean();
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

// The OS is announcing what needs to be redrawn,
// which may be a larger area than what is strictly dirty.
void IGraphics::Draw(const IRECT* const pR)
{
	mDirtyRECT = pR;

	const int n = mControls.GetSize();
	IControl* const* const ppControl = mControls.GetList();
	for (int i = 0; i < n; ++i)
	{
		IControl* const pControl = ppControl[i];
		if (!pControl->IsHidden() && pR->Intersects(pControl->GetRECT()))
		{
			pControl->Draw(this);
		}
	}

	mDirtyRECT = NULL;
	DrawScreen(pR);
}

void IGraphics::OnMouseDown(const int x, const int y, const IMouseMod mod)
{
	ReleaseMouseCapture();
	const int c = GetMouseControlIdx(x, y);
	if (c >= 0)
	{
		mMouseCapture = c;
		mMouseX = x;
		mMouseY = y;
		IControl* const pControl = mControls.Get(c);
		const int paramIdx = pControl->ParamIdx();
		if (paramIdx >= 0)
		{
			mPlug->BeginInformHostOfParamChange(paramIdx);
		}
		pControl->OnMouseDown(x, y, mod);
	}
}

void IGraphics::OnMouseUp(const int x, const int y, const IMouseMod mod)
{
	const int cap = mMouseCapture;
	const int c = cap >= 0 ? cap : GetMouseControlIdx(x, y);
	mMouseY = mMouseX = mMouseCapture = -1;
	IControl* pControl;
	if (c >= 0)
	{
		pControl = mControls.Get(c);
		pControl->OnMouseUp(x, y, mod);
	}
	if (cap >= 0)
	{
		if (cap != c) pControl = mControls.Get(cap);
		const int paramIdx = pControl->ParamIdx();
		if (paramIdx >= 0)
		{
			mPlug->EndInformHostOfParamChange(paramIdx);
		}
	}
}

void IGraphics::OnMouseOver(const int x, const int y, const IMouseMod mod)
{
	assert(mHandleMouseOver == true);

	const int cap = mMouseCapture;
	const int c = cap >= 0 ? cap : GetMouseControlIdx(x, y);
	if (c >= 0)
	{
		mMouseX = x;
		mMouseY = y;
		mControls.Get(c)->OnMouseOver(x, y, mod);
		if (mMouseOver >= 0 && mMouseOver != c)
		{
			mControls.Get(mMouseOver)->OnMouseOut();
		}
		mMouseOver = c;
	}
}

void IGraphics::OnMouseOut()
{
	const int n = mControls.GetSize();
	IControl* const* const ppControl = mControls.GetList();
	for (int i = 0; i < n; ++i)
	{
		IControl* const pControl = ppControl[i];
		pControl->OnMouseOut();
	}
	mMouseOver = -1;
}

void IGraphics::OnMouseDrag(const int x, const int y, const IMouseMod mod)
{
	const int c = mMouseCapture;
	if (c >= 0)
	{
		const int dX = x - mMouseX;
		const int dY = y - mMouseY;
		if (dX != 0 || dY != 0)
		{
			mMouseX = x;
			mMouseY = y;
			mControls.Get(c)->OnMouseDrag(x, y, dX, dY, mod);
		}
	}
}

bool IGraphics::OnMouseDblClick(const int x, const int y, const IMouseMod mod)
{
	ReleaseMouseCapture();
	bool newCapture = false;
	const int c = GetMouseControlIdx(x, y);
	if (c >= 0)
	{
		IControl* const pControl = mControls.Get(c);
		const int paramIdx = pControl->ParamIdx();
		if (paramIdx >= 0)
		{
			mPlug->BeginInformHostOfParamChange(paramIdx);
		}
		if (pControl->MouseDblAsSingleClick() || mod.R)
		{
			mMouseCapture = c;
			mMouseX = x;
			mMouseY = y;
			pControl->OnMouseDown(x, y, mod);
			newCapture = true;
			// OnMouseUp() will call EndInformHostOfParamChange().
		}
		else
		{
			pControl->OnMouseDblClick(x, y, mod);
			if (paramIdx >= 0)
			{
				mPlug->EndInformHostOfParamChange(paramIdx);
			}
		}
	}
	return newCapture;
}

void IGraphics::OnMouseWheel(const int x, const int y, const IMouseMod mod, const float d)
{
	const int cap = mMouseCapture;
	const int c = cap >= 0 ? cap : GetMouseControlIdx(x, y);
	if (c >= 0)
	{
		IControl* const pControl = mControls.Get(c);
		const int paramIdx = pControl->ParamIdx();
		if (paramIdx >= 0)
		{
			mPlug->BeginDelayedInformHostOfParamChange(paramIdx);
		}
		pControl->OnMouseWheel(x, y, mod, d);
		if (paramIdx >= 0)
		{
			mPlug->DelayEndInformHostOfParamChange(paramIdx);
		}
	}
}

bool IGraphics::OnKeyDown(const int x, const int y, const IMouseMod mod, const int key)
{
	const int c = mKeyboardFocus;
	return c >= 0 ? mControls.Get(c)->OnKeyDown(x, y, mod, key) : false;
}

bool IGraphics::OnKeyUp(const int x, const int y, const IMouseMod mod, const int key)
{
	const int c = mKeyboardFocus;
	return c >= 0 ? mControls.Get(c)->OnKeyUp(x, y, mod, key) : false;
}

int IGraphics::GetMouseControlIdx(const int x, const int y)
{
	// The BG is a control and will catch everything, so assume the programmer
	// attached the controls from back to front, and return the frontmost match.
	IControl* const* const ppControl = mControls.GetList();
	for (int i = mControls.GetSize() - 1; i >= 0; --i)
	{
		IControl* const pControl = ppControl[i];
		if (!pControl->IsHidden() && !pControl->IsReadOnly() && pControl->IsHit(x, y))
		{
			return i;
		}
	}
	return -1;
}

void IGraphics::EndInformHostOfParamChange(const int controlIdx)
{
	const int paramIdx = mControls.Get(controlIdx)->ParamIdx();
	if (paramIdx >= 0)
	{
		mPlug->EndInformHostOfParamChange(paramIdx);
	}
}

#ifdef IPLUG_USE_IDLE_CALLS
void IGraphics::OnGUIIdle()
{
	const int n = mControls.GetSize();
	IControl* const* const ppControl = mControls.GetList();
	for (int i = 0; i < n; ++i)
	{
		IControl* const pControl = ppControl[i];
		pControl->OnGUIIdle();
	}
}
#endif
