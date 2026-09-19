@ECHO OFF
sjasmplus.exe -isrc -ipatch -isrc/sgen make_rom.a80

if %errorlevel% neq 0 goto end

	copy /b gsroml_+gsromh_ gs105b.rom >nul
	del gsroml_
	del gsromh_

:end
pause
