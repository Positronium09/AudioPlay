#include "Audio.h"
#include "AudioMetadata.h"

#include <strsafe.h>

#pragma comment (lib, "Mfplat.lib")
#pragma comment (lib, "Mfuuid.lib")
#pragma comment (lib, "Mf.lib")


#define HR_FAIL_ACTION(hresult, action) if (FAILED(hresult)) { action; return hresult; }
#define HR_FAIL(hresult) if (FAILED(hresult)) { return hresult; }

#define CHECK_CLOSED if (state == AudioStates::Closed) { return MP3_E_CLOSED; }


using std::chrono::nanoseconds;
using std::chrono::duration_cast;


class AutoCriticalSection
{
	private:
	LPCRITICAL_SECTION criticalSection;

	public:
	AutoCriticalSection(LPCRITICAL_SECTION section) : 
		criticalSection(section)
	{

		EnterCriticalSection(criticalSection);
	}

	~AutoCriticalSection()
	{
		LeaveCriticalSection(criticalSection);
	}
};

using namespace std::chrono_literals;

#pragma warning (push)
#pragma warning (disable: 6388 28196)
HRESULT AudioPlay::Audio::CreateAudio(_In_ LPCWCH path, _COM_Outptr_ Audio** pPtrMp3)
{
	HRESULT hr = S_OK;

	if (pPtrMp3 == nullptr)
	{
		return E_INVALIDARG;
	}

	Audio* mp3 = new Audio();

	hr = mp3->OpenFile(path);

	(*pPtrMp3) = mp3;

	return hr;
}
#pragma warning (pop)

const AudioPlay::AudioMetadata AudioPlay::Audio::GetMetadata() const
{
	return AudioPlay::AudioMetadata(mediaSource);
}

AudioPlay::Audio::Audio() :
	referenceCount(1), state(AudioStates::Closed), filepath(nullptr), clockPresent(FALSE), volumeControlPresent(FALSE), looping(FALSE)
{
	InitializeCriticalSection(&criticalSection);

	closeEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

AudioPlay::Audio::~Audio()
{
	Stop();
	CloseFile();

	DeleteCriticalSection(&criticalSection);
	CloseHandle(closeEvent);
	CoTaskMemFree(filepath);
}


HRESULT AudioPlay::Audio::CreateTopology(_In_ ComPtr<IMFTopology>& topology, _In_ ComPtr<IMFPresentationDescriptor>& presentationDescriptor)
{
	ComPtr<IMFStreamDescriptor> streamDescriptor;
	ComPtr<IMFTopologyNode> sourceNode;
	ComPtr <IMFTopologyNode> outputNode;
	ComPtr<IMFMediaSink> mediaSink;
	ComPtr<IMFStreamSink> streamSink;

	HRESULT hr = S_OK;

	DWORD streamCount = 0;
	DWORD streamIndex = 0;
	BOOL selected = FALSE;

	hr = presentationDescriptor->GetStreamDescriptorCount(&streamCount); HR_FAIL(hr);
	for (streamIndex; streamIndex < streamCount; streamIndex++)
	{
		ComPtr<IMFMediaTypeHandler> typeHandler;

		GUID majorType = GUID_NULL;
		hr = presentationDescriptor->GetStreamDescriptorByIndex(streamIndex, &selected, &streamDescriptor); HR_FAIL(hr);

		hr = streamDescriptor->GetMediaTypeHandler(&typeHandler); HR_FAIL(hr);

		hr = typeHandler->GetMajorType(&majorType); HR_FAIL(hr);

		if (majorType == MFMediaType_Audio)
		{
			break;
		}
	}

	hr = MFCreateAudioRenderer(nullptr, &mediaSink); HR_FAIL(hr);

	hr = mediaSink->GetStreamSinkByIndex(streamIndex, &streamSink); HR_FAIL(hr);

#pragma region SOURCESTREAM NODE
	MFCreateTopologyNode(MF_TOPOLOGY_SOURCESTREAM_NODE, &sourceNode); HR_FAIL(hr);
	hr = sourceNode->SetUnknown(MF_TOPONODE_SOURCE, mediaSource); HR_FAIL(hr);
	hr = sourceNode->SetUnknown(MF_TOPONODE_PRESENTATION_DESCRIPTOR, presentationDescriptor); HR_FAIL(hr);
	hr = sourceNode->SetUnknown(MF_TOPONODE_STREAM_DESCRIPTOR, streamDescriptor); HR_FAIL(hr);
#pragma endregion
	hr = topology->AddNode(sourceNode); HR_FAIL(hr);

#pragma region OUTPUT_NODE
	hr = MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE, &outputNode); HR_FAIL(hr);
	hr = outputNode->SetObject(streamSink); HR_FAIL(hr);
	hr = outputNode->SetUINT32(MF_TOPONODE_STREAMID, 0); HR_FAIL(hr);
	hr = outputNode->SetUINT32(MF_TOPONODE_NOSHUTDOWN_ON_REMOVE, FALSE); HR_FAIL(hr);
#pragma endregion
	hr = topology->AddNode(outputNode); HR_FAIL(hr);

	hr = sourceNode->ConnectOutput(0, outputNode, 0); HR_FAIL(hr);

	return hr;
}

