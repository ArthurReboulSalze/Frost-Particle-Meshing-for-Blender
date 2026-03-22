@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
set "PATH=%PATH%;C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"

cd build
echo Configuring...
cmake ../blender_frost_adapter -DCMAKE_TOOLCHAIN_FILE=../deps/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows -A x64
if errorlevel 1 exit /b 1

echo Building...
cmake --build . --config Release --parallel 8
