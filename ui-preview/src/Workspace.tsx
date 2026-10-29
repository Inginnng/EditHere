import { useEffect, useRef, useState } from 'react'
import { ArrowDownToLine, ArrowUpRight, Check, Code2, Copy, ImagePlus, Layers2, MessageSquarePlus, Minus, MoreHorizontal, MousePointer2, Plus, ScanLine, Sparkles, Square, Trash2, Type, Undo2, X } from 'lucide-react'
import './workspace.css'

type Annotation = { id: number; title: string; text: string; x: number; y: number; kind: string }
const initialNotes: Annotation[] = [
  { id: 1, title: '标题层级', text: '主标题再大一些，保留现在的留白。副标题颜色调浅，让阅读顺序更清晰。', x: 26, y: 35, kind: '区域批注' },
  { id: 2, title: '按钮样式', text: '主按钮改成深绿色，圆角收小一点。文案改为「探索系列」。', x: 23, y: 65, kind: '区域批注' },
  { id: 3, title: '图片位置', text: '把椅子向右移动一点，给左侧文字留出更多呼吸空间。', x: 73, y: 50, kind: '移动建议' },
]

export function DemoArtwork() {
  return <svg className="artwork-scene" viewBox="0 0 800 480" role="img" aria-label="Morrow 北欧家居网站设计稿">
    <defs>
      <filter id="chair-shadow" x="-60%" y="-60%" width="220%" height="220%"><feGaussianBlur stdDeviation="9" /></filter>
      <clipPath id="hero-clip"><rect x="405" y="62" width="395" height="355" /></clipPath>
    </defs>
    <rect width="800" height="480" fill="#fffefa" />
    <text x="38" y="36" fill="#263a2c" fontFamily="Georgia,serif" fontSize="25" fontWeight="600" letterSpacing="-1.4">morrow<tspan fill="#b06344">.</tspan></text>
    <g fill="#647064" fontFamily="Arial,sans-serif" fontSize="9"><text x="324" y="32">COLLECTION</text><text x="421" y="32">OUR STORY</text><text x="513" y="32">JOURNAL</text><text x="715" y="32">BAG (0)</text></g>
    <path d="M0 62H800" stroke="#e9e9de" />
    <rect y="63" width="800" height="354" fill="#f0f1e8" />
    <text x="43" y="113" fill="#758071" fontFamily="Arial,sans-serif" fontSize="8" letterSpacing="2">CONSIDERED LIVING, EVERY DAY.</text>
    <text x="39" y="178" fill="#2c402f" fontFamily="Georgia,serif" fontSize="58" letterSpacing="-2.5">A quieter</text>
    <text x="39" y="235" fill="#2c402f" fontFamily="Georgia,serif" fontSize="58" letterSpacing="-2.5">kind of home.</text>
    <text x="43" y="272" fill="#7d8476" fontFamily="Arial,sans-serif" fontSize="10">Thoughtfully made objects. A little closer to nature.</text>
    <text x="43" y="289" fill="#7d8476" fontFamily="Arial,sans-serif" fontSize="10">Find your everyday, beautifully reimagined.</text>
    <rect x="43" y="316" width="134" height="37" rx="3" fill="#b67152" /><text x="59" y="339" fill="#fffefa" fontFamily="Arial,sans-serif" fontSize="9" letterSpacing=".6">EXPLORE THE EDIT</text><path d="m158 330 5 5-5 5m-8-5h13" stroke="#fffefa" fill="none" />
    <g clipPath="url(#hero-clip)">
      <path d="M498 63H800V417H498Z" fill="#e5e8dc" />
      <path d="M498 63H525V417H498Z" fill="#daddcf" />
      <circle cx="736" cy="139" r="73" fill="#d4ad81" />
      <path d="M579 348 800 286V417H405Z" fill="#dedecf" />
      <ellipse cx="643" cy="357" rx="105" ry="13" fill="#5e6652" opacity=".14" filter="url(#chair-shadow)" />
      <path d="M697 252 721 367M572 258 555 366" stroke="#845c3c" strokeWidth="9" strokeLinecap="round" />
      <path d="m573 259 125-8M562 322l146-2" stroke="#9d7955" strokeWidth="6" />
      <path d="M568 253 583 141Q586 124 607 127L680 141Q698 144 697 164L689 251Z" fill="#8d6546" />
      <path d="M578 247 591 149Q592 136 606 139L671 151Q684 154 682 168L675 245Z" fill="#b5a28a" />
      <path d="m593 155 79 14m-80-4 78 14m-80-4 78 14m-80-4 78 14m-80-4 78 14m-80-4 78 14m-80-4 78 14m-80-4 78 14" stroke="#d5c8b1" strokeWidth="3" opacity=".65" />
      <path d="M555 251Q616 232 697 246L712 274Q645 300 555 279Z" fill="#906d50" />
      <path d="M559 249Q619 230 694 245L700 267Q635 288 558 269Z" fill="#bbaa90" />
      <path d="M546 235 551 284M708 228 717 279" stroke="#805c40" strokeWidth="7" strokeLinecap="round" /><path d="m533 229 46 2m111-12 39 5" stroke="#a17952" strokeWidth="9" strokeLinecap="round" />
      <path d="M765 270q-11-86 8-144M773 201q-26-8-31-34 29 1 31 34m0-28q28-6 25-35-25 7-25 35m-6 71q-27-1-36-29 26-2 36 29" fill="#546449" />
      <path d="m747 280 40 0-7 52h-27Z" fill="#b67655" />
    </g>
    <g fill="#6d786a" fontFamily="Arial,sans-serif" fontSize="8" letterSpacing="1"><text x="42" y="452">MADE WITH INTENTION.</text><text x="319" y="452">NATURAL MATERIALS</text><text x="586" y="452">DESIGNED TO STAY.</text></g>
    <path d="m280 442 0 14m268-14v14" stroke="#e1e3d9" />
  </svg>
}

