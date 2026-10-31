"""Generate an original, synthetic demo session using only the Python standard library."""
import array
import json
import math
import random
import sys
import wave
from pathlib import Path

destination = Path(sys.argv[1] if len(sys.argv) > 1 else "Demo")
media = destination / "First light_media"
media.mkdir(parents=True, exist_ok=True)
sr = 48000
seconds = 4
rng = random.Random(42)


def write(name, synth):
    values = array.array("h")
    for i in range(sr * seconds):
        t = i / sr
        v = max(-0.95, min(0.95, synth(t)))
        values.extend([int(v * 32767), int(v * 32767)])
    path = media / f"{name}.wav"
    with wave.open(str(path), "wb") as out:
        out.setnchannels(2)
        out.setsampwidth(2)
        out.setframerate(sr)
        out.writeframes(values.tobytes())
    return f"First light_media/{path.name}"


def drums(t):
    kick = t % 0.5
    snare = (t + 0.5) % 1
    hat = t % 0.25
    return (0.48 * math.exp(-kick * 28) * math.sin(2 * math.pi * (48 * kick + 3 * (1 - math.exp(-35 * kick))))
            + 0.16 * math.exp(-snare * 24) * rng.uniform(-1, 1)
            + 0.07 * math.exp(-hat * 130) * rng.uniform(-1, 1))


def bass(t):
    notes = [45, 45, 48, 43, 41, 41, 43, 40]
    f = 440 * 2 ** ((notes[int(t / 0.5) % 8] - 69) / 12)
    envelope = min(1, (t % 0.5) * 70) * math.exp(-(t % 0.5) * 4)
    return envelope * (0.24 * math.sin(2 * math.pi * f * t) + 0.05 * math.sin(4 * math.pi * f * t))


def keys(t):
    chords = [[57, 60, 64, 67], [53, 57, 60, 64]]
    notes = chords[int(t / 2) % 2]
    at = t % 0.5
    env = min(1, at * 160) * math.exp(-at * 8)
    return sum(0.065 * env * math.sin(2 * math.pi * 440 * 2 ** ((n - 69) / 12) * t) for n in notes)


tracks = []
for name, colour, fn in [("01  Pocket drums", "ff80e3c0", drums), ("02  Analog bass", "ff9ba7ff", bass), ("03  Midnight keys", "ffedb07d", keys)]:
    file = write(name, fn)
    clips = [dict(file=file, name=name[4:], start=i * 4, offset=0, length=4, fadeIn=0.005, fadeOut=0.01) for i in range(4)]
    tracks.append(dict(name=name, colour=colour, gain=0.8, pan=0, input=0, mute=False, solo=False, clips=clips, plugins=[]))
notes = [dict(beat=i * 0.5, length=0.35, pitch=[72, 76, 79, 83, 79, 76, 74, 71][i % 8], velocity=82 + (i % 3) * 9, channel=1) for i in range(16)]
midi_clips = [dict(midi=True, name="First light / MIDI", start=i * 8, length=8, offset=0, notes=notes, events=[]) for i in range(4)]
tracks.append(dict(name="04  MIDI lead", colour="ffe58dc6", midi=True, midiChannel=0, gain=0.5, pan=0.12, input=0, mute=False, solo=False, clips=midi_clips, plugins=[]))
project = dict(format="auralis-1", bpm=120, master=0.8, cursor=2, loop=True, loopStart=0, loopEnd=16, tracks=tracks)
(destination / "First light.auralis").write_text(json.dumps(project, indent=2), encoding="utf-8")
print(destination.resolve())
