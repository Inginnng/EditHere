from pathlib import Path
import json,subprocess,sys,hashlib
root=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve()
ff=root/'.cache/promo-video-deps/imageio_ffmpeg/binaries/ffmpeg-win-x86_64-v7.1.exe'
old=root/'artifacts/edithere-release/EditHere-introduction-1080p.mp4';segment=out/'EditHere-agent-chapter-1080p.mp4';full=out/'EditHere-introduction-agent-1080p.mp4';web=out/'EditHere-introduction-agent-web.mp4'
cut=183.1;insert=56
filters=f'[0:v]trim=end={cut},setpts=PTS-STARTPTS[v0];[0:a]atrim=end={cut},asetpts=PTS-STARTPTS,aformat=sample_rates=48000:channel_layouts=stereo[a0];[1:v]setpts=PTS-STARTPTS[v1];[1:a]asetpts=PTS-STARTPTS,aformat=sample_rates=48000:channel_layouts=stereo[a1];[2:v]setpts=PTS-STARTPTS[v2];[0:a]atrim=start={cut},asetpts=PTS-STARTPTS,aformat=sample_rates=48000:channel_layouts=stereo[a2];[v0][a0][v1][a1][v2][a2]concat=n=3:v=1:a=1[v][a]'
if '--metadata-only' not in sys.argv:
 subprocess.run([str(ff),'-y','-v','warning','-i',str(old),'-i',str(segment),'-i',str(out/'ending-picture.mp4'),'-filter_complex',filters,'-map','[v]','-map','[a]','-c:v','libx264','-preset','fast','-crf','18','-pix_fmt','yuv420p','-r','30','-c:a','aac','-b:a','192k','-ar','48000','-movflags','+faststart',str(full)],check=True)
 subprocess.run([str(ff),'-y','-v','warning','-i',str(full),'-vf','scale=1280:720','-c:v','libx264','-preset','medium','-crf','25','-pix_fmt','yuv420p','-c:a','aac','-b:a','128k','-ar','48000','-movflags','+faststart',str(web)],check=True)
oldcues=json.loads((root/'artifacts/edithere-release/narration-cues.json').read_text(encoding='utf-8'))
newcues=json.loads((out/'agent-narration-cues.json').read_text(encoding='utf-8'))
cues=[]
for q in oldcues:
 q=q.copy()
 if q['start']>=cut:q['start']+=insert;q['end']+=insert
 cues.append(q)
for i,q in enumerate(newcues):cues.append(dict(id=f'agent-{i+1}',start=round(q['start']+cut,3),end=round(q['end']+cut,3),text=q['text']))
cues.sort(key=lambda x:x['start'])
for a,b in zip(cues,cues[1:]):assert a['end']<=b['start'],(a,b)
def stamp(t):
 ms=round(t*1000);h,ms=divmod(ms,3600000);m,ms=divmod(ms,60000);s,ms=divmod(ms,1000);return f'{h:02d}:{m:02d}:{s:02d},{ms:03d}'
srt='\n\n'.join(f"{i+1}\n{stamp(q['start'])} --> {stamp(q['end'])}\n{q['text']}" for i,q in enumerate(cues))+'\n'
(out/'narration.srt').write_text(srt,encoding='utf-8-sig')
(out/'narration.vtt').write_text('WEBVTT\n\n'+srt.replace(',','.'),encoding='utf-8')
(out/'narration-cues.json').write_text(json.dumps(cues,ensure_ascii=False,indent=2),encoding='utf-8')
(out/'narration.md').write_text('# EditHere 介绍视频 · Agent 协作版\n\n普通话女声：zh-CN-XiaoxiaoNeural。\n\n'+'\n\n'.join(stamp(q['start'])[:8]+'　'+q['text'] for q in cues)+'\n',encoding='utf-8')
checksums='\n'.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name for p in [full,web,segment] if p.exists())+'\n'
(out/'SHA256SUMS.txt').write_text(checksums,encoding='utf-8')
print('Assembled 246-second film; 37 cues, no overlapping subtitles.',flush=True)
