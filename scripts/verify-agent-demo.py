from pathlib import Path
import base64,io,json,subprocess,sys,hashlib
import cv2,numpy as np
from PIL import Image
import jsonschema
root=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();ff=root/'.cache/promo-video-deps/imageio_ffmpeg/binaries/ffmpeg-win-x86_64-v7.1.exe'
load=lambda p:json.loads(p.read_text(encoding='utf-8'))
feedback=load(out/'take-2/feedback.json');reply=load(out/'take-2/reply.json');native=load(out/'take-2/native-timeline.json');cues=load(out/'narration-cues.json')
jsonschema.validate(feedback,load(root/'schema/feedback-v0.7.schema.json'))
assert reply['ok'] and reply['annotations']==len(feedback['annotations'])==1
assert feedback['annotations'][0]['point']==dict(x=732,y=326)
assert feedback['annotations'][0]['text']=='6 月实际营收应为 84 万元，请按原始数据修正。'
assert native['feedbackAbsentBeforeFinish']
assert Image.open(io.BytesIO(base64.b64decode(feedback['image'].split(',')[1]))).size==(1120,720)
assert len(cues)==37
for i,q in enumerate(cues):
 assert 0<=q['start']<q['end']<=246
 if i:assert cues[i-1]['end']<=q['start']
report=dict(durationSeconds=246,insertStartSeconds=183.1,insertDurationSeconds=56,subtitles=37,voice='zh-CN-XiaoxiaoNeural',nativeProtocol='unique local socket',schema='passed',feedbackPublishedAfterFinish=True,aiFollowup='illustrated handoff, disclosed in video',artifacts={})
for name,w,h in [('EditHere-introduction-agent-1080p.mp4',1920,1080),('EditHere-introduction-agent-web.mp4',1280,720),('EditHere-agent-chapter-1080p.mp4',1920,1080)]:
 p=out/name;cap=cv2.VideoCapture(str(p));frames=round(cap.get(cv2.CAP_PROP_FRAME_COUNT));fps=cap.get(cv2.CAP_PROP_FPS)
 assert (int(cap.get(3)),int(cap.get(4)))==(w,h)
 assert fps==30 and frames==(56 if 'chapter' in name else 246)*30,(name,frames,fps)
 r=subprocess.run([str(ff),'-v','error','-i',str(p),'-f','null','-'],capture_output=True,text=True)
 assert r.returncode==0 and not r.stderr,(name,r.stderr)
 report['artifacts'][name]=dict(width=w,height=h,fps=fps,frames=frames,bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest(),fullDecode='passed');cap.release()
old=cv2.VideoCapture(str(root/'artifacts/edithere-release/EditHere-introduction-1080p.mp4'));new=cv2.VideoCapture(str(out/'EditHere-introduction-agent-1080p.mp4'))
def frame(cap,t):
 cap.set(cv2.CAP_PROP_POS_FRAMES,round(t*30));ok,f=cap.read();assert ok;return f
preserved=[]
for t in [1.5,17.5,69.5,133.5,181,186]:
 a=frame(old,t);b=frame(new,t if t<183.1 else t+56);error=float(np.mean(abs(a.astype(float)-b.astype(float))));assert error<3.0,(t,error);preserved.append(dict(originalTime=t,newTime=t if t<183.1 else t+56,meanPixelError=round(error,3)))
report['originalScenesPreserved']=preserved
samples=[182,184.6,193.4,198.2,205.5,211,213,219.6,233.1,242]
sheet=Image.new('RGB',(1280,((len(samples)+1)//2)*384),(231,237,247))
from PIL import ImageDraw,ImageFont
draw=ImageDraw.Draw(sheet);font=ImageFont.truetype('C:/Windows/Fonts/consola.ttf',17)
for i,t in enumerate(samples):
 b=frame(new,t);im=Image.fromarray(cv2.cvtColor(b,cv2.COLOR_BGR2RGB));im.save(out/f'encoded-review-{i}.jpg',quality=92);sheet.paste(im.resize((640,360)),((i%2)*640,(i//2)*384));draw.text(((i%2)*640+10,(i//2)*384+364),f'{t:.1f} s',fill='#24344f',font=font)
sheet.save(out/'encoded-review-sheet.jpg',quality=91)
new.release();old.release()
r=subprocess.run([str(ff),'-hide_banner','-i',str(out/'EditHere-introduction-agent-1080p.mp4'),'-vn','-af','volumedetect','-f','null','-'],capture_output=True,text=True)
assert r.returncode==0
report['audioLevels']=[x.strip() for x in r.stderr.splitlines() if 'mean_volume' in x or 'max_volume' in x]
probe={}
for name in ['EditHere-introduction-agent-1080p.mp4','EditHere-introduction-agent-web.mp4']:
 r=subprocess.run([str(ff),'-hide_banner','-i',str(out/name),'-t','0','-f','null','-'],capture_output=True,text=True)
 (out/(name+'.probe.txt')).write_text(r.stderr,encoding='utf-8')
 probe[name]=[line.strip() for line in r.stderr.splitlines() if 'Duration:' in line or 'Stream #0:0[0x' in line or 'Stream #0:1[0x' in line]
 data=(out/name).read_bytes();assert data.find(b'moov')<data.find(b'mdat');probe[name].append('MP4 faststart: moov precedes mdat')
cap=cv2.VideoCapture(str(out/'EditHere-introduction-agent-1080p.mp4'))
joinFrames=[5492,5493,7172,7173,7203,7260]
joinSheet=Image.new('RGB',(1280,1152),'#e5ebf6');joinDraw=ImageDraw.Draw(joinSheet)
for i,num in enumerate(joinFrames):
 cap.set(cv2.CAP_PROP_POS_FRAMES,num);ok,b=cap.read();assert ok
 im=Image.fromarray(cv2.cvtColor(b,cv2.COLOR_BGR2RGB));im.save(out/f'join-frame-{num}.jpg',quality=94);joinSheet.paste(im.resize((640,360)),(i%2*640,i//2*384));joinDraw.text((i%2*640+10,i//2*384+364),f'frame {num} / {num/30:.3f} sec',fill='#253653',font=font)
joinSheet.save(out/'join-review-sheet.jpg',quality=94)
cap.set(cv2.CAP_PROP_POS_FRAMES,round(219.6*30));ok,b=cap.read();assert ok
Image.fromarray(cv2.cvtColor(b,cv2.COLOR_BGR2RGB)).crop((180,178,970,952)).save(out/'feedback-code-detail.png')
cap.release();report['streamProbe']=probe;report['joinFrames']=joinFrames
(out/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps(report,ensure_ascii=False,indent=2))
