import { useEffect, useRef, useState } from 'react';
import * as Dialog from '@radix-ui/react-dialog';
import {
  Camera,
  Check,
  ChevronLeft,
  CircleHelp,
  LayoutGrid,
  Menu,
  Moon,
  Send,
  Settings2,
  SlidersHorizontal,
  Sparkles,
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
  ThemeMode,
  ViewId,
} from './bridge/types';
import { detectImageRegions } from './utils/regionDetector';
import WorkspaceView from './views/WorkspaceView';
import AnalysisView from './views/AnalysisView';
import ProvidersView from './views/ProvidersView';
import SettingsView from './views/SettingsView';

const NAV_ITEMS: Array<{ id: ViewId; label: string; icon: typeof LayoutGrid }> = [
  { id: 'workspace', label: '批注工作台', icon: LayoutGrid },
  { id: 'analysis', label: 'AI 解析', icon: Sparkles },
  { id: 'providers', label: '模型服务', icon: SlidersHorizontal },
];

const VIEW_TITLES: Record<ViewId, string> = {
  workspace: '批注工作台',
  analysis: 'AI 解析',
  providers: '模型服务',
  settings: '设置与诊断',
};

export default function App() {
  const [view, setView] = useState<ViewId>('workspace');
  const [agentMode, setAgentMode] = useState(true);
  const [collapsed, setCollapsed] = useState(false);
  const [aboutOpen, setAboutOpen] = useState(false);
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

    const applyDesktopState = (state: Record<string, unknown>) => {
      if (typeof state.agentMode === 'boolean') {
        setAgentMode(state.agentMode);
      }
      if (state.doctor && typeof state.doctor === 'object') {
        setDoctor(state.doctor as DoctorSnapshot);
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
      if (Array.isArray(state.pages) && state.pages.length > 0) {
        const nextPages = state.pages as GalleryPage[];
        setPages(nextPages);
        const requestedActive = typeof state.activePageId === 'string' ? state.activePageId : '';
        setActivePageId(requestedActive || nextPages[0].id);
      }
      if (Array.isArray(state.groupNotes)) {
        setGroupNotes(state.groupNotes as GroupNote[]);
      }
    };

    const unsub = subscribeDesktopEvents((eventName, data) => {
      if (eventName === 'session.state') {
        applyDesktopState(data);
      } else if (eventName === 'view.navigate') {
        const v = data.view;
        if (v === 'workspace' || v === 'analysis' || v === 'providers' || v === 'settings') {
          setView(v);
        }
      }
    });

    desktopRpc<Record<string, unknown>>('bridge.handshake')
      .then((res) => {
        setDesktopConnected(true);
        applyDesktopState(res);
      })
      .catch((err) => {
        console.warn('[EditHere Bridge] handshake failed:', err);
      });

    return unsub;
  }, []);

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      if ((event.ctrlKey || event.metaKey) && ['1', '2', '3', '4'].includes(event.key)) {
        event.preventDefault();
        const order: ViewId[] = ['workspace', 'analysis', 'providers', 'settings'];
        setView(order[Number(event.key) - 1]);
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
        const res = await desktopRpc<{ annotations?: number }>('agent.complete', {
          payload: { pages, groupNotes },
        }, 0);
        notify(`已写入批注并提交返回 AI（共 ${res.annotations ?? totalNotesCount} 条）`);
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
    <div className={`app-shell ${collapsed ? 'sidebar-collapsed' : ''} ${preferences.compactDensity ? 'density-compact' : ''}`}>
      {/* Full-Height macOS Vibrancy Sidebar */}
      <aside className="app-sidebar">
        <div className="sidebar-brand-row">
          <button
            className="brand-lockup-btn"
            aria-label="EditHere 首页"
            onClick={() => setView('workspace')}
          >
            <img className="brand-squircle" src="./brand/edithere-macos.svg" alt="EditHere 图标" />
            <img
              className="brand-wordmark"
              src={isDark ? './brand/edithere-wordmark-light.svg' : './brand/edithere-wordmark.svg'}
              alt="EditHere"
            />
          </button>
          <button
            className="icon-button"
            aria-label={collapsed ? '展开导航栏' : '收起导航栏'}
            title={collapsed ? '展开导航栏' : '收起导航栏'}
            onClick={() => setCollapsed((c) => !c)}
          >
            {collapsed ? <Menu size={16} /> : <ChevronLeft size={16} />}
          </button>
        </div>

        <div className="sidebar-section-label">工作区</div>
        <nav className="primary-navigation" aria-label="主导航">
          {NAV_ITEMS.map(({ id, label, icon: Icon }) => (
            <button
              key={id}
              className={`nav-item ${view === id ? 'active' : ''}`}
              aria-current={view === id ? 'page' : undefined}
              title={label}
              onClick={() => setView(id)}
            >
              <Icon size={17} />
              <span>{label}</span>
            </button>
          ))}
        </nav>

        <div className="sidebar-bottom">
          <div className="sidebar-separator" />
          <button
            className={`nav-item ${view === 'settings' ? 'active' : ''}`}
            aria-current={view === 'settings' ? 'page' : undefined}
            title="设置与诊断"
            onClick={() => setView('settings')}
          >
            <Settings2 size={17} />
            <span>设置与诊断</span>
          </button>
          <button className="nav-item" title="关于 EditHere" onClick={() => setAboutOpen(true)}>
            <CircleHelp size={17} />
            <span>关于</span>
          </button>
          <div className="sidebar-footer">
            <span className="sidebar-footer-version">
              {desktopConnected ? '0.14 Desktop' : '0.14 B2 Shell'}
            </span>
            <button
              className="icon-button"
              aria-label={isDark ? '切换浅色' : '切换深色'}
              title={isDark ? '切换浅色' : '切换深色'}
              onClick={() => handleChangeTheme(isDark ? 'light' : 'dark')}
            >
              {isDark ? <Sun size={15} /> : <Moon size={15} />}
            </button>
          </div>
        </div>
      </aside>

      {/* Seamless Translucent Content Toolbar */}
      <header className="topbar">
        <div className="topbar-left">
          <span className="topbar-view-title">{VIEW_TITLES[view]}</span>
          <span className="topbar-divider" />
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
                providerName={defaultProvider?.name ?? 'OpenAI'}
                onOpenProviders={() => setView('providers')}
                onBack={() => setView('workspace')}
                onToast={notify}
              />
            )}
            {view === 'providers' && (
              <ProvidersView
                providers={providers}
                defaultProviderId={defaultProviderId}
                onUpdateProviders={setProviders}
                onChangeDefault={setDefaultProviderId}
                onToast={notify}
              />
            )}
            {view === 'settings' && (
              <SettingsView
                theme={theme}
                onChangeTheme={handleChangeTheme}
                preferences={preferences}
                onChangePreferences={handleChangePreferences}
                doctor={doctor}
                desktopConnected={desktopConnected}
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

      <Dialog.Root open={aboutOpen} onOpenChange={setAboutOpen}>
        <Dialog.Portal>
          <Dialog.Overlay className="dialog-overlay" />
          <Dialog.Content className="dialog-content">
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 14 }}>
              <img
                src={isDark ? './brand/edithere-lockup-light.svg' : './brand/edithere-lockup.svg'}
                alt="EditHere"
                style={{ height: 28, width: 'auto' }}
              />
              <Dialog.Close className="icon-button" aria-label="关闭">
                <X size={16} />
              </Dialog.Close>
            </div>
            <Dialog.Title className="dialog-title">EditHere · 改这里</Dialog.Title>
            <Dialog.Description className="dialog-desc">
              C++20 / Qt6 原生截图 · OCR · 贴图核心 + WebView2 统一桌面工作台
            </Dialog.Description>
            <p style={{ margin: '0 0 12px', fontSize: 13, lineHeight: 1.7, color: 'var(--secondary)' }}>
              采用统一同心圆角几何系统（12 / 8 / 6 / 4px），支持全屏 HDR 截图、真实视觉切片级「大爆炸」拆解（棋盘格原位镂空、8 角点缩放、路径箭头及手动拉框切分）、原生区域 OCR、屏幕置顶贴图与 Agent 组图批注会话。
            </p>
            <p style={{ margin: 0, fontSize: 12, color: 'var(--muted)' }}>
              快捷键：<code>Ctrl + 1 / 2 / 3 / 4</code> 切换页面 · <code>Ctrl + I</code> 导入图片 · <code>Esc</code> 取消大爆炸选中
            </p>
            <div style={{ display: 'flex', justifyContent: 'flex-end', marginTop: 18 }}>
              <Dialog.Close className="button primary">完成</Dialog.Close>
            </div>
          </Dialog.Content>
        </Dialog.Portal>
      </Dialog.Root>
    </div>
  );
}
