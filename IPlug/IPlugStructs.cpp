#include "IPlugStructs.h"

const IColor IColor::kTransparent(0, 0, 0, 0);
const IColor IColor::kBlack(255, 0, 0, 0);
const IColor IColor::kGray(255, 127, 127, 127);
const IColor IColor::kWhite(255, 255, 255, 255);
const IColor IColor::kRed(255, 255, 0, 0);
const IColor IColor::kGreen(255, 0, 255, 0);
const IColor IColor::kBlue(255, 0, 0, 255);
const IColor IColor::kYellow(255, 255, 255, 0);
const IColor IColor::kOrange(255, 255, 127, 0);

const char* const IText::kDefaultFont = "Arial";
const IColor IText::kDefaultColor = IColor::kBlack;

IRECT IRECT::Union(const IRECT* const pRHS) const
{
	if (Empty()) return *pRHS;
	if (pRHS->Empty()) return *this;
	return IRECT(wdl_min(L, pRHS->L), wdl_min(T, pRHS->T), wdl_max(R, pRHS->R), wdl_max(B, pRHS->B));
}

IRECT IRECT::Intersect(const IRECT* const pRHS) const
{
	if (!Intersects(pRHS)) return IRECT();
	return IRECT(wdl_max(L, pRHS->L), wdl_max(T, pRHS->T), wdl_min(R, pRHS->R), wdl_min(B, pRHS->B));
}

bool IRECT::Intersects(const IRECT* const pRHS) const
{
	return !Empty() && !pRHS->Empty() && R > pRHS->L && L < pRHS->R && B > pRHS->T && T < pRHS->B;
}

void IRECT::Clank(const IRECT* const pRHS)
{
	if (L < pRHS->L)
	{
		R += pRHS->L - L;
		R = wdl_min(pRHS->R, R);
		L = pRHS->L;
	}
	if (T < pRHS->T)
	{
		B += pRHS->T - T;
		B = wdl_min(pRHS->B, B);
		T = pRHS->T;
	}
	if (R > pRHS->R)
	{
		L -= R - pRHS->R;
		L = wdl_max(pRHS->L, L);
		R = pRHS->R;
	}
	if (B > pRHS->B)
	{
		T -= B - pRHS->B;
		T = wdl_max(pRHS->T, T);
		B = pRHS->B;
	}
}

static char* HexStr(char* const str, const int maxlen, const unsigned char* pData, const int size)
{
	assert(maxlen >= 3);

	if (!pData || !size)
	{
		*str = 0;
		return str;
	}

	static const char hex[16] =
	{
		'0', '1', '2', '3', '4', '5', '6', '7',
		'8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
	};

	char* pStr = str;
	int n = maxlen / 3;
	n = wdl_min(n, size);
	for (int i = 0; i < n; ++i)
	{
		const int byte = pData[i];
		pStr[0] = hex[byte >> 4];
		pStr[1] = hex[byte & 15];
		pStr[2] = ' ';
		pStr += 3;
	}
	pStr[-1] = 0;

	return str;
}

int IMidiMsg::Size() const
{
	static const char tbl[32] =
	{
		0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3, 3, 2, 2, 3, 0,
		0, 2, 3, 2, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 1
	};
	static const unsigned int ofs = (kSystemMsg << 4) - 16;

	const int sysMsg = mStatus - ofs, chMsg = mStatus >> 4;
	return tbl[chMsg < kSystemMsg ? chMsg : sysMsg];
}

char* IMidiMsg::ToString(char* const buf, const int bufSize) const
{
	int size = Size();
	size = size ? size : 3;

	return HexStr(buf, bufSize, &mStatus, size);
}

char* ISysEx::ToString(char* const buf, const int bufSize) const
{
	return HexStr(buf, bufSize, (const unsigned char*)mData, mSize);
}
