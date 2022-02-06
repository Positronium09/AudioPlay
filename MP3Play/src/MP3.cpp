#include "MP3.h"
#include "MP3Metadata.h"

#include <strsafe.h>

#pragma comment (lib, "Mfplat.lib")
#pragma comment (lib, "Mfuuid.lib")
#pragma comment (lib, "Mf.lib")


#define HR_FAIL_ACTION(hresult, action) if (FAILED(hresult)) { action; return hresult; }
#define HR_FAIL(hresult) if (FAILED(hresult)) { return hresult; }

#define CHECK_CLOSED if (state == MP3States::Closed) { return MP3_E_CLOSED; }


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
HRESULT MP3Play::MP3::CreateMP3(_In_ LPCWCH path, _COM_Outptr_ MP3** pPtrMp3)
{
	HRESULT hr = S_OK;

	if (pPtrMp3 == nullptr)
	{
		return E_INVALIDARG;
	}

	MP3* mp3 = new MP3();

	hr = mp3->OpenFile(path);

	(*pPtrMp3) = mp3;

	return hr;
}
#pragma warning (pop)

const MP3Play::MP3Metadata MP3Play::MP3::GetMetadata()
{
	return MP3Play::MP3Metadata(mediaSource);
}

MP3Play::MP3::MP3() :
	referenceCount(1), state(MP3States::Closed), filepath(nullptr), clockPresent(FALSE), volumeControlPresent(FALSE), looping(FALSE)
{
	InitializeCriticalSection(&criticalSection);

	closeEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

MP3Play::MP3::~MP3()
{
	Stop();
	CloseFile();

	DeleteCriticalSection(&criticalSection);
	CloseHandle(closeEvent);
	CoTaskMemFree(filepath);
}


HRESULT MP3Play::MP3::CreateTopology(_In_ comptr<IMFTopology>& topology, _In_ comptr<IMFPresentationDescriptor>& presentationDescriptor)
{
	comptr<IMFStreamDescriptor> streamDescriptor;
	comptr<IMFTopologyNode> sourceNode;
	comptr <IMFTopologyNode> outputNode;
	comptr<IMFMediaSink> mediaSink;
	comptr<IMFStreamSink> streamSink;

	HRESULT hr = S_OK;

	DWORD streamCount = 0;
	DWORD streamIndex = 0;
	BOOL selected = FALSE;

	hr = presentationDescriptor->GetStreamDescriptorCount(&streamCount); HR_FAIL(hr);
	for (streamIndex; streamIndex < streamCount; streamIndex++)
	{
		comptr<IMFMediaTypeHandler> typeHandler;

		GUID majorType = GUID_NULL;
		hr = presentationDescriptor->GetStreamDescriptorByIndex(streamIndex, &selected, streamDescriptor.put()); HR_FAIL(hr);

		hr = streamDescriptor->GetMediaTypeHandler(typeHandler.put()); HR_FAIL(hr);

		hr = typeHandler->GetMajorType(&majorType); HR_FAIL(hr);

		if (majorType == MFMediaType_Audio)
		{
			break;
		}
	}

	hr = MFCreateAudioRenderer(nullptr, mediaSink.put()); HR_FAIL(hr);

	hr = mediaSink->GetStreamSinkByIndex(streamIndex, streamSink.put()); HR_FAIL(hr);

#pragma region SOURCESTREAM NODE
	MFCreateTopologyNode(MF_TOPOLOGY_SOURCESTREAM_NODE, sourceNode.put()); HR_FAIL(hr);
	hr = sourceNode->SetUnknown(MF_TOPONODE_SOURCE, mediaSource.get()); HR_FAIL(hr);
	hr = sourceNode->SetUnknown(MF_TOPONODE_PRESENTATION_DESCRIPTOR, presentationDescriptor.get()); HR_FAIL(hr);
	hr = sourceNode->SetUnknown(MF_TOPONODE_STREAM_DESCRIPTOR, streamDescriptor.get()); HR_FAIL(hr);
#pragma endregion
	hr = topology->AddNode(sourceNode.get()); HR_FAIL(hr);

#pragma region OUTPUT_NODE
	hr = MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE, outputNode.put()); HR_FAIL(hr);
	hr = outputNode->SetObject(streamSink.get()); HR_FAIL(hr);
	hr = outputNode->SetUINT32(MF_TOPONODE_STREAMID, 0); HR_FAIL(hr);
	hr = outputNode->SetUINT32(MF_TOPONODE_NOSHUTDOWN_ON_REMOVE, FALSE); HR_FAIL(hr);
#pragma endregion
	hr = topology->AddNode(outputNode.get()); HR_FAIL(hr);

	hr = sourceNode->ConnectOutput(0, outputNode.get(), 0); HR_FAIL(hr);

	return hr;
}

