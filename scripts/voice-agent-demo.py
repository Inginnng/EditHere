from pathlib import Path
import asyncio,json,subprocess,sys,wave
import numpy as np
root=Path(sys.argv[1]).resolve(); out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(root/'.cache/promo-voice-deps'))
import edge_tts
ff=root/'.cache/promo-video-deps/imageio_ffmpeg/binaries/ffmpeg-win-x86_64-v7.1.exe'
plan=[
(.4,4.7,'现在，AI 也能直接调用改这里。'),
(5,12.6,'安装配套技能后，告诉 AI：打开这张图，让我标注要修改的地方。'),
(13,20.8,'AI 通过命令行发起标注，程序打开图片，并等待你的修改意见。'),
(21,27.4,'像刚才一样，点中位置，写下具体要求。'),
(28,33.8,'批注完成，点击“完成并返回 AI”。'),
(34.3,42.4,'结构化反馈这时才会写入文件，包含原图、批注和位置。'),
(42.7,48,'AI 读取这份反馈，就能接着完成修改。'),
(48.4,55.7,'从 AI 发起，到你确认，再交回 AI，整个流程连起来了。')]
rate=48000;track=np.zeros(56*rate,dtype=np.int16);cues=[];voice=out/'voice';voice.mkdir(exist_ok=True)
async def synth(i,text):
    p=voice/f'{i}.mp3'
    if not p.exists(): await asyncio.wait_for(edge_tts.Communicate(text,'zh-CN-XiaoxiaoNeural',rate='+0%').save(str(p)),timeout=55)
    return p
async def main():
    for i,(start,end,text) in enumerate(plan):
        mp3=await synth(i,text);raw=voice/f'{i}-raw.wav'
        subprocess.run([str(ff),'-y','-v','error','-i',str(mp3),'-ar',str(rate),'-ac','1',str(raw)],check=True)
        with wave.open(str(raw),'rb') as w:a=np.frombuffer(w.readframes(w.getnframes()),dtype=np.int16).copy()
        active=np.flatnonzero(abs(a.astype(int))>180)
        if len(active):a=a[max(0,int(active[0])-3840):min(len(a),int(active[-1])+7200)]
        speed=max(1,len(a)/rate/(end-start));assert speed<1.22,(i,speed)
        trim=voice/f'{i}-trim.wav';fit=voice/f'{i}-fit.wav'
        with wave.open(str(trim),'wb') as w:w.setnchannels(1);w.setsampwidth(2);w.setframerate(rate);w.writeframes(a.tobytes())
        subprocess.run([str(ff),'-y','-v','error','-i',str(trim),'-af',f'atempo={speed:.6f},afade=t=in:d=0.015','-ar',str(rate),'-ac','1',str(fit)],check=True)
        with wave.open(str(fit),'rb') as w:a=np.frombuffer(w.readframes(w.getnframes()),dtype=np.int16).copy()
        a=a[:int((end-start)*rate)];pos=round(start*rate);track[pos:pos+len(a)]=a
        cues.append(dict(start=start,end=round((pos+len(a))/rate,3),text=text,speed=round(speed,4)))
        print(i,text,round(len(a)/rate,2),flush=True)
asyncio.run(main())
with wave.open(str(out/'agent-narration.wav'),'wb') as w:w.setnchannels(1);w.setsampwidth(2);w.setframerate(rate);w.writeframes(track.tobytes())
(out/'agent-narration-cues.json').write_text(json.dumps(cues,ensure_ascii=False,indent=2),encoding='utf-8')
