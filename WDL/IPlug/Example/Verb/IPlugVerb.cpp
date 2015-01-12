/*

IPlug verbengine example
(c) Theo Niessink 2010
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


A simple IPlug plug-in effect that shows how to use WDL's verbengine.

*/


#include "IPlugVerb.h"
#include "../../IPlug_include_in_plug_src.h"


enum EParams
{
	kRoomSize = 0,
	kDamp,
	kWidth,
	kDry,
	kWet,
	kNumParams
};


IPlugVerb::IPlugVerb(IPlugInstanceInfo instanceInfo):
	IPLUG_CTOR(kNumParams, 0, instanceInfo)
{
	TRACE;

	GetParam(kRoomSize)->InitDouble("Room Size", 0.5, 0.3, 0.99, 0.001);
	GetParam(kDamp    )->InitDouble("Dampening", 0.5, 0.,  1.,   0.001);
	GetParam(kWidth   )->InitDouble("Width",     1., -1.,  1.,   0.001);

	GetParam(kDry     )->InitDouble("Dry",       1.,  0.,  1.,   0.001);
	GetParam(kWet     )->InitDouble("Wet",       0.5, 0.,  1.,   0.001);
}


void IPlugVerb::OnParamChange(int paramIdx)
{
	IMutexLock lock(this);

	switch (paramIdx)
	{
		case kRoomSize:
			mEngine.SetRoomSize(GetParam(kRoomSize)->Value());
			mEngine.Reset();
			break;

		case kDamp:
			mEngine.SetDampening(GetParam(kDamp)->Value());
			mEngine.Reset();
			break;

		case kWidth:
			mEngine.SetWidth(GetParam(kWidth)->Value());
			break;

		case kDry:
			mDry = GetParam(kDry)->Value();
			break;

		case kWet:
			mWet = GetParam(kWet)->Value();
			break;
	}
}


void IPlugVerb::Reset()
{
	TRACE; IMutexLock lock(this);

	// Let the reverb engine know the sample rate has changed.
	mEngine.SetSampleRate(GetSampleRate());
}


void IPlugVerb::ProcessDoubleReplacing(double** inputs, double** outputs, int nFrames)
{
	if (mDry == 0. && mWet == 1.)
	{
		// Process the entire sample block at once (more efficient?).
		mEngine.ProcessSampleBlock(inputs[0], inputs[1], outputs[0], outputs[1], nFrames);
		return;
	}

	double* in_l = inputs[0];
	double* in_r = inputs[1];

	double* out_l = outputs[0];
	double* out_r = outputs[1];

	for (int i = 0; i < nFrames; ++i)
	{
		// Process left and right samples
		*out_l = *in_l;
		*out_r = *in_r;
		mEngine.ProcessSample(out_l, out_r);

		// Mix dry/wet
		*out_l++ = mDry * *in_l++ + mWet * *out_l;
		*out_r++ = mDry * *in_r++ + mWet * *out_r;
	}
}
