#!/usr/bin/env python3
"""Compose the original EditHere demonstration soundtrack without external samples.

All oscillators, percussion and arrangement are generated here. The seed makes
renders reproducible. Output is a stereo 48 kHz PCM WAV plus composition metadata.
"""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path
import wave
import numpy as np
from scipy import signal

SR = 48000
BPM = 88.0
BEAT = 60.0 / BPM
BAR = BEAT * 4
SEED = 20260921


def frequency(midi):
    return 440.0 * 2.0 ** ((midi - 69) / 12.0)


def write_wav(path, audio):
    pcm = np.round(np.clip(audio, -1, 1) * 32767).astype('<i2')
    with wave.open(str(path), 'wb') as out:
        out.setnchannels(2)
        out.setsampwidth(2)
        out.setframerate(SR)
        out.writeframes(pcm.tobytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--duration', type=float, default=246.0)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    count = round(args.duration * SR)
    mix = np.zeros((count, 2), dtype=np.float32)
    rng = np.random.default_rng(SEED)

    def place(sound, start, gain=1.0, pan=0.0, echo=True):
        begin = round(start * SR)
        if begin < 0 or begin >= count:
            return
        length = min(len(sound), count - begin)
        angle = (pan + 1) * math.pi / 4
        gains = np.array([math.cos(angle), math.sin(angle)], dtype=np.float32)
        mix[begin:begin+length] += sound[:length, None] * gain * gains
        if echo:
            # Quiet asymmetric short delays retain space without a washed-out tail.
            for delay, level, side in [(0.204, .13, 1), (.409, .075, 0), (.683, .036, 1)]:
                offset = begin + round(delay * SR)
                n = min(length, count - offset)
                if n > 0:
                    mix[offset:offset+n, side] += sound[:n] * gain * level

    def piano(midi, seconds=3.2, soft=False):
        t = np.arange(round(seconds * SR), dtype=np.float32) / SR
        f = frequency(midi)
        attack = 1 - np.exp(-t / (.018 if soft else .008))
        envelope = attack * np.exp(-t / (1.12 if soft else .72))
        # A mellow electric-piano body: stable fundamental, quickly decaying upper partials.
        x = np.sin(2*np.pi*f*t + .1*np.sin(2*np.pi*f*2*t)*np.exp(-t/.18))
        x += .23 * np.sin(2*np.pi*f*2*t) * np.exp(-t/.34)
        x += .052 * np.sin(2*np.pi*f*3*t) * np.exp(-t/.18)
        x *= envelope
        x[-round(.08*SR):] *= np.linspace(1, 0, round(.08*SR))
        return x.astype(np.float32)

    def pad(notes, seconds):
        t = np.arange(round(seconds * SR), dtype=np.float32) / SR
        x = np.zeros(len(t), dtype=np.float32)
        for note in notes:
            f = frequency(note)
            phase = rng.uniform(0, 2*np.pi)
            x += (.54*np.sin(2*np.pi*f*.999*t+phase) +
                  .42*np.sin(2*np.pi*f*1.001*t+phase+.3) +
                  .085*np.sin(2*np.pi*f*2*t+phase)) / len(notes)
        env = np.minimum(t/.65, 1) * np.minimum((seconds-t)/1.2, 1)
        x *= np.maximum(env, 0) * (.95 + .05*np.sin(2*np.pi*.18*t))
        return x

    def bass(midi, seconds=1.1):
        t = np.arange(round(seconds*SR), dtype=np.float32)/SR
        f = frequency(midi)
        env = (1-np.exp(-t/.027))*np.exp(-t/.45)
        env *= np.clip((seconds-t)/.1, 0, 1)
        return (np.sin(2*np.pi*f*t)+.15*np.sin(2*np.pi*f*2*t))*env

    def kick():
        t = np.arange(round(.26*SR), dtype=np.float32)/SR
        phase = 2*np.pi*(46*t + (92-46)*.026*(1-np.exp(-t/.026)))
        return np.sin(phase) * (1-np.exp(-t/.0018))*np.exp(-t/.062)

    def hat(opened=False):
        length = .17 if opened else .055
        t = np.arange(round(length*SR), dtype=np.float32)/SR
        noise = rng.normal(0, 1, len(t)).astype(np.float32)
        high = signal.sosfilt(signal.butter(2, [4700, 10000], 'bandpass', fs=SR, output='sos'), noise)
        return (high*np.exp(-t/(.039 if opened else .011))*(1-np.exp(-t/.001))).astype(np.float32)

    def rim():
        t = np.arange(round(.1*SR), dtype=np.float32)/SR
        x = np.sin(2*np.pi*480*t)*np.exp(-t/.012) + .24*np.sin(2*np.pi*1070*t)*np.exp(-t/.006)
        return (x*(1-np.exp(-t/.0006))).astype(np.float32)

    chords = [
        {'name':'Dmaj9', 'bass':38, 'notes':[54,57,61,64], 'arp':[66,69,73,76]},
        {'name':'Amaj9', 'bass':33, 'notes':[52,56,59,61], 'arp':[64,68,71,73]},
        {'name':'F#m9', 'bass':30, 'notes':[52,56,57,61], 'arp':[64,68,69,73]},
        {'name':'E6add9', 'bass':40, 'notes':[52,56,59,61], 'arp':[64,68,71,78]},
    ]
    sections = [
        {'start':0, 'end':10.91, 'name':'Reveal', 'description':'Airy pad and a restrained three-note identity.'},
        {'start':10.91, 'end':54.55, 'name':'Find the point', 'description':'Electric piano and a rounded half-time pulse.'},
        {'start':54.55, 'end':109.09, 'name':'Shape the change', 'description':'A new voicing and sparse answering notes.'},
        {'start':109.09, 'end':158.18, 'name':'Explain precisely', 'description':'Percussion thins, leaving room for the detailed workflow.'},
        {'start':158.18, 'end':183.10, 'name':'Connect', 'description':'The main motif returns with a little lift.'},
        {'start':183.10, 'end':210.0, 'name':'Agent handoff', 'description':'A quieter reset with a low electronic pulse.'},
        {'start':210.0, 'end':239.10, 'name':'Complete and return', 'description':'Piano, light ticks and upper response notes come together.'},
        {'start':239.10, 'end':246.0, 'name':'EditHere', 'description':'A sustained A-major resolution and a smooth final fade.'},
    ]
    bars = math.ceil(args.duration/BAR)
    for bar in range(bars):
        start = bar*BAR
        if start >= 239.1:
            continue
        phrase = bar//2
        order = [0,1,2,3] if bar < 40 or bar >= 66 else [2,0,1,3]
        chord = chords[order[phrase % 4]]
        sparse = 109.09 <= start < 145.45 or 183.1 <= start < 199.1
        energy = .78 if sparse else 1.0
        if bar % 2 == 0:
            place(pad(chord['notes'], BAR*2+1), start, .16, -.18 if phrase%2 else .18, False)
            for i,note in enumerate(chord['notes']):
                place(piano(note, 3.8, True), start+i*.026, .079*energy, -.3+i*.2)
        if bar >= 4:
            pattern = [0, 1.5, 2.5] if bar%2 == 0 else [0.5, 2, 3.5]
            if sparse:
                pattern = [0.5, 2.5]
            for i, beat in enumerate(pattern):
                note = chord['arp'][(i + bar//2) % 4]
                place(piano(note, 2.3), start+beat*BEAT+rng.uniform(-.007,.007), .067*energy, [-.48,.32,.08][i%3])
            for beat in [0, 2.5] if not sparse else [0]:
                place(bass(chord['bass']), start+beat*BEAT, .20*energy, 0, False)
        if bar >= 6 and not (109.09 <= start < 120):
            for beat in [0, 2] if not sparse else [0]:
                place(kick(), start+beat*BEAT, .145*energy, 0, False)
            if not sparse:
                for beat in [1,3]:
                    place(rim(), start+beat*BEAT+.01, .030, -.16, False)
            for i, beat in enumerate([.5,1.5,2.5,3.5] if not sparse else [1.5,3.5]):
                place(hat(bar%4==3 and i==3), start+beat*BEAT+rng.uniform(-.008,.008), .038*energy, -.27 if i%2 else .27, False)
        # Sparse melodic replies are varied between larger phrases; never a loud lead line.
        if bar >= 12 and bar%8 in [4,6] and not sparse:
            melody = [76,73,71] if (bar//8)%2 == 0 else [73,76,78]
            for i,note in enumerate(melody):
                place(piano(note, 3.4, True), start+(.5+i*.75)*BEAT, .052, .3-i*.22)
    # Sonic identity at the opening and a gentle, definite ending.
    for i,note in enumerate([69,73,76]):
        place(piano(note, 4.5, True), .55+i*.24, .12, -.18+i*.18)
    place(pad([45,52,56,61,64], 6.9), 239.1, .22, 0, False)
    for i,note in enumerate([57,64,68,73,76]):
        place(piano(note, 6.5, True), 239.1+i*.085, .10, -.32+i*.16)
    # A soft high-pass removes DC/rumble; a soft low-pass keeps the score behind speech.
    mix = signal.sosfilt(signal.butter(2, 32, 'highpass', fs=SR, output='sos'), mix, axis=0).astype(np.float32)
    mix = signal.sosfilt(signal.butter(2, 7200, 'lowpass', fs=SR, output='sos'), mix, axis=0).astype(np.float32)
    fade_in = round(1.3*SR)
    fade_out = round(3.6*SR)
    mix[:fade_in] *= np.linspace(0,1,fade_in)[:,None]
    mix[-fade_out:] *= np.linspace(1,0,fade_out)[:,None]**1.3
    peak = float(np.abs(mix).max())
    mix *= .63 / max(peak, 1e-8)
    destination = args.output_dir/'EditHere-original-score.wav'
    write_wav(destination, mix)
    metadata = dict(title='EditHere — A Clearer Point', composer='EditHere project / procedural original',
                    external_samples=False, generative_api_used=False, seed=SEED,
                    sample_rate=SR, duration_seconds=args.duration, bpm=BPM,
                    key='A major / F-sharp minor', chord_palette=[c['name'] for c in chords],
                    sections=sections, source='scripts/compose-demo-score.py',
                    usage='Original procedural soundtrack created for the EditHere promotional video; no third-party recordings or sampled music are used.')
    (args.output_dir/'score-arrangement.json').write_text(json.dumps(metadata, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({'score':str(destination),'duration':args.duration,'peak_dbfs':20*math.log10(.63)},ensure_ascii=False))

if __name__ == '__main__':
    main()
