@echo off
rem POC 054, Windows: every transport x frame x output, interleaved rounds. From the folder holding tw.exe:
rem win.cmd ROUNDS   (FFMPEG = ffmpeg.exe path, src.bgra = 352x288 BGRA pictures beside tw.exe)
setlocal enabledelayedexpansion
set ROUNDS=%1
if "%ROUNDS%"=="" set ROUNDS=3
set "POC_SRC=src.bgra:352x288"
set NV=-preset p1 -tune ll -rc constqp -qp 18
for /l %%r in (1,1,%ROUNDS%) do (
  for %%s in ("3840 2160 4 100" "3840 2160 2 200" "2640 2160 4 100") do (
    for %%o in ("-c copy -f null -" "-c:v h264_nvenc %NV% -f null -" "-c:v h264_nvenc %NV% -pix_fmt yuv420p -f null -" "-c:v hevc_nvenc %NV% -f null -" "-c:v h264_nvenc %NV% -pix_fmt yuv444p -f null -") do (
      for %%m in (npipe:64 npipe:1024 stdin:1024 stdin:16384 tcp) do (
        set "POC_OUT=%%~o"
        for /f "tokens=2" %%e in ("%%~o") do set ENC=%%e
        for /f "tokens=*" %%l in ('tw.exe %%m %%~s 2^>^&1') do echo r%%r !ENC! [%%~o] %%l
      )
    )
  )
)
