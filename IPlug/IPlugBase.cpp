#include "IPlugBase.h"
#include "IGraphics.h"
#include "Hosts.h"

#include <stdarg.h>
#include <string.h>

#include "WDL/wdlcstring.h"

template <class SRC, class DEST>
void IPlugBase::CastCopy(DEST* const pDest, const SRC* const pSrc, const int n)
{
	for (int i = 0; i < n; ++i)
	{
		pDest[i] = (DEST)pSrc[i];
	}
}

template <class SRC, class DEST>
void IPlugBase::CastCopyAccumulating(DEST* const pDest, const SRC* const pSrc, const int n)
{
	for (int i = 0; i < n; ++i)
	{
		pDest[i] += (DEST)pSrc[i];
	}
}

static void GetVersionParts(unsigned int version, int* const pParts)
{
	const int ver = version >> 16;
	const int rmaj = (version >> 8) & 0xFF;
	const int rmin = version & 0xFF;

	pParts[0] = ver;
	pParts[1] = rmaj;
	pParts[2] = rmin;
}

int IPlugBase::GetDecimalVersion(const int version)
{
	int parts[3];
	GetVersionParts(version, parts);
	return ((parts[0] * 100) + parts[1]) * 100 + parts[2];
}

char* IPlugBase::GetVersionStr(const int version, char* const buf, const int bufSize)
{
	int parts[3];
	GetVersionParts(version, parts);

	snprintf(buf, bufSize, "%d.%d.%d", parts[0], parts[1], parts[2]);
	return buf;
}

IPlugBase::IPlugBase(
	const int nParams,
	const char* channelIOStr,
	const int nPresets,
	const char* const effectName,
	const char* const productName,
	const char* const mfrName,
	const int vendorVersion,
	const int uniqueID,
	const int mfrID,
	const int latency,
	const int plugDoes
):
	mCurrentPresetIdx(0),
	mParamChangeIdx(-1),
	mEffectName(effectName),
	mProductName(productName),
	mMfrName(mfrName),
	mUniqueID(uniqueID),
	mMfrID(mfrID),
	mVersion(vendorVersion),
	mHost(kHostUninit),
	mHostVersion(0),
	mPlugFlags(plugDoes),
	mSampleRate(kDefaultSampleRate),
	mBlockSize(0),
	mLatency(latency),
	mGraphics(NULL),
	mPresetChunkSize(-1)
{
	assert(plugDoes == (plugDoes & (kPlugIsInst | kPlugDoesMidi)));

	for (int i = 0; i < nPresets; ++i)
	{
		mPresets.Add(new IPreset(i));
	}

	int nInputs = 0, nOutputs = 0;
	while (channelIOStr)
	{
		int nIn = 0, nOut = 0;
		const bool channelIOStrInvalid = sscanf(channelIOStr, "%d-%d", &nIn, &nOut) == 2;
		assert(channelIOStrInvalid);

		nInputs = wdl_max(nInputs, nIn);
		nOutputs = wdl_max(nOutputs, nOut);

		const ChannelIO io(nIn, nOut);
		mChannelIO.Add(&io, 1);

		channelIOStr = strchr(channelIOStr, ' ');
		if (channelIOStr) ++channelIOStr;
	}

	mInData.Resize(nInputs);
	nInputs = mInData.GetSize();

	mOutData.Resize(nOutputs);
	nOutputs = mOutData.GetSize();

	const double** const ppInData = mInData.Get();
	for (int i = 0; i < nInputs; ++i)
	{
		mInChannels.Add(new InChannel(ppInData + i));
	}

	double** const ppOutData = mOutData.Get();
	for (int i = 0; i < nOutputs; ++i)
	{
		mOutChannels.Add(new OutChannel(ppOutData + i));
	}
}

IPlugBase::~IPlugBase()
{
	delete mGraphics;
}

int IPlugBase::GetHostVersion(const bool decimal)
{
	GetHost();
	return decimal ? GetDecimalVersion(mHostVersion) : mHostVersion;
}

