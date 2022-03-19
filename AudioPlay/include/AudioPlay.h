#pragma once

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <atlbase.h>


namespace AudioPlay
{
	template<class T>
	using ComPtr = CComPtr<T>;

	inline HRESULT StartMediaFoundation() { return MFStartup(MF_VERSION); }
	inline HRESULT ShutdownMediaFoundation() { return MFShutdown(); }
}
