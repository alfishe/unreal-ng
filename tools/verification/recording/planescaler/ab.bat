@echo off
rem PlaneScaler A/B with MSVC: ab.bat DIR_A DIR_B. Each folder holds framescaler.h and framescaler.cpp
rem (git show REV:core/recording/src/common/framescaler.cpp > DIR_A\framescaler.cpp ...); run in a
rem "x64 Native Tools" prompt (vcvars64.bat). /O2 /W4: exactness (fc /b), then 6 interleaved rounds
setlocal
if "%~2"=="" (echo usage: ab.bat DIR_A DIR_B & exit /b 2)
set here=%~dp0
for %%v in (A B) do if not exist obj-%%v mkdir obj-%%v
cl /nologo /std:c++20 /O2 /EHsc /W4 /I"%~1" "%here%bench.cpp" "%~1\framescaler.cpp" /Fe:bench-A.exe /Foobj-A\ >nul || exit /b 1
cl /nologo /std:c++20 /O2 /EHsc /W4 /I"%~2" "%here%bench.cpp" "%~2\framescaler.cpp" /Fe:bench-B.exe /Foobj-B\ >nul || exit /b 1
cl /nologo /std:c++20 /O2 /EHsc /W4 /I"%~1" "%here%exact.cpp" "%~1\framescaler.cpp" /Fe:exact-A.exe /Foobj-A\ >nul || exit /b 1
cl /nologo /std:c++20 /O2 /EHsc /W4 /I"%~2" "%here%exact.cpp" "%~2\framescaler.cpp" /Fe:exact-B.exe /Foobj-B\ >nul || exit /b 1
exact-A.exe > exact-A.bin
exact-B.exe > exact-B.bin
fc /b exact-A.bin exact-B.bin >nul || (echo EXACT: the bytes differ & exit /b 1)
echo EXACT: same bytes
for %%r in (1 3 5) do (echo == r%%r a & bench-A.exe & echo == r%%r b & bench-B.exe)
for %%r in (2 4 6) do (echo == r%%r b & bench-B.exe & echo == r%%r a & bench-A.exe)
