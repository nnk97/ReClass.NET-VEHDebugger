#include <ReClassNET_Plugin.hpp>

#include <Windows.h>

/// <summary>Reads memory of the remote process.</summary>
/// <param name="handle">The process handle obtained by OpenRemoteProcess.</param>
/// <param name="address">The address to read from.</param>
/// <param name="buffer">The buffer to read into.</param>
/// <param name="offset">The offset into the buffer.</param>
/// <param name="size">The number of bytes to read.</param>
/// <returns>True if it succeeds, false if it fails.</returns>
extern "C" bool RC_CallConv ReadRemoteMemory(RC_Pointer handle, RC_Pointer address, RC_Pointer buffer, int offset, int size)
{
	buffer = reinterpret_cast<RC_Pointer>(reinterpret_cast<uintptr_t>(buffer) + offset);

	SIZE_T numberOfBytesRead;
	if (ReadProcessMemory(handle, address, buffer, size, &numberOfBytesRead) && size == numberOfBytesRead)
	{
		return true;
	}

	return false;
}

/// <summary>Writes memory to the remote process.</summary>
/// <param name="process">The process handle obtained by OpenRemoteProcess.</param>
/// <param name="address">The address to write to.</param>
/// <param name="buffer">The buffer to write.</param>
/// <param name="offset">The offset into the buffer.</param>
/// <param name="size">The number of bytes to write.</param>
/// <returns>True if it succeeds, false if it fails.</returns>
extern "C" bool RC_CallConv WriteRemoteMemory(RC_Pointer handle, RC_Pointer address, RC_Pointer buffer, int offset, int size)
{
	buffer = reinterpret_cast<RC_Pointer>(reinterpret_cast<uintptr_t>(buffer) + offset);

	DWORD oldProtect;
	if (VirtualProtectEx(handle, address, size, PAGE_EXECUTE_READWRITE, &oldProtect))
	{
		SIZE_T numberOfBytesWritten;
		if (WriteProcessMemory(handle, address, buffer, size, &numberOfBytesWritten))
		{
			VirtualProtectEx(handle, address, size, oldProtect, nullptr);

			if (size == numberOfBytesWritten)
			{
				return true;
			}
		}
	}

	return false;
}