char* IPlugBase::GetHostVersionStr(char* const buf, const int bufSize)
{
	GetHost();
	return GetVersionStr(mHostVersion, buf, bufSize);
}

bool IPlugBase::LegalIO(const int nIn, const int nOut) const
{
	bool legal = false;
	const int n = mChannelIO.GetSize();
	const ChannelIO* const pIO = mChannelIO.Get();
	for (int i = 0; i < n && !legal; ++i)
	{
		legal = (nIn < 0 || nIn == pIO[i].mIn) && (nOut < 0 || nOut == pIO[i].mOut);
	}
	return legal;
}

void IPlugBase::LimitToStereoIO()
{
	const int nIn = NInChannels(), nOut = NOutChannels();
	if (nIn > 2) SetInputChannelConnections(2, nIn - 2, false);
	if (nOut > 2) SetOutputChannelConnections(2, nOut - 2, true);
}

void IPlugBase::SetHost(const char* const host, const int version)
{
	mHost = host && *host ? LookUpHost(host) : kHostUnknown;
	mHostVersion = version;
}

void IPlugBase::AttachGraphics(IGraphics* const pGraphics)
{
	if (pGraphics)
	{
		mMutex.Enter();

		const int n = mParams.GetSize();
		for (int i = 0; i < n; ++i)
		{
			pGraphics->SetParameterFromPlug(i, GetParam(i)->GetNormalized(), true);
		}
		mGraphics = pGraphics;

		mMutex.Leave();
	}
}

void IPlugBase::SetBlockSize(int blockSize)
{
	assert(blockSize >= 0);
	const size_t byteSize = blockSize * sizeof(double);

	const int nIn = NInChannels();
	for (int i = 0; i < nIn; ++i)
	{
		InChannel* pInChannel = mInChannels.Get(i);
		double* const pScratch = pInChannel->ResizeScratchBuffer(blockSize);

		if (pScratch)
			memset(pScratch, 0, byteSize);
		else
			blockSize = 0;
	}

	const int nOut = NOutChannels();
	for (int i = 0; i < nOut; ++i)
	{
		OutChannel* pOutChannel = mOutChannels.Get(i);
		double* const pScratch = pOutChannel->ResizeScratchBuffer(blockSize);

		if (pScratch)
			memset(pScratch, 0, byteSize);
		else
			blockSize = 0;
	}

	mBlockSize = blockSize;
}

void IPlugBase::SetInputChannelConnections(const int idx, int n, const bool connected)
{
	n += idx;
	int iEnd = mInChannels.GetSize();
	iEnd = wdl_min(n, iEnd);

	for (int i = idx; i < iEnd; ++i)
	{
		mInChannels.Get(i)->SetConnection(connected);
	}
}

void IPlugBase::SetOutputChannelConnections(const int idx, int n, const bool connected)
{
	n += idx;
	int iEnd = mOutChannels.GetSize();
	iEnd = wdl_min(n, iEnd);

	for (int i = idx; i < iEnd; ++i)
	{
		mOutChannels.Get(i)->SetConnection(connected);
	}
}

void IPlugBase::AttachInputBuffers(const int idx, int n, const double* const* ppData, const int nFrames)
{
	n += idx;
	int iEnd = mInChannels.GetSize();
	iEnd = wdl_min(n, iEnd);

	for (int i = idx; i < iEnd; ++i)
	{
		mInChannels.Get(i)->AttachInputBuffer(ppData);
	}
}

void IPlugBase::AttachInputBuffers(const int idx, int n, const float* const* ppData, const int nFrames)
{
	n += idx;
	int iEnd = mInChannels.GetSize();
	iEnd = wdl_min(n, iEnd);

	for (int i = idx; i < iEnd; ++i)
	{
		InChannel* const pInChannel = mInChannels.Get(i);
		if (pInChannel->mConnected)
		{
			CastCopy(pInChannel->AttachScratchBuffer(), *ppData++, nFrames);
		}
	}
}

void IPlugBase::AttachOutputBuffers(const int idx, int n, double* const* ppData)
{
	n += idx;
	int iEnd = mOutChannels.GetSize();
	iEnd = wdl_min(n, iEnd);

	for (int i = idx; i < iEnd; ++i)
	{
		mOutChannels.Get(i)->AttachOutputBuffer(ppData);
	}
}

