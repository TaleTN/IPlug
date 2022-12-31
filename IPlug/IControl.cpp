#include "IControl.h"

const float IControl::kGrayedAlpha = 0.25f;

void IControl::SetValueFromPlug(const double value)
{
	assert(value >= 0.0 && value <= 1.0);

	if (GetValue() != value)
	{
		SetValue(value);
		SetDirty(false);
	}
}

void IControl::SetValueFromUserInput(const double value)
{
	assert(value >= 0.0 && value <= 1.0);

	if (GetValue() != value)
	{
		SetValue(value);
		SetDirty();
	}
}

void IControl::SetDirty(const bool pushParamToPlug)
{
	if (!mHide) mDirty = 1;

	if (pushParamToPlug && mParamIdx >= 0)
	{
		const double value = GetValue();
		mPlug->SetParameterFromGUI(mParamIdx, value);

		if (mAutoUpdate)
		{
			IGraphics* const pGraphics = GetGUI();
			if (pGraphics)
			{
				pGraphics->SetParameterFromPlug(mParamIdx, value, true);
			}
		}
	}
}

void IControl::Hide(const bool hide)
{
	mHide = hide;
	Redraw();
	SetDirty(false);
}

void IControl::GrayOut(const bool gray)
{
	mReadOnly = mGrayed = gray;
	SetDirty(false);
}

void IControl::PromptUserInput()
{
	if (mParamIdx >= 0)
	{
		mPlug->GetGUI()->PromptUserInput(this, mPlug->GetParam(mParamIdx));
		Redraw();
	}
}

bool IControl::IsHit(const int x, const int y)
{
	return mRECT.Contains(x, y);
}

char* IControl::GetTextForUserInput(char* const buf, const int bufSize)
{
	assert(bufSize >= 1);
	*buf = 0;
	return buf;
}

IBackgroundControl::IBackgroundControl(
	IPlugBase* const pPlug,
	const IBitmap* const pBitmap
):
	IControl(pPlug)
{
	mRECT = IRECT(0, 0, pBitmap);
	mBitmap = *pBitmap;
}

void IBackgroundControl::Draw(IGraphics* const pGraphics)
{
	LICE_IBitmap* const dest = pGraphics->GetDrawBitmap();

	const int scale = pGraphics->Scale();
	IRECT dirty = *pGraphics->GetDirtyRECT();

	int x1 = mRECT.L;
	int y1 = mRECT.T;

	if (scale)
	{
		// const int mask = (1 << scale) - 1;

		// dirty.L >>= scale;
		// dirty.T >>= scale;
		// dirty.R = (dirty.R >> scale) + !!(dirty.R & mask);
		// dirty.B = (dirty.B >> scale) + !!(dirty.B & mask);

		// x1 >>= scale;
		// y1 >>= scale;

		assert(scale == 1);

		dirty.L >>= 1;
		dirty.T >>= 1;
		dirty.R = (dirty.R >> 1) + (dirty.R & 1);
		dirty.B = (dirty.B >> 1) + (dirty.B & 1);

		x1 >>= 1;
		y1 >>= 1;
	}

	int x2 = x1 + mBitmap.W;
	int y2 = y1 + mBitmap.H;

	int xOfs = dirty.L - x1;
	int yOfs = dirty.T - y1;

	xOfs = wdl_max(xOfs, 0);
	yOfs = wdl_max(yOfs, 0);

	x1 += xOfs;
	y1 += yOfs;

	x2 = wdl_min(x2, dirty.R);
	y2 = wdl_min(y2, dirty.B);

	const int w = x2 - x1;
	const int h = y2 - y1;

	LICE_IBitmap* const src = (LICE_IBitmap*)mBitmap.mData;
	LICE_Blit(dest, src, x1, y1, xOfs, yOfs, w, h, 1.0f, LICE_BLIT_MODE_COPY);
}

void IBackgroundControl::Rescale(IGraphics* const pGraphics)
{
	pGraphics->UpdateIBitmap(&mBitmap);
}

