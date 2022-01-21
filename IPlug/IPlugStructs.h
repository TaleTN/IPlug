#pragma once

#include "Containers.h"

#include <assert.h>
#include <string.h>

#include "WDL/wdlstring.h"
#include "WDL/wdltypes.h"

#include "WDL/lice/lice.h"
#include "WDL/lice/lice_text.h"

// Abstracting the graphics made it easy to go ahead and abstract the OS...
// the cost is this crap redefining some basic stuff.

struct IBitmap
{
	void* mData;
	int W, H; // W*H of single frame.
	int N;    // N = number of frames (for multibitmaps).
	int mID;  // Image resource ID, bit 0 is scale (0 = full, 1 = half).

	IBitmap(const int id, const int w = 0, const int h = 0, const int n = 1)
	: mData(NULL), W(w), H(h), N(n), mID(id) {}

	IBitmap(void* const pData = NULL, const int w = 0, const int h = 0, const int n = 1)
	: mData(pData), W(w), H(h), N(n), mID(0) {}

	inline int ID() const { return mID & ~1; }
	inline int Scale() const { return mID & 1; }
};

union IColor
{
	LICE_pixel mColor;

	struct
	{
		#if LICE_PIXEL_B == 0 && LICE_PIXEL_G == 1 && LICE_PIXEL_R == 2 && LICE_PIXEL_A == 3
		LICE_pixel_chan B, G, R, A;
		#elif LICE_PIXEL_A == 0 && LICE_PIXEL_R == 1 && LICE_PIXEL_G == 2 && LICE_PIXEL_B == 3
		LICE_pixel_chan A, R, G, B;
		#endif
	};

	IColor(const int a = 255, const int r = 0, const int g = 0, const int b = 0)
	{
		Set(LICE_RGBA(r, g, b, a));
	}

	inline IColor(const IColor& rhs) { Set(rhs.Get()); }

	inline void Set(const LICE_pixel color) { mColor = color; }
	inline LICE_pixel Get() const { return mColor; }

	inline LICE_pixel* Ptr() const { return (LICE_pixel*)&mColor; }

	inline bool operator ==(const IColor& rhs) const { return Get() == rhs.Get(); }
	inline bool operator !=(const IColor& rhs) const { return !operator==(rhs); }
	inline bool Empty() const { return !Get(); }

	static const IColor kTransparent, kBlack, kGray, kWhite, kRed, kGreen, kBlue, kYellow, kOrange;
};

namespace IChannelBlend
{
	// Copy over whatever is already there, but look at src alpha.
	static const int kBlendNone = LICE_BLIT_MODE_COPY | LICE_BLIT_USE_ALPHA;

	// Copy completely over whatever is already there.
	static const int kBlendClobber = LICE_BLIT_MODE_COPY;

	static const int kBlendAdd = LICE_BLIT_MODE_ADD | LICE_BLIT_USE_ALPHA;
	static const int kBlendColorDodge = LICE_BLIT_MODE_DODGE | LICE_BLIT_USE_ALPHA;
}

struct IText
{
	static const int kDefaultSize = 28;
	static const IColor kDefaultColor;
	static const char* const kDefaultFont;

	LICE_IFont* mCached;
	const char* mFont;
	int mSize;
	IColor mColor;

	enum EStyle { kStyleNormal = 0, kStyleBold, kStyleItalic, kStyleBoldItalic };
	enum EAlign	{ kAlignNear = 0, kAlignCenter, kAlignFar };
	enum EQuality { kQualityDefault = 0, kQualityNonAntiAliased, kQualityAntiAliased, kQualityClearType };
	char mStyle, mAlign, mQuality, _padding;

	int mOrientation; // Degrees ccwise from normal.

	IText(
		const int size = kDefaultSize,
		const IColor color = kDefaultColor,
		const char* const font = NULL,
		const int style = kStyleNormal,
		const int align = kAlignCenter,
		const int orientation = 0,
		const int quality = kQualityDefault
	):
		mCached(NULL),
		mFont(font ? font : kDefaultFont),
		mSize(size),
		mColor(color),
		mStyle(style),
		mAlign(align),
		mQuality(quality),
		_padding(0),
		mOrientation(orientation)
	{
		assert(style >= kStyleNormal && style <= kStyleBoldItalic);
		assert(align >= kAlignNear && align <= kAlignFar);
		assert(quality >= kQualityDefault && quality <= kQualityClearType);
	}

	IText(
		const IColor color
	):
		mCached(NULL),
		mFont(kDefaultFont),
		mSize(kDefaultSize),
		mColor(color),
		mStyle(kStyleNormal),
		mAlign(kAlignCenter),
		mQuality(kQualityDefault),
		_padding(0),
		mOrientation(0)
	{}
};

struct IRECT
{
	int L, T, R, B;