void IPlugBase::AttachOutputBuffers(const int idx, int n, float* const* ppData)
{
	n += idx;
	int iEnd = mOutChannels.GetSize();
	iEnd = wdl_min(n, iEnd);

	for (int i = idx; i < iEnd; ++i)
	{
		mOutChannels.Get(i)->AttachScratchBuffer(ppData);
	}
}

// Reminder: Lock mutex before calling into any IPlugBase processing functions.

void IPlugBase::ProcessBuffers(float /* sampleType */, const int nFrames)
{
	ProcessDoubleReplacing(mInData.Get(), mOutData.Get(), nFrames);
	const int n = NOutChannels();
	const OutChannel* const* const ppOutChannel = mOutChannels.GetList();
	for (int i = 0; i < n; ++i)
	{
		const OutChannel* const pOutChannel = ppOutChannel[i];
		if (pOutChannel->mConnected)
		{
			CastCopy(pOutChannel->mFDest, *pOutChannel->mDest, nFrames);
		}
	}
}

void IPlugBase::ProcessBuffersAccumulating(float /* sampleType */, const int nFrames)
{
	ProcessDoubleReplacing(mInData.Get(), mOutData.Get(), nFrames);
	const int n = NOutChannels();
	const OutChannel* const* const ppOutChannel = mOutChannels.GetList();
	for (int i = 0; i < n; ++i)
	{
		const OutChannel* const pOutChannel = ppOutChannel[i];
		if (pOutChannel->mConnected)
		{
			CastCopyAccumulating(pOutChannel->mFDest, *pOutChannel->mDest, nFrames);
		}
	}
}

void IPlugBase::PassThroughBuffers(float /* sampleType */, const int nFrames)
{
	IPlugBase::ProcessDoubleReplacing(mInData.Get(), mOutData.Get(), nFrames);
	const int n = NOutChannels();
	const OutChannel* const* const ppOutChannel = mOutChannels.GetList();
	for (int i = 0; i < n; ++i)
	{
		const OutChannel* const pOutChannel = ppOutChannel[i];
		if (pOutChannel->mConnected)
		{
			CastCopy(pOutChannel->mFDest, *pOutChannel->mDest, nFrames);
		}
	}
}

bool IPlugBase::MidiNoteName(int /* noteNumber */, char* const buf, const int bufSize)
{
	assert(bufSize >= 1);
	*buf = 0;
	return false;
}

bool IPlugBase::SendMidiMsgs(const IMidiMsg* const pMsgs, const int n)
{
	bool rc = true;
	for (int i = 0; i < n; ++i)
	{
		rc &= SendMidiMsg(pMsgs + i);
	}
	return rc;
}

void IPlugBase::SetParameterFromGUI(const int idx, const double normalizedValue)
{
	mMutex.Enter();

	GetParam(idx)->SetNormalized(normalizedValue);
	InformHostOfParamChange(idx, normalizedValue, false);
	OnParamChange(idx);

	mMutex.Leave();
}

void IPlugBase::OnParamReset()
{
	const int n = mParams.GetSize();
	for (int i = 0; i < n; ++i)
	{
		OnParamChange(i);
	}
}

void IPlugBase::BeginDelayedInformHostOfParamChange(const int idx)
{
	mMutex.Enter();

	if (idx != mParamChangeIdx)
	{
		EndDelayedInformHostOfParamChange(false);
		BeginInformHostOfParamChange(idx, false);
	}

	mMutex.Leave();
}

void IPlugBase::DelayEndInformHostOfParamChange(const int idx)
{
	mMutex.Enter();

	IGraphics* const pGraphics = GetGUI();
	if (pGraphics)
	{
		mParamChangeIdx = idx;
		int ticks = pGraphics->FPS() / 2; // 0.5 seconds
		ticks = wdl_max(ticks, 1);
		pGraphics->SetParamChangeTimer(ticks);
	}
	else
	{
		EndInformHostOfParamChange(idx, false);
	}

	mMutex.Leave();
}

