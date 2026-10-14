#!/bin/bash
set -euo pipefail

task_root="$(cd "$(dirname "$0")/.." && pwd)"
task_build="$task_root/build"
task_artifacts="$task_build/GuitarToMIDI_artefacts/Release"
task_backup="$task_root/.backups/installed-$(date +%Y%m%d-%H%M%S)"

cmake -S "$task_root" -B "$task_build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build "$task_build" --parallel 4
ctest --test-dir "$task_build" --output-on-failure

install_bundle() {
    local task_source="$1" task_destination="$2"
    /usr/bin/codesign --force --deep --sign - "$task_source"
    /usr/bin/codesign --verify --deep --strict "$task_source"
    mkdir -p "$(dirname "$task_destination")"
    if [ -e "$task_destination" ]; then
        mkdir -p "$task_backup"
        mv "$task_destination" "$task_backup/$(basename "$task_destination")"
    fi
    /usr/bin/ditto "$task_source" "$task_destination"
}

install_bundle "$task_artifacts/AU/Guitar To MIDI.component" \
    "$HOME/Library/Audio/Plug-Ins/Components/Guitar To MIDI.component"
install_bundle "$task_artifacts/VST3/Guitar To MIDI.vst3" \
    "$HOME/Library/Audio/Plug-Ins/VST3/Guitar To MIDI.vst3"
install_bundle "$task_artifacts/Standalone/Guitar To MIDI.app" \
    "$HOME/Applications/Guitar To MIDI.app"

# Validate this component directly. Only ask macOS to scan the full plugin list
# when a first-time installation has not yet been registered.
if ! /usr/bin/auval -v aufx Gtmd Rnyi > "$task_build/auval.log" 2>&1; then
    /usr/bin/auval -a > "$task_build/au-list.log" 2>&1 || true
    /usr/bin/auval -v aufx Gtmd Rnyi > "$task_build/auval.log" 2>&1
fi
cat "$task_build/auval.log"
echo "Installed AU, VST3 and Standalone. Reopen Logic if it was already running."
