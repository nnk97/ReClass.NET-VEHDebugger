#include <cstdint>
#include <iostream>

#include <ReClassNET_Plugin.hpp>
#include <Windows.h>
#include <TlHelp32.h>

#include "../Shared/SharedMemory.hpp"

#include "../ext/blackbone/src/BlackBone/Process/Process.h"
#include "../ext/blackbone/src/BlackBone/ManualMap/MMap.h"

#include "Main.hpp"

DWORD DebuggedProcessId = NULL;
blackbone::Process CurrentDebuggedProcess;
blackbone::ModuleDataPtr RemoteStubModule = nullptr;

CSharedExceptionData* DbgSharedData = nullptr;
HANDLE SharedDataMappingHandle = NULL;

// Events for synchronization
HANDLE ghExceptionOccuredEvent;
HANDLE ghExceptionHandledEvent;
HANDLE ghInitializationFinishedEvent;

bool InitializedFully = false;

#ifdef _DEBUG
void PrintLog(const char* func_name, const char* format, ...)
{
	va_list args_list;
	char buffer[2048] = { 0 };

	va_start(args_list, format);
	vsprintf_s(buffer, format, args_list);
	va_end(args_list);

	printf(" > %s: %s\n", func_name, buffer);
}
#else
__forceinline void PrintLog(const char* func_name, const char* format, ...) {}
#endif

/// <summary>Wait for a debug event within the given timeout.</summary>
/// <param name="evt">[out] The occured debug event.</param>
/// <param name="timeoutInMilliseconds">The timeout in milliseconds.</param>
/// <returns>True if an event occured within the given timeout, false if not.</returns>
extern "C" bool RC_CallConv AwaitDebugEvent(DebugEvent* evt, int timeoutInMilliseconds)
{
	if (!DbgSharedData)
		return false;

	// Wait for a debug event.
	if (WaitForSingleObject(ghExceptionOccuredEvent, timeoutInMilliseconds) != WAIT_OBJECT_0)
		return false;

	PrintLog(__FUNCTION__, "Caught exception!");

	// Copy basic informations.
	evt->ProcessId = reinterpret_cast<RC_Pointer>(DbgSharedData->ExceptionPID);
	evt->ThreadId = reinterpret_cast<RC_Pointer>(DbgSharedData->ExceptionThreadID);

	evt->ExceptionInfo.ExceptionAddress = DbgSharedData->ExceptionRecord.ExceptionAddress;
	evt->ExceptionInfo.ExceptionCode = DbgSharedData->ExceptionRecord.ExceptionCode;
	evt->ExceptionInfo.ExceptionFlags = DbgSharedData->ExceptionRecord.ExceptionFlags;

	// Handle dr6
	DebugRegister6 dr6;
	dr6.Value = DbgSharedData->ContextRecord.Dr6;

	if (dr6.DR0)
		evt->ExceptionInfo.CausedBy = HardwareBreakpointRegister::Dr0;
	else if (dr6.DR1)
		evt->ExceptionInfo.CausedBy = HardwareBreakpointRegister::Dr1;
	else if (dr6.DR2)
		evt->ExceptionInfo.CausedBy = HardwareBreakpointRegister::Dr2;
	else if (dr6.DR3)
		evt->ExceptionInfo.CausedBy = HardwareBreakpointRegister::Dr3;
	else
		evt->ExceptionInfo.CausedBy = HardwareBreakpointRegister::InvalidRegister;

	// Copy registers.
	auto& ctx = DbgSharedData->ContextRecord;
	auto& reg = evt->ExceptionInfo.Registers;

#ifdef RECLASSNET64
	reg.Rax = reinterpret_cast<RC_Pointer>(ctx.Rax);
	reg.Rbx = reinterpret_cast<RC_Pointer>(ctx.Rbx);
	reg.Rcx = reinterpret_cast<RC_Pointer>(ctx.Rcx);
	reg.Rdx = reinterpret_cast<RC_Pointer>(ctx.Rdx);
	reg.Rdi = reinterpret_cast<RC_Pointer>(ctx.Rdi);
	reg.Rsi = reinterpret_cast<RC_Pointer>(ctx.Rsi);
	reg.Rsp = reinterpret_cast<RC_Pointer>(ctx.Rsp);
	reg.Rbp = reinterpret_cast<RC_Pointer>(ctx.Rbp);
	reg.Rip = reinterpret_cast<RC_Pointer>(ctx.Rip);

	reg.R8 = reinterpret_cast<RC_Pointer>(ctx.R8);
	reg.R9 = reinterpret_cast<RC_Pointer>(ctx.R9);
	reg.R10 = reinterpret_cast<RC_Pointer>(ctx.R10);
	reg.R11 = reinterpret_cast<RC_Pointer>(ctx.R11);
	reg.R12 = reinterpret_cast<RC_Pointer>(ctx.R12);
	reg.R13 = reinterpret_cast<RC_Pointer>(ctx.R13);
	reg.R14 = reinterpret_cast<RC_Pointer>(ctx.R14);
	reg.R15 = reinterpret_cast<RC_Pointer>(ctx.R15);
#else
	reg.Eax = reinterpret_cast<RC_Pointer>(ctx.Eax);
	reg.Ebx = reinterpret_cast<RC_Pointer>(ctx.Ebx);
	reg.Ecx = reinterpret_cast<RC_Pointer>(ctx.Ecx);
	reg.Edx = reinterpret_cast<RC_Pointer>(ctx.Edx);
	reg.Edi = reinterpret_cast<RC_Pointer>(ctx.Edi);
	reg.Esi = reinterpret_cast<RC_Pointer>(ctx.Esi);
	reg.Esp = reinterpret_cast<RC_Pointer>(ctx.Esp);
	reg.Ebp = reinterpret_cast<RC_Pointer>(ctx.Ebp);
	reg.Eip = reinterpret_cast<RC_Pointer>(ctx.Eip);
#endif

	return true;
}