void IPlugBase::EndDelayedInformHostOfParamChange(const bool lockMutex)
{
	if (lockMutex) mMutex.Enter();

	IGraphics* const pGraphics = GetGUI();
	if (pGraphics) pGraphics->CancelParamChangeTimer();

	if (mParamChangeIdx >= 0)
	{
		EndInformHostOfParamChange(mParamChangeIdx, false);
		mParamChangeIdx = -1;
	}

	if (lockMutex) mMutex.Leave();
}

// Default passthrough.
void IPlugBase::ProcessDoubleReplacing(const double* const* const inputs, double* const* const outputs, const int nFrames)
{
	assert(nFrames >= 0);
	const size_t byteSize = nFrames * sizeof(double);

	// Mutex is already locked.
	const int nIn = mInChannels.GetSize(), nOut = mOutChannels.GetSize();
	int i = 0;
	for (int n = wdl_min(nIn, nOut); i < n; ++i)
	{
		memcpy(outputs[i], inputs[i], byteSize);
	}
	for (/* same i */; i < nOut; ++i)
	{
		memset(outputs[i], 0, byteSize);
	}
}

bool IPlugBase::AllocPresetChunk(int chunkSize)
{
	if (chunkSize < 0)
	{
		chunkSize = 0;
		const int n = mParams.GetSize();
		for (int i = 0; i < n; ++i)
		{
			const IParam* const pParam = mParams.Get(i);
			if (!pParam->IsGlobal()) chunkSize += pParam->Size();
		}
	}
	mPresetChunkSize = chunkSize;
	return true;
}

void IPlugBase::InitPresetChunk(IPreset* const pPreset, const char* const name)
{
	pPreset->mInitialized = true;
	if (name) pPreset->SetName(name);
	if (mPresetChunkSize < 0) AllocPresetChunk();
	pPreset->mChunk.Alloc(mPresetChunkSize);
	pPreset->mChunk.Clear();
}

static IPreset* GetNextUninitializedPreset(const WDL_PtrList<IPreset>* const pPresets, int* const pIdx)
{
	const int n = pPresets->GetSize();
	for (int i = *pIdx; i < n;)
	{
		IPreset* const pPreset = pPresets->Get(i++);
		if (!(pPreset->mInitialized))
		{
			*pIdx = i;
			return pPreset;
		}
	}
	return NULL;
}

bool IPlugBase::MakeDefaultPreset(const char* const name, int nPresets)
{
	if (nPresets < 0) nPresets = 0x7FFFFFFF;
	for (int i = 0, idx = 0; i < nPresets; ++i)
	{
		IPreset* const pPreset = GetNextUninitializedPreset(&mPresets, &idx);
		if (pPreset)
		{
			InitPresetChunk(pPreset, name);
			SerializePreset(&pPreset->mChunk);
		}
		else
		{
			return false;
		}
	}
	return true;
}

#define SET_PARAM_FROM_VARARG(pParam, vp) \
{ \
	switch (pParam->Type()) \
	{ \
		case IParam::kTypeBool: \
			((IBoolParam*)pParam)->Set(va_arg(vp, int)); \
			break; \
		case IParam::kTypeInt: \
			((IIntParam*)pParam)->Set(va_arg(vp, int)); \
			break; \
		case IParam::kTypeEnum: \
			((IEnumParam*)pParam)->Set(va_arg(vp, int)); \
			break; \
		case IParam::kTypeDouble: \
			((IDoubleParam*)pParam)->Set(va_arg(vp, double)); \
			break; \
		case IParam::kTypeNormalized: \
		default: \
			pParam->SetNormalized(va_arg(vp, double)); \
			break; \
	} \
}

bool IPlugBase::MakePreset(const char* const name, ...)
{
	va_list vp;
	va_start(vp, name);
	const int n = mParams.GetSize();
	for (int i = 0; i < n; ++i)
	{
		IParam* const pParam = GetParam(i);
		SET_PARAM_FROM_VARARG(pParam, vp);
	}
	va_end(vp);
	return MakeDefaultPreset(name);
}

