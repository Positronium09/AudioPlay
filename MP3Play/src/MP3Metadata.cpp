#include "MP3Metadata.h"

#include <Propkey.h>
#include <strsafe.h>


#define HR_FAIL_ACTION(hresult, action) if (FAILED(hresult)) { action; return hresult; }
#define HR_FAIL(hresult) if (FAILED(hresult)) { return hresult; }

#define CHECK_PROPERITYSTORE if (!propertyStore) { return E_POINTER; }


size_t GetHeaderOffset(const BYTE * data)
{
	constexpr char pngHeader[] = "‰PNG";
	constexpr char jfifHeader[] = "ÿØÿà";

	size_t offset = 0;

	size_t length = strlen((char*)data) + 1;
	const BYTE* foundStrpng = (const BYTE*)strstr((const char*)data + length, pngHeader);
	const BYTE* foundStrjfif = (const BYTE*)strstr((const char*)data + length, jfifHeader);

	if (foundStrjfif)
	{
		offset = foundStrjfif - data;
	}
	if (foundStrpng)
	{
		offset = foundStrpng - data;
	}

	return offset;
}


MP3Play::MP3Metadata::MP3Metadata(comptr<IMFMediaSource>& mediaSource)
{
	HRESULT hr = MFGetService(mediaSource.get(), MF_PROPERTY_HANDLER_SERVICE, IID_PPV_ARGS(propertyStore.put()));
	if (FAILED(hr))
	{
		propertyStore = nullptr;
	}
}

MP3Play::MP3Metadata::~MP3Metadata()
{
	propertyStore = nullptr;
}

HRESULT MP3Play::MP3Metadata::GetTitle(_Outref_result_maybenull_ LPWCH& title) const
{
	CHECK_PROPERITYSTORE;
	HRESULT hr = S_OK;

	SIZE_T length = -1;
	LPWCH titleVal = nullptr;

	PROPVARIANT var;
	PropVariantInit(&var);

	hr = propertyStore->GetValue(PKEY_Title, &var); HR_FAIL(hr);

	titleVal = var.bstrVal;

	hr = StringCbLength(titleVal, STRSAFE_MAX_CCH * sizeof(WCHAR), &length); HR_FAIL(hr);
	length += sizeof(WCHAR);

	title = reinterpret_cast<LPWCH>(CoTaskMemAlloc(length));

	if (title != nullptr)
	{
		hr = StringCbCopy(title, length, titleVal);
	}

	PropVariantClear(&var);

	return hr;
}

HRESULT MP3Play::MP3Metadata::GetAlbumName(_Outref_result_maybenull_ LPWCH& albumName) const
{
	CHECK_PROPERITYSTORE;
	HRESULT hr = S_OK;

	SIZE_T length = -1;
	LPWCH albumNameVal = nullptr;

	PROPVARIANT var;
	PropVariantInit(&var);

	hr = propertyStore->GetValue(PKEY_Music_AlbumTitle, &var); HR_FAIL(hr);

	albumNameVal = var.bstrVal;

	hr = StringCbLength(albumNameVal, STRSAFE_MAX_CCH * sizeof(WCHAR), &length); HR_FAIL(hr);
	length += sizeof(WCHAR);

	albumName = reinterpret_cast<LPWCH>(CoTaskMemAlloc(length));

	if (albumName != nullptr)
	{
		hr = StringCbCopy(albumName, length, albumNameVal);
	}

	PropVariantClear(&var);

	return hr;
}

HRESULT MP3Play::MP3Metadata::GetArtist(_Outref_result_maybenull_ LPWCH& artist) const
{
	CHECK_PROPERITYSTORE;
	HRESULT hr = S_OK;

	SIZE_T length = -1;
	LPWCH artistVal = nullptr;

	PROPVARIANT var;
	PropVariantInit(&var);

	hr = propertyStore->GetValue(PKEY_Music_Artist, &var); HR_FAIL(hr);

	artistVal = var.bstrVal;

	hr = StringCbLength(artistVal, STRSAFE_MAX_CCH * sizeof(WCHAR), &length); HR_FAIL(hr);
	length += sizeof(WCHAR);

	artist = reinterpret_cast<LPWCH>(CoTaskMemAlloc(length));

	if (artist != nullptr)
	{
		hr = StringCbCopy(artist, length, artistVal);
	}

	PropVariantClear(&var);

	return hr;
}

