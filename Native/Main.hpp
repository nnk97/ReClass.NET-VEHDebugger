#pragma once

#include <ReClassNET_Plugin.hpp>
#include <Windows.h>

extern HANDLE hPluginModule;

extern "C" bool RC_CallConv ReadRemoteMemory(RC_Pointer handle, RC_Pointer address, RC_Pointer buffer, int offset, int size);
extern "C" RC_Pointer RC_CallConv OpenRemoteProcess(RC_Pointer id, ProcessAccess desiredAccess);
extern "C" void RC_CallConv CloseRemoteProcess(RC_Pointer handle);
extern "C" bool RC_CallConv IsProcessValid(RC_Pointer handle);