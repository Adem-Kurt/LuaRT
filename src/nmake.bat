@echo off

set dir=%cd%

if "%1" == "setup" @(
    @cd core 
    @nmake.bat ..\..\bin\lua54.dll
    @nmake.bat ..\..\lib\lua54-static.lib
    @cd ..\modules
    @nmake.bat %1
    @cd ..\core
    @nmake.bat setup
) else (
    for /f %%i in ('dir /ad/b "%dir%"') do @(@if exist %%i/nmake.bat @(@cd %%i & @nmake.bat %1& @cd ..) else @(@if exist %%i/Makefile @(@cd %%i & @nmake.exe %1 & @cd ..)))
)