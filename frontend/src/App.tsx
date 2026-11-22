import { useEffect, useRef, useState } from 'react';
import * as Dialog from '@radix-ui/react-dialog';
import {
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
  buildFeedbackPayload,
  createInitialPages,
  createInitialProviders,
  INITIAL_GROUP_NOTES,
  previewShellBridge,
} from './bridge/mockBridge';
import type {
  GalleryPage,
  GeneralPreferences,
  GroupNote,
  ProviderConfig,
  ThemeMode,
  ViewId,
} from './bridge/types';
import WorkspaceView from './views/WorkspaceView';
import AnalysisView from './views/AnalysisView';
import ProvidersView from './views/ProvidersView';
import SettingsView from './views/SettingsView';

const NAV_ITEMS: Array<{ id: ViewId; label: string; icon: typeof LayoutGrid }> = [
  { id: 'workspace', label: '批注工作台', icon: LayoutGrid },
  { id: 'analysis', label: 'AI 解析', icon: Sparkles },
  { id: 'providers', label: '模型服务', icon: SlidersHorizontal },
];

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
  }, [theme, isDark]);

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
      const newPage: GalleryPage = {
        id,
        name: cleanName,
        routeHint: file.name,
        width: img.naturalWidth || 800,
        height: img.naturalHeight || 480,
        imageUrl: objectUrl,
        blocks: [
          {
            id: 'imported-main',
            label: '主体画面区',
            source: {
              x1: Math.round((img.naturalWidth || 800) * 0.1),
              y1: Math.round((img.naturalHeight || 480) * 0.12),
              x2: Math.round((img.naturalWidth || 800) * 0.9),
              y2: Math.round((img.naturalHeight || 480) * 0.85),
            },
            dx: 0,
            dy: 0,
          },
        ],
        annotations: [],
      };
      setPages((prev) => [...prev, newPage]);
      setActivePageId(id);
      setView('workspace');
      notify(`已添加页面「${cleanName}」(${newPage.width}×${newPage.height})`);
    };
    img.src = objectUrl;
  };

  const handleCompleteAgent = async () => {
    const payload = buildFeedbackPayload(pages, groupNotes);
    const receipt = await previewShellBridge.submitAgentSession(payload);
    notify(`${receipt.message}（已带 ${payload.pages.length} 个有批注页 + ${payload.groupNotes.length} 条整组意见）`);
  };

  const handleCancelAgent = async () => {
    const receipt = await previewShellBridge.cancelAgentSession();
    notify(receipt.message);
  };

  const defaultProvider = providers.find((p) => p.id === defaultProviderId) ?? providers[0];
  const totalNotesCount = pages.reduce((acc, p) => acc + p.annotations.length, 0) + groupNotes.length;

  return (
    <div className={`app-shell ${collapsed ? 'sidebar-collapsed' : ''} ${preferences.compactDensity ? 'density-compact' : ''}`}>
      {/* Clean Unified Top Bar */}
      <header className="topbar">
        <div className="topbar-left">
          <div className="brand-mark">
            <span className="brand-icon">
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" aria-hidden="true">
                <path d="M5 4H2v18h18v-3M7 13l4 4L22 6l-4-4L7 13Z" stroke="currentColor" strokeWidth="2" strokeLinejoin="round" />
              </svg>
            </span>
            <span>EditHere · 改这里</span>
          </div>
          <span className="topbar-divider" />
          <span className="topbar-meta">
            <span>B2 统一前端空壳</span>
            <span>·</span>
            <span>{pages.length} 页组图</span>
            <span>·</span>
            <span>{totalNotesCount} 条意见</span>
          </span>
        </div>

        <div className="topbar-right">
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

      {/* Collapsible Navigation Rail */}
      <aside className="app-sidebar">
        <div className="sidebar-top-row">
          <span className="sidebar-section-label">工作区</span>
          <button
            className="icon-button"
            aria-label={collapsed ? '展开导航栏' : '收起导航栏'}
            onClick={() => setCollapsed((c) => !c)}
          >
            {collapsed ? <Menu size={16} /> : <ChevronLeft size={16} />}
          </button>
        </div>

        <nav className="primary-navigation" aria-label="主导航">
          {NAV_ITEMS.map(({ id, label, icon: Icon }) => (
            <button
              key={id}
              className={`nav-item ${view === id ? 'active' : ''}`}
              aria-current={view === id ? 'page' : undefined}
              title={label}
              onClick={() => setView(id)}
            >
              <Icon size={18} />
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
            <Settings2 size={18} />
            <span>设置与诊断</span>
          </button>
          <button className="nav-item" title="关于 B2 架构" onClick={() => setAboutOpen(true)}>
            <CircleHelp size={18} />
            <span>关于</span>
          </button>
          <div className="sidebar-footer">
            <span className="sidebar-footer-version">0.14 B2 Shell</span>
            <button
              className="icon-button"
              aria-label={isDark ? '切换浅色' : '切换深色'}
              title={isDark ? '切换浅色' : '切换深色'}
              onClick={() => setTheme(isDark ? 'light' : 'dark')}
            >
              {isDark ? <Sun size={15} /> : <Moon size={15} />}
            </button>
          </div>
        </div>
      </aside>

      {/* Main Content Area */}
      <main className="app-main" style={{ overflow: view === 'workspace' ? 'hidden' : 'auto' }}>
        {view === 'workspace' && (
          <WorkspaceView
            pages={pages}
            activePageId={activePageId}
            groupNotes={groupNotes}
            onSelectPage={setActivePageId}
            onUpdatePages={setPages}
            onUpdateGroupNotes={setGroupNotes}
            onImportImage={handleImportImage}
            onAnalyze={() => setView('analysis')}
            onToast={notify}
          />
        )}
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
            onChangeTheme={setTheme}
            preferences={preferences}
            onChangePreferences={setPreferences}
            onToast={notify}
          />
        )}
      </main>

      {toast && (
        <div className="toast" role="status">
          <Check size={15} className="toast-ok" />
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
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 8 }}>
              <Dialog.Title className="dialog-title">EditHere · 0.14 B2 统一前端空壳</Dialog.Title>
              <Dialog.Close className="icon-button" aria-label="关闭">
                <X size={16} />
              </Dialog.Close>
            </div>
            <Dialog.Description className="dialog-desc">
              C++20/Qt6 原生截图·OCR·贴图底层 + 系统 WebView 统一 React/Radix 工作台
            </Dialog.Description>
            <p style={{ margin: '0 0 12px', fontSize: 13, lineHeight: 1.7, color: 'var(--secondary)' }}>
              当前为 <strong>里程碑 3 交互空壳</strong>，用于确认整体界面质感、组图工作台交互、外置批注徽章、实时大爆炸拖拽、无 TabBar 分组设置和深浅主题配色。确认满意后，下一步将把这套前端直接接入 C++ WebView2/WKWebView/WebKitGTK Bridge 与统一 Web/MCP 入口。
            </p>
            <p style={{ margin: 0, fontSize: 12, color: 'var(--muted)' }}>
              快捷键：<code>Ctrl + 1 / 2 / 3 / 4</code> 切换页面 · <code>Ctrl + I</code> 导入图片
            </p>
            <div style={{ display: 'flex', justifyContent: 'flex-end', marginTop: 16 }}>
              <Dialog.Close className="button primary">确认</Dialog.Close>
            </div>
          </Dialog.Content>
        </Dialog.Portal>
      </Dialog.Root>
    </div>
  );
}
