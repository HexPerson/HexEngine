@echo off
rem Compiles the FidelityFX FSR 2.2 pass shaders to cs_5_0 bytecode headers for the
rem plugin's D3D11 backend (ffx_fsr2_dx11.cpp includes fsr2_<pass>.h).
rem
rem   CompileShaders.cmd <fxc.exe> <ffx-fsr2-api\shaders dir> <output dir>
rem
rem One fixed permutation - it must match FFX_FSR2_DX11_*_FLAGS in ffx_fsr2_dx11.h:
rem HDR colour, render-res unjittered motion vectors, standard depth, LUT Lanczos,
rem no 16-bit types, SPD without wave intrinsics (SM5 has none). Headers are only
rem regenerated when missing; the FSR2 sources are pinned, so delete the output
rem directory (or Rebuild) after changing anything here.
rem
rem The passes compile from a build-local copy of AMD's shaders with ONE patch:
rem FSR 2.2.1 declares r_lock_status / rw_lock_status as `unorm float2`, but its own
rem runtime creates that texture as R16G16_FLOAT - and it has to be float, the lock
rem lifetime is written as 2.0 and read back through saturate(lifetime - 1). DX12
rem doesn't validate a view's format against the declared return type; D3D11 does
rem (debug-layer error on every dispatch). Dropping `unorm` from those four
rem declarations makes them match. Every other `unorm` declaration is bound to a
rem genuinely UNORM texture and must stay: D3D11 rejects FLOAT-declared UAVs on
rem UNORM views just as strictly.
setlocal

set "FXC=%~1"
set "SRC=%~2"
set "OUT=%~3"

if not exist "%FXC%" (
  for %%F in (fxc.exe) do set "FXC=%%~$PATH:F"
)
if not exist "%FXC%" (
  echo error FSR0001: fxc.exe not found ^(Windows SDK^). Looked for "%~1" and on PATH.
  exit /b 1
)
if not exist "%SRC%\ffx_fsr2_accumulate_pass.hlsl" (
  echo error FSR0002: FidelityFX-FSR2 shaders not found at "%SRC%".
  echo Run: cmake --build --preset required-modules-bootstrap-debug
  exit /b 1
)
if not exist "%OUT%" mkdir "%OUT%"

set "PSRC=%OUT%\src"
if not exist "%PSRC%\patched.stamp" (
  if not exist "%PSRC%" mkdir "%PSRC%"
  xcopy /q /y "%SRC%\*.h" "%PSRC%\" >nul
  xcopy /q /y "%SRC%\*.hlsl" "%PSRC%\" >nul
  powershell -NoProfile -ExecutionPolicy Bypass -Command "$p = '%PSRC%\ffx_fsr2_callbacks_hlsl.h'; $t = [IO.File]::ReadAllText($p); $re = [regex]'unorm (FfxFloat32x2>\s+rw?_lock_status\b)'; if ($re.Matches($t).Count -ne 4) { exit 1 }; [IO.File]::WriteAllText($p, $re.Replace($t, '$1'))"
  if errorlevel 1 (
    echo error FSR0004: could not patch the r_lock_status / rw_lock_status declarations in ffx_fsr2_callbacks_hlsl.h
    exit /b 1
  )
  > "%PSRC%\patched.stamp" echo lock_status unorm removed
)

set DEFS=/D FFX_GPU=1 /D FFX_HLSL=1 /D FFX_HALF=0 /D SPD_NO_WAVE_OPERATIONS=1 /D FFX_FSR2_OPTION_REPROJECT_USE_LANCZOS_TYPE=1 /D FFX_FSR2_OPTION_HDR_COLOR_INPUT=1 /D FFX_FSR2_OPTION_LOW_RESOLUTION_MOTION_VECTORS=1 /D FFX_FSR2_OPTION_JITTERED_MOTION_VECTORS=0 /D FFX_FSR2_OPTION_INVERTED_DEPTH=0

call :compile depth_clip                 depth_clip                 0 || exit /b 1
call :compile reconstruct_previous_depth reconstruct_previous_depth 0 || exit /b 1
call :compile lock                       lock                       0 || exit /b 1
call :compile accumulate                 accumulate                 0 || exit /b 1
call :compile accumulate                 accumulate_sharpen         1 || exit /b 1
call :compile rcas                       rcas                       0 || exit /b 1
call :compile compute_luminance_pyramid  compute_luminance_pyramid  0 || exit /b 1
call :compile autogen_reactive           autogen_reactive           0 || exit /b 1
call :compile tcr_autogen                tcr_autogen                0 || exit /b 1
exit /b 0

:compile
set "HDR=%OUT%\fsr2_%2.h"
if exist "%HDR%" exit /b 0
echo FSR2: compiling %2
rem fxc's output goes to a log: AMD's sources raise a few benign warnings (X3078,
rem X3203) that would otherwise show up as build warnings on every clean build.
"%FXC%" /nologo /T cs_5_0 /E CS %DEFS% /D FFX_FSR2_OPTION_APPLY_SHARPENING=%3 /Vn g_fsr2_%2 /Fh "%HDR%.tmp" "%PSRC%\ffx_fsr2_%1_pass.hlsl" >"%HDR%.log" 2>&1
if errorlevel 1 (
  type "%HDR%.log"
  echo error FSR0003: fxc failed for ffx_fsr2_%1_pass.hlsl
  if exist "%HDR%.tmp" del "%HDR%.tmp"
  exit /b 1
)
move /y "%HDR%.tmp" "%HDR%" >nul
exit /b 0
