"""Make quiet seating/catch cues from Radial's original, approved press timbres."""
from array import array
from pathlib import Path
import math
import sys
import wave

sounds = Path(__file__).resolve().parents[1] / 'assets/sounds'
# Delay and pitch of the softer confirming catch retain each material's feel.
styles = {
    'crystal-whisper': (.048, 1.12), 'watch-tick': (.037, .87),
    'felt-click': (.040, .90), 'walnut-tap': (.046, .86),
    'bamboo-ticks': (.043, 1.13), 'tiny-gears': (.032, 1.08),
    'linen-tick': (.041, .94), 'cork-touch': (.049, .84),
    'cedar-knock': (.047, .88), 'bamboo-bead': (.044, 1.10),
    'porcelain-touch': (.050, 1.16), 'crystal-hush': (.054, 1.09),
}
for name, (delay, pitch) in styles.items():
    with wave.open(str(sounds / f'{name}-press.wav'), 'rb') as source:
        assert (source.getnchannels(), source.getsampwidth(), source.getframerate()) == (1, 2, 48000)
        samples = array('h', source.readframes(source.getnframes()))
    if sys.byteorder != 'little': samples.byteswap()
    rate = 48000
    length = math.ceil((delay + len(samples) / rate / pitch + .006) * rate)
    result = [0.0] * length
    for start, speed, gain in [(0, 1.0, .60), (round(delay*rate), pitch, .78)]:
        for i in range(math.ceil((len(samples)-1)/speed)):
            pos = i*speed
            j = int(pos)
            if j+1 >= len(samples): break
            result[start+i] += gain*(samples[j]*(1-(pos-j))+samples[j+1]*(pos-j))
    peak = max(map(abs, result))
    gain = min(1.0, .95*max(map(abs, samples))/peak)
    fade = round(.002*rate)
    pcm = array('h', (round(v*gain*min(1, i/fade, (length-1-i)/fade)) for i,v in enumerate(result)))
    assert pcm[0] == pcm[-1] == 0 and max(map(abs, pcm)) < max(map(abs, samples))
    if sys.byteorder != 'little': pcm.byteswap()
    target = sounds / f'{name}-modifier-lock.wav'
    with wave.open(str(target), 'wb') as output:
        output.setparams((1, 2, rate, 0, 'NONE', 'not compressed'))
        output.writeframes(pcm.tobytes())
    print(f'{name}: {length/rate*1000:.0f} ms')
