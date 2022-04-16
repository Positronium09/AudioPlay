#include <iostream>
#include <algorithm>
#include <functional>
#include <chrono>
#include <thread>
#include <iomanip>
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#include <shlwapi.h>

#include "Audio.h"
#include "AudioMetadata.h"

#pragma comment (lib, "Shlwapi.lib")

using namespace std::chrono_literals;
using std::chrono::milliseconds;
using std::chrono::nanoseconds;
using std::chrono::seconds;
using std::chrono::minutes;
using std::chrono::hours;
using std::chrono::duration_cast;

using AudioPlay::ComPtr;

volatile BOOL running = TRUE;

void PrintDuration(std::reference_wrapper<ComPtr<AudioPlay::Audio>> mp3)
{
	while (mp3.get() && running)
	{
		if (mp3.get()->GetState() != AudioPlay::AudioStates::Started)
		{
			continue;
		}
		milliseconds milliSeconds;
		mp3.get()->GetPosition(milliSeconds);

		seconds second = duration_cast<seconds>(milliSeconds);
		milliSeconds %= 1000;

		minutes minute = duration_cast<minutes>(second);
		second %= 60;

		hours hour = duration_cast<hours>(minute);
		minute %= 60;

		float volume = -1.0f;
		mp3.get()->GetVolume(volume);
		BOOL mute = FALSE;
		mp3.get()->GetMute(mute);

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

void Callback(IMFMediaEvent* p_event)
{
	ComPtr<IMFMediaEvent> event = p_event;

	MessageBeep(MB_OK);
}

int main()
{
	AudioPlay::StartMediaFoundation();

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


		ComPtr<AudioPlay::Audio> mp3;

		AudioPlay::Audio::CreateAudio(file, [](IMFMediaEvent* p_event)
		{
			ComPtr<IMFMediaEvent> event = p_event;

			OutputDebugString(TEXT("\nEvent fired\n"));
		}, &mp3);

		while (mp3->GetState() != AudioPlay::AudioStates::Ready);

		std::thread t{ PrintDuration, std::ref(mp3) };
		AudioPlay::AudioMetadata metadata = mp3->GetMetadata();

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
		mp3->SetLoop(TRUE);

		int volume = 50;
		mp3->SetVolume(volume / 100.0f);

		HWND console = GetConsoleWindow();

		while ((mp3->GetState() != AudioPlay::AudioStates::Stopped) && running)
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
				mp3->GetMute(mute);
				mp3->SetMute((BOOL)!mute); // Abomination to make "Using logical or when bitwise '~' was probably intended" go away
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

	AudioPlay::ShutdownMediaFoundation();

	return 0;
}