/// <summary>Handles the debug event described by evt.</summary>
/// <param name="evt">[in] The (modified) event returned by AwaitDebugEvent.</param>
extern "C" void RC_CallConv HandleDebugEvent(DebugEvent* evt)
{
	if (!DbgSharedData)
	{
		PrintLog(__FUNCTION__, "DbgSharedData is not setup!");
		return;
	}

	DbgSharedData->ContinueStatus = evt->ContinueStatus;

	auto r1 = ResetEvent(ghExceptionOccuredEvent);
	auto r2 = SetEvent(ghExceptionHandledEvent);

	PrintLog(__FUNCTION__, "Finished exception handling and signaled to stub! (0x%X, 0x%X)", r1, r2);
}

/// <summary>Sets a hardware breakpoint.</summary>
/// <param name="processId">The identifier of the process returned by EnumerateProcesses.</param>
/// <param name="address">The address of the breakpoint.</param>
/// <param name="reg">The register to use.</param>
/// <param name="type">The type of the breakpoint.</param>
/// <param name="size">The size of the breakpoint.</param>
/// <param name="set">True to set the breakpoint, false to remove it.</param>
/// <returns>True if it succeeds, false if it fails.</returns>
extern "C" bool RC_CallConv SetHardwareBreakpoint(RC_Pointer id, RC_Pointer address, HardwareBreakpointRegister reg, HardwareBreakpointTrigger type, HardwareBreakpointSize size, bool set)
{
	if (!InitializedFully)
	{
		PrintLog(__FUNCTION__, "Debugger is not attached!");
		return false;
	}

	if (!DbgSharedData)
	{
		PrintLog(__FUNCTION__, "DbgSharedData is not setup!");
		return false;
	}

	if (!CurrentDebuggedProcess.valid())
	{
		PrintLog(__FUNCTION__, "CurrentDebuggedProcess is not valid!");
		return false;
	}

	if (reg == HardwareBreakpointRegister::InvalidRegister)
	{
		PrintLog(__FUNCTION__, "Tried to set register on InvalidRegister!");
		return false;
	}

	if (type == HardwareBreakpointTrigger::Execute)
	{
		PrintLog(__FUNCTION__, "HardwareBreakpointTrigger Execute isn't supported! (Didn't test or think about it yet)");
		return false;
	}

	CDebugRegisterInfo* FakeRegisterInfo = &DbgSharedData->DebugRegisters[0];
	if (reg == HardwareBreakpointRegister::Dr0)
	{
		FakeRegisterInfo = &DbgSharedData->DebugRegisters[0];
		PrintLog(__FUNCTION__, "Target DR0");
	}
	else if (reg == HardwareBreakpointRegister::Dr1)
	{
		FakeRegisterInfo = &DbgSharedData->DebugRegisters[1];
		PrintLog(__FUNCTION__, "Target DR1");
	}
	else if (reg == HardwareBreakpointRegister::Dr2)
	{
		FakeRegisterInfo = &DbgSharedData->DebugRegisters[2];
		PrintLog(__FUNCTION__, "Target DR2");
	}
	else if (reg == HardwareBreakpointRegister::Dr3)
	{
		FakeRegisterInfo = &DbgSharedData->DebugRegisters[3];
		PrintLog(__FUNCTION__, "Target DR3");
	}
	else
		return false;

	if (set)
	{
		// Register is already taken, but we can handle it later, just not now :zzz:
		if ((reg == HardwareBreakpointRegister::Dr0 && DbgSharedData->DebugRegisters[0].Address) ||
			(reg == HardwareBreakpointRegister::Dr1 && DbgSharedData->DebugRegisters[1].Address) ||
			(reg == HardwareBreakpointRegister::Dr2 && DbgSharedData->DebugRegisters[2].Address) ||
			(reg == HardwareBreakpointRegister::Dr3 && DbgSharedData->DebugRegisters[3].Address))
		{
			PrintLog(__FUNCTION__, "Debug register is already taken!");
			return false;
		}

		auto AddPageGuard = [](void* VaArg) -> bool
		{
			void* Va = PageAligned(VaArg);

			MEMORY_BASIC_INFORMATION64 mbi;
			auto MbiResult = CurrentDebuggedProcess.memory().Query(reinterpret_cast<blackbone::ptr_t>(Va), &mbi);
			if (MbiResult != STATUS_SUCCESS)
			{
				PrintLog(__FUNCTION__, "VirtualQuery failed on 0x%p!", Va);
				return false;
			}

			DWORD NewProtection = mbi.Protect | PAGE_GUARD;
			auto ProtResult = CurrentDebuggedProcess.memory().Protect(reinterpret_cast<blackbone::ptr_t>(Va), PageSize, NewProtection, &NewProtection);
			if (ProtResult != STATUS_SUCCESS)
			{
				PrintLog(__FUNCTION__, "VirtualProtect failed on 0x%p!", Va);
				return false;
			}

			PrintLog(__FUNCTION__, "Successfully added PAGE_GUARD to 0x%p...", Va);
			return true;
		};

		if (!AddPageGuard(address) || !AddPageGuard(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(address) + ReclassSizeToNumber(size) - 1)))
			return false;

		FakeRegisterInfo->Address = address;
		FakeRegisterInfo->Size = size;
		FakeRegisterInfo->Type = type;

		PrintLog(__FUNCTION__, "Breakpoint on 0x%p added! (%p size)", address, size);

		return true;
	}
	else
	{
		// No address, it's already cleared?
		if (!FakeRegisterInfo->Address)
		{
			PrintLog(__FUNCTION__, "That register wasn't even used?");
			return true;
		}

		auto AlignedAddress = PageAligned(FakeRegisterInfo->Address);

		MEMORY_BASIC_INFORMATION64 mbi;
		auto MbiResult = CurrentDebuggedProcess.memory().Query(reinterpret_cast<blackbone::ptr_t>(AlignedAddress), &mbi);
		if (MbiResult != STATUS_SUCCESS)
		{
			PrintLog(__FUNCTION__, "VirtualQuery failed on 0x%p when trying to remove PAGE_GUARD!", AlignedAddress);
			return false;
		}

		DWORD NewProtection = mbi.Protect & ~PAGE_GUARD;
		PrintLog(__FUNCTION__, "Current protection: %p, new protection: %p", mbi.Protect, NewProtection);
		auto ProtResult = CurrentDebuggedProcess.memory().Protect(reinterpret_cast<blackbone::ptr_t>(AlignedAddress), PageSize, NewProtection, &NewProtection);
		if (ProtResult != STATUS_SUCCESS)
		{
			PrintLog(__FUNCTION__, "VirtualProtect failed on 0x%p when trying to remove PAGE_GUARD!", AlignedAddress);
			return false;
		}

		PrintLog(__FUNCTION__, "Removed PAGE_GUARD on page 0x%p! (Bp for address 0x%p)", AlignedAddress, FakeRegisterInfo->Address);

		FakeRegisterInfo->Address = NULL;

		return true;
	}

	return false;
}

