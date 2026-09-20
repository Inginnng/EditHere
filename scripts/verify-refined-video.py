from pathlib import Path
import hashlib, json, subprocess, sys
import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont
root, out = (Path(x).resolve() for x in sys.argv[1:3])
ff = root / '.cache/promo-video-deps/imageio_ffmpeg/binaries/ffmpeg-win-x86_64-v7.1.exe'
files = ['EditHere-introduction-refined-1080p.mp4', 'EditHere-introduction-refined-web.mp4', 'EditHere-agent-chapter-refined-1080p.mp4']
report = {'durationSeconds':246, 'agentStartsAt':183.1, 'wordmarks':'original SVG paths, no font dependency', 'music':'new original score with rebuilt clean narration', 'files':{}}
for name in files:
    p=out/name
    result=subprocess.run([str(ff),'-v','error','-i',str(p),'-f','null','-'],capture_output=True,text=True)
    if result.returncode or result.stderr.strip(): raise RuntimeError(result.stderr)
    cap=cv2.VideoCapture(str(p)); width=int(cap.get(cv2.CAP_PROP_FRAME_WIDTH)); height=int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT)); frames=int(cap.get(cv2.CAP_PROP_FRAME_COUNT)); fps=cap.get(cv2.CAP_PROP_FPS); cap.release()
    expected=1680 if 'chapter' in name else 7380
    assert frames==expected and abs(fps-30)<0.001,(name,frames,fps)
    assert (width,height)==((1280,720) if 'web' in name else (1920,1080)),(name,width,height)
    data=p.read_bytes();assert 0<data.find(b'moov')<data.find(b'mdat')
    report['files'][name]={'width':width,'height':height,'frames':frames,'fps':fps,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'fullDecode':'passed','faststart':True}
cap=cv2.VideoCapture(str(out/files[0]));samples=[1.5,5.2,17.5,67.5,100.5,133.5,183.067,183.1,184.9,206,219.6,239.067,239.1,242]
sheet=Image.new('RGB',(1280,384*((len(samples)+1)//2)),'#eaf0fa');d=ImageDraw.Draw(sheet);font=ImageFont.truetype('C:/Windows/Fonts/consola.ttf',17)
for i,t in enumerate(samples):
    cap.set(cv2.CAP_PROP_POS_FRAMES,round(t*30));ok,b=cap.read();assert ok
    im=Image.fromarray(cv2.cvtColor(b,cv2.COLOR_BGR2RGB));im.save(out/f'final-review-{i}.jpg',quality=94);sheet.paste(im.resize((640,360)),(i%2*640,i//2*384));d.text((i%2*640+10,i//2*384+364),f'{t:.3f}s',fill='#20374d',font=font)
sheet.save(out/'final-review-sheet.jpg',quality=92)
# The product recordings are unchanged; branding is outside these native window crops.
old=cv2.VideoCapture(str(out.parent/'edithere-agent-demo/EditHere-introduction-agent-1080p.mp4'))
report['nativeFootageChecks']=[]
for t in [17.5,34.0,133.5,206.0]:
    cap.set(cv2.CAP_PROP_POS_FRAMES,round(t*30));old.set(cv2.CAP_PROP_POS_FRAMES,round(t*30));ok1,a=cap.read();ok2,b=old.read();assert ok1 and ok2
    error=float(np.abs(a[180:940,400:1500].astype(np.float32)-b[180:940,400:1500]).mean())
    assert error<3.0,(t,error)
    report['nativeFootageChecks'].append({'time':t,'meanPixelDifference':round(error,3)})
cap.release();old.release()
report['audio']=json.loads((out/'EditHere-introduction-refined-1080p.audio-verification.json').read_text(encoding='utf8'))
report['mix']=json.loads((out/'audio-verification.json').read_text(encoding='utf8'))
(out/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
(out/'SHA256SUMS-refined.txt').write_text(''.join(v['sha256']+'  '+k+'\n' for k,v in report['files'].items()),encoding='utf8')
print(json.dumps({k:{'bytes':v['bytes'],'sha256':v['sha256']} for k,v in report['files'].items()},ensure_ascii=False))
