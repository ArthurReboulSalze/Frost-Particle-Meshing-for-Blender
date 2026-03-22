@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" > nul 2>&1
if errorlevel 1 call "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" > nul 2>&1
if errorlevel 1 call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" > nul 2>&1
if errorlevel 1 call "C:\Program Files (x86)\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" > nul 2>&1
if errorlevel 1 call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" > nul 2>&1
if errorlevel 1 call "C:\Program Files (x86)\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" > nul 2>&1

echo =========================
echo Checking Main PYD...
dumpbin /dependents "%~dp0frost_blender_addon\blender_frost_adapter.pyd"

echo =========================
echo Checking Boost Thread...
for %%f in ("%~dp0frost_blender_addon\boost_thread*.dll") do dumpbin /dependents "%%f"

echo =========================
echo Checking Imath...
dumpbin /dependents "%~dp0frost_blender_addon\Imath-3_2.dll"

echo =========================
echo Checking OpenEXR...
dumpbin /dependents "%~dp0frost_blender_addon\OpenEXR-3_4.dll"

