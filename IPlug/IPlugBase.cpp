#include "IPlugBase.h"
#include "IGraphics.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

template <class SRC, class DEST> 
void CastCopy(DEST* pDest, SRC* pSrc, int n)
{
  for (int i = 0; i < n; ++i, ++pDest, ++pSrc) {
    *pDest = (DEST) *pSrc;
  }
}

void GetVersionParts(int version, int* pVer, int* pMaj, int* pMin)
{
  *pVer = (version & 0xFFFF0000) >> 16;
  *pMaj = (version & 0x0000FF00) >> 8;
  *pMin = version & 0x000000FF;
}

int GetDecimalVersion(int version)
{
 int ver, rmaj, rmin;
 GetVersionParts(version, &ver, &rmaj, &rmin);
 return 10000 * ver + 100 * rmaj + rmin;
}

void GetVersionStr(int version, char* str)
{
  int ver, rmaj, rmin;
  GetVersionParts(version, &ver, &rmaj, &rmin);
  //if (rmin) {
  //  sprintf(str, "v%d.%d.%d", ver, rmaj, rmin);
  //}
  //else
  //if (rmaj) {
    sprintf(str, "v%d.%02d", ver, rmaj);
  //}
  //else {
  //  sprintf(str, "v%d", ver);
  //}
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

  for (int i = 0; i < nParams; ++i) {
    mParams.Add(new IParam);
  }

  for (int i = 0; i < nPresets; ++i) {
    mPresets.Add(new IPreset(i));
  }

  assert(strlen(effectName) < MAX_EFFECT_NAME_LEN);
  assert(strlen(productName) < MAX_PRODUCT_NAME_LEN);
  assert(strlen(mfrName) < MAX_MFR_NAME_LEN);

  strcpy(mEffectName, effectName);
  strcpy(mProductName, productName);
  strcpy(mMfrName, mfrName);

  int nInputs = 0, nOutputs = 0;
  while (channelIOStr) {
    int nIn = 0, nOut = 0;
    bool channelIOStrValid = sscanf(channelIOStr, "%d-%d", &nIn, &nOut) == 2;
    assert(channelIOStrValid);
    nInputs = MAX(nInputs, nIn);
    nOutputs = MAX(nOutputs, nOut);
    mChannelIO.Add(new ChannelIO(nIn, nOut));
    channelIOStr = strstr(channelIOStr, " ");
    if (channelIOStr) {
      ++channelIOStr;
    }
  }

  mInData.Resize(nInputs);
  mOutData.Resize(nOutputs);

  double** ppInData = mInData.Get();
  for (int i = 0; i < nInputs; ++i, ++ppInData) {
    InChannel* pInChannel = new InChannel;
    pInChannel->mConnected = false;
    pInChannel->mSrc = ppInData;
    mInChannels.Add(pInChannel);
  }
  double** ppOutData = mOutData.Get();
  for (int i = 0; i < nOutputs; ++i, ++ppOutData) {
    OutChannel* pOutChannel = new OutChannel;
    pOutChannel->mConnected = false;
    pOutChannel->mDest = ppOutData;
    pOutChannel->mFDest = 0;
    mOutChannels.Add(pOutChannel);
  }
}

IPlugBase::~IPlugBase()
{ 
  TRACE;
	DELETE_NULL(mGraphics);
  mParams.Empty(true);
  mPresets.Empty(true);
  mInChannels.Empty(true);
  mOutChannels.Empty(true);
  mChannelIO.Empty(true);
}

int IPlugBase::GetHostVersion(bool decimal)
{
  GetHost();
  if (decimal) {
    return GetDecimalVersion(mHostVersion);
  }    
  return mHostVersion;
} 

void IPlugBase::GetHostVersionStr(char* str)
{
  GetVersionStr(mHostVersion, str);
}

bool IPlugBase::LegalIO(int nIn, int nOut)
{
  bool legal = false;
  int i, n = mChannelIO.GetSize();
  for (i = 0; i < n && !legal; ++i) {
    ChannelIO* pIO = mChannelIO.Get(i);
    legal = ((nIn < 0 || nIn == pIO->mIn) && (nOut < 0 || nOut == pIO->mOut));
  }
  Trace(TRACELOC, "%d:%d:%s", nIn, nOut, (legal ? "legal" : "illegal"));
  return legal;  
}

void IPlugBase::LimitToStereoIO()
{
  int nIn = NInChannels(), nOut = NOutChannels();
  if (nIn > 2) {
    SetInputChannelConnections(2, nIn - 2, false);
  }
  if (nOut > 2) {
    SetOutputChannelConnections(2, nOut - 2, true);  
  }
}