	inline IRECT() { Clear(); }
	IRECT(const int l, const int t, const int r, const int b): L(l), R(r), T(t), B(b) {}
	IRECT(const int x, const int y, const IBitmap* const pBitmap): L(x), T(y), R(x + pBitmap->W), B(y + pBitmap->H) {}

	bool Empty() const
	{
		#if defined(_WIN64) || defined(__LP64__)
		WDL_UINT64 p[2];

		memcpy(&p[0], &L, sizeof(WDL_UINT64));
		memcpy(&p[1], &R, sizeof(WDL_UINT64));

		return !(p[0] || p[1]);
		#else
		return !(L || T || R || B);
		#endif
	}

	inline WDL_UINT64* Ptr() const { return (WDL_UINT64*)&L; }

	void Clear()
	{
		memset(&L, 0, 4 * sizeof(int));
	}

	bool operator ==(const IRECT& rhs) const
	{
		#if defined(_WIN64) || defined(__LP64__)
		WDL_UINT64 p[2], q[2];

		memcpy(&p[0], &L, sizeof(WDL_UINT64));
		memcpy(&p[1], &R, sizeof(WDL_UINT64));

		memcpy(&q[0], &rhs.L, sizeof(WDL_UINT64));
		memcpy(&q[1], &rhs.R, sizeof(WDL_UINT64));

		return p[0] == q[0] && p[1] == q[1];
		#else
		return L == rhs.L && T == rhs.T && R == rhs.R && B == rhs.B;
		#endif
	}

	bool operator !=(const IRECT& rhs) const
	{
		return !(*this == rhs);
	}

	inline int W() const { return R - L; }
	inline int H() const { return B - T; }
	float MW() const { return 0.5f * (float)(L + R); }
	float MH() const { return 0.5f * (float)(T + B); }

	IRECT Union(const IRECT* pRHS) const;
	IRECT Intersect(const IRECT* pRHS) const;
	bool Intersects(const IRECT* pRHS) const;

	bool Contains(const IRECT* pRHS) const
	{
		return !Empty() && L <= pRHS->L && R >= pRHS->R && T <= pRHS->T && B >= pRHS->B;
	}

	bool Contains(const int x, const int y) const
	{
		return x >= L && x < R && y >= T && y < B;
	}

	void Clank(const IRECT* pRHS);

	void Adjust(const int w)
	{
		L -= w; R += w;
	}

	void Adjust(const int w, const int h)
	{
		L -= w; T -= h; R += w; B += h;
	}

	void Adjust(const int l, const int t, const int r, const int b)
	{
		L += l; T += t; R += r; B += b;
	}

	void Downscale(const int scale)
	{
		assert(scale >= 0 && scale < 32);
		L >>= scale; T >>= scale; R >>= scale; B >>= scale;
	}

	void Upscale(const int scale)
	{
		assert(scale >= 0 && scale < 32);
		L <<= scale; T <<= scale; R <<= scale; B <<= scale;
	}
};

struct IMouseMod
{
	unsigned int L:1, R:1, S:1, C:1, A:1, W:1, _unused:26;
	inline IMouseMod(): L(0), R(0), S(0), C(0), A(0), W(0), _unused(0) {}

	IMouseMod(const bool l, const bool r = false, const bool s = false, const bool c = false, const bool a = false, const bool w = false)
	: L(l), R(r), S(s), C(c), A(a), W(w), _unused(0) {}

	unsigned int Get() const
	{
		return (W << 5) | (A << 4) | (C << 3) | (S << 2) | (R << 1) | L;
	}

	void Set(const unsigned int i)
	{
		*this = IMouseMod(i & 1, (i >> 1) & 1, (i >> 2) & 1, (i >> 3) & 1, (i >> 4) & 1, (i >> 5) & 1);
	}
};

struct IMidiMsg
{
	int mOffset;
	unsigned char mStatus, mData1, mData2, _padding;

	enum EStatusMsg
	{
		kNone = 0,
		kNoteOff = 8,
		kNoteOn = 9,
		kPolyAftertouch = 10,
		kControlChange = 11,
		kProgramChange = 12,
		kChannelAftertouch = 13,
		kPitchWheel = 14,
		kSystemMsg = 15
	};

