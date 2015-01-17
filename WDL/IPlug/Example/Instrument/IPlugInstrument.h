#ifndef __IPLUGINSTRUMENT_H__
#define __IPLUGINSTRUMENT_H__

/*

IPlug instrument example
(c) Theo Niessink 2009-2015
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


Simple IPlug instrument that shows how to receive and sample-accurately
process MIDI messages.

*/


#include "../../IPlug_include_in_plug_hdr.h"
#include "../../IMidiQueue.h"


class IPlugInstrument: public IPlug
{
public:
	IPlugInstrument(IPlugInstanceInfo instanceInfo);
	~IPlugInstrument() {}

	void Reset();
	void OnParamChange(int paramIdx);

	void ProcessMidiMsg(IMidiMsg* pMsg);
	void ProcessDoubleReplacing(double** inputs, double** outputs, int nFrames);

private:
	double WDL_FIXALIGN mVolume;
	bool mVelocity;

	IMidiQueue mMidiQueue;

	int mNote;
	double WDL_FIXALIGN mFreq;
	double WDL_FIXALIGN mGain;

	double WDL_FIXALIGN mPhase; // Value in [0, 1].
	double WDL_FIXALIGN mSamplePeriod;
} WDL_FIXALIGN;


#endif