void IPlugBase::SetHost(const char* host, int version)
{
  mHost = LookUpHost(host);
  mHostVersion = version;
  
  char vStr[32];
  GetVersionStr(version, vStr);
  Trace(TRACELOC, "host_%sknown:%s:%s", (mHost == kHostUnknown ? "un" : ""), host, vStr);
}

void IPlugBase::AttachGraphics(IGraphics* pGraphics)
{
	if (pGraphics) {
    WDL_MutexLock lock(&mMutex);
    int i, n = mParams.GetSize();
		for (i = 0; i < n; ++i) {
			pGraphics->SetParameterFromPlug(i, GetParam(i)->GetNormalized(), true);
		}
    pGraphics->PrepDraw();
		mGraphics = pGraphics;
	}
}

// Decimal = VVVVRRMM, otherwise 0xVVVVRRMM.
int IPlugBase::GetEffectVersion(bool decimal)   
{
  if (decimal) {
    return GetDecimalVersion(mVersion);
  }
  return mVersion;
}

void IPlugBase::GetEffectVersionStr(char* str)
{
  GetVersionStr(mVersion, str);
  #if defined _DEBUG
    strcat(str, "D");
  #elif defined TRACER_BUILD
    strcat(str, "T");
  #endif
}

double IPlugBase::GetSamplesPerBeat()
{
	double tempo = GetTempo();
	if (tempo > 0.0) {
		return GetSampleRate() * 60.0 / tempo;	
	}
	return 0.0;
}

void IPlugBase::SetSampleRate(double sampleRate)
{
  mSampleRate = sampleRate;
}

void IPlugBase::SetBlockSize(int blockSize)
{
  if (blockSize != mBlockSize) {
    int i, nIn = NInChannels(), nOut = NOutChannels();
    for (i = 0; i < nIn; ++i) {
      InChannel* pInChannel = mInChannels.Get(i);
      double* pScratch = pInChannel->mScratchBuf.Resize(blockSize);
      memset(pScratch, 0, blockSize * sizeof(double));
      if (!pInChannel->mConnected) {
        *(pInChannel->mSrc) = pScratch;
      }
    }
    for (i = 0; i < nOut; ++i) {
      OutChannel* pOutChannel = mOutChannels.Get(i);
      double* pScratch = pOutChannel->mScratchBuf.Resize(blockSize);
      memset(pScratch, 0, blockSize * sizeof(double));
      if (!pOutChannel->mConnected) {
        *(pOutChannel->mDest) = pScratch;
      }
    }
    mBlockSize = blockSize;
  }
}

void IPlugBase::SetInputChannelConnections(int idx, int n, bool connected)
{
  int iEnd = MIN(idx + n, mInChannels.GetSize());
  for (int i = idx; i < iEnd; ++i) {
    InChannel* pInChannel = mInChannels.Get(i);
    pInChannel->mConnected = connected;
    if (!connected) {
      *(pInChannel->mSrc) = pInChannel->mScratchBuf.Get();
    }
  }
}

void IPlugBase::SetOutputChannelConnections(int idx, int n, bool connected)
{
  int iEnd = MIN(idx + n, mOutChannels.GetSize());
  for (int i = idx; i < iEnd; ++i) {
    OutChannel* pOutChannel = mOutChannels.Get(i);
    pOutChannel->mConnected = connected;
    if (!connected) {
      *(pOutChannel->mDest) = pOutChannel->mScratchBuf.Get();
    } 
  }
}

bool IPlugBase::IsInChannelConnected(int chIdx) 
{ 
  return (chIdx < mInChannels.GetSize() && mInChannels.Get(chIdx)->mConnected);
}

