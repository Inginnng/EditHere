#!/usr/bin/env python3
"""Rebuild clean narration, duck an original score, and mux it with a video.

Usage:
  python scripts/mix-demo-audio.py --output-dir C:/.../edithere-video-refinement \
    --music C:/.../EditHere-original-score.wav --video C:/.../silent-final.mp4 \
    --output C:/.../EditHere-introduction-refined-1080p.mp4

The input video's existing audio is deliberately never included. --mux-only
reuses the previously verified final-mix.wav when the picture render changes.
"""
from __future__ import annotations
import argparse
import json
import math
import re
import subprocess
from pathlib import Path
import wave
import numpy as np
from scipy.ndimage import maximum_filter1d, gaussian_filter1d

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MEDIA = Path('C:/Users/InGing/.codex/visualizations/2026/09/12/01a0955e-ff46-76d0-be3b-61dab9a71caa')
SR = 48000
DURATION = 246.0


def execute(args):
    result = subprocess.run([str(x) for x in args], capture_output=True, text=True, encoding='utf-8', errors='replace')
    if result.returncode:
        raise RuntimeError(result.stderr[-6000:])
    return result.stderr


def measure(ffmpeg, path):
    stderr = execute([ffmpeg, '-hide_banner', '-i', path, '-af', 'loudnorm=I=-16:TP=-1.5:LRA=11:print_format=json', '-f','null','-'])
    matches = re.findall(r'\{\s*"input_i".*?\}', stderr, re.S)
    if not matches:
        raise RuntimeError('FFmpeg did not return loudness metadata for '+str(path))
    data = json.loads(matches[-1])
    return {'integrated_lufs':float(data['input_i']), 'true_peak_dbtp':float(data['input_tp']),
            'loudness_range_lu':float(data['input_lra'])}


def read_pcm(path):
    with wave.open(str(path), 'rb') as src:
        if src.getframerate()!=SR or src.getsampwidth()!=2 or src.getnchannels()!=2:
            raise ValueError('Expected stereo 48 kHz 16-bit PCM: '+str(path))
        return np.frombuffer(src.readframes(src.getnframes()), dtype='<i2').reshape(-1,2).astype(np.float32)/32768.0


