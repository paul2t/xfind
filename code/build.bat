@echo off

::pushd ..\build
::cl /nologo /O2 /W4 /I..\code -DAPP_WIN32 ..\code\directory_listener.cpp
::popd

set program=xfind

set DX12=0 :: not working
set EXECUTE=0
set DEBUG=1
set INTERNAL=1
set RELEASE=1

pushd ..
IF NOT EXIST build mkdir build
set UseCtime=1
call where ctime 1>NUL 2>NUL
IF NOT "%ERRORLEVEL%" == "0" set UseCtime=0
IF "%UseCtime%" == "1" ctime -begin tests.ctm

set GraphicsBuildFlags=-DOPENGL#1
if "%DX12%" == "1" set GraphicsBuildFlags=-DDX12#1
set IgnoredWarnings=-D_CRT_SECURE_NO_WARNINGS -wd4201 -wd4702 -wd4127 -wd4100 -wd4189 -wd4505 -wd4408 -wd4706 -wd4701 -wd4703 /EHsc
set CommonCompilerFlags=-MD -nologo -fp:fast -Gm- -GR- -EHa- -Oi -WX -W4 -DAPP_WIN32=1 -FC -Z7 %GraphicsBuildFlags% %IgnoredWarnings% /I..\code /I..\code\glfw /I..\code\imgui
if "%devel%" EQU "1" set RELEASE=0
if %RELEASE% EQU 1 set DEBUG=0 && set INTERNAL=0
if %DEBUG% EQU 1 ( set CommonCompilerFlags=%CommonCompilerFlags% -Od
) ELSE ( set CommonCompilerFlags=%CommonCompilerFlags% -O2
)
if %INTERNAL% EQU 1 set CommonCompilerFlags=%CommonCompilerFlags% -DAPP_INTERNAL=1
set GraphicsLinkFlags=glfw3.lib opengl32.lib
if "%DX12%" == "1" set GraphicsLinkFlags=d3d12.lib d3dcompiler.lib
set CommonLinkerFlags=-incremental:no -opt:ref %GraphicsLinkFlags% /IGNORE:4217 /IGNORE:4049 /LIBPATH:..\code\GLFW\

IF NOT EXIST build mkdir build
pushd build

cl %CommonCompilerFlags% ..\code\%program%.cpp /link %CommonLinkerFlags%
set builderror=%ERRORLEVEL%

:: ResourceHacker is available here : http://www.angusj.com/resourcehacker/
if %builderror% == 0 if %RELEASE% == 1 (
	echo Setting icon
	ResourceHacker.exe -open xfind.exe -save xfind.exe -action addskip -res ..\code\resources\xfind.ico -mask ICONGROUP,MAINICON,
)

popd

IF "%UseCtime%" == "1" ctime -end tests.ctm %builderror%

popd

if %builderror% GTR 0 (
    exit /b %builderror%
)

if %EXECUTE% NEQ 1 exit /b 0


echo.
echo ================================
echo.

pushd ..

IF NOT EXIST data mkdir data
pushd data

set args=%*
..\build\%program%.exe %args%

popd

popd

echo.
echo ================================
echo.