export default function Workspace({ onAnalyze, onToast }: { onAnalyze: () => void; onToast: (message: string) => void }) {
  const [notes, setNotes] = useState(initialNotes)
  const [selected, setSelected] = useState(1)
  const [tool, setTool] = useState('选择')
  const [zoom, setZoom] = useState(100)
  const [showMore, setShowMore] = useState(false)
  const [editing, setEditing] = useState<number | null>(null)
  const [imageUrl, setImageUrl] = useState('')
  const [filename, setFilename] = useState('morrow-homepage.png')
  const workspaceRoot = useRef<HTMLDivElement>(null)
  const fileInput = useRef<HTMLInputElement>(null)
  const nextId = useRef(4)
  useEffect(() => () => { if (imageUrl) URL.revokeObjectURL(imageUrl) }, [imageUrl])
  useEffect(() => {
    const importShortcut = (event: KeyboardEvent) => {
      const root = workspaceRoot.current
      if (!(event.ctrlKey || event.metaKey) || event.altKey || event.repeat || event.key.toLowerCase() !== 'i') return
      if (!root || root.closest('[hidden]') || root.getClientRects().length === 0) return
      event.preventDefault()
      fileInput.current?.click()
    }
    window.addEventListener('keydown', importShortcut)
    return () => window.removeEventListener('keydown', importShortcut)
  }, [])
  const addNote = (x = 52, y = 32) => {
    const id = nextId.current++
    setNotes(v => [...v, { id, title: '新的修改建议', text: '在这里写下你想调整的内容…', x, y, kind: tool === '箭头' ? '移动建议' : '区域批注' }])
    setSelected(id); setEditing(id)
  }
  const copyJson = async () => {
    try { await navigator.clipboard.writeText(JSON.stringify({ image: filename, annotations: notes }, null, 2)); onToast('批注 JSON 已复制') }
    catch { onToast('浏览器未允许访问剪贴板，请使用导出文件') }
  }
  const exportJson = () => {
    const url = URL.createObjectURL(new Blob([JSON.stringify({ image: filename, annotations: notes }, null, 2)], { type: 'application/json' }))
    const anchor = document.createElement('a'); anchor.href = url; anchor.download = 'edithere-feedback.json'; anchor.click(); URL.revokeObjectURL(url); setShowMore(false); onToast('已导出批注文件')
  }
  return <div ref={workspaceRoot} className="workspace-page">
    <div className="workspace-heading"><div><h1>批注工作台</h1><p>导入图片，添加批注并整理修改意见。</p></div><button className="workspace-import" onClick={() => fileInput.current?.click()}><ImagePlus size={16} />导入图片<span>Ctrl I</span></button></div>
    <input ref={fileInput} type="file" accept="image/*" hidden onChange={event => { const file = event.target.files?.[0]; if (!file) return; setImageUrl(URL.createObjectURL(file)); setFilename(file.name); setNotes([]); setZoom(100); onToast('图片已导入，选择工具后点击画面添加批注') }} />
    <section className="workspace-editor" aria-label="图片批注编辑器">
      <div className="editor-topbar"><div className="editor-file"><span className="editor-file-icon"><Layers2 size={15} /></span><span>{filename}</span><span className="editor-file-status"><Check size={12} />本地草稿</span></div><div className="editor-zoom"><button aria-label="缩小画面" onClick={() => setZoom(v => Math.max(60, v - 10))}><Minus size={14} /></button><span>{zoom}%</span><button aria-label="放大画面" onClick={() => setZoom(v => Math.min(140, v + 10))}><Plus size={14} /></button><span className="editor-small-divider" /><button aria-label="适应画布" onClick={() => setZoom(100)}><ScanLine size={15} /></button></div></div>
      <div className="workspace-editing-area">
        <div className="editor-canvas"><div className="editor-canvas-label"><span /><span>画布 01</span><span>·</span><span>{imageUrl ? '已导入图片' : '800 × 480 · 示例画面'}</span></div><div className={`editor-artboard ${tool !== '选择' ? 'editor-artboard-drawing' : ''}`} style={{ transform: `scale(${zoom / 100})` }} onClick={event => { if (tool === '选择') return; const bounds = event.currentTarget.getBoundingClientRect(); addNote(Math.min(94, Math.max(6, (event.clientX - bounds.left) / bounds.width * 100)), Math.min(90, Math.max(8, (event.clientY - bounds.top) / bounds.height * 100))); setTool('选择') }}>
          {imageUrl ? <img className="editor-imported-image" src={imageUrl} alt="导入的批注画面" /> : <DemoArtwork />}
          {notes.map(note => <button key={note.id} className={`annotation-pin annotation-pin-${note.id % 3} ${selected === note.id ? 'annotation-pin-selected' : ''}`} style={{ left: `${note.x}%`, top: `${note.y}%` }} aria-label={`选择批注 ${note.id}：${note.title}`} onClick={event => { event.stopPropagation(); setSelected(note.id) }}><span>{note.id}</span>{selected === note.id && <span className="annotation-selected-label">{note.title}</span>}</button>)}
          {selected === 1 && notes.some(note => note.id === 1) && !imageUrl && <div className="annotation-selection-outline" />}
        </div><div className="editor-canvas-hint">{tool === '选择' ? '点击编号，查看对应修改建议' : `已选择${tool}工具 · 点击画面添加批注`}</div></div>
        <aside className="annotation-sidebar"><div className="annotation-sidebar-heading"><div><MessageSquarePlus size={16} /><strong>修改建议</strong><span>{notes.length.toString().padStart(2, '0')}</span></div><button aria-label="添加批注" onClick={() => addNote()}><Plus size={16} /></button></div><div className="annotation-notes">{notes.map(note => <article className={`annotation-note ${selected === note.id ? 'annotation-note-selected' : ''}`} key={note.id} onClick={() => setSelected(note.id)}><div className="annotation-note-title"><span className="annotation-number">{note.id.toString().padStart(2, '0')}</span><strong>{note.title}</strong><button className="annotation-delete" aria-label={`删除批注 ${note.id}`} onClick={event => { event.stopPropagation(); setNotes(v => v.filter(n => n.id !== note.id)); if (selected === note.id) setSelected(0) }}><Trash2 size={13} /></button></div>{editing === note.id ? <textarea autoFocus aria-label={`编辑批注 ${note.id}`} value={note.text} onChange={event => setNotes(v => v.map(n => n.id === note.id ? { ...n, text: event.target.value } : n))} onBlur={() => setEditing(null)} /> : <button className="annotation-note-text" onClick={() => { setSelected(note.id); setEditing(note.id) }}>{note.text}</button>}<div className="annotation-note-meta"><Square size={10} /><span>{note.kind}</span><span>·</span><span>{Math.round(note.x)}%, {Math.round(note.y)}%</span></div></article>)}{notes.length === 0 && <div className="annotation-empty"><MessageSquarePlus size={24} /><p>给画面留一句建议</p><span>选择下方工具，点击你想修改的位置。</span></div>}</div><div className="annotation-sidebar-foot"><span className="annotation-status-dot" />所有改动仅保存在当前预览</div></aside>
      </div>
      <div className="editor-toolbar"><div className="editor-tools" role="toolbar" aria-label="批注工具">{[{ name: '选择', icon: MousePointer2 }, { name: '矩形', icon: Square }, { name: '箭头', icon: ArrowUpRight }, { name: '文字', icon: Type }].map(({ name, icon: Icon }) => <button key={name} aria-label={`${name}工具`} aria-pressed={tool === name} className={tool === name ? 'editor-tool-active' : ''} onClick={() => setTool(name)} title={name}><Icon size={17} /></button>)}<span className="editor-toolbar-divider" /><button aria-label={imageUrl ? '清空当前批注' : '恢复示例批注'} title={imageUrl ? '清空当前批注' : '恢复示例批注'} onClick={() => { setNotes(imageUrl ? [] : initialNotes); setSelected(imageUrl ? 0 : 1); setEditing(null); nextId.current = imageUrl ? 1 : 4; onToast(imageUrl ? '已清空当前图片的批注' : '已恢复三条示例批注') }}><Undo2 size={17} /></button></div><div className="editor-toolbar-actions"><div className="editor-json-wrap"><div className="editor-json-button"><button onClick={copyJson}><Code2 size={15} /><span>复制 JSON</span></button><button aria-label="JSON 更多选项" aria-expanded={showMore} onClick={() => setShowMore(v => !v)}><MoreHorizontal size={16} /></button></div>{showMore && <div className="editor-json-menu"><button onClick={exportJson}><ArrowDownToLine size={15} />导出 JSON 文件</button><button onClick={() => { setShowMore(false); copyJson() }}><Copy size={15} />复制完整批注</button><button onClick={() => setShowMore(false)}><X size={15} />关闭菜单</button></div>}</div><button className="editor-analyze" onClick={onAnalyze}><Sparkles size={16} />AI 解析<span><ArrowUpRight size={14} /></span></button></div></div>
    </section>
  </div>
}