HRESULT AudioPlay::Audio::CreateMediaSource(_In_ LPCWCH path)
{
	ComPtr<IMFSourceResolver> sourceResolver;

	HRESULT hr = S_OK;

	hr = MFCreateSourceResolver(&sourceResolver); HR_FAIL(hr);

	MF_OBJECT_TYPE objectType = MF_OBJECT_INVALID;
	hr = sourceResolver->CreateObjectFromURL(path, MF_RESOLUTION_MEDIASOURCE | MF_RESOLUTION_CONTENT_DOES_NOT_HAVE_TO_MATCH_EXTENSION_OR_MIME_TYPE, 
		NULL, &objectType, reinterpret_cast<IUnknown**>(&mediaSource)); HR_FAIL(hr);

	return hr;
}

HRESULT AudioPlay::Audio::OpenFile(_In_ LPCWCH path)
{
	ComPtr<IMFTopology> topology;
	ComPtr<IMFPresentationDescriptor> presentationDescriptor;

	HRESULT hr = S_OK;

	
	if (state != AudioStates::Closed)
	{
		hr = CloseFile(); HR_FAIL_ACTION(hr, state = AudioStates::Closed);
	}

	hr = MFCreateTopology(&topology); HR_FAIL_ACTION(hr, state = AudioStates::Closed);

	hr = MFCreateMediaSession(nullptr, &mediaSession); HR_FAIL_ACTION(hr, state = AudioStates::Closed);

	hr = mediaSession->BeginGetEvent(static_cast<IMFAsyncCallback*>(this), nullptr); HR_FAIL_ACTION(hr, state = AudioStates::Closed);

	hr = CreateMediaSource(path); HR_FAIL_ACTION(hr, state = AudioStates::Closed);

	mediaSource->CreatePresentationDescriptor(&presentationDescriptor); HR_FAIL_ACTION(hr, state = AudioStates::Closed);
	hr = CreateTopology(topology, presentationDescriptor); HR_FAIL_ACTION(hr, state = AudioStates::Closed);

	hr = mediaSession->SetTopology(NULL, topology); HR_FAIL_ACTION(hr, state = AudioStates::Closed);


	size_t length;

	hr = StringCbLengthW(path, STRSAFE_MAX_CCH * sizeof(WCHAR), &length); HR_FAIL(hr);
	length += sizeof(WCHAR);

	filepath = reinterpret_cast<LPWCH>(CoTaskMemAlloc(length));

	if (filepath != nullptr)
	{
		hr = StringCbCopyW(filepath, length, path);
	}


	state = AudioStates::Opening;

	return hr;
}
HRESULT AudioPlay::Audio::CloseFile()
{
	HRESULT hr = S_OK;

	if (state == AudioStates::Closed)
	{
		return hr;
	}

	state = AudioStates::Closing;

	hr = mediaSession->Close();

	WaitForSingleObject(closeEvent, static_cast<DWORD>(duration_cast<milliseconds>(10s).count()));

	hr = mediaSession->Shutdown(); HR_FAIL(hr);

	hr = mediaSource->Shutdown();

	mediaSession = nullptr;
	mediaSource = nullptr;
	presentationClock = nullptr;
	simpleAudioVolume = nullptr;

	clockPresent = FALSE;
	volumeControlPresent = FALSE;

	if (filepath)
	{
		CoTaskMemFree(filepath);
	}
	filepath = nullptr;

	state = AudioStates::Closed;

	return hr;
}

