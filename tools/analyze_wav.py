import sys
import wave

import numpy as np


def load(path):
    with wave.open(path) as w:
        width, channels, rate = w.getsampwidth(), w.getnchannels(), w.getframerate()
        raw = np.frombuffer(w.readframes(w.getnframes()), dtype=np.uint8)
    if width == 3:
        padded = np.zeros((raw.size // 3, 4), dtype=np.uint8)
        padded[:, 1:] = raw.reshape(-1, 3)
        data = padded.view(np.int32).ravel() / 2.0**31
    elif width == 2:
        data = raw.view(np.int16) / 2.0**15
    else:
        data = raw.view(np.int32) / 2.0**31
    return data.reshape(-1, channels), rate


def stats(path):
    data, rate = load(path)
    peak = float(np.abs(data).max())
    rms = float(np.sqrt((data**2).mean()))
    return {
        "seconds": round(float(len(data) / rate), 2),
        "peak_db": round(float(20 * np.log10(max(peak, 1e-9))), 2),
        "rms_db": round(float(20 * np.log10(max(rms, 1e-9))), 2),
        "dc": round(float(np.abs(data.mean(axis=0)).max()), 5),
        "clipped": int((np.abs(data) >= 0.999).sum()),
        "finite": bool(np.isfinite(data).all()),
    }


if __name__ == "__main__":
    for path in sys.argv[1:]:
        print(path, stats(path))