IBitmapControl::IBitmapControl(
	IPlugBase* const pPlug,
	const int x,
	const int y,
	const int paramIdx,
	const IBitmap* const pBitmap
):
	IControl(pPlug, NULL, paramIdx)
{
	if (pBitmap)
	{
		mTargetRECT = mRECT = IRECT(x, y, pBitmap);
		mBitmap = *pBitmap;
	}

	mTooltip = NULL;
	mValue = 0.0;
}

IBitmapControl::IBitmapControl(
	IPlugBase* const pPlug,
	const int x,
	const int y,
	const IBitmap* const pBitmap
):
	IControl(pPlug)
{
	mTargetRECT = mRECT = IRECT(x, y, pBitmap);
	mBitmap = *pBitmap;

	mTooltip = NULL;
	mValue = 0.0;
}

void IBitmapControl::Draw(IGraphics* const pGraphics)
{
	LICE_IBitmap* const dest = pGraphics->GetDrawBitmap();

	const int scale = pGraphics->Scale();

	const int x = mRECT.L >> scale;
	const int y = mRECT.T >> scale;

	const int n = mBitmap.N - 1;
	int i = (int)((double)n * mValue + 0.5);

	if (mReverse) i = n - i;

	LICE_IBitmap* const src = (LICE_IBitmap*)mBitmap.mData;
	const float weight = mGrayed ? kGrayedAlpha : 1.0f;

	LICE_Blit(dest, src, x, y, 0, i * mBitmap.H, mBitmap.W, mBitmap.H, weight, LICE_BLIT_MODE_COPY | LICE_BLIT_USE_ALPHA);
}

void IBitmapControl::SetValue(const double value)
{
	assert(value >= 0.0 && value <= 1.0);
	mValue = value;
}

void IBitmapControl::UpdateValue(double value)
{
	value = wdl_max(value, 0.0);
	value = wdl_min(value, 1.0);

	if (mValue != value)
	{
		mValue = value;
		SetDirty();
	}
}

void IBitmapControl::SetTargetArea(const IRECT* const pR)
{
	mTargetRECT = *pR;
}

bool IBitmapControl::IsHit(const int x, const int y)
{
	return mTargetRECT.Contains(x, y);
}

void IBitmapControl::Clamp(const double lo, const double hi)
{
	assert(lo >= 0.0 && lo <= 1.0);
	assert(hi >= 0.0 && hi <= 1.0);

	mValue = wdl_max(mValue, lo);
	mValue = wdl_min(mValue, hi);
}

void IBitmapControl::Rescale(IGraphics* const pGraphics)
{
	pGraphics->UpdateIBitmap(&mBitmap);
}

ISwitchControl::ISwitchControl(
	IPlugBase* const pPlug,
	const int x,
	const int y,
	const int paramIdx,
	const IBitmap* const pBitmap
):
	IBitmapControl(pPlug, x, y, paramIdx, pBitmap)
{
	mDisablePrompt = 1;
	mDblAsSingleClick = 1;
}

void ISwitchControl::OnMouseDown(int, int, const IMouseMod mod)
{
	if (mod.R && !mDisablePrompt)
	{
		PromptUserInput();
		return;
	}

	if (!mod.L) return;

	int n = mBitmap.N - 1;
	n = wdl_max(n, 1);

	const double m = (double)n;
	int i = (int)(m * mValue + 0.5);

	if (i++ >= n) i = 0;
	mValue = (double)i / m;

	SetDirty();
}

void ISwitchControl::Draw(IGraphics* const pGraphics)
{
	if (mBitmap.N == 1 && (mValue >= 0.5) == mReverse) return;
	IBitmapControl::Draw(pGraphics);
}

IInvisibleSwitchControl::IInvisibleSwitchControl(
	IPlugBase* const pPlug,
	const IRECT* const pR,
	const int paramIdx
):
	IControl(pPlug, NULL, paramIdx)
{
	mDisablePrompt = 1;
	mDblAsSingleClick = 1;

	if (pR) mTargetRECT = *pR;
	mTooltip = NULL;
	mValue = 0.0;
}