def write_pcm(path, audio):
    with wave.open(str(path), 'wb') as dst:
        dst.setnchannels(2); dst.setsampwidth(2); dst.setframerate(SR)
        dst.writeframes(np.round(np.clip(audio,-1,1)*32767).astype('<i2').tobytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ffmpeg', type=Path, default=ROOT/'.cache/promo-video-deps/imageio_ffmpeg/binaries/ffmpeg-win-x86_64-v7.1.exe')
    parser.add_argument('--base-narration', type=Path, default=ROOT/'artifacts/edithere-release/narration.wav')
    parser.add_argument('--agent-narration', type=Path, default=DEFAULT_MEDIA/'edithere-agent-demo/agent-narration.wav')
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--music', type=Path)
    parser.add_argument('--video', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--mux-only', action='store_true')
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    mixed_path = args.output_dir/'final-mix.wav'
    if not args.mux_only:
        if args.music is None:
            parser.error('--music is required unless --mux-only is selected')
        clean_path = args.output_dir/'clean-narration.wav'
        # Use original PCM narration, removing the old inaudible score completely.
        graph = ('[0:a]asplit=2[base0][base1];'
                 '[base0]atrim=end=183.1,asetpts=PTS-STARTPTS,aformat=sample_rates=48000:channel_layouts=stereo[v0];'
                 '[1:a]atrim=end=56,apad=whole_dur=56,asetpts=PTS-STARTPTS,aformat=sample_rates=48000:channel_layouts=stereo[v1];'
                 '[base1]atrim=start=183.1:end=190,asetpts=PTS-STARTPTS,aformat=sample_rates=48000:channel_layouts=stereo[v2];'
                 '[v0][v1][v2]concat=n=3:v=0:a=1,apad=whole_dur=246,atrim=end=246[out]')
        execute([args.ffmpeg,'-y','-v','warning','-i',args.base_narration,'-i',args.agent_narration,
                 '-filter_complex',graph,'-map','[out]','-ar',SR,'-ac',2,'-c:a','pcm_s16le',clean_path])
        voice_stats = measure(args.ffmpeg, clean_path)
        score_stats = measure(args.ffmpeg, args.music)
        voice = read_pcm(clean_path)
        music = read_pcm(args.music)
        expected = round(DURATION*SR)
        if len(voice)!=expected or len(music)!=expected:
            raise ValueError(f'Both stems must be exactly 246 seconds: voice={len(voice)/SR}, score={len(music)/SR}')
        voice_gain_db = min(-16-voice_stats['integrated_lufs'], -2.0-voice_stats['true_peak_dbtp'])
        music_gain_db = min(-23-score_stats['integrated_lufs'], -6.0-score_stats['true_peak_dbtp'])
        voice *= 10**(voice_gain_db/20)
        music *= 10**(music_gain_db/20)
        # 10 ms speech envelope; 70 ms look-ahead and a smooth 140 ms hold
        # keep music from pumping up between adjacent words.
        block = 480
        rms = np.sqrt(np.mean(voice.reshape(-1,block,2)**2, axis=(1,2)))
        dbfs = 20*np.log10(np.maximum(rms,1e-7))
        speech = np.clip((dbfs+46)/17,0,1)
        speech = maximum_filter1d(speech, size=15, mode='nearest')
        speech = gaussian_filter1d(speech, sigma=6, mode='nearest')
        duck_db = -5.4*speech
        positions = np.arange(expected, dtype=np.float32)/block
        envelope = np.interp(positions, np.arange(len(duck_db)), 10**(duck_db/20)).astype(np.float32)
        music *= envelope[:,None]
        mixed = voice+music
        peak = float(np.max(np.abs(mixed)))
        master_gain = min(1.0, 10**(-1.6/20)/max(peak,1e-9))
        mixed *= master_gain
        ducked_path = args.output_dir/'ducked-score.wav'
        voice_normal_path = args.output_dir/'normalized-narration.wav'
        write_pcm(voice_normal_path,voice*master_gain)
        write_pcm(ducked_path,music*master_gain)
        write_pcm(mixed_path,mixed)
        # 30 seconds crosses the chapter transition and includes meaningful speech gaps.
        sample_start = 178.0
        write_pcm(args.output_dir/'EditHere-music-audition-30s.wav', mixed[round(sample_start*SR):round((sample_start+30)*SR)])
        final_stats = measure(args.ffmpeg,mixed_path)
        ducked_stats = measure(args.ffmpeg,ducked_path)
        normalized_voice_stats = measure(args.ffmpeg,voice_normal_path)
        report = {
            'duration_seconds':DURATION,'sample_rate':SR,'channels':2,
            'narration_sources':[str(args.base_narration),str(args.agent_narration)],
            'narration_splices_seconds':[183.1,239.1],
            'music_source':str(args.music),
            'original_video_audio_used':False,
            'narration_input':voice_stats,'music_input':score_stats,
            'narration_gain_db':voice_gain_db,'music_gain_db':music_gain_db,
            'ducking_max_db':5.4,'ducking_average_db':float(-duck_db.mean()),
            'master_gain_db':20*math.log10(master_gain),
            'final_mix':final_stats,'narration_in_mix':normalized_voice_stats,'music_in_mix':ducked_stats,
            'sample_peak_dbfs':20*math.log10(float(np.abs(mixed).max())),
            'clipped_samples':int((np.abs(mixed)>=1).sum()),
            'audition_start_seconds':sample_start,'audition_duration_seconds':30,
        }
        (args.output_dir/'audio-verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        if report['clipped_samples'] or final_stats['true_peak_dbtp'] > -1.0:
            raise RuntimeError('Audio headroom verification failed: '+json.dumps(final_stats))
        print(json.dumps(report,ensure_ascii=False),flush=True)
    if args.video:
        if not args.output:
            parser.error('--output is required when --video is supplied')
        execute([args.ffmpeg,'-y','-v','warning','-i',args.video,'-i',mixed_path,
                 '-map','0:v:0','-map','1:a:0','-c:v','copy','-c:a','aac','-b:a','192k','-ar',SR,
                 '-t',DURATION,'-movflags','+faststart',args.output])
        final_encoded_stats = measure(args.ffmpeg,args.output)
        if final_encoded_stats['true_peak_dbtp'] > -.7:
            raise RuntimeError('Encoded AAC has insufficient peak headroom: '+json.dumps(final_encoded_stats))
        (args.output_dir/(args.output.stem+'.audio-verification.json')).write_text(
            json.dumps({'output':str(args.output),'encoded_audio':final_encoded_stats,
                        'original_video_audio_used':False,'music_mix':str(mixed_path)},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print(json.dumps({'video':str(args.output),'encoded_audio':final_encoded_stats},ensure_ascii=False),flush=True)

if __name__ == '__main__':
    main()
