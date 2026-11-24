import { useEffect, useState } from 'react';
import {
  ArrowDownToLine,
  ArrowLeft,
  Check,
  Copy,
  FileText,
  LoaderCircle,
  RefreshCw,
  Settings2,
  ShieldCheck,
  Sparkles,
  Square,
} from 'lucide-react';
import { desktopRpc, isDesktopWebView } from '../bridge/desktopBridge';
import { SceneArtwork } from '../components/DemoScenes';
import { generateAnalysisText } from '../bridge/mockBridge';
import type { GalleryPage, GroupNote } from '../bridge/types';
import {
  attachRedactedPagesToPayload,
  composeBaseWithEditsToPng,
  loadBitmapForPage,
} from '../utils/canvasCompose';

interface AnalysisViewProps {
  pages: GalleryPage[];
  groupNotes: GroupNote[];
  providerId: string;
  providerName: string;
  hasDesktopPages: boolean;
  onOpenProviders: () => void;
  onBack: () => void;
  onToast: (message: string) => void;
}

export default function AnalysisView({
  pages,
  groupNotes,
  providerId,
  providerName,
  hasDesktopPages,
  onOpenProviders,
  onBack,
  onToast,
}: AnalysisViewProps) {
  const [selectedPageId, setSelectedPageId] = useState(pages[0]?.id ?? 'home');
  const [output, setOutput] = useState(() => generateAnalysisText(pages, groupNotes));
  const [busy, setBusy] = useState(false);
  const [sourceTab, setSourceTab] = useState<'image' | 'notes'>('image');
  const [redactedPreviewUrl, setRedactedPreviewUrl] = useState<string | null>(null);

  const selectedPage = pages.find((p) => p.id === selectedPageId) ?? pages[0];
  const totalNotes = pages.reduce((sum, p) => sum + p.annotations.length, 0) + groupNotes.length;
  const mosaicCount = selectedPage?.mosaics?.length ?? 0;
  const brushCount = selectedPage?.brushStrokes?.length ?? 0;

  useEffect(() => {
    setOutput(generateAnalysisText(pages, groupNotes));
  }, [pages, groupNotes]);

  useEffect(() => {
    let cancelled = false;
    if (!selectedPage || (mosaicCount === 0 && brushCount === 0)) {
      setRedactedPreviewUrl(null);
      return;
    }
    loadBitmapForPage(selectedPage).then((img) => {
      if (cancelled || !img) return;
      const rendered = composeBaseWithEditsToPng(selectedPage, img);
      if (rendered.dataUrl) {
        setRedactedPreviewUrl(rendered.dataUrl);
      }
    });
    return () => {
      cancelled = true;
    };
  }, [selectedPage?.id, selectedPage?.imageUrl, selectedPage?.mosaics, selectedPage?.brushStrokes, mosaicCount, brushCount]);

  const handleAnalyze = async () => {
    if (busy) {
      if (isDesktopWebView()) {
        await desktopRpc('ai.cancel').catch(() => {});
      }
      setBusy(false);
      return;
    }

    if (isDesktopWebView() && hasDesktopPages) {
      setBusy(true);
      try {
        const redactedPayload = await attachRedactedPagesToPayload(pages, groupNotes);
        const res = await desktopRpc<{
          ok?: boolean;
          text?: string;
          inputTokens?: number;
          outputTokens?: number;
        }>(
          'ai.analyze',
          {
            providerId,
            payload: redactedPayload,
          },
          180000
        );
        if (res?.text) {
          setOutput(res.text);
        }
        const tokHint =
          res?.inputTokens || res?.outputTokens
            ? `（输入 ${res.inputTokens ?? 0} / 输出 ${res.outputTokens ?? 0} tokens）`
            : '';
        onToast(`已完成「${providerName}」视觉解析${tokHint}`);
      } catch (err) {
        onToast(err instanceof Error ? err.message : 'AI 解析失败，请检查模型配置与 Key');
      } finally {
        setBusy(false);
      }
      return;
    }

    setBusy(true);
    setTimeout(() => {
      setOutput(generateAnalysisText(pages, groupNotes));
      setBusy(false);
      onToast(
        isDesktopWebView()
          ? '当前为示例画面，已从工作台本地批注生成结构化清单（截图或导入图片后可调用真实模型）'
          : '已结合工作台批注生成结构化修改清单'
      );
    }, 550);
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
          <p>自动对马赛克遮挡区域做防泄漏重绘后再交付模型，结合批注、画笔与布局位移输出结构化需求。</p>
        </div>
        <button className="button secondary sm" onClick={onBack}>
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

          <div className="analysis-source-tabs">
            <button
              className={`analysis-source-tab ${sourceTab === 'image' ? 'active' : ''}`}
              onClick={() => setSourceTab('image')}
            >
              画面预览
            </button>
            <button
              className={`analysis-source-tab ${sourceTab === 'notes' ? 'active' : ''}`}
              onClick={() => setSourceTab('notes')}
            >
              本页批注 ({selectedPage.annotations.length})
            </button>
          </div>

          {sourceTab === 'image' ? (
            <div className="analysis-preview-box">
              {redactedPreviewUrl ? (
                <img src={redactedPreviewUrl} alt={selectedPage.name} />
              ) : (
                <SceneArtwork sceneId={selectedPage.sceneId} imageUrl={selectedPage.imageUrl} />
              )}
              {mosaicCount > 0 && (
                <span className="badge success analysis-redacted-tag">
                  <ShieldCheck size={12} /> 已遮罩 {mosaicCount} 处敏感区域 · AI 不可见
                </span>
              )}
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
                backdropFilter: 'blur(8px)',
                color: 'var(--accent)',
              }}
            >
              <LoaderCircle size={17} className="spin" />
              <span>正在调用「{providerName}」分析画面与批注…</span>
            </div>
          )}

          <div className="card-foot">
            <span>{output.replace(/\s/g, '').length} 字 · Markdown 纯文本</span>
            <div style={{ display: 'flex', gap: 8 }}>
              <button className="button ghost sm" onClick={() => setOutput(generateAnalysisText(pages, groupNotes))}>
                <RefreshCw size={13} /> 从工作台同步
              </button>
              <button className="button ghost sm" onClick={() => void exportMarkdown()}>
                <ArrowDownToLine size={13} /> 导出 .md
              </button>
            </div>
          </div>
        </section>
      </div>

      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginTop: 16, gap: 12, flexWrap: 'wrap' }}>
        <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6, fontSize: 12, color: 'var(--muted)' }}>
          <Check size={14} />
          发送给模型前已烘焙马赛克遮罩像素，敏感信息不会泄露给 AI
        </span>
        <div style={{ display: 'flex', gap: 8 }}>
          <button className="button secondary" onClick={() => void handleAnalyze()}>
            {busy ? <Square size={13} /> : <Sparkles size={14} />}
            {busy ? '停止解析' : isDesktopWebView() && hasDesktopPages ? 'AI 重新解析' : '本地生成清单'}
          </button>
          <button className="button primary" onClick={() => void copyText()}>
            <Copy size={14} /> 复制修改说明
          </button>
        </div>
      </div>
    </div>
  );
}
