#include "IControl.h"

const float IControl::kGrayedAlpha = 0.25f;

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

	const int x = mRECT.L >> scale;
	const int y = mRECT.T >> scale;

	LICE_IBitmap* const src = (LICE_IBitmap*)mBitmap.mData;

	LICE_Blit(dest, src, x, y, 0, 0, mBitmap.W, mBitmap.H, 1.0f, LICE_BLIT_MODE_COPY);
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
	IControl(pPlug, paramIdx)
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

void IBitmapControl::SetValueFromPlug(const double value)
{
	if (mValue != value)
	{
		mValue = value;
		SetDirty(false);
		Redraw();
	}
}

void IBitmapControl::SetValueFromUserInput(const double value)
{
	if (mValue != value)
	{
		mValue = value;
		SetDirty();
		Redraw();
	}
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

void IBitmapControl::SetDirty(const bool pushParamToPlug)
{
	mDirty = 1;

	if (pushParamToPlug && mParamIdx >= 0)
	{
		mPlug->SetParameterFromGUI(mParamIdx, mValue);
	}
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
	IControl(pPlug, paramIdx)
{
	mDisablePrompt = 1;
	mDblAsSingleClick = 1;

	mTargetRECT = *pR;
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

void IInvisibleSwitchControl::SetTargetArea(const IRECT* const pR)
{
	mTargetRECT = *pR;
}

bool IInvisibleSwitchControl::IsHit(const int x, const int y)
{
	return mTargetRECT.Contains(x, y);
}

void IInvisibleSwitchControl::SetDirty(const bool pushParamToPlug)
{
	if (pushParamToPlug && mParamIdx >= 0)
	{
		mPlug->SetParameterFromGUI(mParamIdx, mValue);
	}
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
		mValue = mDefaultValue;
		SetDirty();
	}
}

void IFaderControl::OnMouseWheel(int, int, const IMouseMod mod, const float d)
{
	if (mod.C | mod.W) UpdateValue((mod.C ? 0.001 : 0.01) * d + mValue);
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
		mValue = mDefaultValue;
		SetDirty();
	}
}

void IKnobMultiControl::OnMouseWheel(int, int, const IMouseMod mod, const float d)
{
	if (mod.C | mod.W) UpdateValue((mod.C ? 0.001 : 0.01) * d + mValue);
}

void IKnobMultiControl::SetValueFromPlug(const double value)
{
	if (mDefaultValue < 0.0) mDefaultValue = value;
	IBitmapControl::SetValueFromPlug(value);
}

bool IBitmapOverlayControl::Draw(IGraphics* pGraphics)
{
	if (mValue < 0.5) {
		mTargetRECT = mTargetArea;
		return true;	// Don't draw anything.
	}
	else {
		mTargetRECT = mRECT;
		return IBitmapControl::Draw(pGraphics);
	}
}

void ITextControl::SetTextFromPlug(char* str)
{
	if (strcmp(mStr.Get(), str)) {
		SetDirty(false);
		mStr.Set(str);
	}
}

bool ITextControl::Draw(IGraphics* pGraphics)
{
  char* cStr = mStr.Get();
	if (CSTR_NOT_EMPTY(cStr)) {
  	return pGraphics->DrawIText(&mText, cStr, &mRECT);
  }
  return true;
}

ICaptionControl::ICaptionControl(IPlugBase* pPlug, IRECT* pR, int paramIdx, IText* pText, bool showParamLabel)
:   ITextControl(pPlug, pR, pText), mShowParamLabel(showParamLabel) 
{
    mParamIdx = paramIdx;
}

bool ICaptionControl::Draw(IGraphics* pGraphics)
{
    IParam* pParam = mPlug->GetParam(mParamIdx);
    char cStr[32];
    pParam->GetDisplayForHost(cStr);
    mStr.Set(cStr);
    if (mShowParamLabel) {
        mStr.Append(" ");
        mStr.Append(pParam->GetLabelForHost());
    }
    return ITextControl::Draw(pGraphics);
}

void IEditableTextControl::SetBGColor(const IColor *pBGColor)
{
	if (pBGColor)
		mBGColor = *pBGColor;
	else
		mBGColor.A = 0;
}

void IEditableTextControl::OnMouseDown(int x, int y, IMouseMod* pMod)
{
	if (!mDisablePrompt)
	{
		mPlug->GetGUI()->PromptUserInput(this);
		Redraw();
	}
}

bool IEditableTextControl::Draw(IGraphics* pGraphics)
{
	if (mBGColor.A && !pGraphics->FillIRect(&mBGColor, &mRECT)) return false;
	if (!mSecure) return ITextControl::Draw(pGraphics);

	int n = mStr.GetLength();
	if (!n) return true;
	char str[100];
	n = MIN(n, sizeof(str) - 1);
	memset(str, '*', n);
	str[n] = '\0';
	return pGraphics->DrawIText(&mText, str, &mRECT);
}

IURLControl::IURLControl(IPlugBase* pPlug, IRECT* pR, const char* url, const char* backupURL, const char* errMsgOnFailure)
: IControl(pPlug, pR)
{
  memset(mURL, 0, MAX_URL_LEN);
  memset(mBackupURL, 0, MAX_URL_LEN);
  memset(mErrMsg, 0, MAX_NET_ERR_MSG_LEN);
  if (CSTR_NOT_EMPTY(url)) {
    strcpy(mURL, url);
  }
  if (CSTR_NOT_EMPTY(backupURL)) {
    strcpy(mBackupURL, backupURL);
  }
  if (CSTR_NOT_EMPTY(errMsgOnFailure)) {
    strcpy(mErrMsg, errMsgOnFailure);
  }
}

void IURLControl::OnMouseDown(int x, int y, IMouseMod* pMod) 
{
  bool opened = false;
  if (CSTR_NOT_EMPTY(mURL)) {
    opened = mPlug->GetGUI()->OpenURL(mURL, mErrMsg); 
  }
  if (!opened && CSTR_NOT_EMPTY(mBackupURL)) {
    opened = mPlug->GetGUI()->OpenURL(mBackupURL, mErrMsg);
  }
}

void IFileSelectorControl::OnMouseDown(int x, int y, IMouseMod* pMod)
{
	if (mPlug && mPlug->GetGUI()) {
        mState = kFSSelecting;
        SetDirty(false);

		mPlug->GetGUI()->PromptForFile(&mFile, mFileAction, mDir.Get(), mExtensions.Get());
        mValue += 1.0;
        if (mValue > 1.0) {
            mValue = 0.0;
        }
        mState = kFSDone;
		SetDirty();
	}
}

bool IFileSelectorControl::Draw(IGraphics* pGraphics) 
{
    if (mState == kFSSelecting) {
        pGraphics->DrawBitmap(&mBitmap, &mRECT, 0, 0);
    }
    return true;
}
	
void IFileSelectorControl::GetLastSelectedFileForPlug(WDL_String* pStr)
{
	pStr->Set(mFile.Get());
}

void IFileSelectorControl::SetLastSelectedFileFromPlug(char* file)
{
	mFile.Set(file);
}

bool IFileSelectorControl::IsDirty()
{
	if (mDirty) {
		return true;
	}
    if (mState == kFSDone) {
        mState = kFSNone;
        return true;
    }
    return false;
}