HRESULT MP3Play::MP3::CreateMediaSource(_In_ LPCWCH path)
{
	comptr<IMFSourceResolver> sourceResolver;

	HRESULT hr = S_OK;

	hr = MFCreateSourceResolver(sourceResolver.put()); HR_FAIL(hr);

	MF_OBJECT_TYPE objectType = MF_OBJECT_INVALID;
	hr = sourceResolver->CreateObjectFromURL(path, MF_RESOLUTION_MEDIASOURCE | MF_RESOLUTION_CONTENT_DOES_NOT_HAVE_TO_MATCH_EXTENSION_OR_MIME_TYPE, 
		NULL, &objectType, reinterpret_cast<IUnknown**>(mediaSource.put())); HR_FAIL(hr);

	return hr;
}

HRESULT MP3Play::MP3::OpenFile(_In_ LPCWCH path)
{
	comptr<IMFTopology> topology;
	comptr<IMFPresentationDescriptor> presentationDescriptor;

	HRESULT hr = S_OK;

	
	if (state != MP3States::Closed)
	{
		hr = CloseFile(); HR_FAIL_ACTION(hr, state = MP3States::Closed);
	}

	hr = MFCreateTopology(topology.put()); HR_FAIL_ACTION(hr, state = MP3States::Closed);

	hr = MFCreateMediaSession(nullptr, mediaSession.put()); HR_FAIL_ACTION(hr, state = MP3States::Closed);

	hr = mediaSession->BeginGetEvent(static_cast<IMFAsyncCallback*>(this), nullptr); HR_FAIL_ACTION(hr, state = MP3States::Closed);

	hr = CreateMediaSource(path); HR_FAIL_ACTION(hr, state = MP3States::Closed);

	mediaSource->CreatePresentationDescriptor(presentationDescriptor.put()); HR_FAIL_ACTION(hr, state = MP3States::Closed);
	hr = CreateTopology(topology, presentationDescriptor); HR_FAIL_ACTION(hr, state = MP3States::Closed);

	hr = mediaSession->SetTopology(NULL, topology.get()); HR_FAIL_ACTION(hr, state = MP3States::Closed);


	SIZE_T length;

	hr = StringCbLengthW(path, STRSAFE_MAX_CCH * sizeof(WCHAR), &length); HR_FAIL(hr);
	length += sizeof(WCHAR);

	filepath = reinterpret_cast<LPWCH>(CoTaskMemAlloc(length));

	if (filepath != nullptr)
	{
		hr = StringCbCopyW(filepath, length, path);
	}


	state = MP3States::Opening;

	return hr;
}
HRESULT MP3Play::MP3::CloseFile()
{
	HRESULT hr = S_OK;

	if (state == MP3States::Closed)
	{
		return hr;
	}

	state = MP3States::Closing;

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

	state = MP3States::Closed;

	return hr;
}

HRESULT MP3Play::MP3::GetFilePath(_Outref_result_maybenull_ LPWCH& path)
{
	if (filepath == nullptr)
	{
		path = nullptr;
		return E_POINTER;
	}
	HRESULT hr = S_OK;
	
	SIZE_T length;

	hr = StringCbLengthW(filepath, STRSAFE_MAX_CCH * sizeof(WCHAR), &length); HR_FAIL(hr);
	length += sizeof(WCHAR);

	path = reinterpret_cast<LPWCH>(CoTaskMemAlloc(length));

	if (path != nullptr)
	{
		hr = StringCbCopyW(path, length, filepath);
	}

	return hr;
}

HRESULT MP3Play::MP3::Start()
{
	CHECK_CLOSED;
	HRESULT hr = S_OK;

	PROPVARIANT var;
	PropVariantInit(&var);

	var.vt = VT_EMPTY;

	hr = mediaSession->Start(&GUID_NULL, &var);

	PropVariantClear(&var);

	state = MP3States::Starting;

	return hr;
}
HRESULT MP3Play::MP3::Start(_In_ const milliseconds position)
{
	CHECK_CLOSED;
	HRESULT hr = S_OK;

	PROPVARIANT var;
	PropVariantInit(&var);

	var.vt = VT_I8;
	var.hVal.QuadPart = duration_cast<nanoseconds>(position).count() / 100;

	hr = mediaSession->Start(&GUID_NULL, &var);

	PropVariantClear(&var);

	state = MP3States::Starting;

	return hr;
}

