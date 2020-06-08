#include <Windows.h>
#include <TlHelp32.h>

#include <cstdio>
#include <vector>

#include "../Shared/SharedMemory.hpp"

CSharedExceptionData* DbgSharedData = nullptr;

#define DEBUG_MSG_BOXES // Enable error logging

#ifdef DEBUG_MSG_BOXES
void PrintLog(const char* format, ...)
{
	va_list args_list;
	char buffer[2048] = { 0 };

	va_start(args_list, format);
	vsprintf_s(buffer, format, args_list);
	va_end(args_list);

	MessageBoxA(NULL, buffer, "VEHDbg", 0);
}
#else
__forceinline void PrintLog(const char* format, ...) {}
#endif

// Events for synchronization
HANDLE ghExceptionOccuredEvent;
HANDLE ghExceptionHandledEvent;
HANDLE ghInitializationFinishedEvent;

std::vector<DWORD> PausedThreadList;

void SuspendAllThreads(DWORD CurrentThreadId)
{
	PausedThreadList.clear();

	HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (h != INVALID_HANDLE_VALUE)
	{
		THREADENTRY32 te;
		te.dwSize = sizeof(te);
		if (Thread32First(h, &te))
		{
			do
			{
				if (te.dwSize >= FIELD_OFFSET(THREADENTRY32, th32OwnerProcessID) + sizeof(te.th32OwnerProcessID))
				{
					if (te.th32ThreadID != CurrentThreadId && te.th32OwnerProcessID == GetCurrentProcessId())
					{
						HANDLE ThreadHandle = ::OpenThread(THREAD_ALL_ACCESS, FALSE, te.th32ThreadID);
						if (ThreadHandle != NULL)
						{
							PausedThreadList.push_back(te.th32ThreadID);
							SuspendThread(ThreadHandle);
							CloseHandle(ThreadHandle);
						}
					}
				}
				te.dwSize = sizeof(te);
			} while (Thread32Next(h, &te));
		}
		CloseHandle(h);
	}
}

void ResumeAllThreads()
{
	for (auto ThreadId : PausedThreadList)
	{
		HANDLE ThreadHandle = ::OpenThread(THREAD_ALL_ACCESS, FALSE, ThreadId);
		if (ThreadHandle != NULL)
		{
			ResumeThread(ThreadHandle);
			CloseHandle(ThreadHandle);
		}
	}
}

LONG WINAPI VEHHandlerImpl(PEXCEPTION_POINTERS pExceptionInfo)
{
	static PVOID GuardPageAddr = nullptr;
	static BOOL TriggerEvent = false;

	if (pExceptionInfo->ExceptionRecord->ExceptionCode == STATUS_GUARD_PAGE_VIOLATION)
	{
		auto FaultyAddress = reinterpret_cast<void*>(pExceptionInfo->ExceptionRecord->ExceptionInformation[1]);
		auto IsWriteViolation = pExceptionInfo->ExceptionRecord->ExceptionInformation[0] == 1;

		if (!FaultyAddress)
			goto LB_CONTINUE_SEARCH;

		bool IsCausedByUs = false;
		for (auto iDR = 0u; iDR < 4u; iDR++)
		{
			const auto& EmulatedRegisterData = DbgSharedData->DebugRegisters[iDR];
			if (!EmulatedRegisterData.Address)
				continue;

			if (PageAligned(EmulatedRegisterData.Address) == PageAligned(FaultyAddress))
				IsCausedByUs = true;
		}

		if (!IsCausedByUs)
			goto LB_CONTINUE_SEARCH;

		TriggerEvent = false;

		SuspendAllThreads(GetCurrentThreadId());

		for (auto iDR = 0u; iDR < 4u; iDR++)
		{
			const auto& EmulatedRegisterData = DbgSharedData->DebugRegisters[iDR];
			if (!EmulatedRegisterData.Address)
				continue;

			if (EmulatedRegisterData.Type == HardwareBreakpointTrigger::Execute || (EmulatedRegisterData.Type == HardwareBreakpointTrigger::Write && !IsWriteViolation))
				continue;

			auto ExcAddress = reinterpret_cast<std::uintptr_t>(FaultyAddress);
			auto WantedAddress = reinterpret_cast<std::uintptr_t>(EmulatedRegisterData.Address);
			if (ExcAddress >= WantedAddress && ExcAddress < WantedAddress + ReclassSizeToNumber(EmulatedRegisterData.Size))
			{
				DbgSharedData->ExceptionPID = GetCurrentProcessId();
				DbgSharedData->ExceptionThreadID = GetCurrentThreadId();

				std::memcpy(&DbgSharedData->ExceptionRecord, pExceptionInfo->ExceptionRecord, sizeof(EXCEPTION_RECORD));
				std::memcpy(&DbgSharedData->ContextRecord, pExceptionInfo->ContextRecord, sizeof(CONTEXT));

				// Pass which register caused exception
				DebugRegister6 dr6;
				dr6.Value = NULL;
				
				switch (iDR)
				{
				case 0:
					dr6.DR0 = 1;
					break;
				case 1:
					dr6.DR1 = 1;
					break;
				case 2:
					dr6.DR2 = 1;
					break;
				case 3:
					dr6.DR3 = 1;
					break;
				}

				DbgSharedData->ContextRecord.Dr6 = dr6.Value;

				/* This executes too early here, we need to fix ExceptionAddress (either disassm or just let it run 1 more instr)
				ResetEvent(ghExceptionHandledEvent);
				SetEvent(ghExceptionOccuredEvent);

				WaitForSingleObject(ghExceptionHandledEvent, INFINITE); */

				TriggerEvent = true;
			}
		}

		GuardPageAddr = PageAligned(FaultyAddress);
		pExceptionInfo->ContextRecord->EFlags |= 0x100; // Set SINGLE_STEP flag

		return EXCEPTION_CONTINUE_EXECUTION;
	}
	else if (pExceptionInfo->ExceptionRecord->ExceptionCode == STATUS_SINGLE_STEP)
	{
		if (GuardPageAddr)
		{
			MEMORY_BASIC_INFORMATION mbi;
			if (VirtualQuery(GuardPageAddr, &mbi, sizeof(mbi)))
			{
				DWORD NewProtection = mbi.Protect | PAGE_GUARD;
				if (!VirtualProtect(GuardPageAddr, PageSize, NewProtection, &NewProtection))
					PrintLog("Failed to restore PAGE_GUARD on 0x%p!", GuardPageAddr);
			}

			GuardPageAddr = nullptr;

			if (TriggerEvent)
			{
				DbgSharedData->ExceptionRecord.ExceptionAddress = pExceptionInfo->ExceptionRecord->ExceptionAddress;

				ResetEvent(ghExceptionHandledEvent);
				SetEvent(ghExceptionOccuredEvent);

				WaitForSingleObject(ghExceptionHandledEvent, INFINITE);

				TriggerEvent = false;
			}
		}

		ResumeAllThreads();

		return EXCEPTION_CONTINUE_EXECUTION;
	}

LB_CONTINUE_SEARCH:
	return EXCEPTION_CONTINUE_SEARCH;
}

