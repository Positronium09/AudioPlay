#pragma once

#include "AudioPlay.h"

#include <chrono>

#define MP3_E_CLOSED _HRESULT_TYPEDEF_(0x80080000L)


namespace AudioPlay
{
	class AudioMetadata;
	enum class AudioStates
	{
		Ready,
		Starting,
		Started,
		Pausing,
		Paused,
		Stopping,
		Stopped,
		Opening,
		Closing,
		Closed
	};

	class Audio : public IMFAsyncCallback
	{
		using milliseconds = std::chrono::milliseconds;

		private:
		ULONG referenceCount;

		volatile AudioStates state;
		
		LPWCH filepath;
		
		volatile BOOL clockPresent;
		volatile BOOL volumeControlPresent;
		BOOL looping;

		CRITICAL_SECTION criticalSection;
		HANDLE closeEvent;

		ComPtr<IMFMediaSession> mediaSession;
		ComPtr<IMFMediaSource> mediaSource;
		ComPtr<IMFSimpleAudioVolume> simpleAudioVolume;
		ComPtr<IMFPresentationClock> presentationClock;

		private:
		HRESULT CreateMediaSource(_In_ LPCWCH path);
		HRESULT CreateTopology(_In_ ComPtr<IMFTopology>& topology, _In_ ComPtr<IMFPresentationDescriptor>& presentationDescriptor);

		protected:
		virtual HRESULT OnMESessionCapabilitiesChanged(_In_ ComPtr<IMFMediaEvent>& mediaEvent);
		virtual HRESULT OnMESessionTopologySet(_In_ ComPtr<IMFMediaEvent>& mediaEvent);
		virtual HRESULT OnMESessionStarted(_In_ ComPtr<IMFMediaEvent>& mediaEvent);
		virtual HRESULT OnMESessionPaused(_In_ ComPtr<IMFMediaEvent>& mediaEvent);
		virtual HRESULT OnMESessionStopped(_In_ ComPtr<IMFMediaEvent>& mediaEvent);
		virtual HRESULT OnMESessionEnded(_In_ ComPtr<IMFMediaEvent>& mediaEvent);
		virtual HRESULT OnMESessionClosed(_In_ ComPtr<IMFMediaEvent>& mediaEvent);
		virtual HRESULT OnMENewPresentation(_In_ ComPtr<IMFMediaEvent>& mediaEvent);
		Audio();

		public:
		virtual ~Audio();

		static HRESULT CreateAudio(_In_ LPCWCH path, _COM_Outptr_ Audio** pPtrMp3);

#pragma region IMPLEMENT_IUnknown

		template<class T>
		STDMETHODIMP QueryInterface(_COM_Outptr_ T** pPtr) { return QueryInterface(__uuidof(T), (void**)pPtr); }
		STDMETHODIMP QueryInterface(REFIID riid, _COM_Outptr_ void** pPtr);

		STDMETHODIMP_(ULONG) AddRef();
		STDMETHODIMP_(ULONG) Release();

#pragma endregion

#pragma region IMPLEMENT IMFAsyncCallback

		STDMETHODIMP GetParameters(DWORD* pdwFlags, DWORD* pdwQueue) { UNREFERENCED_PARAMETER(pdwFlags); UNREFERENCED_PARAMETER(pdwQueue); return E_NOTIMPL; }
		STDMETHODIMP Invoke(IMFAsyncResult* asyncResult);

#pragma endregion

		const AudioMetadata GetMetadata();

		AudioStates GetState() { return state; }
		HRESULT OpenFile(_In_ LPCWCH path);
		HRESULT CloseFile();
		// Use CoTaskMemFree when you are done with the pointer
		HRESULT GetFilePath(_Outref_result_maybenull_ LPWCH& path);

		// Always returns S_OK
		HRESULT Loop(_In_ BOOL loop) { looping = loop; return S_OK; }
		// Always returns S_OK
		HRESULT GetLoop(_Out_ BOOL* loop) { if (loop != nullptr) { (*loop) = looping; } return S_OK; }


		HRESULT Start();
		HRESULT Start(_In_ const milliseconds position);
		
		HRESULT Pause();
		
		HRESULT Stop();
		
		HRESULT Seek(_In_ const milliseconds position);
		
		HRESULT GetPosition(_Out_ milliseconds& position);
		HRESULT GetDuration(_Out_ milliseconds& duration);
		
		HRESULT GetVolume(_Out_ float* volume);
		HRESULT SetVolume(_In_ const float volume);

		HRESULT GetMute(_Out_ BOOL* mute);
		HRESULT SetMute(_In_ const BOOL mute);
	};
}
