#include <ReClassNET_Plugin.hpp>

#include <Windows.h>

/// <summary>Opens the remote process.</summary>
/// <param name="id">The identifier of the process returned by EnumerateProcesses.</param>
/// <param name="desiredAccess">The desired access.</param>
/// <returns>A handle to the remote process or nullptr if an error occured.</returns>
extern "C" RC_Pointer RC_CallConv OpenRemoteProcess(RC_Pointer id, ProcessAccess desiredAccess)
{
	DWORD access = STANDARD_RIGHTS_REQUIRED | PROCESS_TERMINATE | PROCESS_QUERY_INFORMATION | SYNCHRONIZE;
	switch (desiredAccess)
	{
	case ProcessAccess::Read:
		access |= PROCESS_VM_READ;
		break;
	case ProcessAccess::Write:
		access |= PROCESS_VM_OPERATION | PROCESS_VM_WRITE;
		break;
	case ProcessAccess::Full:
		access |= PROCESS_VM_READ | PROCESS_VM_OPERATION | PROCESS_VM_WRITE;
		break;
	}

	const auto handle = OpenProcess(access, FALSE, static_cast<DWORD>(reinterpret_cast<size_t>(id)));

	if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
	{
		return nullptr;
	}

	return handle;
}

/// <summary>Closes the handle to the remote process.</summary>
/// <param name="handle">The process handle obtained by OpenRemoteProcess.</param>
extern "C" void RC_CallConv CloseRemoteProcess(RC_Pointer handle)
{
	if (handle)
		CloseHandle(handle);
}