bool IPlugBase:: IsOutChannelConnected(int chIdx) 
{
  return (chIdx < mOutChannels.GetSize() && mOutChannels.Get(chIdx)->mConnected); 
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

// If latency changes after initialization (often not supported by the host).
void IPlugBase::SetLatency(int samples)
{
  mLatency = samples;
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

void IPlugBase::SetParameterFromGUI(int idx, double normalizedValue)
{
  Trace(TRACELOC, "%d:%f", idx, normalizedValue);
  WDL_MutexLock lock(&mMutex);
  GetParam(idx)->SetNormalized(normalizedValue);
  InformHostOfParamChange(idx, normalizedValue);
	OnParamChange(idx);
}

void IPlugBase::OnParamReset()
{
	for (int i = 0; i < mParams.GetSize(); ++i) {
		OnParamChange(i);
	}
	//Reset();
}

void IPlugBase::DelayEndInformHostOfParamChange(int idx)
{
  IMutexLock lock(this);

  IGraphics* pGraphics = GetGUI();
  if (pGraphics) {
    mParamChangeIdx = idx;
    int ticks = pGraphics->FPS() / 2; // 0.5 seconds
    if (ticks < 1) ticks = 1;
    pGraphics->SetParamChangeTimer(ticks);
  }
  else {
    EndInformHostOfParamChange(idx);
  }
}

void IPlugBase::EndDelayedInformHostOfParamChange()
{
  IMutexLock lock(this);

  IGraphics* pGraphics = GetGUI();
  if (pGraphics) {
    pGraphics->CancelParamChangeTimer();
  }

  if (mParamChangeIdx >= 0) {
    EndInformHostOfParamChange(mParamChangeIdx);
    mParamChangeIdx = -1;
  }
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
  if (!(mPresets.GetSize())) {
    mPresets.Add(new IPreset(0));
    MakeDefaultPreset();
  }
}

void IPlugBase::PruneUninitializedPresets()
{
  int i = 0;
  while (i < mPresets.GetSize()) {
    IPreset* pPreset = mPresets.Get(i);
    if (pPreset->mInitialized) {
      ++i;
    }
    else {
      mPresets.Delete(i, true);
    }
  }
}

bool IPlugBase::RestorePreset(int idx)
{
  bool restoredOK = false;
  if (idx >= 0 && idx < mPresets.GetSize()) {
    IPreset* pPreset = mPresets.Get(idx);

    if (!(pPreset->mInitialized)) {
      pPreset->mInitialized = true;
      MakeDefaultUserPresetName(&mPresets, pPreset->mName);
      restoredOK = SerializeParams(&(pPreset->mChunk)); 
    }
    else {
      restoredOK = (UnserializeParams(&(pPreset->mChunk), 0) > 0);
    }

    if (restoredOK) {
      mCurrentPresetIdx = idx;
      RedrawParamControls();
    }
  }
  return restoredOK;
}

bool IPlugBase::RestorePreset(const char* name)
{
  if (CSTR_NOT_EMPTY(name)) {
    int n = mPresets.GetSize();
    for (int i = 0; i < n; ++i) {
      IPreset* pPreset = mPresets.Get(i);
      if (!strcmp(pPreset->mName, name)) {
        return RestorePreset(i);
      }
    }
  }
  return false;
}

const char* IPlugBase::GetPresetName(int idx)
{
  if (idx >= 0 && idx < mPresets.GetSize()) {
    return mPresets.Get(idx)->mName;
  }
  return "";
}

void IPlugBase::ModifyCurrentPreset(const char* name)
{
  if (mCurrentPresetIdx >= 0 && mCurrentPresetIdx < mPresets.GetSize()) {
    IPreset* pPreset = mPresets.Get(mCurrentPresetIdx);
    pPreset->mChunk.Clear();


    SerializeParams(&(pPreset->mChunk));

    if (CSTR_NOT_EMPTY(name)) 
    {
      assert(strlen(name) < MAX_PRESET_NAME_LEN);
      strcpy(pPreset->mName, name);
    }
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

int IPlugBase::UnserializePresets(const int fromIdx, const int toIdx, const ByteChunk* const pChunk, int pos)
{
	for (int i = fromIdx; i < toIdx && pos >= 0; ++i)
	{
		IPreset* const pPreset = mPresets.Get(i);
		pos = pChunk->GetStr(&pPreset->mName, pos);
		pos = pChunk->GetBool(&pPreset->mInitialized, pos);
		if (pPreset->mInitialized)
		{
			pos = UnserializePreset(pChunk, pos);
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

int IPlugBase::UnserializePreset(const ByteChunk* const pChunk, int pos)
{
	const int n = mParams.GetSize();
	for (int i = 0; i < n && pos >= 0; ++i)
	{
		IParam* const pParam = mParams.Get(i);
		if (!pParam->IsGlobal()) pos = pParam->Unserialize(pChunk, pos);
	}
	return pos;
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

void IPlugBase::DumpPresetSrcCode(const char* filename, const char* paramEnumNames[])
{
  static bool sDumped = false;
  if (!sDumped) {
    sDumped = true;
    int i, n = NParams();
    FILE* fp = fopen(filename, "w");
    fprintf(fp, "  MakePresetFromNamedParams(\"name\", %d", n - 1);
    for (i = 0; i < n - 1; ++i) {
      IParam* pParam = GetParam(i);
      char paramVal[32];
      switch (pParam->Type()) {
        case IParam::kTypeBool:
          sprintf(paramVal, "%s", (pParam->Bool() ? "true" : "false"));
          break;
        case IParam::kTypeInt: 
          sprintf(paramVal, "%d", pParam->Int());
          break;
        case IParam::kTypeEnum:
          sprintf(paramVal, "%d", pParam->Int());
          break;
        case IParam::kTypeDouble:
        default:
          sprintf(paramVal, "%.2f", pParam->Value());
          break;
      }
      fprintf(fp, ",\n    %s, %s", paramEnumNames[i], paramVal);
    }
    fprintf(fp, ");\n");
    fclose(fp);
  } 
}