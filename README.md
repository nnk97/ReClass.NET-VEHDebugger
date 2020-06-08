[ReClass.NET](https://github.com/ReClassNET/ReClass.NET) - VEH debugger plugin
=================================
It's **very** slow, do not expect anything good out of it, but it seems to catch access/writes just ok, so maybe someone will find this useful one day.

I didn't want to bother with editing ReClass.NET internals, so it kind of emulates hardware breakpoints.

"Stub" dll is injected using [Blackbone](https://github.com/DarthTon/Blackbone) library by DarthTon.

Compiling
-----
To compile the plugin it requires the usual [ReClass.NET](https://github.com/ReClassNET/ReClass.NET) plugin folders layout, like that:
```
..\ReClass.NET\
..\ReClass.NET\ReClass.NET\ReClass.NET.csproj
..\ReClass.NET-VEHDebugger\
..\ReClass.NET-VEHDebugger\ReClass.NET - VEH Debugger.sln
..\ReClass.NET-VEHDebugger\Native\SamplePlugin.csproj
```
