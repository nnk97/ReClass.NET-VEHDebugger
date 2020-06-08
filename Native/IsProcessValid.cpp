#include <ReClassNET_Plugin.hpp>

#include <Windows.h>

/// <summary>Queries if the process is valid.</summary>
/// <param name="handle">The process handle obtained by OpenRemoteProcess.</param>
/// <returns>True if the process is valid, false if not.</returns>
extern "C" bool RC_CallConv IsProcessValid(RC_Pointer handle)
{
	if (handle == nullptr)
	{
		return false;
	}

	const auto retn = WaitForSingleObject(handle, 0);
	if (retn == WAIT_FAILED)
	{
		return false;
	}

	return retn == WAIT_TIMEOUT;
}