HRESULT AudioPlay::Audio::GetFilePath(_Outref_result_maybenull_ LPWCH& path)
{
	if (filepath == nullptr)
	{
		path = nullptr;
		return E_POINTER;
	}
	HRESULT hr = S_OK;
	
	size_t length;

	hr = StringCbLengthW(filepath, STRSAFE_MAX_CCH * sizeof(WCHAR), &length); HR_FAIL(hr);
	length += sizeof(WCHAR);

	path = reinterpret_cast<LPWCH>(CoTaskMemAlloc(length));

	if (path != nullptr)
	{
		hr = StringCbCopyW(path, length, filepath);
	}

	return hr;
}

HRESULT AudioPlay::Audio::Start()
{
	CHECK_CLOSED;
	HRESULT hr = S_OK;

	PROPVARIANT var;
	PropVariantInit(&var);

	var.vt = VT_EMPTY;

	hr = mediaSession->Start(&GUID_NULL, &var);

	PropVariantClear(&var);

	state = AudioStates::Starting;

	return hr;
}
HRESULT AudioPlay::Audio::Start(_In_ const milliseconds position)
{
	CHECK_CLOSED;
	HRESULT hr = S_OK;

	PROPVARIANT var;
	PropVariantInit(&var);

	var.vt = VT_I8;
	var.hVal.QuadPart = duration_cast<nanoseconds>(position).count() / 100;

	hr = mediaSession->Start(&GUID_NULL, &var);

	PropVariantClear(&var);

	state = AudioStates::Starting;

	return hr;
}

HRESULT AudioPlay::Audio::Pause()
{
	CHECK_CLOSED;
	HRESULT hr = S_OK;

	hr = mediaSession->Pause();

	state = AudioStates::Pausing;

	return hr;

}

HRESULT AudioPlay::Audio::Stop()
{
	CHECK_CLOSED;
	HRESULT hr = S_OK;

	hr = mediaSession->Stop();

	state = AudioStates::Stopping;

	return hr;
}

HRESULT AudioPlay::Audio::Seek(_In_ const milliseconds position)
{
	CHECK_CLOSED;
	HRESULT hr = S_OK;

	PROPVARIANT var;
	PropVariantInit(&var);

	var.vt = VT_I8;
	var.hVal.QuadPart = duration_cast<nanoseconds>(position).count() / 100;

	hr = mediaSession->Start(&GUID_NULL, &var);

	PropVariantClear(&var);

	if (state == AudioStates::Paused || state == AudioStates::Pausing ||
		state == AudioStates::Stopped || state == AudioStates::Stopping)
	{
		hr = mediaSession->Pause();
	}

	return hr;
}

HRESULT AudioPlay::Audio::GetPosition(_Out_ milliseconds& position)
{
	CHECK_CLOSED;
	HRESULT hr = S_OK;

	MFTIME mfTime = -1;

	if (state == AudioStates::Opening || state == AudioStates::Closed || state == AudioStates::Closing)
	{
		position = milliseconds{ -1 };
		return E_FAIL;
	}
	hr = presentationClock->GetTime(&mfTime); HR_FAIL_ACTION(hr, position = milliseconds{ -1 });
	nanoseconds nanosec{ mfTime * 100 };

	position = duration_cast<milliseconds>(nanosec);

	return hr;
}
HRESULT AudioPlay::Audio::GetDuration(_Out_ milliseconds& duration)
{
	CHECK_CLOSED;
	ComPtr<IMFPresentationDescriptor> presentationDescriptor;

	HRESULT hr = S_OK;

	if (state == AudioStates::Opening || state == AudioStates::Closed || state == AudioStates::Closing)
	{
		duration = milliseconds{ -1 };
		return E_FAIL;
	}
	hr = mediaSource->CreatePresentationDescriptor(&presentationDescriptor); HR_FAIL_ACTION(hr, duration = milliseconds{ -1 });

	MFTIME mfTime = -1;
	hr = presentationDescriptor->GetUINT64(MF_PD_DURATION, reinterpret_cast<UINT64*>(&mfTime)); HR_FAIL_ACTION(hr, duration = milliseconds{ -1 });

	nanoseconds nanosec{ mfTime * 100 };

	duration = duration_cast<milliseconds>(nanosec);

	return hr;
}

