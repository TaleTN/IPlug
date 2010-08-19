#ifndef _IKEYBOARDCONTROL_
#define _IKEYBOARDCONTROL_

/*

IKeyboardControl
(c) Theo Niessink 2009, 2010
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


IKeyboardControl is a (musical) keyboard for IPlug instruments. The keyboard
starts and ends at C. Only despressed keys are drawn by this control, so the
entire keyboard (with all its keys released) should already be visible (e.g.
because it's in the background).

pRegularKeys should contain 6 bitmaps (C/F, D, E/B, G, A), while pSharpKey
should only contain 1 bitmap (for all flat/sharp keys).

pKeyCoords should contain the x-coordinates of each key relative to the
start of the octave. (Note that only the coordinates for the flat/sharp keys
are actually used, the coordinates for the "regular" keys are ignored.)

Here is code snippet defining a 4-octave keyboard starting at MIDI note 48
(C3):

	IBitmap regular = pGraphics->LoadIBitmap(REGULAR_KEYS_ID, REGULAR_KEYS_PNG, 6);
	IBitmap sharp   = pGraphics->LoadIBitmap(SHARP_KEY_ID,    SHARP_KEY_PNG);

	//                    C#      D#          F#      G#        A#
	int coords[12] = { 0, 13, 23, 39, 46, 69, 82, 92, 107, 115, 131, 138 };

	// Store a pointer to the keyboard in member variable IControl* mKeyboard
	mKeyboard = new IKeyboardControl(this, x, y, 48, 4, &regular, &sharp, coords);

	pGraphics->AttachControl(mKeyboard);

The plug-in should provide the following methods, so the keyboard control
can pull status information from the plug-in:

	// Should return true if one or more keys are playing
	int GetNumKeys();

	// Should return true if the key is playing
	bool GetKeyStatus(int key);

(Instead of int you can also use any other integer type for these methods,
e.g. "char GetNumKeys()" or "bool GetKeyStatus(char note)" will also work.)

When the keyboard should be redrawn, e.g. when the plug-in has received a
MIDI Note On/Off message, the plug-in should call mKeyboard->SetDirty().

You should include this header file after your plug-in class has already
been declared, so it is propbably best to include it in your plug-in's main
.cpp file, e.g.:

	#include "MyPlug.h"
	#include "WDL/IPlug/IKeyboardControl.h" // Include after MyPlug.h

*/


class IKeyboardControl: public IControl
{
public:
	IKeyboardControl(IPlugBase* pPlug, int x, int y, int minNote, int nOctaves, IBitmap* pRegularKeys, IBitmap* pSharpKey, const int pKeyCoords[12]):
	IControl(pPlug, &IRECT(x, y, x + (nOctaves * 7 + 1) * pRegularKeys->W, y + pRegularKeys->H / pRegularKeys->N), -1),
	mRegularKeys(*pRegularKeys), mSharpKey(*pSharpKey),
	mRegularKeyH(mRegularKeys.H / mRegularKeys.N), mKeyCoords(pKeyCoords), mOctaveWidth(mRegularKeys.W * 7),
	mNumOctaves(nOctaves), mMaxKey(nOctaves * 12), mKey(-1), mMinNote(minNote),
	mNoteOn(IMidiMsg::kNoteOn << 4), mNoteOff(IMidiMsg::kNoteOff << 4) { mDblAsSingleClick = true; }

	~IKeyboardControl() {}

	inline void SetMidiCh(BYTE ch)
	{
		mNoteOn  = IMidiMsg::kNoteOn  << 4 | ch;
		mNoteOff = IMidiMsg::kNoteOff << 4 | ch;
	}

	inline BYTE GetMidiCh() const { return mNoteOn & 0x0F; }

	virtual void OnMouseDown(int x, int y, IMouseMod* pMod)
	{
		if (pMod->R) return;

		// Skip if this key is already being played using the mouse.
		int key = GetMouseKey(x, y);
		if (key == mKey) return;

		// Send a Note Off for the previous key (if any).
		if (mKey != -1) SendNoteOff();
		// Send a Note On for the new key.
		mKey = key;
		if (mKey != -1) SendNoteOn();

		// Update the keyboard in the GUI.
		SetDirty();
	}

	virtual void OnMouseUp(int x, int y, IMouseMod* pMod)
	{
		// Skip if no key is playing.
		if (mKey == -1) return;

		// Send a Note Off.
		SendNoteOff();
		mKey = -1;

		// Update the keyboard in the GUI.
		SetDirty();
	}

	virtual void OnMouseDrag(int x, int y, int dX, int dY, IMouseMod* pMod) { OnMouseDown(x, y, pMod); }

	virtual void OnMouseWheel(int x, int y, IMouseMod* pMod, int d) {}