void HandleLoad()
{
	char ShMemObjectName[MAX_PATH];
	sprintf_s(ShMemObjectName, "RclsVEH_Data_%u", GetCurrentProcessId());

	HANDLE hMapFile = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, ShMemObjectName);
	if (!hMapFile)
	{
		PrintLog("Failed to OpenFileMappingA on %s!", ShMemObjectName);
		return;
	}

	DbgSharedData = reinterpret_cast<CSharedExceptionData*>(MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, NULL, NULL, sizeof(CSharedExceptionData)));
	if (!DbgSharedData)
	{
		PrintLog("Failed to MapViewOfFile on %s!", ShMemObjectName);
		CloseHandle(hMapFile);
		return;
	}

	CloseHandle(hMapFile);

	auto OpenEventByName = [](const char* EvtName) -> HANDLE
	{
		if (!EvtName)
			return NULL;
		char ShEvtName[MAX_PATH];
		sprintf_s(ShEvtName, "RclsVEH_Evt_%s_%u", EvtName, GetCurrentProcessId());

		return OpenEventA(EVENT_ALL_ACCESS, false, ShEvtName);
	};

	ghExceptionOccuredEvent = OpenEventByName("ExceptionOccured");
	ghExceptionHandledEvent = OpenEventByName("ExceptionHandled");
	ghInitializationFinishedEvent = OpenEventByName("InitializationFinished");

	if (!ghExceptionOccuredEvent || !ghExceptionHandledEvent || !ghInitializationFinishedEvent)
	{
		PrintLog("Failed to open events! (0x%X, 0x%X)", ghExceptionOccuredEvent, ghExceptionHandledEvent);
		return;
	}

	if (!AddVectoredExceptionHandler(TRUE, VEHHandlerImpl))
	{
		PrintLog("Failed to execute AddVectoredExceptionHandler!");
		return;
	}

	SetEvent(ghInitializationFinishedEvent);
}

// This probably doesn't work, because game is crashing :o
void HandleUnload()
{
	RemoveVectoredExceptionHandler(VEHHandlerImpl);

	if (ghExceptionHandledEvent)
	{
		CloseHandle(ghExceptionHandledEvent);
		ghExceptionHandledEvent = NULL;
	}

	if (ghExceptionOccuredEvent)
	{
		CloseHandle(ghExceptionOccuredEvent);
		ghExceptionOccuredEvent = NULL;
	}

	if (DbgSharedData)
	{
		UnmapViewOfFile(DbgSharedData);
		DbgSharedData = nullptr;
	}
}

BOOL WINAPI DllMain(HMODULE hMod, DWORD dwReason, PVOID)
{
	if (dwReason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(hMod);
		CreateThread(NULL, NULL, reinterpret_cast<LPTHREAD_START_ROUTINE>(HandleLoad), NULL, NULL, NULL);
	}
	else if (dwReason == DLL_PROCESS_DETACH)
	{
		HandleUnload();
	}
	return TRUE;
}