HRESULT AudioPlay::Audio::GetVolume(_Out_ float& volume) const
{
	CHECK_CLOSED;
	HRESULT hr = S_OK;

	if (state == AudioStates::Opening || state == AudioStates::Closed || state == AudioStates::Closing)
	{
		volume = -1.0f;
		return E_FAIL;
	}

	hr = simpleAudioVolume->GetMasterVolume(&volume); HR_FAIL_ACTION(hr, volume = -1.0f);

	return hr;
}
HRESULT AudioPlay::Audio::SetVolume(_In_ const float volume)
{
	CHECK_CLOSED;
	HRESULT hr = S_OK;

	if (state == AudioStates::Opening || state == AudioStates::Closed || state == AudioStates::Closing)
	{
		return E_FAIL;
	}
	hr = simpleAudioVolume->SetMasterVolume(volume);

	return hr;
}

HRESULT AudioPlay::Audio::GetMute(_Out_ BOOL& mute) const
{
	CHECK_CLOSED;
	HRESULT hr = S_OK;

	if (state == AudioStates::Opening || state == AudioStates::Closed || state == AudioStates::Closing)
	{
		mute = FALSE;
		return E_FAIL;
	}

	hr = simpleAudioVolume->GetMute(&mute);

	return hr;
}
HRESULT AudioPlay::Audio::SetMute(_In_ const BOOL mute)
{
	CHECK_CLOSED;
	HRESULT hr = S_OK;

	if (state == AudioStates::Opening || state == AudioStates::Closed || state == AudioStates::Closing)
	{
		return E_FAIL;
	}

	hr = simpleAudioVolume->SetMute(mute);

	return hr;
}

#pragma region IMPLEMET_IUnknown

STDMETHODIMP_(ULONG) AudioPlay::Audio::AddRef()
{
	InterlockedIncrement(&referenceCount);
	return referenceCount;
}

STDMETHODIMP_(ULONG) AudioPlay::Audio::Release()
{
	ULONG newRefCount = InterlockedDecrement(&referenceCount);

	if (referenceCount == 0)
	{
		delete this;
	}

	return newRefCount;
}

STDMETHODIMP AudioPlay::Audio::QueryInterface(REFIID riid, _COM_Outptr_ void** pPtr)
{
	if (riid == IID_IUnknown)
	{
		*pPtr = static_cast<IUnknown*>(this);
	}
	else if (riid == IID_IMFAsyncCallback)
	{
		*pPtr = static_cast<IMFAsyncCallback*>(this);
	}
	else
	{
		*pPtr = NULL;
		return E_NOINTERFACE;
	}

	AddRef();
	return S_OK;
}

#pragma endregion

STDMETHODIMP AudioPlay::Audio::Invoke(IMFAsyncResult* asyncResult)
{
	AutoCriticalSection section(&criticalSection);

	ComPtr<IMFMediaEvent> mediaEvent;
	MediaEventType mediaEventType;

	HRESULT hr = S_OK;


	hr = mediaSession->EndGetEvent(asyncResult, &mediaEvent); HR_FAIL(hr);

	hr = mediaEvent->GetType(&mediaEventType);
	if (SUCCEEDED(hr))
	{
		switch (mediaEventType)
		{
			case MESessionCapabilitiesChanged:
			{
				OnMESessionCapabilitiesChanged(mediaEvent);
				break;
			}

			case MESessionTopologySet:
			{
				hr = OnMESessionTopologySet(mediaEvent);
				break;
			}
			case MESessionStarted:
			{
				hr = OnMESessionStarted(mediaEvent);
				break;
			}
			case MESessionPaused:
			{
				hr = OnMESessionPaused(mediaEvent);
				break;
			}
			case MESessionStopped:
			{
				hr = OnMESessionStopped(mediaEvent);
				break;
			}
			case MESessionEnded:
			{
				hr = OnMESessionEnded(mediaEvent);
				break;
			}
			case MESessionClosed:
			{
				hr = OnMESessionClosed(mediaEvent);
				break;
			}
			case MENewPresentation:
			{
				hr = OnMENewPresentation(mediaEvent);
				break;
			}
		}
	}

	if (mediaEventType != MESessionClosed)
	{
		hr = mediaSession->BeginGetEvent(static_cast<IMFAsyncCallback*>(this), nullptr); HR_FAIL(hr);
	}

	return hr;
}

