@echo off
rem Wireshark on Windows runs only executables from its extcap folder: this starts the Python script beside it
py -3 "%~dp0unreal-ng-extcap.py" %*
