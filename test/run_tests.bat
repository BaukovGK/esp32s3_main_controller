@echo off
echo === RO Plant Unit Tests ===
echo.

if not exist build mkdir build
cd build

rem Определяем доступный генератор
where cl >nul 2>nul
if %errorlevel%==0 (
    echo Compiler: MSVC
    cmake .. -G "NMake Makefiles" %*
    if %errorlevel% neq 0 goto :error
    nmake
    if %errorlevel% neq 0 goto :error
) else (
    where gcc >nul 2>nul
    if %errorlevel%==0 (
        echo Compiler: GCC/MinGW
        cmake .. -G "MinGW Makefiles" %*
        if %errorlevel% neq 0 goto :error
        cmake --build .
        if %errorlevel% neq 0 goto :error
    ) else (
        echo ERROR: No C compiler found. Install MSVC or MinGW.
        goto :error
    )
)

echo.
echo === Running Tests ===
echo.
ctest --output-on-failure
set TEST_RESULT=%errorlevel%

cd ..

if %TEST_RESULT%==0 (
    echo.
    echo === ALL TESTS PASSED ===
) else (
    echo.
    echo === SOME TESTS FAILED ===
)

exit /b %TEST_RESULT%

:error
cd ..
echo.
echo === BUILD FAILED ===
exit /b 1
