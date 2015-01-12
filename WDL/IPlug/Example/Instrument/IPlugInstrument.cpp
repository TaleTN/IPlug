/*

IPlug instrument example
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


Simple IPlug instrument that shows how to receive and sample-accurately
process MIDI messages.

*/


#include "IPlugInstrument.h"
#include "../../IPlug_include_in_plug_src.h"


enum EParams
{
	kVolume = 0,
	kVelocity,
	kNumParams
};

#define NONE -1


IPlugInstrument::IPlugInstrument(IPlugInstanceInfo instanceInfo):
IPLUG_CTOR(kNumParams, 0, instanceInfo),
mVolume(0.), mVelocity(false), mNote(NONE), mPhase(0.), mSamplePeriod(1./44100.)
{
	TRACE;

	GetParam(kVolume)->InitDouble("Volume", 0.5, 0., 1., 0.001);
	GetParam(kVelocity)->InitBool("Velocity", true);
}


void IPlugInstrument::Reset()
{
	TRACE;
	IMutexLock lock(this);

	mSamplePeriod = 1./GetSampleRate();
}


void IPlugInstrument::OnParamChange(int paramIdx)
{
	IMutexLock lock(this);

	switch (paramIdx)
	{
		case kVolume:
			mVolume = GetParam(kVolume)->Value();
			break;

		case kVelocity:
			mVelocity = GetParam(kVelocity)->Bool();
			break;
	}
}


void IPlugInstrument::ProcessMidiMsg(IMidiMsg* pMsg)
{
	// List all MIDI messages this plugin will handle.
	switch (pMsg->StatusMsg())
	{
		case IMidiMsg::kNoteOn:
		case IMidiMsg::kNoteOff:
			break;

		case IMidiMsg::kControlChange:
			switch (pMsg->ControlChangeIdx())
			{
				case IMidiMsg::kChannelVolume:
				{
					// Update the volume parameter in the UI only.
					double volume = pMsg->ControlChange(IMidiMsg::kChannelVolume);
					if (GetGUI())
						GetGUI()->SetParameterFromPlug(kVolume, volume, false);
					else
						GetParam(kVolume)->Set(volume);
					InformHostOfParamChange(kVolume, volume);
				}
				break;

				case IMidiMsg::kAllNotesOff:
					break;

				// Discard all other Control Change messages.
				default:
					SendMidiMsg(pMsg);
					return;
			}
			break;

		// Discard all other MIDI messages.
		default:
			#if !defined(PLUG_DOES_MIDI_IN) || defined(PLUG_DOES_MIDI_OUT)
			SendMidiMsg(pMsg);
			#endif
			return;
	}

	// Don't handle the MIDI message just yet (we'll do that in
	// ProcessDoubleReplacing), but instead add it to the queue.
	mMidiQueue.Add(pMsg);
}


void IPlugInstrument::ProcessDoubleReplacing(double** inputs, double** outputs, int nFrames)
{
	double* output = outputs[0];
	for (int offset = 0; offset < nFrames; ++offset)
	{

		// Handle any MIDI messages in the queue.
		while (!mMidiQueue.Empty())
		{
			IMidiMsg* pMsg = mMidiQueue.Peek();
			// Stop when we've reached the current sample frame (offset).
			if (pMsg->mOffset > offset)
				break;

			// Handle the MIDI message.
			int status = pMsg->StatusMsg();
			switch (status)
			{
				case IMidiMsg::kNoteOn:
				case IMidiMsg::kNoteOff:
				{
					int velocity = pMsg->Velocity();
					// Note On
					if (status == IMidiMsg::kNoteOn && velocity)
					{
						mNote = pMsg->NoteNumber();
						mFreq = 440. * pow(2., (mNote - 69.) / 12.);
						mGain = velocity / 127.;
					}
					// Note Off
					else // if (status == IMidiMsg::kNoteOff || !velocity)
					{
						if (pMsg->NoteNumber() == mNote)
							mNote = NONE;
					}
					break;
				}

				case IMidiMsg::kControlChange:
					switch (pMsg->ControlChangeIdx())
					{
						case IMidiMsg::kChannelVolume:
							mVolume = pMsg->ControlChange(IMidiMsg::kChannelVolume);
							break;

						case IMidiMsg::kAllNotesOff:
							if (mNote != NONE || mPhase != 0.)
							{
								mNote = NONE;
								mPhase = 0.;
							}
							break;
					}
					break;
			}

			// Delete the MIDI message we've just handled from the queue.
			mMidiQueue.Remove();
		}

		// Now that the MIDI messages have been handled we are ready to
		// generate a sample of audio.
		if (mNote == NONE)
		{
			*output++ = 0.;
		}
		else // if (mNote != NONE)
		{
			double gain = mVolume;
			if (mVelocity)
				gain *= mGain;

			// Output a (non-band-limited) square wave.
			double phase = mPhase * mFreq;
			if (phase - floor(phase) < 0.50) // 50% duty cycle
				*output++ = +gain;
			else
				*output++ = -gain;
		}

		mPhase += mSamplePeriod;
	}

	// Try to keep the phase below 1.
	if (mNote == NONE && mPhase >= 1.)
		mPhase -= floor(mPhase);

	// Update the offsets of any MIDI messages still in the queue.
	mMidiQueue.Flush(nFrames);
}