bool IPlugBase::MakePresetFromNamedParams(const char* const name, const int nParamsNamed, ...)
{
	va_list vp;
	va_start(vp, nParamsNamed);
	const int n = mParams.GetSize();
	for (int i = 0; i < nParamsNamed; ++i)
	{
		const int paramIdx = va_arg(vp, int);
		assert(paramIdx >= 0 && paramIdx < n);
		IParam* const pParam = GetParam(paramIdx);
		SET_PARAM_FROM_VARARG(pParam, vp);
	}
	va_end(vp);
	return MakeDefaultPreset(name);
}

bool IPlugBase::MakePresetFromChunk(const char* const name, const ByteChunk* pChunk)
{
	int idx = 0;
	IPreset* pPreset = GetNextUninitializedPreset(&mPresets, &idx);
	if (pPreset)
	{
		InitPresetChunk(pPreset, name);
		pPreset->mChunk.PutChunk(pChunk);
		return true;
	}
	return false;
}

/* static void MakeDefaultUserPresetName(const WDL_PtrList<IPreset>* const pPresets, IPreset* const pPreset)
{
	static const char* const DEFAULT_USER_PRESET_NAME = "User Preset %d";
	static const size_t len = 11; // strlen("User Preset")

	int nDefaultNames = 0;
	const int n = pPresets->GetSize();
	for (int i = 0; i < n; ++i)
	{
		if (!strncmp(pPresets->Get(i)->mName.Get(), DEFAULT_USER_PRESET_NAME, len))
		{
			++nDefaultNames;
		}
	}
	pPreset->mName.SetFormatted(IPreset::kMaxNameLen, "%s %d", DEFAULT_USER_PRESET_NAME, nDefaultNames + 1);
} */

void IPlugBase::EnsureDefaultPreset()
{
	if (!(mPresets.GetSize()))
	{
		mPresets.Add(new IPreset);
		MakeDefaultPreset();
	}
}

void IPlugBase::PruneUninitializedPresets()
{
	int i = 0;
	while (i < mPresets.GetSize())
	{
		IPreset* pPreset = mPresets.Get(i);
		if (pPreset->mInitialized)
			++i;
		else
			mPresets.Delete(i, true);
	}
}

bool IPlugBase::RestorePreset(int idx)
{
	bool restoredOK = false;
	if (idx < 0) idx = mCurrentPresetIdx;
	IPreset* const pPreset = mPresets.Get(idx);
	if (pPreset)
	{
		if (!pPreset->mInitialized)
		{
			InitPresetChunk(pPreset);
			// MakeDefaultUserPresetName(&mPresets, pPreset);
			restoredOK = SerializePreset(&pPreset->mChunk);
		}
		else
		{
			restoredOK = UnserializePreset(&pPreset->mChunk, 0) >= 0;
			OnParamReset();
		}

		if (restoredOK)
		{
			OnPresetChange(idx);
			mCurrentPresetIdx = idx;
			RedrawParamControls();
		}
	}
	return restoredOK;
}

bool IPlugBase::RestorePreset(const char* const name)
{
	if (name && *name)
	{
		const int n = mPresets.GetSize();
		for (int i = 0; i < n; ++i)
		{
			IPreset* const pPreset = mPresets.Get(i);
			if (!strcmp(pPreset->mName.Get(), name))
			{
				return RestorePreset(i);
			}
		}
	}
	return false;
}

const char* IPlugBase::GetPresetName(int idx) const
{
	if (idx < 0) idx = mCurrentPresetIdx;
	const IPreset* const pPreset = mPresets.Get(idx);
	return pPreset ? pPreset->mName.Get() : "";
}

void IPlugBase::ModifyCurrentPreset(const char* const name)
{
	IPreset* const pPreset = mPresets.Get(mCurrentPresetIdx);
	if (pPreset)
	{
		if (!pPreset->mInitialized)
			InitPresetChunk(pPreset);
		else
			pPreset->mChunk.Clear();

		SerializePreset(&pPreset->mChunk);

		if (name && *name) pPreset->SetName(name);
	}
}

