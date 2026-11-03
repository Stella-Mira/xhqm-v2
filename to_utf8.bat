@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion

:: ============================================================
:: 将源码 / 文档统一为「无 BOM 的 UTF-8」
::
::   覆盖：C/C++ 源码、CMake、文档、配置（见下面 extensions）
::   不动：Windows 产物 .sln / .vcxproj / .filters / .bat / .ps1
::         （Visual Studio 保存 .sln 时会写回 BOM，属正常，不必跟它较劲）
::
::   写回用 .NET UTF8Encoding($false)：Windows PowerShell 5.1 的
::   Out-File -Encoding UTF8 会写 BOM（那是「带 BOM 的 UTF-8」，
::   跑一遍反而污染全仓库），故不用它。
::   ReadAllText / WriteAllText 原样保留行尾与末尾换行。
:: ============================================================

echo 正在将源码/文档转换为无BOM的UTF-8编码...
echo.

set "extensions=.h .hpp .hh .hxx .cpp .cc .c .cxx .inl .ipp .md .txt .cmake .json .yml .yaml"

:: 仓库根目录也要处理（dir /ad 不含根自身）
call :do_dir "%CD%"
for /f "delims=" %%d in ('dir /s /b /ad 2^>nul ^| findstr /v /i "\\\.svn \\build \\build_lite \\build-shared"') do call :do_dir "%%d"

echo.
echo 转换完成！
pause
exit /b 0

:do_dir
pushd %1
for %%e in (%extensions%) do (
    for %%f in (*%%e) do (
        if exist "%%f" (
            echo   处理: %%~nxf
            powershell -NoProfile -Command "$p='%%f'; $c=[IO.File]::ReadAllText($p); [IO.File]::WriteAllText($p, $c, (New-Object Text.UTF8Encoding($false)))"
        )
    )
)
popd
exit /b 0
