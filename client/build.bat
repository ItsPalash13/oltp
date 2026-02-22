@echo off
cd /d "%~dp0"
echo Building client.exe...

set ROOT=..
set INC=%ROOT%/include
set SRC=%ROOT%/src
set CLIENT=.
set NETWORK=%SRC%/network

g++ -std=c++17 -I%INC% -c %CLIENT%/main.cpp -o main.o
if %ERRORLEVEL% NEQ 0 exit /b 1

g++ -std=c++17 -I%INC% -c %NETWORK%/tcp_client.cpp -o tcp_client.o
if %ERRORLEVEL% NEQ 0 exit /b 1

g++ -std=c++17 main.o tcp_client.o -lws2_32 -o client.exe
if %ERRORLEVEL% NEQ 0 exit /b 1

echo Built client\client.exe