bool IPlugBase::SerializePresets(const int fromIdx, const int toIdx, ByteChunk* const pChunk) const
{
	bool savedOK = true;
	for (int i = fromIdx; i < toIdx && savedOK; ++i)
	{
		IPreset* const pPreset = mPresets.Get(i);
		pChunk->PutStr(&pPreset->mName);
		pChunk->PutBool(pPreset->mInitialized);
		if (pPreset->mInitialized)
		{
			savedOK &= !!pChunk->PutChunk(&pPreset->mChunk);
		}
	}
	return savedOK;
}

int IPlugBase::UnserializePresets(const int fromIdx, const int toIdx, const ByteChunk* const pChunk, int pos, const int version)
{
	for (int i = fromIdx; i < toIdx && pos >= 0; ++i)
	{
		IPreset* const pPreset = mPresets.Get(i);
		pos = pChunk->GetStr(&pPreset->mName, pos);
		pos = pChunk->GetBool(&pPreset->mInitialized, pos);
		if (pPreset->mInitialized)
		{
			pos = UnserializePreset(pChunk, pos, version);
			OnParamReset();
			if (pos >= 0)
			{
				pPreset->mChunk.Clear();
				SerializePreset(&pPreset->mChunk);
			}
		}
	}
	return pos;
}

bool IPlugBase::SerializeBank(ByteChunk* const pChunk)
{
	bool savedOK = true;
	const int n = mParams.GetSize();
	for (int i = 0; i < n && savedOK; ++i)
	{
		IParam* const pParam = mParams.Get(i);
		if (pParam->IsGlobal()) savedOK &= pParam->Serialize(pChunk);
	}
	return savedOK ? SerializePresets(0, NPresets(), pChunk) : savedOK;
}

int IPlugBase::UnserializeBank(const ByteChunk* const pChunk, int pos)
{
	const int n = mParams.GetSize();
	for (int i = 0; i < n && pos >= 0; ++i)
	{
		IParam* const pParam = mParams.Get(i);
		if (pParam->IsGlobal()) pos = pParam->Unserialize(pChunk, pos);
	}
	return pos >= 0 ? UnserializePresets(0, NPresets(), pChunk, pos) : pos;
}

bool IPlugBase::SerializePreset(ByteChunk* const pChunk)
{
	bool savedOK = true;
	const int n = mParams.GetSize();
	for (int i = 0; i < n && savedOK; ++i)
	{
		IParam* const pParam = mParams.Get(i);
		if (!pParam->IsGlobal()) savedOK &= pParam->Serialize(pChunk);
	}
	return savedOK;
}

int IPlugBase::UnserializePreset(const ByteChunk* const pChunk, int pos, int /* version */)
{
	const int n = mParams.GetSize();
	for (int i = 0; i < n && pos >= 0; ++i)
	{
		IParam* const pParam = mParams.Get(i);
		if (!pParam->IsGlobal()) pos = pParam->Unserialize(pChunk, pos);
	}
	return pos;
}

int IPlugBase::GetParamsChunkSize(const int fromIdx, const int toIdx) const
{
	int size = 0;
	for (int i = fromIdx; i < toIdx; ++i)
	{
		size += mParams.Get(i)->Size();
	}
	return size;
}

bool IPlugBase::SerializeParams(const int fromIdx, const int toIdx, ByteChunk* const pChunk) const
{
	bool savedOK = true;
	for (int i = fromIdx; i < toIdx && savedOK; ++i)
	{
		savedOK &= mParams.Get(i)->Serialize(pChunk);
	}
	return savedOK;
}

int IPlugBase::UnserializeParams(const int fromIdx, const int toIdx, const ByteChunk* const pChunk, int pos)
{
	for (int i = fromIdx; i < toIdx && pos >= 0; ++i)
	{
		pos = mParams.Get(i)->Unserialize(pChunk, pos);
	}
	return pos;
}

bool IPlugBase::SerializeState(ByteChunk* const pChunk)
{
	return SerializeParams(0, NParams(), pChunk);
}

