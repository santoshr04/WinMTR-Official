@echo off
echo Installing Visual Studio C++ Desktop Development Workload...
"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vs_installer.exe" modify --installPath "C:\Program Files\Microsoft Visual Studio\2022\Community" --passive --add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended --norestart --quiet
echo Exit code: %ERRORLEVEL%