void IInvisibleSwitchControl::OnMouseDown(int, int, const IMouseMod mod)
{
	if (mod.L)
	{
		mValue = mValue < 0.5 ? 1.0 : 0.0;
		SetDirty();
	}
}

void IInvisibleSwitchControl::SetValue(const double value)
{
	assert(value >= 0.0 && value <= 1.0);
	mValue = value;
}

void IInvisibleSwitchControl::SetTargetArea(const IRECT* const pR)
{
	mTargetRECT = *pR;
}

bool IInvisibleSwitchControl::IsHit(const int x, const int y)
{
	return mTargetRECT.Contains(x, y);
}

IContactControl::IContactControl(
	IPlugBase* const pPlug,
	const int x,
	const int y,
	const int paramIdx,
	const IBitmap* const pBitmap
):
	ISwitchControl(pPlug, x, y, paramIdx, pBitmap)
{}

void IContactControl::OnMouseUp(int, int, IMouseMod)
{
	mValue = 0.0;
	SetDirty();
}

IFaderControl::IFaderControl(
	IPlugBase* const pPlug,
	const int x,
	const int y,
	const int len,
	const int paramIdx,
	const IBitmap* const pBitmap,
	const int direction
):
	IBitmapControl(pPlug, x, y, paramIdx, pBitmap),
	mDefaultValue(-1.0),
	mHandleRECT(mRECT),
	mLen(len)
{
	mDirection = direction;

	if (direction == kVertical)
		mTargetRECT.B = mRECT.B = y + len;
	else
		mTargetRECT.R = mRECT.R = x + len;
}

void IFaderControl::UpdateHandleRECT()
{
	const double range = (double)(mLen - GetHandleHeadroom());
	const double pos = range * mValue;

	if (mDirection == kVertical)
	{
		const int offs = (int)(range - pos) + mTargetRECT.T - mHandleRECT.T;
		mHandleRECT.T += offs;
		mHandleRECT.B += offs;
	}
	else
	{
		const int offs = (int)pos + mTargetRECT.L - mHandleRECT.L;
		mHandleRECT.L += offs;
		mHandleRECT.R += offs;
	}
}

void IFaderControl::OnMouseDown(const int x, const int y, const IMouseMod mod)
{
	if (mod.R && !mDisablePrompt)
	{
		PromptUserInput();
		return;
	}

	if (!mod.L || mHandleRECT.Contains(x, y)) return;

	SnapToMouse(x, y);
}

void IFaderControl::OnMouseDrag(const int x, const int y, const int dX, const int dY, const IMouseMod mod)
{
	if (!mod.L) return;

	const int headroom = GetHandleHeadroom();
	double range = (double)(mLen - headroom);

	if (mod.C) range *= 10.0;

	const int delta = mDirection == kVertical ? -dY : dX;
	UpdateValue((double)delta / range + mValue);
}

void IFaderControl::OnMouseDblClick(int, int, IMouseMod)
{
	if (mValue != mDefaultValue)
	{
		mValue = wdl_max(mDefaultValue, 0.0);
		SetDirty();
	}
}

void IFaderControl::OnMouseWheel(int, int, const IMouseMod mod, const float d)
{
	if (mod.C | mod.W)
	{
		const double delta = mod.C ? 0.001 : 0.01;
		UpdateValue((double)d * delta + mValue);
	}
}

void IFaderControl::SnapToMouse(const int x, const int y)
{
	int headroom = GetHandleHeadroom();
	const double range = (double)(mLen - headroom);
	headroom >>= 1;

	double ofs;
	int pos;

	if (mDirection == kVertical)
	{
		ofs = 1.0;
		pos = mTargetRECT.T - y + headroom;
	}
	else
	{
		ofs = 0.0;
		pos = x - mTargetRECT.L - headroom;
	}

	UpdateValue((double)pos / range + ofs);
}

