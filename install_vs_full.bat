@echo off
cd /d "C:\Program Files (x86)\Microsoft Visual Studio\Installer"
vs_installer.exe modify --installPath "C:\Program Files\Microsoft Visual Studio\2022\Community" --add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended --passive --norestart
echo Exit code: %ERRORLEVEL%
pause