HRESULT MP3Play::MP3::Pause()
{
	CHECK_CLOSED;
	HRESULT hr = S_OK;

	hr = mediaSession->Pause();

	state = MP3States::Pausing;

	return hr;

}

HRESULT MP3Play::MP3::Stop()
{
	CHECK_CLOSED;
	HRESULT hr = S_OK;

	hr = mediaSession->Stop();

	state = MP3States::Stopping;

	return hr;
}

HRESULT MP3Play::MP3::Seek(_In_ const milliseconds position)
{
	CHECK_CLOSED;
	HRESULT hr = S_OK;

	PROPVARIANT var;
	PropVariantInit(&var);

	var.vt = VT_I8;
	var.hVal.QuadPart = duration_cast<nanoseconds>(position).count() / 100;

	hr = mediaSession->Start(&GUID_NULL, &var);

	PropVariantClear(&var);

	if (state == MP3States::Paused || state == MP3States::Pausing ||
		state == MP3States::Stopped || state == MP3States::Stopping)
	{
		hr = mediaSession->Pause();
	}

	return hr;
}

HRESULT MP3Play::MP3::GetPosition(_Out_ milliseconds& position)
{
	CHECK_CLOSED;
	HRESULT hr = S_OK;

	MFTIME mfTime = -1;

	if (state == MP3States::Opening || state == MP3States::Closed || state == MP3States::Closing)
	{
		position = milliseconds{ -1 };
		return E_FAIL;
	}
	hr = presentationClock->GetTime(&mfTime); HR_FAIL_ACTION(hr, position = milliseconds{ -1 });
	nanoseconds nanosec{ mfTime * 100 };

	position = duration_cast<milliseconds>(nanosec);

	return hr;
}
HRESULT MP3Play::MP3::GetDuration(_Out_ milliseconds& duration)
{
	CHECK_CLOSED;
	comptr<IMFPresentationDescriptor> presentationDescriptor;

	HRESULT hr = S_OK;

	if (state == MP3States::Opening || state == MP3States::Closed || state == MP3States::Closing)
	{
		duration = milliseconds{ -1 };
		return E_FAIL;
	}
	hr = mediaSource->CreatePresentationDescriptor(presentationDescriptor.put()); HR_FAIL_ACTION(hr, duration = milliseconds{ -1 });

	MFTIME mfTime = -1;
	hr = presentationDescriptor->GetUINT64(MF_PD_DURATION, reinterpret_cast<UINT64*>(&mfTime)); HR_FAIL_ACTION(hr, duration = milliseconds{ -1 });

	nanoseconds nanosec{ mfTime * 100 };

	duration = duration_cast<milliseconds>(nanosec);

	return hr;
}

HRESULT MP3Play::MP3::GetVolume(_Out_ float* volume)
{
	CHECK_CLOSED;
	HRESULT hr = S_OK;

	if (state == MP3States::Opening || state == MP3States::Closed || state == MP3States::Closing)
	{
		(*volume) = -1.0f;
		return E_FAIL;
	}

	hr = simpleAudioVolume->GetMasterVolume(volume); HR_FAIL_ACTION(hr, (*volume) = -1.0f);

	return hr;
}
HRESULT MP3Play::MP3::SetVolume(_In_ const float volume)
{
	CHECK_CLOSED;
	HRESULT hr = S_OK;

	if (state == MP3States::Opening || state == MP3States::Closed || state == MP3States::Closing)
	{
		return E_FAIL;
	}
	hr = simpleAudioVolume->SetMasterVolume(volume);

	return hr;
}

HRESULT MP3Play::MP3::GetMute(_Out_ BOOL* mute)
{
	CHECK_CLOSED;
	HRESULT hr = S_OK;

	if (state == MP3States::Opening || state == MP3States::Closed || state == MP3States::Closing)
	{
		(*mute) = FALSE;
		return E_FAIL;
	}

	hr = simpleAudioVolume->GetMute(mute);

	return hr;
}
HRESULT MP3Play::MP3::SetMute(_In_ const BOOL mute)
{
	CHECK_CLOSED;
	HRESULT hr = S_OK;

	if (state == MP3States::Opening || state == MP3States::Closed || state == MP3States::Closing)
	{
		return E_FAIL;
	}

	hr = simpleAudioVolume->SetMute(mute);

	return hr;
}

