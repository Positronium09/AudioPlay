#pragma once

#include "MP3Play.h"

#include <wincodec.h>


namespace MP3Play
{
	class MP3Metadata
	{
		friend class MP3;

		comptr<IPropertyStore> propertyStore;

		protected:
		MP3Metadata(comptr<IMFMediaSource>& mediaSource);

		public:
		virtual ~MP3Metadata();

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
