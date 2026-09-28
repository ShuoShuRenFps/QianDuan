@echo off
chcp 65001 >nul
REM  AI助教答疑平台 一键编译脚本 (MinGW / w64devkit g++)
setlocal
set "SRC=main.cpp"
set "OUT=ai_platform.exe"
set "CXX=g++"
where g++ >nul 2>nul && goto build
if exist "D:\HuanJing\w64devkit\bin\g++.exe"          set "CXX=D:\HuanJing\w64devkit\bin\g++.exe" && goto build
if exist "D:\Microsoft VS Code\w64devkit\bin\g++.exe" set "CXX=D:\Microsoft VS Code\w64devkit\bin\g++.exe" && goto build
if exist "C:\msys64\ucrt64\bin\g++.exe"        set "CXX=C:\msys64\ucrt64\bin\g++.exe" && goto build
if exist "C:\mingw64\bin\g++.exe"              set "CXX=C:\mingw64\bin\g++.exe" && goto build
echo [错误] 未找到 g++，请确认 MinGW/w64devkit 已加入 PATH。
pause & exit /b 1
:build
echo 使用编译器: %CXX%
%CXX% -std=c++17 -O2 -Wall %SRC% -o %OUT% -lws2_32
if errorlevel 1 ( echo [失败] 编译出错 & pause & exit /b 1 )
echo [成功] 已生成 %OUT%，运行后浏览器打开 http://localhost:8080
pause
endlocal
