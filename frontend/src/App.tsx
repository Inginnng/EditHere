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
            <Dialog.Title className="dialog-title">EditHere · 0.14 B2 统一前端交互原型</Dialog.Title>
            <Dialog.Description className="dialog-desc">
              C++20 / Qt6 原生截图·OCR·贴图核心 + 系统 WebView 统一 Apple 风格工作台
            </Dialog.Description>
            <p style={{ margin: '0 0 12px', fontSize: 13, lineHeight: 1.7, color: 'var(--secondary)' }}>
              当前已启用真实视觉切片级「大爆炸」交互（无虚假语义标签、棋盘格镂空原位、8 角点缩放、白色外描边路径箭头及空白处拉框切分）与本地像素连通域检测（导入本地图片时即时分割候选块）。确认视觉与交互细节后，将在 M4 正式接入 C++ WebView2/WKWebView/WebKitGTK Bridge。
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