void IFaderControl::Draw(IGraphics* const pGraphics)
{
	UpdateHandleRECT();

	LICE_IBitmap* const dest = pGraphics->GetDrawBitmap();

	const int scale = pGraphics->Scale();

	const int x = mHandleRECT.L >> scale;
	const int y = mHandleRECT.T >> scale;

	LICE_IBitmap* const src = (LICE_IBitmap*)mBitmap.mData;
	const float weight = mGrayed ? kGrayedAlpha : 1.0f;

	LICE_Blit(dest, src, x, y, 0, 0, mBitmap.W, mBitmap.H, weight, LICE_BLIT_MODE_COPY | LICE_BLIT_USE_ALPHA);
}

void IFaderControl::PromptUserInput()
{
	if (mParamIdx >= 0)
	{
		mPlug->GetGUI()->PromptUserInput(this, mPlug->GetParam(mParamIdx), &mHandleRECT);
		Redraw();
	}
}

void IFaderControl::SetValueFromPlug(const double value)
{
	if (mDefaultValue < 0.0) mDefaultValue = value;
	IBitmapControl::SetValueFromPlug(value);
}

void IFaderControl::Rescale(IGraphics* const pGraphics)
{
	pGraphics->UpdateIBitmap(&mBitmap);
}

const double IKnobMultiControl::kDefaultGearing = 4.0;

IKnobMultiControl::IKnobMultiControl(
	IPlugBase* const pPlug,
	const int x,
	const int y,
	const int paramIdx,
	const IBitmap* const pBitmap
	// const int direction
):
	IBitmapControl(pPlug, x, y, paramIdx, pBitmap)
{
	// mDirection = direction;
	mGearing = kDefaultGearing;
	mDefaultValue = -1.0;
}

void IKnobMultiControl::OnMouseDown(int, int, const IMouseMod mod)
{
	if (mod.R && !mDisablePrompt) PromptUserInput();
}

void IKnobMultiControl::OnMouseDrag(int, int, int, const int dY, const IMouseMod mod)
{
	if (!mod.L) return;

	double gearing = mGearing;
	if (mod.C) gearing *= 10.0;

	double value = mValue;
	// if (mDirection == kVertical)
	// {
		value -= dY / (mTargetRECT.H() * gearing);
	// }
	// else
	// {
		// value += dX / (mTargetRECT.W() * gearing);
	// }

	UpdateValue(value);
}

void IKnobMultiControl::OnMouseDblClick(int, int, IMouseMod)
{
	if (mValue != mDefaultValue)
	{
		mValue = wdl_max(mDefaultValue, 0.0);
		SetDirty();
	}
}

void IKnobMultiControl::OnMouseWheel(int, int, const IMouseMod mod, const float d)
{
	if (mod.C | mod.W)
	{
		const double delta = mod.C ? 0.001 : 0.01;
		UpdateValue((double)d * delta + mValue);
	}
}

void IKnobMultiControl::SetValueFromPlug(const double value)
{
	if (mDefaultValue < 0.0) mDefaultValue = value;
	IBitmapControl::SetValueFromPlug(value);
}

ITextControl::ITextControl(
	IPlugBase* const pPlug,
	const IRECT* const pR,
	const IText* const pFont,
	const char* const str
):
	IControl(pPlug, pR)
{
	if (pFont) mFont = *pFont;
	mStr.Set(str);
}

void ITextControl::SetTextFromPlug(const char* const str)
{
	if (strcmp(mStr.Get(), str))
	{
		SetDirty(false);
		mStr.Set(str);
	}
}

void ITextControl::Draw(IGraphics* const pGraphics)
{
	pGraphics->DrawIText(&mFont, mStr.Get(), &mRECT);
}

bool ITextControl::IsHit(int /* x */, int /* y */)
{
	return false;
}

void ITextControl::Rescale(IGraphics* /* pGraphics */)
{
	mFont.mCached = NULL;
}