#pragma region EVENT_HANDLERS

HRESULT AudioPlay::Audio::OnMESessionTopologySet(_In_ ComPtr<IMFMediaEvent>& mediaEvent)
{
	UNREFERENCED_PARAMETER(mediaEvent);

	HRESULT hr = S_OK;

	presentationClock = nullptr;
	hr = mediaSession->GetClock(reinterpret_cast<IMFClock**>(&presentationClock)); HR_FAIL(hr);
	clockPresent = true;

	if (volumeControlPresent)
	{
		state = AudioStates::Ready;
	}
	
	return hr;
}

HRESULT AudioPlay::Audio::OnMESessionCapabilitiesChanged(_In_ ComPtr<IMFMediaEvent>& mediaEvent)
{
	UNREFERENCED_PARAMETER(mediaEvent);

	HRESULT hr = S_OK;

	simpleAudioVolume = nullptr;
	hr = MFGetService(mediaSession, MR_POLICY_VOLUME_SERVICE, IID_PPV_ARGS(&simpleAudioVolume)); HR_FAIL(hr);
	
	if (!volumeControlPresent && clockPresent)
	{
		state = AudioStates::Ready;
	}
	volumeControlPresent = true;

	return hr;
}

HRESULT AudioPlay::Audio::OnMESessionStarted(_In_ ComPtr<IMFMediaEvent>& mediaEvent)
{
	UNREFERENCED_PARAMETER(mediaEvent);

	HRESULT hr = S_OK;

	state = AudioStates::Started;

	return hr;
}

HRESULT AudioPlay::Audio::OnMESessionPaused(_In_ ComPtr<IMFMediaEvent>& mediaEvent)
{
	UNREFERENCED_PARAMETER(mediaEvent);

	HRESULT hr = S_OK;

	state = AudioStates::Paused;

	return hr;
}

HRESULT AudioPlay::Audio::OnMESessionStopped(_In_ ComPtr<IMFMediaEvent>& mediaEvent)
{
	UNREFERENCED_PARAMETER(mediaEvent);

	HRESULT hr = S_OK;

	state = AudioStates::Stopped;

	return hr;
}

HRESULT AudioPlay::Audio::OnMESessionEnded(_In_ ComPtr<IMFMediaEvent>& mediaEvent)
{
	UNREFERENCED_PARAMETER(mediaEvent);

	HRESULT hr = S_OK;

	if (looping)
	{
		Start(0ms);
	}
	else
	{
		state = AudioStates::Stopped;
	}

	return hr;
}

HRESULT AudioPlay::Audio::OnMESessionClosed(_In_ ComPtr<IMFMediaEvent>& mediaEvent)
{
	UNREFERENCED_PARAMETER(mediaEvent);

	HRESULT hr = S_OK;

	state = AudioStates::Closed;

	SetEvent(closeEvent);

	return hr;
}

HRESULT AudioPlay::Audio::OnMENewPresentation(_In_ ComPtr<IMFMediaEvent>& mediaEvent)
{
	ComPtr<IMFTopology> topology;
	ComPtr<IMFPresentationDescriptor> presentationDescriptor;

	HRESULT hr = S_OK;


	PROPVARIANT var;
	PropVariantInit(&var);
	var.vt = VT_UNKNOWN;

	hr = MFCreateTopology(&topology); HR_FAIL_ACTION(hr, state = AudioStates::Closed);

	hr = mediaEvent->GetValue(&var); HR_FAIL_ACTION(hr, state = AudioStates::Closed);

	hr = var.punkVal->QueryInterface(&presentationDescriptor); HR_FAIL_ACTION(hr, state = AudioStates::Closed);

	CreateTopology(topology, presentationDescriptor); HR_FAIL_ACTION(hr, state = AudioStates::Closed);

	hr = mediaSession->SetTopology(NULL, topology); HR_FAIL_ACTION(hr, state = AudioStates::Closed);

	state = AudioStates::Opening;

	return hr;
}

#pragma endregion