	enum EControlChangeMsg
	{
		kBankSelect = 0,
		kModWheel = 1,
		kBreathController = 2,
		kUndefined003 = 3,
		kFootController = 4,
		kPortamentoTime = 5,
		kDataEntry = 6,
		kChannelVolume = 7,
		kBalance = 8,
		kUndefined009 = 9,
		kPan = 10,
		kExpressionController = 11,
		kEffectControl1 = 12,
		kEffectControl2 = 13,
		kUndefined014 = 14,
		kUndefined015 = 15,
		kGeneralPurposeController1 = 16,
		kGeneralPurposeController2 = 17,
		kGeneralPurposeController3 = 18,
		kGeneralPurposeController4 = 19,
		kUndefined020 = 20,
		kUndefined021 = 21,
		kUndefined022 = 22,
		kUndefined023 = 23,
		kUndefined024 = 24,
		kUndefined025 = 25,
		kUndefined026 = 26,
		kUndefined027 = 27,
		kUndefined028 = 28,
		kUndefined029 = 29,
		kUndefined030 = 30,
		kUndefined031 = 31,
		kSustainOnOff = 64,
		kPortamentoOnOff = 65,
		kSustenutoOnOff = 66,
		kSoftPedalOnOff = 67,
		kLegatoOnOff = 68,
		kHold2OnOff = 69,
		kSoundVariation = 70,
		kResonance = 71,
		kReleaseTime = 72,
		kAttackTime = 73,
		kCutoffFrequency = 74,
		kDecayTime = 75,
		kVibratoRate = 76,
		kVibratoDepth = 77,
		kVibratoDelay = 78,
		kSoundControllerUndefined = 79,
		kGeneralPurposeController5 = 80,
		kGeneralPurposeController6 = 81,
		kGeneralPurposeController7 = 82,
		kGeneralPurposeController8 = 83,
		kPortamentoControl = 84,
		kUndefined085 = 85,
		kUndefined086 = 86,
		kUndefined087 = 87,
		kUndefined088 = 88,
		kHiResVelocityPrefix = 88,
		kUndefined089 = 89,
		kUndefined090 = 90,
		kReverbSendLevel = 91,
		kTremoloDepth = 92,
		kChorusDepth = 93,
		kCelesteDepth = 94,
		kPhaserDepth = 95,
		kDataIncrement = 96,
		kDataDecrement = 97,
		kNonRegisteredParamLSB = 98,
		kNonRegisteredParamMSB = 99,
		kRegisteredParamLSB = 100,
		kRegisteredParamMSB = 101,
		kUndefined102 = 102,
		kUndefined103 = 103,
		kUndefined104 = 104,
		kUndefined105 = 105,
		kUndefined106 = 106,
		kUndefined107 = 107,
		kUndefined108 = 108,
		kUndefined109 = 109,
		kUndefined110 = 110,
		kUndefined111 = 111,
		kUndefined112 = 112,
		kUndefined113 = 113,
		kUndefined114 = 114,
		kUndefined115 = 115,
		kUndefined116 = 116,
		kUndefined117 = 117,
		kUndefined118 = 118,
		kUndefined119 = 119,
		kAllSoundOff = 120,
		kResetAllControllers = 121,
		kLocalControlOnOff = 122,
		kAllNotesOff = 123,
		kOmniModeOff = 124,
		kOmniModeOn = 125,
		kMonoModeOn = 126,
		kPolyModeOn = 127
	};

	inline IMidiMsg() { Clear(); }

	IMidiMsg(const int offs, const int s = 0, const int d1 = 0, const int d2 = 0)
	: mOffset(offs), mStatus(s), mData1(d1), mData2(d2), _padding(0)
	{
		#ifndef NDEBUG
		if (s) assert(s & 0x80);
		assert(d1 >= 0 && d1 <= 127);
		assert(d2 >= 0 && d2 <= 127);
		#endif
	}

	IMidiMsg(const int offs, const void* const buf)
	{
		mOffset = offs;
		memcpy(&mStatus, buf, 4 * sizeof(unsigned char));
	}

	void Clear()
	{
		memset(&mOffset, 0, sizeof(int) + 4 * sizeof(unsigned char));
	}

	int Size() const;
	char* ToString(char* buf, int bufSize = 9) const;
};

struct ISysEx
{
	int mOffset, mSize;
	const void* mData;

	ISysEx(const int offs = 0, const void* const pData = NULL, const int size = 0)
	: mOffset(offs), mData(pData), mSize(size) {}

	void Clear()
	{
		mSize = mOffset = 0;
		mData = NULL;
	}

	char* ToString(char* buf, int bufSize = 128) const;
};

struct IPreset
{
	static const int kMaxNameLen = 256;

	bool mInitialized;
	WDL_FastString mName;
	ByteChunk mChunk;

	IPreset(): mInitialized(false) {}

	IPreset(const int idx)
	: mInitialized(false)
	{
		mName.SetFormatted(kMaxNameLen, "- %d -", idx + 1);
	}

	void SetName(const char* const name)
	{
		mName.Set(name, kMaxNameLen);
	}
};

// For VK_* see WDL/swell/swell-types.h.
/* enum
{
	KEY_SPACE = VK_SPACE,
	KEY_LEFTARROW = VK_LEFT,
	KEY_UPARROW = VK_UP,
	KEY_RIGHTARROW = VK_RIGHT,
	KEY_DOWNARROW = VK_DOWN,
	KEY_DIGIT_0 = 0x30,
	KEY_DIGIT_9 = KEY_DIGIT_0 + 9,
	KEY_ALPHA_A = 0x41,
	KEY_ALPHA_Z = KEY_ALPHA_A + 25
}; */
