import { useEffect, useRef, useState } from 'react';
import {
  Camera,
  Check,
  Moon,
  Send,
  Settings,
  Sun,
  X,
} from 'lucide-react';
import {
  desktopRpc,
  isDesktopWebView,
  subscribeDesktopEvents,
} from './bridge/desktopBridge';
import {
  buildFeedbackPayload,
  createInitialPages,
  createInitialProviders,
  INITIAL_DOCTOR_SNAPSHOT,
  INITIAL_GROUP_NOTES,
  previewShellBridge,
} from './bridge/mockBridge';
import type {
  DoctorSnapshot,
  GalleryPage,
  GeneralPreferences,
  GroupNote,
  ProviderConfig,
  SettingsTabId,
  ThemeMode,
  ViewId,
} from './bridge/types';
import { attachRedactedPagesToPayload } from './utils/canvasCompose';
import { detectImageRegions } from './utils/regionDetector';
import WorkspaceView from './views/WorkspaceView';
import AnalysisView from './views/AnalysisView';
import SettingsView from './views/SettingsView';

const VIEW_TITLES: Record<ViewId, string> = {
  workspace: '批注工作台',
  analysis: 'AI 解析',
  settings: '设置',
};

export default function App() {
  const [view, setView] = useState<ViewId>('workspace');
  const [settingsTab, setSettingsTab] = useState<SettingsTabId>('general');
  const [agentMode, setAgentMode] = useState(true);
  const [toast, setToast] = useState('');
  const toastTimer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);

  const [theme, setTheme] = useState<ThemeMode>(() => {
    try {
      const saved = localStorage.getItem('edithere-b2-theme');
      return saved === 'light' || saved === 'dark' || saved === 'system' ? saved : 'light';
    } catch {
      return 'light';
    }
  });
  const [systemDark, setSystemDark] = useState(() => matchMedia('(prefers-color-scheme: dark)').matches);
  const isDark = theme === 'dark' || (theme === 'system' && systemDark);

  const [pages, setPages] = useState<GalleryPage[]>(() => createInitialPages());
  const [activePageId, setActivePageId] = useState<string>('home');
  const [groupNotes, setGroupNotes] = useState<GroupNote[]>(() => INITIAL_GROUP_NOTES);
  const [doctor, setDoctor] = useState<DoctorSnapshot>(INITIAL_DOCTOR_SNAPSHOT);
  const [desktopConnected, setDesktopConnected] = useState(false);
  const [hasDesktopPages, setHasDesktopPages] = useState(false);

  const [providers, setProviders] = useState<ProviderConfig[]>(() => createInitialProviders());
  const [defaultProviderId, setDefaultProviderId] = useState('openai');

  const [preferences, setPreferences] = useState<GeneralPreferences>({
    includeCoordinates: true,
    includeOcr: true,
    captureShadow: true,
    compactDensity: false,
    exportMode: 'file',
    language: '简体中文',
  });

  const notify = (msg: string) => {
    clearTimeout(toastTimer.current);
    setToast(msg);
    toastTimer.current = setTimeout(() => setToast(''), 3200);
  };

  useEffect(() => () => clearTimeout(toastTimer.current), []);

  useEffect(() => {
    const media = matchMedia('(prefers-color-scheme: dark)');
    const onChange = () => setSystemDark(media.matches);
    media.addEventListener('change', onChange);
    return () => media.removeEventListener('change', onChange);
  }, []);

  useEffect(() => {
    document.documentElement.dataset.theme = isDark ? 'dark' : 'light';
    try {
      localStorage.setItem('edithere-b2-theme', theme);
    } catch {
      // ignore storage errors
    }
    if (isDesktopWebView()) {
      desktopRpc('theme.sync', { isDark, theme }).catch(() => {});
    }
  }, [theme, isDark]);

  useEffect(() => {
    if (!isDesktopWebView()) return;

    const applyAiState = (ai: Record<string, unknown>) => {
      if (typeof ai.defaultProviderId === 'string' && ai.defaultProviderId) {
        setDefaultProviderId(ai.defaultProviderId);
      }
      if (Array.isArray(ai.providers) && ai.providers.length > 0) {
        setProviders(ai.providers as ProviderConfig[]);
      }
    };

    const applyDesktopState = (state: Record<string, unknown>) => {
      if (typeof state.agentMode === 'boolean') {
        setAgentMode(state.agentMode);
      }
      if (state.doctor && typeof state.doctor === 'object') {
        setDoctor(state.doctor as DoctorSnapshot);
      }
      if (state.ai && typeof state.ai === 'object') {
        applyAiState(state.ai as Record<string, unknown>);
      }
      if (state.preferences && typeof state.preferences === 'object') {
        const p = state.preferences as Record<string, unknown>;
        if (p.theme === 'light' || p.theme === 'dark' || p.theme === 'system') {
          setTheme(p.theme);
        }
        setPreferences((prev) => ({
          ...prev,
          includeOcr: typeof p.includeOcr === 'boolean' ? p.includeOcr : prev.includeOcr,
          captureShadow: typeof p.captureShadow === 'boolean' ? p.captureShadow : prev.captureShadow,
          exportMode: p.exportMode === 'embedded' ? 'embedded' : 'file',
          language:
            p.language === 'English' || p.language === '繁體中文' || p.language === '简体中文'
              ? p.language
              : prev.language,
        }));
      }
      if (typeof state.hasDesktopPages === 'boolean') {
        setHasDesktopPages(state.hasDesktopPages);
      }
      if (Array.isArray(state.pages) && state.pages.length > 0) {
        const nextPages = state.pages as GalleryPage[];
        setPages(nextPages);
        const requestedActive = typeof state.activePageId === 'string' ? state.activePageId : '';
        setActivePageId(requestedActive || nextPages[0].id);
        setHasDesktopPages(true);
      }
      if (Array.isArray(state.groupNotes)) {
        setGroupNotes(state.groupNotes as GroupNote[]);
      }
    };

    const unsub = subscribeDesktopEvents((eventName, data) => {
      if (eventName === 'session.state') {
        applyDesktopState(data);
      } else if (eventName === 'ai.state') {
        applyAiState(data);
      } else if (eventName === 'view.navigate') {
        const v = data.view;
        if (v === 'workspace' || v === 'analysis' || v === 'settings') {
          setView(v);
        } else if (v === 'providers') {
          setSettingsTab('providers');
          setView('settings');
        }
      }
    });

    desktopRpc<Record<string, unknown>>('bridge.handshake')
      .then((res) => {
        setDesktopConnected(true);
        applyDesktopState(res);
        return desktopRpc<Record<string, unknown>>('ai.getState').then(applyAiState).catch(() => {});
      })
      .catch((err) => {
        console.warn('[EditHere Bridge] handshake failed:', err);
      });

    return unsub;
  }, []);

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.ctrlKey || event.metaKey) {
        if (event.key === '1') {
          event.preventDefault();
          setView('workspace');
        } else if (event.key === '2') {
          event.preventDefault();
          setView('analysis');
        } else if (event.key === '3' || event.key === ',') {
          event.preventDefault();
          setView('settings');
        }
      }
    };
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, []);

  const handleImportImage = (file: File) => {
    const objectUrl = URL.createObjectURL(file);
    const img = new Image();
    img.onload = () => {
      const id = `import-${Date.now()}`;
      const cleanName = file.name.replace(/\.[^.]+$/, '') || file.name;
      const naturalW = img.naturalWidth || 800;
      const naturalH = img.naturalHeight || 480;
      const blocks = detectImageRegions(img, naturalW, naturalH);
      const newPage: GalleryPage = {
        id,
        name: cleanName,
        routeHint: file.name,
        width: naturalW,
        height: naturalH,
        imageUrl: objectUrl,
        blocks,
        annotations: [],
      };
      setPages((prev) => [...prev, newPage]);
      setActivePageId(id);
      setView('workspace');
      notify(`已导入「${cleanName}」(${naturalW}×${naturalH}，识别到 ${blocks.length} 个候选区域)`);
    };
    img.src = objectUrl;
  };

  const handleChangeTheme = (nextTheme: ThemeMode) => {
    setTheme(nextTheme);
    if (isDesktopWebView()) {
      desktopRpc('settings.save', { theme: nextTheme }).catch(() => {});
    }
  };

  const handleChangePreferences = (nextPrefs: GeneralPreferences) => {
    setPreferences(nextPrefs);
    if (isDesktopWebView()) {
      desktopRpc('settings.save', { ...nextPrefs }).catch(() => {});
    }
  };

  const handleSaveProviders = async (nextProviders: ProviderConfig[], nextDefaultId: string) => {
    setProviders(nextProviders);
    setDefaultProviderId(nextDefaultId);
    if (!isDesktopWebView()) return;
    try {
      const res = await desktopRpc<Record<string, unknown>>('ai.saveProviders', {
        defaultProviderId: nextDefaultId,
        providers: nextProviders.map((p) => ({
          id: p.id,
          name: p.name,
          model: p.model,
          endpoint: p.endpoint,
          protocol: p.protocol,
          timeout: Number(p.timeout) || 120,
          note: p.note,
          key: p.key,
          keyEdited: Boolean(p.keyEdited),
        })),
      });
      if (Array.isArray(res?.providers)) {
        setProviders(res.providers as ProviderConfig[]);
      }
    } catch (err) {
      notify(err instanceof Error ? err.message : '保存模型服务配置失败');
    }
  };

  const handleTestProvider = async (cfg: ProviderConfig): Promise<{ ok: boolean; message: string }> => {
    if (!isDesktopWebView()) {
      await new Promise((r) => setTimeout(r, 550));
      return { ok: true, message: '连通性演示校验通过（未消耗真实额度）' };
    }
    try {
      const res = await desktopRpc<{ ok?: boolean; reply?: string }>(
        'ai.testConnection',
        {
          id: cfg.id,
          endpoint: cfg.endpoint,
          model: cfg.model,
          protocol: cfg.protocol,
          timeout: Number(cfg.timeout) || 120,
          key: cfg.key,
        },
        45000
      );
      return {
        ok: true,
        message: res?.reply ? `连接成功（响应：${res.reply}）` : '接口连通性校验通过',
      };
    } catch (err) {
      return {
        ok: false,
        message: err instanceof Error ? err.message : '连接校验失败',
      };
    }
  };

  const handleCaptureStart = async () => {
    if (!isDesktopWebView()) {
      notify('桌面客户端里将隐藏主窗并唤起 C++ HDR / 零延迟全屏截图');
      return;
    }
    try {
      await desktopRpc('capture.start');
    } catch (err) {
      notify(err instanceof Error ? err.message : '启动截图失败');
    }
  };

  const handleRequestOpenFiles = async (): Promise<boolean> => {
    if (!isDesktopWebView()) return false;
    try {
      await desktopRpc('file.openDialog', {}, 0);
      setView('workspace');
      return true;
    } catch (err) {
      notify(err instanceof Error ? err.message : '打开文件失败');
      return true;
    }
  };

  const handleCompleteAgent = async () => {
    if (isDesktopWebView()) {
      try {
        const redactedPayload = await attachRedactedPagesToPayload(pages, groupNotes);
        const res = await desktopRpc<{ annotations?: number }>(
          'agent.complete',
          { payload: redactedPayload },
          0
        );
        notify(`已写入批注与脱敏画面并提交返回 AI（共 ${res.annotations ?? totalNotesCount} 条）`);
      } catch (err) {
        notify(err instanceof Error ? err.message : '提交返回失败');
      }
      return;
    }
    const payload = buildFeedbackPayload(pages, groupNotes);
    const receipt = await previewShellBridge.submitAgentSession(payload);
    notify(`${receipt.message}（已带 ${payload.pages.length} 个有批注页 + ${payload.groupNotes.length} 条整组意见）`);
  };

  const handleCancelAgent = async () => {
    if (isDesktopWebView()) {
      await desktopRpc('agent.cancel').catch(() => {});
      notify('已取消本次 AI 会话');
      return;
    }
    const receipt = await previewShellBridge.cancelAgentSession();
    notify(receipt.message);
  };

  const defaultProvider = providers.find((p) => p.id === defaultProviderId) ?? providers[0];
  const totalNotesCount = pages.reduce((acc, p) => acc + p.annotations.length, 0) + groupNotes.length;

  return (
    <div className={`app-shell ${preferences.compactDensity ? 'density-compact' : ''}`}>
      {/* Full-Width Seamless Translucent Window Header (Zero Left Sidebar) */}
      <header className="topbar">
        <div className="topbar-left">
          <button
            className="brand-lockup-btn"
            aria-label="EditHere 工作台"
            title="回到批注工作台"
            onClick={() => setView('workspace')}
          >
            <img className="brand-squircle" src="./brand/edithere-macos.svg" alt="EditHere 图标" />
            <img
              className="brand-wordmark"
              src={isDark ? './brand/edithere-wordmark-light.svg' : './brand/edithere-wordmark.svg'}
              alt="EditHere"
            />
          </button>
          <span className="topbar-divider" />
          <span className="topbar-view-title">{VIEW_TITLES[view]}</span>
          <span className="topbar-meta">
            <span>{pages.length} 页组图</span>
            <span>·</span>
            <span>{totalNotesCount} 条意见</span>
          </span>
        </div>

        <div className="topbar-right">
          <button className="button secondary sm" onClick={handleCaptureStart} title="截取屏幕画面">
            <Camera size={13} /> 屏幕截图
          </button>

          <div className="segmented-control" aria-label="工作模式切换">
            <button className={agentMode ? 'selected' : ''} onClick={() => setAgentMode(true)}>
              Agent 会话
            </button>
            <button className={!agentMode ? 'selected' : ''} onClick={() => setAgentMode(false)}>
              独立模式
            </button>
          </div>

          {agentMode && (
            <>
              <span className="session-pill">
                <span className="session-dot" />
                AI 等待你的批注
              </span>
              <button className="button secondary sm" onClick={handleCancelAgent}>
                取消
              </button>
              <button className="button primary sm" onClick={handleCompleteAgent}>
                <Send size={13} /> 完成并返回 AI
              </button>
            </>
          )}

          <span className="topbar-divider" />

          <button
            className="icon-button"
            aria-label={isDark ? '切换浅色' : '切换深色'}
            title={isDark ? '切换浅色外观' : '切换深色外观'}
            onClick={() => handleChangeTheme(isDark ? 'light' : 'dark')}
          >
            {isDark ? <Sun size={15} /> : <Moon size={15} />}
          </button>

          <button
            className={`icon-button ${view === 'settings' ? 'is-active' : ''}`}
            aria-label="设置、模型服务与关于"
            title="设置、模型服务与关于 (Ctrl+,)"
            onClick={() => setView((cur) => (cur === 'settings' ? 'workspace' : 'settings'))}
          >
            <Settings size={15} />
          </button>
        </div>
      </header>

      {/* Main Content Area */}
      <main className={`app-main ${view === 'workspace' ? 'is-workspace' : 'is-page-view'}`}>
        {view === 'workspace' && (
          <WorkspaceView
            pages={pages}
            activePageId={activePageId}
            groupNotes={groupNotes}
            includeOcr={preferences.includeOcr}
            onToggleOcr={() =>
              handleChangePreferences({ ...preferences, includeOcr: !preferences.includeOcr })
            }
            onCapture={handleCaptureStart}
            onSelectPage={setActivePageId}
            onUpdatePages={setPages}
            onUpdateGroupNotes={setGroupNotes}
            onImportImage={handleImportImage}
            onRequestImport={handleRequestOpenFiles}
            onAnalyze={() => setView('analysis')}
            onToast={notify}
          />
        )}
        {view !== 'workspace' && (
          <div className="page-sheet">
            {view === 'analysis' && (
              <AnalysisView
                pages={pages}
                groupNotes={groupNotes}
                providerId={defaultProvider?.id ?? 'openai'}
                providerName={defaultProvider?.name ?? 'OpenAI'}
                hasDesktopPages={hasDesktopPages}
                onOpenProviders={() => {
                  setSettingsTab('providers');
                  setView('settings');
                }}
                onBack={() => setView('workspace')}
                onToast={notify}
              />
            )}
            {view === 'settings' && (
              <SettingsView
                activeTab={settingsTab}
                onChangeTab={setSettingsTab}
                onBack={() => setView('workspace')}
                isDark={isDark}
                theme={theme}
                onChangeTheme={handleChangeTheme}
                preferences={preferences}
                onChangePreferences={handleChangePreferences}
                doctor={doctor}
                desktopConnected={desktopConnected}
                providers={providers}
                defaultProviderId={defaultProviderId}
                onSaveProviders={handleSaveProviders}
                onTestProvider={handleTestProvider}
                onToast={notify}
              />
            )}
          </div>
        )}
      </main>

      {toast && (
        <div className="toast" role="status">
          <Check size={14} className="toast-ok" />
          <span>{toast}</span>
          <button className="icon-button" aria-label="关闭提示" onClick={() => setToast('')}>
            <X size={13} />
          </button>
        </div>
      )}
    </div>
  );
}
