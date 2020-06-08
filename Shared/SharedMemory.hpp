#include <ReClassNET_Plugin.hpp>

class CDebugRegisterInfo
{
public:
	RC_Pointer Address;
	HardwareBreakpointSize Size;
	HardwareBreakpointTrigger Type;
};

class CSharedExceptionData
{
public:
	CDebugRegisterInfo DebugRegisters[4];
	DebugContinueStatus ContinueStatus;

	DWORD ExceptionPID;
	DWORD ExceptionThreadID;

	EXCEPTION_RECORD ExceptionRecord;
	CONTEXT ContextRecord;
};

constexpr const auto PageSize = 0x1000;
auto PageAligned = [](void* Va) { return ((PVOID)((ULONG_PTR)(Va) & ~(PageSize - 1))); };

auto ReclassSizeToNumber = [](HardwareBreakpointSize s)
{
	switch (s)
	{
	case HardwareBreakpointSize::Size1:
		return 1u;
	case HardwareBreakpointSize::Size2:
		return 2u;
	case HardwareBreakpointSize::Size4:
		return 4u;
	case HardwareBreakpointSize::Size8:
		return 8u;
	}
	return 0u;
};