int IPlugBase::UnserializeState(const ByteChunk* const pChunk, const int startPos)
{
	return UnserializeParams(0, NParams(), pChunk, startPos);
}

void IPlugBase::RedrawParamControls()
{
	if (mGraphics)
	{
		const int n = mParams.GetSize();
		for (int i = 0; i < n; ++i)
		{
			const double v = mParams.Get(i)->GetNormalized();
			mGraphics->SetParameterFromPlug(i, v, true);
		}
	}
}

bool IPlugBase::OnGUIRescale(int /* wantScale */)
{
	GetGUI()->Rescale(IGraphics::kScaleFull);
	return true;
}

IPlugBase::InChannel::InChannel(
	const double** const pSrc
):
	mConnected(false),
	mSrc(pSrc)
{}

void IPlugBase::InChannel::SetConnection(const bool connected)
{
	mConnected = connected;
	if (!connected) *mSrc = mScratchBuf.Get();
}

void IPlugBase::InChannel::AttachInputBuffer(const double* const*& ppData)
{
	if (mConnected) *mSrc = *ppData++;
}

double* IPlugBase::InChannel::ResizeScratchBuffer(const int size)
{
	double* const buf = mScratchBuf.ResizeOK(size);
	if (!mConnected) *mSrc = buf;

	return buf;
}

double* IPlugBase::InChannel::AttachScratchBuffer()
{
	double* const buf = mScratchBuf.Get();
	*mSrc = buf;

	return buf;
}

IPlugBase::OutChannel::OutChannel(
	double** const pDest
):
	mConnected(false),
	mDest(pDest),
	mFDest(NULL)
{}

void IPlugBase::OutChannel::SetConnection(const bool connected)
{
	mConnected = connected;
	if (!connected) *mDest = mScratchBuf.Get();
}

void IPlugBase::OutChannel::AttachOutputBuffer(double* const*& ppData)
{
	if (mConnected) *mDest = *ppData++;
}

double* IPlugBase::OutChannel::ResizeScratchBuffer(const int size)
{
	double* const buf = mScratchBuf.ResizeOK(size);
	if (!mConnected) *mDest = buf;
	return buf;
}

void IPlugBase::OutChannel::AttachScratchBuffer(float* const*& ppData)
{
	if (mConnected)
	{
		*mDest = mScratchBuf.Get();
		mFDest = *ppData++;
	}
}

#ifndef NDEBUG

void IPlugBase::DebugLog(const char* format, ...)
{
	va_list va;
	va_start(va, format);

	char str[128];
	vsnprintf(str, sizeof(str), format, va);

	va_end(va);

	IPlugDebugLog(str);
}

void IPlugBase::DumpPresetSrcCode(const char* const paramEnumNames[], const char* const name)
{
	const int n = NParams();
	WDL_FastString str;
	str.SetFormatted(27 + IPreset::kMaxNameLen + 3 + 11, "MakePresetFromNamedParams(\"%s\", %d", name, n);
	for (int i = 0; i < n; ++i)
	{
		str.AppendFormatted(3 + (int)strlen(paramEnumNames[i]) + 2, ",\n\t%s, ", paramEnumNames[i]);
		const IParam* const pParam = GetParam(i);
		const int type = pParam->Type();
		switch (type)
		{
			case IParam::kTypeBool:
			{
				str.Append(((IBoolParam*)pParam)->Bool() ? "true" : "false");
				break;
			}
			case IParam::kTypeInt:
			case IParam::kTypeEnum:
			{
				const int v = type == IParam::kTypeInt ? ((IIntParam*)pParam)->Int() : ((IEnumParam*)pParam)->Int();
				str.AppendFormatted(11, "%d", v);
				break;
			}
			default:
			{
				const double v = type == IParam::kTypeDouble ? ((IDoubleParam*)pParam)->Value() : pParam->GetNormalized();
				const int idx = str.GetLength();
				str.AppendFormatted(24, "%.17g", v);
				if (!strchr(str.Get() + idx, '.')) str.Append(".0");
				break;
			}
		}
	}
	str.Append(");");

	IPlugDebugLog(str.Get());
}

#endif // NDEBUG