#pragma region IMPLEMET_IUnknown

STDMETHODIMP_(ULONG) MP3Play::MP3::AddRef()
{
	InterlockedIncrement(&referenceCount);
	return referenceCount;
}

STDMETHODIMP_(ULONG) MP3Play::MP3::Release()
{
	ULONG newRefCount = InterlockedDecrement(&referenceCount);

	if (referenceCount == 0)
	{
		delete this;
	}

	return newRefCount;
}

STDMETHODIMP MP3Play::MP3::QueryInterface(REFIID riid, _COM_Outptr_ void** pPtr)
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

STDMETHODIMP MP3Play::MP3::Invoke(IMFAsyncResult* asyncResult)
{
	AutoCriticalSection section(&criticalSection);

	comptr<IMFMediaEvent> mediaEvent;
	MediaEventType mediaEventType;

	HRESULT hr = S_OK;


	hr = mediaSession->EndGetEvent(asyncResult, mediaEvent.put()); HR_FAIL(hr);

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

HRESULT MP3Play::MP3::OnMESessionTopologySet(_In_ comptr<IMFMediaEvent>& mediaEvent)
{
	HRESULT hr = S_OK;

	hr = mediaSession->GetClock(reinterpret_cast<IMFClock**>(presentationClock.put())); HR_FAIL(hr);
	clockPresent = true;

	if (volumeControlPresent)
	{
		state = MP3States::Ready;
	}
	
	return hr;
}

HRESULT MP3Play::MP3::OnMESessionCapabilitiesChanged(_In_ comptr<IMFMediaEvent>& mediaEvent)
{
	HRESULT hr = S_OK;

	hr = MFGetService(mediaSession.get(), MR_POLICY_VOLUME_SERVICE, IID_PPV_ARGS(simpleAudioVolume.put())); HR_FAIL(hr);
	
	if (!volumeControlPresent && clockPresent)
	{
		state = MP3States::Ready;
	}
	volumeControlPresent = true;

	return hr;
}

HRESULT MP3Play::MP3::OnMESessionStarted(_In_ comptr<IMFMediaEvent>& mediaEvent)
{
	HRESULT hr = S_OK;

	state = MP3States::Started;

	return hr;
}

HRESULT MP3Play::MP3::OnMESessionPaused(_In_ comptr<IMFMediaEvent>& mediaEvent)
{
	HRESULT hr = S_OK;

	state = MP3States::Paused;

	return hr;
}

HRESULT MP3Play::MP3::OnMESessionStopped(_In_ comptr<IMFMediaEvent>& mediaEvent)
{
	HRESULT hr = S_OK;

	state = MP3States::Stopped;

	return hr;
}

HRESULT MP3Play::MP3::OnMESessionEnded(_In_ comptr<IMFMediaEvent>& mediaEvent)
{
	HRESULT hr = S_OK;


	if (looping)
	{
		Start(0ms);
	}
	else
	{
		state = MP3States::Stopped;
	}

	return hr;
}

HRESULT MP3Play::MP3::OnMESessionClosed(_In_ comptr<IMFMediaEvent>& mediaEvent)
{
	HRESULT hr = S_OK;

	state = MP3States::Closed;

	SetEvent(closeEvent);

	return hr;
}

HRESULT MP3Play::MP3::OnMENewPresentation(_In_ comptr<IMFMediaEvent>& mediaEvent)
{
	comptr<IMFTopology> topology;
	comptr<IMFPresentationDescriptor> presentationDescriptor;

	HRESULT hr = S_OK;


	PROPVARIANT var;
	PropVariantInit(&var);
	var.vt = VT_UNKNOWN;

	hr = MFCreateTopology(topology.put()); HR_FAIL_ACTION(hr, state = MP3States::Closed);

	hr = mediaEvent->GetValue(&var); HR_FAIL_ACTION(hr, state = MP3States::Closed);

	hr = var.punkVal->QueryInterface(presentationDescriptor.put()); HR_FAIL_ACTION(hr, state = MP3States::Closed);

	CreateTopology(topology, presentationDescriptor); HR_FAIL_ACTION(hr, state = MP3States::Closed);

	hr = mediaSession->SetTopology(NULL, topology.get()); HR_FAIL_ACTION(hr, state = MP3States::Closed);

	state = MP3States::Opening;

	return hr;
}

#pragma endregion