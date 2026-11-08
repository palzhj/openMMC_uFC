@echo off
rem Build the standalone Windows executable (dist\ufc-fru-editor.exe).
cd /d %~dp0
python -m PyInstaller --onefile --windowed --name ufc-fru-editor --distpath dist --workpath build --specpath build fru_gui_tk.py
if errorlevel 1 goto :error
echo built: %cd%\dist\ufc-fru-editor.exe
goto :eof
:error
echo build failed
exit /b 1
