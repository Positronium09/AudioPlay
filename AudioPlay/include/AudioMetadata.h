#pragma once

#include "AudioPlay.h"

#include <wincodec.h>


namespace AudioPlay
{
	class AudioMetadata
	{
		friend class Audio;

		ComPtr<IPropertyStore> propertyStore;

		protected:
		AudioMetadata(ComPtr<IMFMediaSource>& mediaSource);

		public:
		virtual ~AudioMetadata();

		// Use CoTaskMemFree when you are done with the pointer
		HRESULT GetTitle(_Outref_result_maybenull_ LPWCH& title) const;
		// Use CoTaskMemFree when you are done with the pointer
		HRESULT GetAlbumName(_Outref_result_maybenull_ LPWCH& albumName) const;
		// Use CoTaskMemFree when you are done with the pointer
		HRESULT GetArtist(_Outref_result_maybenull_ LPWCH& artist) const;
		// Use PropVariantClear when you are done
		// https://docs.microsoft.com/en-us/windows/win32/medfound/metadata-properties-for-media-files
		HRESULT GetProperity(_In_ REFPROPERTYKEY properityKey, _Out_ PROPVARIANT& value) const;
		// Use PropVariantClear when you are done
		HRESULT GetPropertiyKeyByIndex(_In_ DWORD index, _Out_ PROPERTYKEY& names) const;
		HRESULT GetProperityCount(_Out_ DWORD& count) const;

		HRESULT GetThumbnail(_COM_Outptr_ IWICBitmapFrameDecode** pPtrthumbnail);
	};
}
