#!/bin/sh
# Build the standalone Linux binary (dist/ufc-fru-editor).
set -e
cd "$(dirname "$0")"
python3 -m PyInstaller --onefile --windowed --name ufc-fru-editor \
    --distpath dist --workpath build --specpath build fru_gui_tk.py
echo "built: $(pwd)/dist/ufc-fru-editor"
