"""Publish a verified portable build to one permanent installation directory."""
import os
import re
import shutil
from pathlib import Path

DATA_MARKER = ".stable-layout-v1"
PREFERENCES = ("audio-device.xml", "plugins.xml")


def contained(path: Path, parent: Path) -> Path:
    resolved = path.resolve()
    if not resolved.is_relative_to(parent.resolve()):
        raise ValueError(f"Path escapes the installation directory: {path}")
    return path


def latest_legacy_data(dist: Path) -> Path | None:
    candidates = []
    for folder in dist.iterdir():
        match = re.fullmatch(r"Auralis-(\d+)\.(\d+)\.(\d+)", folder.name)
        data = folder / "Data"
        if not match or not data.is_dir() or data.is_symlink() or not data.resolve().is_relative_to(dist.resolve()):
            continue
        state = data / "audio-device.xml"
        last_used = state.stat().st_mtime_ns if state.is_file() else 0
        candidates.append((last_used, tuple(map(int, match.groups())), data))
    return max(candidates, default=(0, (), None))[2]


def migrate_data(install: Path, dist: Path, backup: Path) -> Path | None:
    data = contained(install / "Data", install)
    data.mkdir(parents=True, exist_ok=True)
    if (data / DATA_MARKER).exists():
        return None
    source = latest_legacy_data(dist)
    if source:
        for path in source.rglob("*"):
            if not path.is_file() or path.is_symlink() or path.name == DATA_MARKER:
                continue
            destination = contained(data / path.relative_to(source), data)
            if destination.exists():
                continue  # Never replace recordings or cached edits during migration.
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, destination)
        for name in PREFERENCES:
            previous, current = source / name, contained(data / name, data)
            if not previous.is_file() or previous.is_symlink():
                continue
            if current.is_file() and previous.stat().st_mtime_ns > current.stat().st_mtime_ns:
                backup.mkdir(parents=True, exist_ok=True)
                saved = backup / name
                if not saved.exists():
                    shutil.copy2(current, saved)
                shutil.copy2(previous, current)
    (data / DATA_MARKER).write_text("Auralis settings and media stay here across application updates.\n", encoding="utf-8")
    return source


def publish(staging: Path, install: Path, backup: Path) -> Path | None:
    """Replace app files only. An open executable blocks before any Data change."""
    executable = staging / "Auralis.exe"
    if not executable.is_file():
        raise FileNotFoundError(executable)
    contained(install, install.parent).mkdir(parents=True, exist_ok=True)
    target = contained(install / "Auralis.exe", install)
    previous = backup.parent / "previous-Auralis.exe"
    if target.is_file() and not previous.exists():
        previous.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(target, previous)
    incoming = contained(install / "Auralis.exe.update", install)
    shutil.copy2(executable, incoming)
    try:
        os.replace(incoming, target)
    except PermissionError as error:
        incoming.unlink(missing_ok=True)
        raise RuntimeError("Close Auralis, then run python tests/package.py again. The current installation and Data were not changed.") from error
    source = migrate_data(install, install.parent, backup)
    for path in staging.rglob("*"):
        relative = path.relative_to(staging)
        if not path.is_file() or path == executable or "Data" in relative.parts:
            continue
        destination = contained(install / relative, install)
        destination.parent.mkdir(parents=True, exist_ok=True)
        temporary = contained(destination.with_name(destination.name + ".update"), install)
        shutil.copy2(path, temporary)
        os.replace(temporary, destination)
    return source
