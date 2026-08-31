@echo off
setlocal
call "C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
python generate_icon.py
rc /nologo /fo taskbar_monitor.res taskbar_monitor.rc
cl /nologo /W4 /O2 /DUNICODE=0 /D_UNICODE=0 taskbar_monitor.c taskbar_monitor.res /link /SUBSYSTEM:WINDOWS /OUT:TaskbarMonitor.exe shell32.lib user32.lib advapi32.lib gdi32.lib
if errorlevel 1 exit /b %errorlevel%
echo Built TaskbarMonitor.exe
