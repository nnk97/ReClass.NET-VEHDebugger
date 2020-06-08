#include "Main.hpp"
#include <iostream>

#include <Windows.h>

HANDLE hInitThread = INVALID_HANDLE_VALUE;
HANDLE hPluginModule;

void InitializationThread()
{
#ifdef _DEBUG
	AllocConsole();
	SetConsoleTitleA("ReClass.NET - VEH Debugger - Debug output");

	freopen("CONOUT$", "w", stdout);
	freopen("CONOUT$", "w", stderr);
	freopen("CONIN$", "r", stdin);
#endif
}

BOOL WINAPI DllMain(HMODULE hMod, DWORD dwReason, PVOID)
{
	if (dwReason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(hMod);
		hPluginModule = hMod;
		hInitThread = CreateThread(NULL, NULL, reinterpret_cast<LPTHREAD_START_ROUTINE>(InitializationThread), NULL, NULL, NULL);
	}
	else if (dwReason == DLL_PROCESS_DETACH)
	{
		if (hInitThread && hInitThread != INVALID_HANDLE_VALUE)
		{
			TerminateThread(hInitThread, NULL);
			hInitThread = INVALID_HANDLE_VALUE;
		}
	}
	return TRUE;
}