HRESULT MP3Play::MP3Metadata::GetProperity(_In_ REFPROPERTYKEY properityKey, _Out_ PROPVARIANT& value) const
{
	CHECK_PROPERITYSTORE;
	HRESULT hr = S_OK;

	PropVariantInit(&value);

	hr = propertyStore->GetValue(properityKey, &value);

	return hr;
}

HRESULT MP3Play::MP3Metadata::GetPropertiyKeyByIndex(_In_ DWORD index, _Out_ PROPERTYKEY& value) const
{
	CHECK_PROPERITYSTORE;
	HRESULT hr = S_OK;

	hr = propertyStore->GetAt(index, &value);

	return hr;
}

HRESULT MP3Play::MP3Metadata::GetProperityCount(_Out_ DWORD& count) const
{
	CHECK_PROPERITYSTORE;
	HRESULT hr = S_OK;

	hr = propertyStore->GetCount(&count);

	return hr;
}

#pragma warning (push)
#pragma warning (disable: 6388 28196)
HRESULT MP3Play::MP3Metadata::GetThumbnail(_COM_Outptr_ IWICBitmapFrameDecode** pPtrthumbnail)
{
	comptr<IWICImagingFactory> factory;
	comptr<IWICStream> stream;
	comptr<IWICBitmapDecoder> decoder;

	IStream* thumbnailStream;

	HRESULT hr = S_OK;

	BYTE* data = nullptr;
	ULONG streamSize = 0;
	size_t offset = 0;

	ULARGE_INTEGER newSize = { 0 };
	LARGE_INTEGER seekPos = { 0 };

	ULONG read = -1;
	DWORD written = -1;


	if (pPtrthumbnail == nullptr)
	{
		return E_INVALIDARG;
	}
	*pPtrthumbnail = nullptr;

	PROPVARIANT thumbnail;
	PropVariantInit(&thumbnail);

	hr = propertyStore->GetValue(PKEY_ThumbnailStream, &thumbnail); HR_FAIL(hr);

	if (thumbnail.vt == VT_EMPTY)
	{
		return S_FALSE;
	}

	thumbnailStream = thumbnail.pStream;

	PropVariantClear(&thumbnail);

	STATSTG stat;
	hr = thumbnailStream->Stat(&stat, NULL); HR_FAIL(hr);

	streamSize = static_cast<ULONG>(stat.cbSize.QuadPart);
	data = new BYTE[streamSize];

	if (!data)
	{
		return E_POINTER;
	}

	thumbnailStream->Read(data, streamSize, &read); HR_FAIL(hr);

	offset = GetHeaderOffset(data);
	streamSize -= static_cast<ULONG>(offset);

	newSize.QuadPart = streamSize;
	seekPos.QuadPart = 0;
	thumbnailStream->Seek(seekPos, STREAM_SEEK_SET, nullptr); HR_FAIL_ACTION(hr, delete[] data);
	thumbnailStream->SetSize(newSize); HR_FAIL_ACTION(hr, delete[] data);
	thumbnailStream->Seek(seekPos, STREAM_SEEK_SET, nullptr); HR_FAIL_ACTION(hr, delete[] data);
	thumbnailStream->Write(data + offset, streamSize, &written); HR_FAIL_ACTION(hr, delete[] data);
	thumbnailStream->Seek(seekPos, STREAM_SEEK_SET, nullptr); HR_FAIL_ACTION(hr, delete[] data);

	delete[] data;

	hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put())); HR_FAIL(hr);

	hr = factory->CreateStream(stream.put()); HR_FAIL(hr);
	hr = stream->InitializeFromIStream(thumbnailStream); HR_FAIL(hr);

	hr = factory->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.put()); HR_FAIL(hr);

	hr = decoder->GetFrame(0, pPtrthumbnail); HR_FAIL(hr);

	return hr;
}
#pragma warning (pop)