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

const char* StatusMsgStr(IMidiMsg::EStatusMsg msg)
{
  switch (msg) {
    case IMidiMsg::kNone: return "none";
    case IMidiMsg::kNoteOff: return "noteoff";
    case IMidiMsg::kNoteOn: return "noteon";
    case IMidiMsg::kPolyAftertouch: return "aftertouch";
    case IMidiMsg::kControlChange: return "controlchange";
    case IMidiMsg::kProgramChange: return "programchange";
    case IMidiMsg::kChannelAftertouch: return "channelaftertouch";
    case IMidiMsg::kPitchWheel: return "pitchwheel";
    default:  return "unknown";
	};
}

void IMidiMsg::LogMsg()
{
#ifdef TRACER_BUILD
  Trace(TRACELOC, "midi:(%s:%d:%d)", StatusMsgStr(StatusMsg()), NoteNumber(), Velocity());
#endif
}

void ISysEx::Clear()
{
  mOffset = mSize = 0;
  mData = NULL;
}

char* SysExStr(char *str, int maxlen, const BYTE* pData, int size)
{
  assert(str != NULL && maxlen >= 3);

  if (!pData || !size) {
    *str = '\0';
    return str;
  }

  char* pStr = str;
  int n = maxlen / 3;
  if (n > size) n = size;
  for (int i = 0; i < n; ++i, ++pData) {
    sprintf(pStr, "%02X", (int)*pData);
    pStr += 2;
    *pStr++ = ' ';
  }
  *--pStr = '\0';

  return str;
}

void ISysEx::LogMsg()
{
#ifdef TRACER_BUILD
  char str[96];
  Trace(TRACELOC, "sysex:(%d:%s)", mSize, SysExStr(str, sizeof(str), mData, mSize));
#endif
}
