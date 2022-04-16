#pragma once

#include "AudioPlay.h"

#include <chrono>

#define AUDIO_E_CLOSED _HRESULT_TYPEDEF_(0x80080000L)
#define AUDIO_TIMEOUT _HRESULT_TYPEDEF_(0x00090000L)


namespace AudioPlay
{
	using MediaEventCallback = void (*)(IMFMediaEvent*);

	class AudioMetadata;
	enum class AudioStates
	{
		Ready = 0x001,
		Starting = 0x002,
		Started = 0x004,
		Pausing = 0x008,
		Paused = 0x010,
		Stopping = 0x020,
		Stopped = 0x040,
		Opening = 0x080,
		Closing = 0x100,
		Closed = 0x200,
		Start = Started | Starting,
		Pause = Paused | Pausing,
		Stop = Stopped | Stopping,
		Close = Closed | Closing
	};

	constexpr AudioStates operator&(const AudioStates& lhs, const AudioStates& rhs)
	{
		return (AudioStates)((int)lhs & (int)rhs);
	}
	constexpr AudioStates operator|(const AudioStates& lhs, const AudioStates& rhs)
	{
		return (AudioStates)((int)lhs | (int)rhs);
	}


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

		MediaEventCallback callback;

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
		Audio(MediaEventCallback callback);

		public:
		virtual ~Audio();

		static HRESULT CreateAudio(_In_opt_z_ LPCWCH path, _COM_Outptr_ Audio** pPtrMp3);
		static HRESULT CreateAudio(_In_opt_z_ LPCWCH path, _In_ MediaEventCallback callback, _COM_Outptr_ Audio** pPtrMp3);

		#pragma region IMPLEMENT_IUnknown

		template<class T>
		STDMETHODIMP QueryInterface(_COM_Outptr_ T** pPtr) { return QueryInterface(__uuidof(T), (void**)pPtr); }
		STDMETHODIMP QueryInterface(REFIID riid, _COM_Outptr_ void** pPtr);

		STDMETHODIMP_(ULONG) AddRef();
		STDMETHODIMP_(ULONG) Release();

		#pragma endregion

		#pragma region IMPLEMENT_IMFAsyncCallback

		STDMETHODIMP GetParameters(DWORD* pdwFlags, DWORD* pdwQueue)
		{
			UNREFERENCED_PARAMETER(pdwFlags); UNREFERENCED_PARAMETER(pdwQueue);
			return E_NOTIMPL;
		}
		STDMETHODIMP Invoke(IMFAsyncResult* asyncResult);

		#pragma endregion

		const AudioMetadata GetMetadata() const;

		AudioStates GetState() const { return state; }
		bool CheckState(_In_ AudioStates state) const;
		HRESULT OpenFile(_In_ LPCWCH path);
		HRESULT CloseFile();
		// Use CoTaskMemFree when you are done with the pointer
		HRESULT GetFilePath(_Outref_result_maybenull_ LPWCH& path);

		// Always returns S_OK
		HRESULT SetLoop(_In_ BOOL loop) { looping = loop; return S_OK; }
		// Always returns S_OK
		HRESULT GetLoop(_Out_ BOOL& loop) const { loop = looping; return S_OK; }

		HRESULT WaitForState(_In_ AudioStates state);
		HRESULT WaitForState(_In_ AudioStates state, _In_ const milliseconds timeout);

		HRESULT Start();
		HRESULT Start(_In_ const milliseconds position);

		HRESULT Pause();

		HRESULT Stop();

		HRESULT Seek(_In_ const milliseconds position);

		HRESULT GetPosition(_Out_ milliseconds& position);
		HRESULT GetDuration(_Out_ milliseconds& duration);

		HRESULT GetVolume(_Out_ float& volume) const;
		HRESULT SetVolume(_In_ const float volume);

		HRESULT GetMute(_Out_ BOOL& mute) const;
		HRESULT SetMute(_In_ const BOOL mute);
	};
}
