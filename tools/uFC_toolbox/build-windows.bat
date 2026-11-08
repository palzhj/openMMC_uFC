@echo off
rem Build the standalone Windows executable (dist\ufc-toolbox.exe).
cd /d %~dp0
python -m PyInstaller --onefile --windowed --name ufc-toolbox --distpath dist --workpath build --specpath build fru_gui_tk.py
if errorlevel 1 goto :error
echo built: %cd%\dist\ufc-toolbox.exe
goto :eof
:error
echo build failed
exit /b 1
