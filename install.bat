@echo off
setlocal

set "BUILD_DIR=%~dp0build\Release"
set "VCPKG_BIN=%~dp0deps\vcpkg\installed\x64-windows\bin"
set "ADDON_DIR=%~dp0frost_blender_addon"

echo Installing Frost Adapter to %ADDON_DIR%...

:: Copy Python Extension
echo Copying .pyd...
for %%f in ("%BUILD_DIR%\blender_frost_adapter*.pyd") do (
    copy /Y "%%f" "%ADDON_DIR%\blender_frost_adapter.pyd"
)
if errorlevel 1 goto :error

:: Copy Dependencies
echo Copying DLL dependencies...

:: TBB
copy /Y "%VCPKG_BIN%\tbb12.dll" "%ADDON_DIR%\"

:: OpenEXR / Imath
copy /Y "%VCPKG_BIN%\OpenEXR-3_4.dll" "%ADDON_DIR%\"
copy /Y "%VCPKG_BIN%\OpenEXRCore-3_4.dll" "%ADDON_DIR%\"
copy /Y "%VCPKG_BIN%\OpenEXRUtil-3_4.dll" "%ADDON_DIR%\"
copy /Y "%VCPKG_BIN%\Imath-3_2.dll" "%ADDON_DIR%\"
copy /Y "%VCPKG_BIN%\Iex-3_4.dll" "%ADDON_DIR%\"
copy /Y "%VCPKG_BIN%\IlmThread-3_4.dll" "%ADDON_DIR%\"

:: Boost (Filesystem, Thread, System, Chrono, ProgramOptions, DateTime, Regex, Atomic, Serialization)
:: Using wildcards to avoid hardcoding exact version if it changes slightly, but being specific enough
copy /Y "%VCPKG_BIN%\boost_filesystem-vc143-mt-x64-*.dll" "%ADDON_DIR%\"
copy /Y "%VCPKG_BIN%\boost_thread-vc143-mt-x64-*.dll" "%ADDON_DIR%\"
copy /Y "%VCPKG_BIN%\boost_system-vc143-mt-x64-*.dll" "%ADDON_DIR%\"
copy /Y "%VCPKG_BIN%\boost_chrono-vc143-mt-x64-*.dll" "%ADDON_DIR%\"
copy /Y "%VCPKG_BIN%\boost_program_options-vc143-mt-x64-*.dll" "%ADDON_DIR%\"
copy /Y "%VCPKG_BIN%\boost_date_time-vc143-mt-x64-*.dll" "%ADDON_DIR%\"
copy /Y "%VCPKG_BIN%\boost_regex-vc143-mt-x64-*.dll" "%ADDON_DIR%\"
copy /Y "%VCPKG_BIN%\boost_atomic-vc143-mt-x64-*.dll" "%ADDON_DIR%\"
copy /Y "%VCPKG_BIN%\boost_serialization-vc143-mt-x64-*.dll" "%ADDON_DIR%\"

:: Other deps
copy /Y "%VCPKG_BIN%\tinyxml2.dll" "%ADDON_DIR%\"
copy /Y "%VCPKG_BIN%\zlib1.dll" "%ADDON_DIR%\"
copy /Y "%VCPKG_BIN%\xxhash.dll" "%ADDON_DIR%\"
copy /Y "%VCPKG_BIN%\glog.dll" "%ADDON_DIR%\"
copy /Y "%VCPKG_BIN%\b2-1.dll" "%ADDON_DIR%\"

echo.
echo Installation Complete!
echo You can now install the 'frost_blender_addon' folder in Blender.
goto :eof

:error
echo.
echo Installation Failed!
exit /b 1