/// <summary>Attach a debugger to the process.</summary>
/// <param name="id">The identifier of the process returned by EnumerateProcesses.</param>
/// <returns>True if it succeeds, false if it fails.</returns>
extern "C" bool RC_CallConv AttachDebuggerToProcess(RC_Pointer id)
{
	// Tried to attach to another process before detaching from prev?
	if (DebuggedProcessId != 0)
	{
		PrintLog(__FUNCTION__, "Tried to attach before detaching from PID %X!", DebuggedProcessId);
		return false;
	}

	if (CurrentDebuggedProcess.Attach(reinterpret_cast<DWORD>(id), PROCESS_ALL_ACCESS) != STATUS_SUCCESS)
	{
		PrintLog(__FUNCTION__, "Blackbone failed to attach to target process!");
		return false;
	}

	char ShMemObjectName[MAX_PATH];
	sprintf_s(ShMemObjectName, "RclsVEH_Data_%u", reinterpret_cast<DWORD>(id));

	SharedDataMappingHandle = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, NULL, sizeof(CSharedExceptionData), ShMemObjectName);
	if (SharedDataMappingHandle == NULL)
	{
		PrintLog(__FUNCTION__, "Failed on CreateFileMappingA!");
		return false;
	}

	DbgSharedData = reinterpret_cast<CSharedExceptionData*>(MapViewOfFile(SharedDataMappingHandle, FILE_MAP_ALL_ACCESS, NULL, 0u, sizeof(CSharedExceptionData)));
	if (!DbgSharedData)
	{
		PrintLog(__FUNCTION__, "Failed on MapViewOfFile!");
		return false;
	}

	auto CreateEventByName = [id](const char* EvtName) -> HANDLE
	{
		if (!EvtName)
			return NULL;
		char ShEvtName[MAX_PATH];
		sprintf_s(ShEvtName, "RclsVEH_Evt_%s_%u", EvtName, reinterpret_cast<DWORD>(id));

		return CreateEventA(NULL, TRUE, FALSE, ShEvtName);
	};

	ghExceptionOccuredEvent = CreateEventByName("ExceptionOccured");
	ghExceptionHandledEvent = CreateEventByName("ExceptionHandled");
	ghInitializationFinishedEvent = CreateEventByName("InitializationFinished");

	if (!ghExceptionOccuredEvent || !ghExceptionHandledEvent || !ghInitializationFinishedEvent)
	{
		PrintLog(__FUNCTION__, "Failed to create events! (0x%X, 0x%X, 0x%X)", ghExceptionOccuredEvent, ghExceptionHandledEvent, ghInitializationFinishedEvent);
		return false;
	}

	wchar_t PathBuffer[MAX_PATH];
	if (!GetModuleFileNameW(reinterpret_cast<HMODULE>(hPluginModule), PathBuffer, MAX_PATH))
	{
#ifndef RECLASSNET64
		wcscpy_s(PathBuffer, L"Plugins\\VEHDbg_stub_x86.dll");
#else 
		wcscpy_s(PathBuffer, L"Plugins\\VEHDbg_stub_x64.dll");
#endif
	}
	else
	{
		std::wstring StubPath;
		std::wstring_view PluginPath(PathBuffer);
		StubPath = PluginPath.substr(0, PluginPath.find_last_of(L'\\'));

#ifndef RECLASSNET64
		StubPath.append(L"\\VEHDbg_stub_x86.dll");
#else 
		StubPath.append(L"\\VEHDbg_stub_x64.dll");
#endif
		wcscpy_s(PathBuffer, StubPath.c_str());
	}

	auto Result = CurrentDebuggedProcess.modules().Inject(PathBuffer);
	if (Result.success())
	{
		RemoteStubModule = *Result;
		DebuggedProcessId = reinterpret_cast<DWORD>(id);
		PrintLog(__FUNCTION__, "Stub injected, loading ok! Waiting for signal from stub dll...");

		WaitForSingleObject(ghInitializationFinishedEvent, INFINITE);

		PrintLog(__FUNCTION__, "Signal recived, loading ok!");

		InitializedFully = true;
		return true;
	}
	else
	{
		PrintLog(__FUNCTION__, "Failed to inject stub module! (0x%X)", Result.status);
		return false;
	}
}

