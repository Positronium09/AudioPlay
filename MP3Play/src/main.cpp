#include <iostream>
#include <chrono>
#include <thread>
#include <iomanip>
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#include <shlwapi.h>
#include <winrt/base.h>

#include "MP3.h"
#include "MP3Metadata.h"

#pragma comment (lib, "Shlwapi.lib")

template<class T>
using comptr = winrt::com_ptr<T>;

using namespace std::chrono_literals;
using std::chrono::milliseconds;
using std::chrono::nanoseconds;
using std::chrono::seconds;
using std::chrono::minutes;
using std::chrono::hours;
using std::chrono::duration_cast;


volatile BOOL running = TRUE;

void printDuration(comptr<MP3Play::MP3>* mp3)
{
	while ((*mp3) && running)
	{
		if ((*mp3)->GetState() != MP3Play::MP3States::Started)
		{
			continue;
		}
		milliseconds milliSeconds;
		HRESULT hr = (*mp3)->GetPosition(milliSeconds);

		seconds second = duration_cast<seconds>(milliSeconds);
		milliSeconds %= 1000;

		minutes minute = duration_cast<minutes>(second);
		second %= 60;

		hours hour = duration_cast<hours>(minute);
		minute %= 60;

		float volume = -1.0f;
		mp3->get()->GetVolume(&volume);
		BOOL mute = FALSE;
		mp3->get()->GetMute(&mute);

		std::wcout << 
			hour.count() << " Hours : " << 
			std::setw(2) << minute.count() << " Minutes : " << 
			std::setw(2) << second.count() << " Seconds : " << 
			std::setw(4) << milliSeconds.count() << " Milliseconds" << 
			" Volume: " << std::setw(3) << std::setprecision(1) << volume <<
			" Mute: " << std::setw(1) << mute <<
			'\r';
	}
}

EXTERN_C
BOOL WINAPI CtrlHandle(_In_ DWORD dwCtrlType)
{
	UNREFERENCED_PARAMETER(dwCtrlType);

	if (dwCtrlType == CTRL_C_EVENT)
	{
		running = FALSE;
		return TRUE;
	}

	return FALSE;
}

int main()
{
	winrt::init_apartment();
	
	MP3Play::StartMediaFoundation();

	(void)_setmode(_fileno(stdout), _O_U16TEXT);
	SetConsoleCtrlHandler(CtrlHandle, TRUE);

	{
		WCHAR file[1024] = { 0 };
		SecureZeroMemory(&file, sizeof(OPENFILENAME));
		OPENFILENAMEW openFile = { 0 };
		SecureZeroMemory(&openFile, sizeof(OPENFILENAMEW));

		openFile.lStructSize = sizeof(OPENFILENAMEW);
		openFile.hwndOwner = GetDesktopWindow();
		openFile.hInstance = GetModuleHandle(NULL);
		openFile.lpstrFile = file;
		openFile.nMaxFile = sizeof(file);
		openFile.lpstrFilter = L"MP3 files\0*.mp3\0\0;";
		openFile.lpstrFileTitle = nullptr;
		openFile.nMaxFileTitle = NULL;
		openFile.lpstrInitialDir = L"C:\\Users\\%USERNAME%\\Music";
		openFile.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

		if (!GetOpenFileName(&openFile))
		{
			return 0;
		}


		comptr<MP3Play::MP3> mp3;

		HRESULT hr = MP3Play::MP3::CreateMP3(file, mp3.put());

		while (mp3->GetState() != MP3Play::MP3States::Ready);

		std::thread t{ printDuration, &mp3 };
		MP3Play::MP3Metadata metadata = mp3->GetMetadata();

		LPCWSTR filename = PathFindFileName(file);
		SetConsoleTitleW(filename);

		LPWCH title = nullptr;
		metadata.GetTitle(title);
		if (title)
		{
			std::wcout << L"Title: " << title << '\n';
			CoTaskMemFree(title);
		}
		LPWCH artist = nullptr;
		metadata.GetArtist(artist);
		if (artist)
		{
			std::wcout << L"Artist: " << artist << '\n';
			CoTaskMemFree(artist);
		}
		LPWCH albumName = nullptr;
		metadata.GetAlbumName(albumName);
		if (albumName)
		{
			std::wcout << L"Album name: " << albumName << '\n';
			CoTaskMemFree(albumName);
		}

		std::wcout << "--------------------------------------------------------------------------------------\n";
		std::wcout << "Controls\n";
		std::wcout << "S to start\nP to pause\nESC to exit\nM to mute / unmute\nUp and down arrows for volume\n";

		mp3->Start(0s);
		mp3->Pause();
		mp3->Loop(TRUE);

		int volume = 50;
		mp3->SetVolume(volume / 100.0f);

		HWND console = GetConsoleWindow();

		while ((mp3->GetState() != MP3Play::MP3States::Stopped) && running)
		{
			if (GetAsyncKeyState(VK_ESCAPE) & 1 && GetForegroundWindow() == console)
			{
				running = FALSE;
			}
			if (GetAsyncKeyState('P') & 1 && GetForegroundWindow() == console)
			{
				mp3->Pause();
			}
			if (GetAsyncKeyState('S') & 1 && GetForegroundWindow() == console)
			{
				mp3->Start();
			}
			if (GetAsyncKeyState('M') & 1 && GetForegroundWindow() == console)
			{
				BOOL mute;
				mp3->GetMute(&mute);
				mp3->SetMute(!mute);
			}
			if (GetAsyncKeyState(VK_UP) & 1 && GetForegroundWindow() == console)
			{
				volume = std::clamp(volume + 10, 0, 100);
				mp3->SetVolume(volume / 100.0f);
			}
			if (GetAsyncKeyState(VK_DOWN) & 1 && GetForegroundWindow() == console)
			{
				volume = std::clamp(volume - 10, 0, 100);
				mp3->SetVolume(volume / 100.0f);
			}
		}
		t.join();
		mp3->CloseFile();
	}

	MP3Play::ShutdownMediaFoundation();

	winrt::uninit_apartment();

	return 0;
}
