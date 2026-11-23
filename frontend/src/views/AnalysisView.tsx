import { useEffect, useRef, useState } from 'react';
import { ArrowDownToLine, ArrowLeft, Check, Copy, FileText, LoaderCircle, RefreshCw, Settings2, Sparkles } from 'lucide-react';
import { desktopRpc, isDesktopWebView } from '../bridge/desktopBridge';
import { SceneArtwork } from '../components/DemoScenes';
import { generateAnalysisText } from '../bridge/mockBridge';
import type { GalleryPage, GroupNote } from '../bridge/types';

interface AnalysisViewProps {
  pages: GalleryPage[];
  groupNotes: GroupNote[];
  providerName: string;
  onOpenProviders: () => void;
  onBack: () => void;
  onToast: (message: string) => void;
}

export default function AnalysisView({
  pages,
  groupNotes,
  providerName,
  onOpenProviders,
  onBack,
  onToast,
}: AnalysisViewProps) {
  const [selectedPageId, setSelectedPageId] = useState(pages[0]?.id ?? 'home');
  const [output, setOutput] = useState(() => generateAnalysisText(pages, groupNotes));
  const [busy, setBusy] = useState(false);
  const [sourceTab, setSourceTab] = useState<'image' | 'notes'>('image');
  const timerRef = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);

  const selectedPage = pages.find((p) => p.id === selectedPageId) ?? pages[0];
  const totalNotes = pages.reduce((sum, p) => sum + p.annotations.length, 0) + groupNotes.length;

  useEffect(() => () => clearTimeout(timerRef.current), []);

  useEffect(() => {
    setOutput(generateAnalysisText(pages, groupNotes));
  }, [pages, groupNotes]);

  const runDemoParse = () => {
    if (busy) {
      clearTimeout(timerRef.current);
      setBusy(false);
      return;
    }
    setBusy(true);
    timerRef.current = setTimeout(() => {
      setOutput(generateAnalysisText(pages, groupNotes));
      setBusy(false);
      onToast('已结合工作台全部批注与位移生成修改清单');
    }, 900);
  };

  const copyText = async () => {
    try {
      if (isDesktopWebView()) {
        await desktopRpc('clipboard.copyText', { text: output });
      } else {
        await navigator.clipboard.writeText(output);
      }
      onToast('已复制修改说明纯文本');
    } catch {
      onToast('剪贴板受限，请直接手动复制文本框内容');
    }
  };

  const exportMarkdown = async () => {
    if (isDesktopWebView()) {
      try {
        const res = await desktopRpc<{ saved?: boolean }>('file.saveText', {
          defaultName: 'EditHere-修改清单.md',
          text: output,
        }, 0);
        if (res.saved) onToast('已导出 EditHere-修改清单.md');
      } catch (err) {
        onToast(err instanceof Error ? err.message : '导出失败');
      }
      return;
    }
    const url = URL.createObjectURL(new Blob([output], { type: 'text/markdown;charset=utf-8' }));
    const a = document.createElement('a');
    a.href = url;
    a.download = 'EditHere-修改清单.md';
    a.click();
    URL.revokeObjectURL(url);
    onToast('已导出 EditHere-修改清单.md');
  };

  return (
    <div className="page-container">
      <div className="page-heading">
        <div>
          <h1>AI 解析</h1>
          <p>把当前工作台的多页截图、位移变化与批注整理成纯文本修改需求，核对后直接交付给任意 Agent。</p>
        </div>
        <button className="button secondary" onClick={onBack}>
          <ArrowLeft size={14} /> 返回工作台
        </button>
      </div>

      <div className="analysis-layout">
        <section className="analysis-card">
          <div className="card-head">
            <h2>输入上下文</h2>
            <span className="badge neutral">{totalNotes} 条意见</span>
          </div>

          <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6, padding: '10px 14px', borderBottom: '1px solid var(--line)' }}>
            {pages.map((p) => (
              <button
                key={p.id}
                className={`button sm ${p.id === selectedPage.id ? 'primary' : 'secondary'}`}
                onClick={() => setSelectedPageId(p.id)}
              >
                {p.name} ({p.annotations.length})
              </button>
            ))}
          </div>

          <div style={{ display: 'flex', gap: 16, padding: '10px 16px 0', borderBottom: '1px solid var(--line)' }}>
            <button
              style={{
                border: 0,
                background: 'none',
                padding: '0 0 8px',
                borderBottom: sourceTab === 'image' ? '2px solid var(--accent)' : '2px solid transparent',
                color: sourceTab === 'image' ? 'var(--ink)' : 'var(--muted)',
                fontWeight: sourceTab === 'image' ? 600 : 400,
              }}
              onClick={() => setSourceTab('image')}
            >
              画面预览
            </button>
            <button
              style={{
                border: 0,
                background: 'none',
                padding: '0 0 8px',
                borderBottom: sourceTab === 'notes' ? '2px solid var(--accent)' : '2px solid transparent',
                color: sourceTab === 'notes' ? 'var(--ink)' : 'var(--muted)',
                fontWeight: sourceTab === 'notes' ? 600 : 400,
              }}
              onClick={() => setSourceTab('notes')}
            >
              本页批注 ({selectedPage.annotations.length})
            </button>
          </div>

          {sourceTab === 'image' ? (
            <div style={{ margin: 14, border: '1px solid var(--line)', borderRadius: 'var(--r-control)', overflow: 'hidden', aspectRatio: '5/3' }}>
              <SceneArtwork sceneId={selectedPage.sceneId} imageUrl={selectedPage.imageUrl} />
            </div>
          ) : (
            <div style={{ padding: 14, display: 'flex', flexDirection: 'column', gap: 10, minHeight: 185 }}>
              {selectedPage.annotations.length === 0 ? (
                <p style={{ margin: 0, color: 'var(--muted)', fontSize: 12 }}>当前页面暂无批注。</p>
              ) : (
                selectedPage.annotations.map((n) => (
                  <div key={n.id} style={{ display: 'flex', gap: 8, fontSize: 12, lineHeight: 1.55 }}>
                    <span className="badge accent">{n.number}</span>
                    <span>{n.text || '（纯布局位置调整）'}</span>
                  </div>
                ))
              )}
            </div>
          )}

          <div style={{ margin: '0 14px 14px', paddingTop: 12, borderTop: '1px solid var(--line)', display: 'flex', flexDirection: 'column', gap: 10 }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 8, color: 'var(--muted)', fontSize: 12 }}>
              <FileText size={15} />
              <span>{selectedPage.name} · {selectedPage.width}×{selectedPage.height}</span>
            </div>
            <button
              className="button secondary"
              style={{ justifyContent: 'space-between', width: '100%' }}
              onClick={onOpenProviders}
            >
              <span>解析模型：{providerName}</span>
              <Settings2 size={14} />
            </button>
          </div>
        </section>

        <section className="analysis-card" style={{ position: 'relative' }}>
          <div className="card-head">
            <h2>结构化修改清单</h2>
            <span className="badge success">可编辑 · 实时同步工作台</span>
          </div>

          <textarea
            className="analysis-textarea"
            aria-label="可编辑的修改清单"
            spellCheck={false}
            value={output}
            onChange={(e) => setOutput(e.target.value)}
          />

          {busy && (
            <div
              style={{
                position: 'absolute',
                inset: '45px 0 42px',
                display: 'flex',
                alignItems: 'center',
                justifyContent: 'center',
                gap: 8,
                background: 'var(--surface-elevated)',
                color: 'var(--accent)',
              }}
            >
              <LoaderCircle size={17} className="spin" />
              <span>正在结合图像与位移重新生成说明…</span>
            </div>
          )}

          <div className="card-foot">
            <span>{output.replace(/\s/g, '').length} 字 · Markdown 纯文本</span>
            <div style={{ display: 'flex', gap: 8 }}>
              <button className="button ghost sm" onClick={() => setOutput(generateAnalysisText(pages, groupNotes))}>
                <RefreshCw size={13} /> 从工作台同步
              </button>
              <button className="button ghost sm" onClick={exportMarkdown}>
                <ArrowDownToLine size={13} /> 导出 .md
              </button>
            </div>
          </div>
        </section>
      </div>

      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginTop: 16 }}>
        <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6, fontSize: 12, color: 'var(--muted)' }}>
          <Check size={14} /> 仅在点击「重新解析」时调用配置的模型接口；直接让 Agent 读图时无需使用此页
        </span>
        <div style={{ display: 'flex', gap: 8 }}>
          <button className="button secondary" onClick={runDemoParse}>
            {busy ? <LoaderCircle size={14} className="spin" /> : <Sparkles size={14} />}
            {busy ? '停止' : '演示重新解析'}
          </button>
          <button className="button primary" onClick={copyText}>
            <Copy size={14} /> 复制修改说明
          </button>
        </div>
      </div>
    </div>
  );
}