/// <summary>Detach a debugger from the remote process.</summary>
/// <param name="id">The identifier of the process returned by EnumerateProcesses.</param>
extern "C" void RC_CallConv DetachDebuggerFromProcess(RC_Pointer id)
{
	if (DebuggedProcessId != reinterpret_cast<DWORD>(id))
	{
		PrintLog(__FUNCTION__, "Trying to detach from wrong process!");
		return;
	}

	if (RemoteStubModule)
	{
		CurrentDebuggedProcess.modules().Unload(RemoteStubModule);
		RemoteStubModule = nullptr;
	}

	SetHardwareBreakpoint(0, 0, HardwareBreakpointRegister::Dr0, HardwareBreakpointTrigger::Access, HardwareBreakpointSize::Size8, false);
	SetHardwareBreakpoint(0, 0, HardwareBreakpointRegister::Dr1, HardwareBreakpointTrigger::Access, HardwareBreakpointSize::Size8, false);
	SetHardwareBreakpoint(0, 0, HardwareBreakpointRegister::Dr2, HardwareBreakpointTrigger::Access, HardwareBreakpointSize::Size8, false);
	SetHardwareBreakpoint(0, 0, HardwareBreakpointRegister::Dr3, HardwareBreakpointTrigger::Access, HardwareBreakpointSize::Size8, false);

	InitializedFully = false;

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

	if (SharedDataMappingHandle)
	{
		CloseHandle(SharedDataMappingHandle);
		SharedDataMappingHandle = NULL;
	}

	CurrentDebuggedProcess.Detach();
	DebuggedProcessId = 0;

	PrintLog(__FUNCTION__, "Detached from process!");
}
