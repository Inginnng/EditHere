import { useEffect, useRef, useState } from 'react';
import { ArrowDownToLine, ArrowLeft, Check, Copy, FileText, LoaderCircle, Settings2, Sparkles } from 'lucide-react';
import { DemoArtwork } from './Workspace';

const notes = ['主标题再大一些，保留现在的留白。副标题颜色调浅，让阅读顺序更清晰。','主按钮改成深绿色，圆角收小一点。文案改为「探索系列」。','把椅子向右移动一点，给左侧文字留出更多呼吸空间。'];
const example = `首页首屏修改意见

1. 标题层级
适当增大主标题「A quieter kind of home.」，保留原有字体、两行排版和留白。副标题颜色调浅，让标题、说明与按钮的阅读顺序更清晰。

2. 按钮样式
将「EXPLORE THE EDIT」主按钮改为深绿色，圆角收小，文案改为「探索系列」。保持清晰的文字对比度。

3. 图片位置
将右侧椅子展示图向右移动，让图片右边缘与页面内容的右侧边界对齐，并拉开它与左侧标题的间距。`;

export default function Analysis({provider,onSettings,onBack,onToast}:{provider:string;onSettings:()=>void;onBack:()=>void;onToast:(message:string)=>void}) {
  const [output,setOutput] = useState(example);
  const [tab,setTab] = useState<'image'|'notes'>('image');
  const [busy,setBusy] = useState(false);
  const timer=useRef<ReturnType<typeof setTimeout>|undefined>(undefined);
  useEffect(()=>()=>clearTimeout(timer.current),[]);
  const generate=()=>{if(busy){clearTimeout(timer.current);setBusy(false);return;}setBusy(true);timer.current=setTimeout(()=>{setOutput(example);setBusy(false);onToast('演示解析完成，结果可以直接编辑');},1500);};
  const copy=async()=>{try{await navigator.clipboard.writeText(output);onToast('已复制纯文本修改意见');}catch{onToast('复制失败，请选中文字后复制');}};
  const download=()=>{const url=URL.createObjectURL(new Blob([output],{type:'text/plain;charset=utf-8'}));const anchor=document.createElement('a');anchor.href=url;anchor.download='EditHere-修改意见.txt';anchor.click();URL.revokeObjectURL(url);};
  return <div className="analysis-page">
    <div className="page-heading page-heading-with-action"><div><h1>AI 解析</h1><p>结合画面与批注，整理成可直接执行的修改意见。</p></div><button className="button secondary" onClick={onBack}><ArrowLeft size={15}/>返回批注</button></div>
    <div className="analysis-layout">
      <section className="analysis-source">
        <div className="panel-heading"><h2>画面与批注</h2><span className="badge neutral">示例项目</span></div>
        <div className="source-tabs" role="tablist" aria-label="原始内容"><button role="tab" aria-selected={tab==='image'} className={tab==='image'?'active':''} onClick={()=>setTab('image')}>画面</button><button role="tab" aria-selected={tab==='notes'} className={tab==='notes'?'active':''} onClick={()=>setTab('notes')}>批注 <span>3</span></button></div>
        {tab==='image'?<div className="analysis-artwork"><DemoArtwork/><span className="analysis-pin a1">1</span><span className="analysis-pin a2">2</span><span className="analysis-pin a3">3</span></div>:<div className="source-notes">{notes.map((note,index)=><div key={note}><span>{index+1}</span><p>{note}</p></div>)}</div>}
        <div className="source-file"><FileText size={16}/><div><strong>morrow-homepage.png</strong><span>800 × 480 · 3 条批注</span></div></div>
        <div className="source-configuration"><span>解析服务</span><button className="source-model-button" onClick={onSettings}><img src={`./providers/${['OpenAI','Claude','DeepSeek','GLM','Kimi'].includes(provider)?provider.toLowerCase():'openai'}.svg`} alt=""/><strong>{provider}</strong><Settings2 size={15}/></button></div>
        <p className="source-preview-note">当前展示内置示例。真实 API 将在后端接入后调用。</p>
      </section>
      <section className="analysis-document"><div className="panel-heading"><h2>修改意见</h2><span className="badge neutral">可编辑</span></div><textarea className="analysis-output" aria-label="可编辑的修改意见" spellCheck={false} value={output} onChange={event=>setOutput(event.target.value)}/>{busy&&<div className="analysis-progress" role="status"><LoaderCircle className="spin" size={18}/><span>正在演示解析过程…</span></div>}<div className="document-footer"><span>纯文本 · {output.replace(/\s/g,'').length} 字</span><button className="text-button" onClick={download}><ArrowDownToLine size={14}/>导出 .txt</button></div></section>
    </div>
    <div className="analysis-actions"><span><Check size={15}/>只复制文字，方便交给 Agent</span><div><button className="button secondary" onClick={generate}>{busy?<LoaderCircle size={15} className="spin"/>:<Sparkles size={15}/>} {busy?'停止演示':'演示解析'}</button><button className="button primary" onClick={copy}><Copy size={15}/>复制修改意见</button></div></div>
  </div>;
}
