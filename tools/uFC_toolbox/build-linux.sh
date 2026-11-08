#!/bin/sh
# Build the standalone Linux binary (dist/ufc-toolbox).
set -e
cd "$(dirname "$0")"
python3 -m PyInstaller --onefile --windowed --name ufc-toolbox \
    --distpath dist --workpath build --specpath build fru_gui_tk.py
echo "built: $(pwd)/dist/ufc-toolbox"
