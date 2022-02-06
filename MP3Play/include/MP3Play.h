#pragma once

#include <chrono>
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <winrt/base.h>


namespace MP3Play
{
	template<class T>
	using comptr = winrt::com_ptr<T>;
	template<class T>
	using comref = winrt::impl::com_ref<T>;

	inline HRESULT StartMediaFoundation() { return MFStartup(MF_VERSION); }
	inline HRESULT ShutdownMediaFoundation() { return MFShutdown(); }
}