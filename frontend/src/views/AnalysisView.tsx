import { useI18n } from '../i18n';
import { useEffect, useState } from 'react';
import {
  AiSparkleIcon,
  BackIcon,
  CopyTextIcon,
  DocumentIcon,
  ExportFileIcon,
  SettingsIcon,
  SpinnerIcon,
  StopIcon,
  SyncIcon,
} from '../components/AppIcons';
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
  const t = useI18n();
  const [selectedPageId, setSelectedPageId] = useState(pages[0]?.id ?? 'home');
  const [output, setOutput] = useState(() => generateAnalysisText(pages, groupNotes, t));
  const [busy, setBusy] = useState(false);
  const [sourceTab, setSourceTab] = useState<'image' | 'notes'>('image');
  const [redactedPreviewUrl, setRedactedPreviewUrl] = useState<string | null>(null);

  const selectedPage = pages.find((p) => p.id === selectedPageId) ?? pages[0];
  const totalNotes = pages.reduce((sum, p) => sum + p.annotations.length, 0) + groupNotes.length;
  const mosaicCount = selectedPage?.mosaics?.length ?? 0;
  const brushCount = selectedPage?.brushStrokes?.length ?? 0;

  useEffect(() => {
    setOutput(generateAnalysisText(pages, groupNotes, t));
  }, [pages, groupNotes, t]);

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
            ? t("analysis.tokenCounts", { input: res.inputTokens ?? 0, output: res.outputTokens ?? 0 })
            : '';
        onToast(t("analysis.complete", { provider: providerName, tokens: tokHint }));
      } catch (err) {
        onToast(err instanceof Error ? err.message : t("analysis.aiAnalysisFailedCheckTheModelSettings"));
      } finally {
        setBusy(false);
      }
      return;
    }

    setBusy(true);
    setTimeout(() => {
      setOutput(generateAnalysisText(pages, groupNotes, t));
      setBusy(false);
      onToast(
        isDesktopWebView()
          ? t("analysis.generatedAChecklistFromLocalAnnotationsFor")
          : t("analysis.revisionChecklistGeneratedFromWorkspaceAnnotations")
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
      onToast(t("analysis.revisionChecklistCopiedAsPlainText"));
    } catch {
      onToast(t("analysis.clipboardAccessIsRestrictedCopyTheText"));
    }
  };

  const exportMarkdown = async () => {
    if (isDesktopWebView()) {
      try {
        const res = await desktopRpc<{ saved?: boolean }>('file.saveText', {
          defaultName: t("analysis.edithereRevisionChecklistMd"),
          text: output,
        }, 0);
        if (res.saved) onToast(t("analysis.exportedTheEdithereRevisionChecklist"));
      } catch (err) {
        onToast(err instanceof Error ? err.message : t("workspace.exportFailed"));
      }
      return;
    }
    const url = URL.createObjectURL(new Blob([output], { type: 'text/markdown;charset=utf-8' }));
    const a = document.createElement('a');
    a.href = url;
    a.download = t("analysis.edithereRevisionChecklistMd");
    a.click();
    URL.revokeObjectURL(url);
    onToast(t("analysis.exportedTheEdithereRevisionChecklist"));
  };

  return (
    <div className="page-container">
      <div className="page-heading">
        <div>
          <h1>{t("app.aiAnalysis")}</h1>
        </div>
        <button className="button secondary sm" onClick={onBack}>
          <BackIcon size={14} />{t("settings.backToWorkspace")}</button>
      </div>

      <div className="analysis-layout">
        <section className="analysis-card">
          <div className="card-head">
            <h2>{t("analysis.inputContext")}</h2>
            <span className="badge neutral">{t("app.noteCount", { count: totalNotes })}</span>
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
            >{t("analysis.imagePreview")}</button>
            <button
              className={`analysis-source-tab ${sourceTab === 'notes' ? 'active' : ''}`}
              onClick={() => setSourceTab('notes')}
            >{t("analysis.pageNotes", { count: selectedPage.annotations.length })}
            </button>
          </div>

          {sourceTab === 'image' ? (
            <div className="analysis-preview-box">
              {redactedPreviewUrl ? (
                <img src={redactedPreviewUrl} alt={selectedPage.name} />
              ) : (
                <SceneArtwork sceneId={selectedPage.sceneId} imageUrl={selectedPage.imageUrl} />
              )}
            </div>
          ) : (
            <div style={{ padding: 14, display: 'flex', flexDirection: 'column', gap: 10, minHeight: 185 }}>
              {selectedPage.annotations.length === 0 ? (
                <p style={{ margin: 0, color: 'var(--muted)', fontSize: 12 }}>{t("workspace.thisPageHasNoAnnotationsYet")}</p>
              ) : (
                selectedPage.annotations.map((n) => (
                  <div key={n.id} style={{ display: 'flex', gap: 8, fontSize: 12, lineHeight: 1.55 }}>
                    <span className="badge accent">{n.number}</span>
                    <span>{n.text || t("analysis.layoutAdjustmentOnly")}</span>
                  </div>
                ))
              )}
            </div>
          )}

          <div style={{ margin: '0 14px 14px', paddingTop: 12, borderTop: '1px solid var(--line)', display: 'flex', flexDirection: 'column', gap: 10 }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 8, color: 'var(--muted)', fontSize: 12 }}>
              <DocumentIcon size={15} />
              <span>{selectedPage.name} · {selectedPage.width}×{selectedPage.height}</span>
            </div>
            <button
              className="button secondary"
              style={{ justifyContent: 'space-between', width: '100%' }}
              onClick={onOpenProviders}
            >
              <span>{t("analysis.analysisModel", { provider: providerName })}</span>
              <SettingsIcon size={14} />
            </button>
          </div>
        </section>

        <section className="analysis-card" style={{ position: 'relative' }}>
          <div className="card-head">
            <h2>{t("analysis.revisionChecklist")}</h2>
            <span className="badge success">{t("analysis.editableSyncedWithWorkspace")}</span>
          </div>

          <textarea
            className="analysis-textarea"
            aria-label={t("analysis.editableRevisionChecklist")}
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
              <SpinnerIcon size={17} className="spin" />
              <span>{t("analysis.analyzing", { provider: providerName })}</span>
            </div>
          )}

          <div className="card-foot">
            <span>{t("analysis.characterCount", { count: output.replace(/\s/g, '').length })}</span>
            <div style={{ display: 'flex', gap: 8 }}>
              <button className="button ghost sm" onClick={() => setOutput(generateAnalysisText(pages, groupNotes, t))}>
                <SyncIcon size={14} />{t("analysis.syncFromWorkspace")}</button>
              <button className="button ghost sm" onClick={() => void exportMarkdown()}>
                <ExportFileIcon size={14} />{t("analysis.exportMd")}</button>
            </div>
          </div>
        </section>
      </div>

      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'flex-end', marginTop: 16, gap: 8, flexWrap: 'wrap' }}>
        <button className="button secondary" onClick={() => void handleAnalyze()}>
          {busy ? <StopIcon size={14} /> : <AiSparkleIcon size={14} />}
          {busy ? t("analysis.stopAnalysis") : isDesktopWebView() && hasDesktopPages ? t("analysis.runAiAnalysisAgain") : t("analysis.generateChecklistLocally")}
        </button>
        <button className="button primary" onClick={() => void copyText()}>
          <CopyTextIcon size={14} />{t("analysis.copyRevisionChecklist")}</button>
      </div>
    </div>
  );
}