	// Draws only the keys that are currently playing.
	virtual bool Draw(IGraphics* pGraphics)
	{
		// Skip if no keys are playing.
		if (((PLUG_CLASS_NAME*)mPlug)->GetNumKeys() == 0) return true;

		// "Regular" keys
		IRECT r(mRECT.L, mRECT.T, mRECT.L + mRegularKeys.W, mRECT.T + mRegularKeyH);
		int key = 0;
		while (key < mMaxKey)
		{
			// Draw the key.
			int note = key % 12;
			if (((PLUG_CLASS_NAME*)mPlug)->GetKeyStatus(key))
			{
				if (note == 0 || note == 5) // C or F
					pGraphics->DrawBitmap(&mRegularKeys, &r, 1, &mBlend);
				else if (note == 2) // D
					pGraphics->DrawBitmap(&mRegularKeys, &r, 2, &mBlend);
				else if (note == 4 || note == 11) // E or B
					pGraphics->DrawBitmap(&mRegularKeys, &r, 3, &mBlend);
				else if (note == 7) // G
					pGraphics->DrawBitmap(&mRegularKeys, &r, 4, &mBlend);
				else // if (note == 8) // A
					pGraphics->DrawBitmap(&mRegularKeys, &r, 5, &mBlend);
			}

			// Next, please!
			if (note == 4 || note == 11) // E or B
				key++;
			else
				key += 2;
			r.L += mRegularKeys.W;
			r.R += mRegularKeys.W;
		}
		// Draw the high C.
		if (((PLUG_CLASS_NAME*)mPlug)->GetKeyStatus(key))
			pGraphics->DrawBitmap(&mRegularKeys, &r, 6, &mBlend);

		// Flat/sharp keys
		r.L = mRECT.L + mKeyCoords[1] + 1;
		r.R = r.L + mSharpKey.W;
		r.B = mRECT.T + mSharpKey.H;
		key = 1;
		while (key <= mMaxKey)
		{
			// Draw the key.
			int note = key % 12;
			if (((PLUG_CLASS_NAME*)mPlug)->GetKeyStatus(key))
				pGraphics->DrawBitmap(&mSharpKey, &r, 0, &mBlend);

			// Next, please!
			int dX;
			if (note == 3) // D#
			{
				key += 3;
				dX = mKeyCoords[6]; // F#
			}
			else if (note == 10) // A#
			{
				key += 3;
				dX = mOctaveWidth + mKeyCoords[1]; // C#
			}
			else
			{
				key += 2;
				dX = mKeyCoords[note + 2];
			}
			dX -= mKeyCoords[note];
			r.L += dX;
			r.R += dX;
		}

		return true;
	}

protected:
	// Returns the key number at the (x, y) coordinates.
	int GetMouseKey(int x, int y)
	{
		// Skip if the coordinates are outside the keyboard's rectangle.
		if (x < mTargetRECT.L || x >= mTargetRECT.R || y < mTargetRECT.T || y >= mTargetRECT.B) return -1;
		x -= mTargetRECT.L;
		y -= mTargetRECT.T;

		// Calculate the octave.
		int octave = x / mOctaveWidth;
		x -= octave * mOctaveWidth;

		// Flat/sharp key
		int note;
		double h = mSharpKey.H;
		if (y < mSharpKey.H && octave < mNumOctaves)
		{
			// C#
			if (x < mKeyCoords[1]) goto RegularKey;
			if (x < mKeyCoords[1] + mSharpKey.W)
			{
				note = 1;
				goto CalcVelocity;
			}
			// D#
			if (x < mKeyCoords[3]) goto RegularKey;
			if (x < mKeyCoords[3] + mSharpKey.W)
			{
				note = 3;
				goto CalcVelocity;
			}
			// F#
			if (x < mKeyCoords[6]) goto RegularKey;
			if (x < mKeyCoords[6] + mSharpKey.W)
			{
				note = 6;
				goto CalcVelocity;
			}
			// G#
			if (x < mKeyCoords[8]) goto RegularKey;
			if (x < mKeyCoords[8] + mSharpKey.W)
			{
				note = 8;
				goto CalcVelocity;
			}
			// A#
			if (x < mKeyCoords[10]) goto RegularKey;
			if (x < mKeyCoords[10] + mSharpKey.W)
			{
				note = 10;
				goto CalcVelocity;
			}
		}

	RegularKey:
		h = mRegularKeyH;
		int n = x / mRegularKeys.W;
		if (n < 3) // C..E
			note = n * 2;
		else // F..B
			note = n * 2 - 1;

	CalcVelocity:
		// Calculate the velocity depeding on the vertical coordinate
		// relative to the key height.
		mVelocity = 1 + int((double)y / h * 126. + 0.5);

		return note + octave * 12;
	}

	// Sends a Note On/Off MIDI message to the plug-in.
	void SendNoteOn()  { mPlug->ProcessMidiMsg(&IMidiMsg(0, mNoteOn,  mMinNote + mKey, mVelocity)); }
	void SendNoteOff() { mPlug->ProcessMidiMsg(&IMidiMsg(0, mNoteOff, mMinNote + mKey, 64       )); }

	IBitmap mRegularKeys, mSharpKey;
	int mRegularKeyH;
	const int* mKeyCoords;
	int mOctaveWidth, mNumOctaves;
	int mMaxKey, mKey;
	int mMinNote, mVelocity;
	BYTE mNoteOn, mNoteOff;
};


#endif // _IKEYBOARDCONTROL_
