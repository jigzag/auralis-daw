"""Package the portable executable and its complete corresponding source."""
import shutil
import zipfile
import re
import os
import atexit
import uuid
from pathlib import Path
from portable_install import publish

root = Path(__file__).resolve().parent.parent
version = re.search(r"project\(Auralis VERSION ([\d.]+)", (root / "CMakeLists.txt").read_text()).group(1)
verification = root / f"test-output-v{version.replace('.', '')}"
dist = root / "dist"
dist.mkdir(exist_ok=True)
package_work = root / "build" / "packaging"
package_work.mkdir(parents=True, exist_ok=True)
assert package_work.resolve().is_relative_to(root)
staging = package_work / ("staging-" + uuid.uuid4().hex)
staging.mkdir()
assert staging.resolve().parent == package_work.resolve()


def clean_staging():
    if staging.exists():
        assert staging.resolve().parent == package_work.resolve() and staging.name.startswith("staging-")
        shutil.rmtree(staging)


atexit.register(clean_staging)
portable = staging / "Auralis"
portable.mkdir(parents=True, exist_ok=True)
shutil.copy2(root / "build/Auralis_artefacts/Release/Auralis.exe", portable / "Auralis.exe")
shutil.copy2(root / "build/Auralis_artefacts/JuceLibraryCode/icon.ico", portable / "Auralis.ico")
for name in ("README.md", "LICENSE.md", "COPYING", "preview.png", "piano-roll.png"):
    shutil.copy2(root / name, portable / name)
shutil.copytree(root / "Demo", portable / "Demo", dirs_exist_ok=True)
shutil.copy2(verification / "results.txt", portable / "verification.txt")
shutil.copy2(verification / "asio-diagnostics.txt", portable / "asio-diagnostics.txt")
shutil.copy2(verification / "monitor-paint-stress.txt", portable / "monitor-paint-stress.txt")
shutil.copy2(verification / "live-midi-release.txt", portable / "live-midi-release.txt")
shutil.copy2(verification / "piano-edit-verification.txt", portable / "piano-edit-verification.txt")
shutil.copy2(verification / "piano-selection-preview.png", portable / "piano-selection-preview.png")
shutil.copy2(verification / "piano-navigation-preview.png", portable / "piano-navigation-preview.png")
shutil.copy2(verification / "track-control-verification.txt", portable / "track-control-verification.txt")
shutil.copy2(verification / "recording-range-verification.txt", portable / "recording-range-verification.txt")
shutil.copy2(verification / "recording-controls-preview.png", portable / "recording-controls-preview.png")
shutil.copy2(verification / "arrangement-wide-zoom-preview.png", portable / "arrangement-wide-zoom-preview.png")
shutil.copy2(verification / "piano-wide-zoom-preview.png", portable / "piano-wide-zoom-preview.png")
shutil.copy2(verification / "cut-tool-preview.png", portable / "cut-tool-preview.png")
shutil.copy2(verification / "cut-verification.txt", portable / "cut-verification.txt")
for name in ("cut-focus-verification.txt", "cut-controls-preview.png"):
    shutil.copy2(verification / name, portable / name)
shutil.copy2(verification / "time-selection-preview.png", portable / "time-selection-preview.png")
shutil.copy2(verification / "range-mp3-verification.txt", portable / "range-mp3-verification.txt")
shutil.copy2(verification / "project-open-verification.txt", portable / "project-open-verification.txt")
shutil.copy2(verification / "track-delete-verification.txt", portable / "track-delete-verification.txt")
shutil.copy2(verification / "clip-gain-verification.txt", portable / "clip-gain-verification.txt")
for name in ("project-safety-verification.txt", "reverb-preview.png", "delay-preview.png", "space-effects-verification.txt", "tuner-preview.png", "compressor-preview.png", "native-dynamics-verification.txt", "equalizer-preview.png", "equalizer-verification.txt", "pitch-editor-loop-preview.png", "pitch-editor-loop-verification.txt", "pitch-vertical-zoom-preview.png", "pitch-vertical-zoom-verification.txt", "pitch-curve-measurements.txt", "pitch-curve-preview.png", "pitch-curve-verification.txt", "pitch-scoop-dialog-preview.png", "pitch-scoop-curve-preview.png", "pitch-scoop-verification.txt", "pitch-follow-preview.png", "pitch-follow-verification.txt", "pitch-drag-preview.png", "pitch-preview-verification.txt", "pitch-roll-preview.png", "pitch-roll-verification.txt", "pitch-correction-verification.txt", "pitch-correction-dialog-preview.png", "normalize-verification.txt", "normalize-dialog-preview.png"):
    shutil.copy2(verification / name, portable / name)
