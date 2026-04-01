@echo off
setlocal

cmake --preset vs2022-x64-debug || goto :error
cmake --build --preset minimal-bootstrap-debug || goto :error
cmake --build --preset sln-build-debug-modern || goto :error

echo.
echo HexEngine setup completed successfully using canonical CMake orchestration.
pause
exit /b 0

:error
echo.
echo Setup failed. See command output above.
pause
exit /b 1
