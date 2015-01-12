#ifndef _IDRAWBARCONTROL_
#define _IDRAWBARCONTROL_

/*

IDrawbarControl
(c) Theo Niessink 2009-2011
<http://www.taletn.com/>


This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software in a
   product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.


IDrawbarControl is a drawbar (stepped vertical slider) for IPlug plug-ins.

The value for len is the height of the drawbar, *not* including the
tip/handle. The value for nSteps should be 9 for a drawbar range of 0..8.

*/


class IDrawbarControl: public IControl
{
public:
	IDrawbarControl(IPlugBase* pPlug, int x, int y, int len, int nSteps, int paramIdx, IBitmap* pBitmap):
	IControl(pPlug, &IRECT(x, y, pBitmap), paramIdx),
	mLen(len), mMax(nSteps - 1), mBitmap(*pBitmap) {}

	~IDrawbarControl() {}

	virtual void OnMouseDown(int x, int y, IMouseMod* pMod)
	{
		if (pMod->R)
		{
			PromptUserInput();
			return;
		}

		y -= mRECT.T;
		if (y < mHandleY)
		{
			mValue = mValue - floor((double)y / mLen * mMax) / mMax;
			SetDirty();
		}
	}

	virtual void OnMouseDrag(int x, int y, int dX, int dY, IMouseMod* pMod)
	{
		if (pMod->R) return;
		mValue += (double)dY / mLen;
		SetDirty();
	}

	virtual void OnMouseWheel(int x, int y, IMouseMod* pMod, int d)
	{
		mValue -= (double)d / mMax;
		SetDirty();
	}

	inline double GetSteppedValue() const { return floor(mValue * mMax + 0.5) / mMax; }
	inline int GetHandleOffs() const { return int((1. - GetSteppedValue()) * mLen); }

	void PromptUserInput()
	{
		if (mParamIdx >= 0 && !mDisablePrompt)
		{
			IRECT tmp = mRECT;
			int h = mRECT.H() - int(mLen);
			mRECT.T += mHandleY;
			mRECT.B = mRECT.T + h;
			mPlug->GetGUI()->PromptUserInput(this, mPlug->GetParam(mParamIdx));
			mRECT = tmp;
			Redraw();
		}
	}

	virtual void SetDirty(bool pushParamToPlug = true)
	{
		IControl::SetDirty(pushParamToPlug);

		// mHandleOffs = GetHandleOffs();
		double value = GetSteppedValue();
		mHandleOffs = int((1. - value) * mLen);
		mHandleY    = int(      value  * mLen);
	}

	virtual bool Draw(IGraphics* pGraphics) { return pGraphics->DrawBitmap(&mBitmap, &mRECT, 0, mHandleOffs, &mBlend); }

protected:
	double mLen, mMax;
	IBitmap mBitmap;
	int mHandleOffs, mHandleY;
};


#endif // _IDRAWBARCONTROL_