shutil.copy2(verification / "clip-gain-preview.png", portable / "clip-gain-preview.png")
shutil.copy2(verification / "clip-trim-verification.txt", portable / "clip-trim-verification.txt")
shutil.copy2(verification / "clip-trim-preview.png", portable / "clip-trim-preview.png")
shutil.copy2(verification / "overlap-verification.txt", portable / "overlap-verification.txt")
shutil.copy2(verification / "overlap-preview.png", portable / "overlap-preview.png")
shutil.copy2(verification / "loop-verification.txt", portable / "loop-verification.txt")
shutil.copy2(verification / "range-repeat-preview.png", portable / "range-repeat-preview.png")
for name in ("overlap-trim-verification.txt", "overlap-trim-preview.png"):
    shutil.copy2(verification / name, portable / name)
shutil.copy2(verification / "recording-alignment-verification.txt", portable / "recording-alignment-verification.txt")
for name in ("recording-waveform-preview.png", "recording-waveform-verification.txt"):
    shutil.copy2(verification / name, portable / name)
for name in ("plugin-registry-verification.txt", "timeline-lanes-verification.txt", "timeline-lanes-preview.png"):
    shutil.copy2(verification / name, portable / name)
for name in ("mixer-preview.png", "mixer-verification.txt"):
    shutil.copy2(verification / name, portable / name)
for name in ("recent-projects-preview.png", "recent-projects-verification.txt"):
    shutil.copy2(verification / name, portable / name)
for name in ("track-details-preview.png", "track-details-verification.txt"):
    shutil.copy2(verification / name, portable / name)
for name in ("transport-follow-preview.png", "transport-follow-verification.txt"):
    shutil.copy2(verification / name, portable / name)
for name in ("track-drag-preview.png", "track-drag-verification.txt"):
    shutil.copy2(verification / name, portable / name)
for name in ("fixed-ruler-preview.png", "fixed-ruler-verification.txt"):
    shutil.copy2(verification / name, portable / name)
shutil.copy2(verification / "plugin-purpose-verification.txt", portable / "plugin-purpose-verification.txt")
for name in ("master-inserts-preview.png", "master-inserts-verification.txt"):
    shutil.copy2(verification / name, portable / name)
notices = portable / "ThirdParty"
notices.mkdir(exist_ok=True)
shutil.copy2(root / "vendor/JUCE/LICENSE.md", notices / "JUCE-LICENSE.md")
shutil.copy2(root / "vendor/ASIO/ASIOSDK/LICENSE.txt", notices / "ASIO-LICENSE.txt")
shutil.copy2(root / "vendor/ASIO/COPYING-GPL-3.0", notices / "ASIO-COPYING-GPL-3.0.txt")
shutil.copy2(root / "vendor/lame-3.100/COPYING", notices / "LAME-LGPL.txt")
shutil.copy2(root / "vendor/lame-3.100/LICENSE", notices / "LAME-LICENSE.txt")
shutil.copy2(root / "vendor/lame-3.100/doc/html/contributors.html", notices / "LAME-contributors.html")
shutil.copy2(root / "vendor/LAME-SOURCE.txt", notices / "LAME-SOURCE.txt")
(portable / "SOURCE.txt").write_text(
    f"Complete corresponding source: Auralis-{version}-source.zip, supplied with this release.\n"
    "Includes JUCE 8.0.6 commit 51a8a6d7aeae7326956d747737ccf1575e61e209, Steinberg ASIO SDK 2.3.4 (GPLv3 alternative), LAME 3.100 (LGPL), dependency notices, and build scripts.\n",
    encoding="utf-8",
)

with zipfile.ZipFile(staging / f"Auralis-{version}-Windows-x64.zip", "w", zipfile.ZIP_DEFLATED, compresslevel=6) as out:
    for path in portable.rglob("*"):
        if path.is_file() and "Data" not in path.relative_to(portable).parts:
            out.write(path, Path("Auralis") / path.relative_to(portable))

source_items = ["CMakeLists.txt", "build.ps1", "README.md", "LICENSE.md", "COPYING", ".gitignore", "Source", "tests", "vendor", "Demo", "Assets"]
with zipfile.ZipFile(staging / f"Auralis-{version}-source.zip", "w", zipfile.ZIP_DEFLATED, compresslevel=6) as out:
    for name in source_items:
        item = root / name
        paths = [item] if item.is_file() else item.rglob("*")
        for path in paths:
            if path.is_file() and ".git" not in path.relative_to(root).parts and "__pycache__" not in path.parts and path.name not in ("asio-sdk.zip", "lame-3.100.tar.gz"):
                out.write(path, Path("auralis-daw") / path.relative_to(root))

archives = (staging / f"Auralis-{version}-source.zip", staging / f"Auralis-{version}-Windows-x64.zip")
for path in archives:
    with zipfile.ZipFile(path) as archive:
        assert archive.testzip() is None
    print(f"{path.name}: {path.stat().st_size:,} bytes")

# Both archives and all app files are ready before touching the fixed installation.
migrated = publish(portable, dist / "Auralis", package_work / "migration-backup")
for path in archives:
    os.replace(path, dist / path.name)
if migrated:
    print(f"Migrated local Data from: {migrated}")
print(f"Updated fixed launch path: {dist / 'Auralis' / 'Auralis.exe'}")
clean_staging()
