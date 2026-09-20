"""Assemble the refreshed 190s base and 56s Agent chapter without audio.

Usage: python scripts/assemble-branded-picture.py ROOT OUTPUT
The two renderers write OUTPUT/base and OUTPUT/agent. Mix audio separately with
mix-demo-audio.py so the old score is never layered into the new soundtrack.
"""
from pathlib import Path
import json, subprocess, sys
root, out = (Path(p).resolve() for p in sys.argv[1:3])
ff = root / '.cache/promo-video-deps/imageio_ffmpeg/binaries/ffmpeg-win-x86_64-v7.1.exe'
inputs = [out / 'base/EditHere-branded-base-picture.mp4', out / 'agent/EditHere-agent-chapter-1080p.mp4', out / 'agent/ending-picture.mp4']
for p in inputs:
    if not p.is_file(): raise SystemExit(f'Missing rendered input: {p}')
output = out / 'EditHere-introduction-branded-picture.mp4'
filters = '[0:v]trim=end=183.1,setpts=PTS-STARTPTS[v0];[1:v]setpts=PTS-STARTPTS[v1];[2:v]setpts=PTS-STARTPTS[v2];[v0][v1][v2]concat=n=3:v=1:a=0[v]'
args = [str(ff), '-y', '-v', 'warning']
for p in inputs: args += ['-i', str(p)]
args += ['-filter_complex', filters, '-map', '[v]', '-an', '-c:v', 'libx264', '-preset', 'fast', '-crf', '18', '-pix_fmt', 'yuv420p', '-r', '30', '-t', '246', '-movflags', '+faststart', str(output)]
subprocess.run(args, check=True)
(out / 'picture-assembly.json').write_text(json.dumps({'inputs': [str(p) for p in inputs], 'durationSeconds': 246, 'fps': 30, 'agentStartsAt': 183.1, 'audio': 'none; rebuilt clean narration and original score are mixed separately', 'wordmarks': ['assets/brand/edithere-wordmark.svg', 'assets/brand/edithere-wordmark-light.svg'], 'output': str(output)}, ensure_ascii=False, indent=2), encoding='utf-8')
print(output)
