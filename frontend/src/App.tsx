import { useEffect, useRef, useState } from 'react';
import {
  CaptureIcon,
  CheckIcon,
  CloseIcon,
  MoonIcon,
  SendIcon,
  SettingsIcon,
  SunIcon,
} from './components/AppIcons';
import {
  desktopRpc,
  isDesktopWebView,
  subscribeDesktopEvents,
} from './bridge/desktopBridge';
import {
  buildFeedbackPayload,
  createInitialPages,
  createInitialProviders,
  INITIAL_DIAGNOSTICS_INFO,
  INITIAL_DOCTOR_SNAPSHOT,
  INITIAL_GROUP_NOTES,
  INITIAL_PREFERENCES,
  previewShellBridge,
} from './bridge/mockBridge';
import type {
  DiagnosticsInfo,
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
  const [diagnosticsInfo, setDiagnosticsInfo] = useState<DiagnosticsInfo>(INITIAL_DIAGNOSTICS_INFO);
  const [desktopConnected, setDesktopConnected] = useState(false);
  const [hasDesktopPages, setHasDesktopPages] = useState(false);

  const [providers, setProviders] = useState<ProviderConfig[]>(() => createInitialProviders());
  const [defaultProviderId, setDefaultProviderId] = useState('openai');

  const [preferences, setPreferences] = useState<GeneralPreferences>(() => ({
    ...INITIAL_PREFERENCES,
    shortcuts: { ...INITIAL_PREFERENCES.shortcuts },
  }));

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

    const hydratePreferences = (p: Record<string, unknown>) => {
      if (p.theme === 'light' || p.theme === 'dark' || p.theme === 'system') {
        setTheme(p.theme);
      }
      setPreferences((prev) => ({
        ...prev,
        language:
          p.language === 'system' || p.language === 'zh_CN' || p.language === 'zh_TW' || p.language === 'en'
            ? p.language
            : prev.language,
        ocrLanguage:
          p.ocrLanguage === 'system' || p.ocrLanguage === 'zh_CN' || p.ocrLanguage === 'en'
            ? p.ocrLanguage
            : prev.ocrLanguage,
        captureOnStartup:
          typeof p.captureOnStartup === 'boolean' ? p.captureOnStartup : prev.captureOnStartup,
        launchAtLogin: typeof p.launchAtLogin === 'boolean' ? p.launchAtLogin : prev.launchAtLogin,
        launchAtLoginNotice:
          typeof p.launchAtLoginNotice === 'string' ? p.launchAtLoginNotice : prev.launchAtLoginNotice,
        fitImageOnOpen: typeof p.fitImageOnOpen === 'boolean' ? p.fitImageOnOpen : prev.fitImageOnOpen,
        confirmBeforeDiscard:
          typeof p.confirmBeforeDiscard === 'boolean' ? p.confirmBeforeDiscard : prev.confirmBeforeDiscard,
        defaultTool: typeof p.defaultTool === 'number' ? p.defaultTool : prev.defaultTool,
        traySingleClick: typeof p.traySingleClick === 'string' ? p.traySingleClick : prev.traySingleClick,
        trayDoubleClick: typeof p.trayDoubleClick === 'string' ? p.trayDoubleClick : prev.trayDoubleClick,
        trayActions: Array.isArray(p.trayActions)
          ? (p.trayActions as GeneralPreferences['trayActions'])
          : prev.trayActions,
        includeOcr: typeof p.includeOcr === 'boolean' ? p.includeOcr : prev.includeOcr,
        includeRecognizedText:
          typeof p.includeRecognizedText === 'boolean'
            ? p.includeRecognizedText
            : prev.includeRecognizedText,
        includeWindowTitle:
          typeof p.includeWindowTitle === 'boolean' ? p.includeWindowTitle : prev.includeWindowTitle,
        exportMode:
          p.exportMode === 'embedded' || p.exportMode === 'none' || p.exportMode === 'file'
            ? p.exportMode
            : prev.exportMode,
        copyJsonTextWithImage:
          typeof p.copyJsonTextWithImage === 'boolean'
            ? p.copyJsonTextWithImage
            : prev.copyJsonTextWithImage,
        captureShadow: typeof p.captureShadow === 'boolean' ? p.captureShadow : prev.captureShadow,
        captureCornerRadius:
          typeof p.captureCornerRadius === 'number' ? p.captureCornerRadius : prev.captureCornerRadius,
        feedbackDir: typeof p.feedbackDir === 'string' ? p.feedbackDir : prev.feedbackDir,
        scrollAxis: p.scrollAxis === 'horizontal' ? 'horizontal' : p.scrollAxis === 'vertical' ? 'vertical' : prev.scrollAxis,
        scrollAutoCrop: typeof p.scrollAutoCrop === 'boolean' ? p.scrollAutoCrop : prev.scrollAutoCrop,
        scrollUltraLong: typeof p.scrollUltraLong === 'boolean' ? p.scrollUltraLong : prev.scrollUltraLong,
        checkUpdatesOnStartup:
          typeof p.checkUpdatesOnStartup === 'boolean'
            ? p.checkUpdatesOnStartup
            : prev.checkUpdatesOnStartup,
        updateSource:
          p.updateSource === 'auto' ||
          p.updateSource === 'atomgit' ||
          p.updateSource === 'github' ||
          p.updateSource === 'gitee'
            ? p.updateSource
            : prev.updateSource,
        shortcuts:
          p.shortcuts && typeof p.shortcuts === 'object'
            ? (p.shortcuts as Record<string, string>)
            : prev.shortcuts,
        shortcutDefinitions: Array.isArray(p.shortcutDefinitions)
          ? (p.shortcutDefinitions as GeneralPreferences['shortcutDefinitions'])
          : prev.shortcutDefinitions,
      }));
    };

    const applyDesktopState = (state: Record<string, unknown>) => {
      if (typeof state.agentMode === 'boolean') {
        setAgentMode(state.agentMode);
      }
      if (state.doctor && typeof state.doctor === 'object') {
        setDoctor(state.doctor as DoctorSnapshot);
      }
      if (state.diagnostics && typeof state.diagnostics === 'object') {
        setDiagnosticsInfo(state.diagnostics as DiagnosticsInfo);
      }
      if (state.ai && typeof state.ai === 'object') {
        applyAiState(state.ai as Record<string, unknown>);
      }
      if (state.preferences && typeof state.preferences === 'object') {
        hydratePreferences(state.preferences as Record<string, unknown>);
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
        } else if (v === 'diagnostics' || v === 'about') {
          setSettingsTab('diagnostics');
          setView('settings');
        } else if (v === 'shortcuts') {
          setSettingsTab('shortcuts');
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
      if ((event.target as HTMLElement | null)?.closest('.shortcut-capture-btn.recording')) {
        return;
      }
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
      desktopRpc('settings.save', { theme: nextTheme }).catch((err) => {
        notify(err instanceof Error ? err.message : '切换主题失败');
      });
    }
  };

  const handleSaveSettingsPatch = async (patch: Record<string, unknown>): Promise<{ ok: boolean; error?: string }> => {
    if (!isDesktopWebView()) {
      setPreferences((prev) => ({
        ...prev,
        ...(patch as Partial<GeneralPreferences>),
      }));
      return { ok: true };
    }
    try {
      const updated = await desktopRpc<Record<string, unknown>>('settings.save', patch);
      setPreferences((prev) => ({
        ...prev,
        language:
          updated.language === 'system' ||
          updated.language === 'zh_CN' ||
          updated.language === 'zh_TW' ||
          updated.language === 'en'
            ? updated.language
            : prev.language,
        ocrLanguage:
          updated.ocrLanguage === 'system' ||
          updated.ocrLanguage === 'zh_CN' ||
          updated.ocrLanguage === 'en'
            ? updated.ocrLanguage
            : prev.ocrLanguage,
        captureOnStartup:
          typeof updated.captureOnStartup === 'boolean'
            ? updated.captureOnStartup
            : prev.captureOnStartup,
        launchAtLogin:
          typeof updated.launchAtLogin === 'boolean' ? updated.launchAtLogin : prev.launchAtLogin,
        launchAtLoginNotice:
          typeof updated.launchAtLoginNotice === 'string'
            ? updated.launchAtLoginNotice
            : prev.launchAtLoginNotice,
        fitImageOnOpen:
          typeof updated.fitImageOnOpen === 'boolean' ? updated.fitImageOnOpen : prev.fitImageOnOpen,
        confirmBeforeDiscard:
          typeof updated.confirmBeforeDiscard === 'boolean'
            ? updated.confirmBeforeDiscard
            : prev.confirmBeforeDiscard,
        defaultTool: typeof updated.defaultTool === 'number' ? updated.defaultTool : prev.defaultTool,
        traySingleClick:
          typeof updated.traySingleClick === 'string' ? updated.traySingleClick : prev.traySingleClick,
        trayDoubleClick:
          typeof updated.trayDoubleClick === 'string' ? updated.trayDoubleClick : prev.trayDoubleClick,
        includeOcr: typeof updated.includeOcr === 'boolean' ? updated.includeOcr : prev.includeOcr,
        includeRecognizedText:
          typeof updated.includeRecognizedText === 'boolean'
            ? updated.includeRecognizedText
            : prev.includeRecognizedText,
        includeWindowTitle:
          typeof updated.includeWindowTitle === 'boolean'
            ? updated.includeWindowTitle
            : prev.includeWindowTitle,
        exportMode:
          updated.exportMode === 'embedded' ||
          updated.exportMode === 'none' ||
          updated.exportMode === 'file'
            ? updated.exportMode
            : prev.exportMode,
        copyJsonTextWithImage:
          typeof updated.copyJsonTextWithImage === 'boolean'
            ? updated.copyJsonTextWithImage
            : prev.copyJsonTextWithImage,
        captureShadow:
          typeof updated.captureShadow === 'boolean' ? updated.captureShadow : prev.captureShadow,
        captureCornerRadius:
          typeof updated.captureCornerRadius === 'number'
            ? updated.captureCornerRadius
            : prev.captureCornerRadius,
        feedbackDir: typeof updated.feedbackDir === 'string' ? updated.feedbackDir : prev.feedbackDir,
        scrollAxis: updated.scrollAxis === 'horizontal' ? 'horizontal' : 'vertical',
        scrollAutoCrop:
          typeof updated.scrollAutoCrop === 'boolean' ? updated.scrollAutoCrop : prev.scrollAutoCrop,
        scrollUltraLong:
          typeof updated.scrollUltraLong === 'boolean' ? updated.scrollUltraLong : prev.scrollUltraLong,
        checkUpdatesOnStartup:
          typeof updated.checkUpdatesOnStartup === 'boolean'
            ? updated.checkUpdatesOnStartup
            : prev.checkUpdatesOnStartup,
        updateSource:
          updated.updateSource === 'auto' ||
          updated.updateSource === 'atomgit' ||
          updated.updateSource === 'github' ||
          updated.updateSource === 'gitee'
            ? updated.updateSource
            : prev.updateSource,
        shortcuts:
          updated.shortcuts && typeof updated.shortcuts === 'object'
            ? (updated.shortcuts as Record<string, string>)
            : prev.shortcuts,
      }));
      return { ok: true };
    } catch (err) {
      const msg = err instanceof Error ? err.message : '保存设置失败';
      notify(msg);
      return { ok: false, error: msg };
    }
  };

  const handleResetShortcuts = async () => {
    if (!isDesktopWebView()) {
      setPreferences((prev) => ({
        ...prev,
        shortcuts: { ...INITIAL_PREFERENCES.shortcuts },
      }));
      notify('已恢复默认快捷键');
      return;
    }
    try {
      const updated = await desktopRpc<Record<string, unknown>>('settings.resetShortcuts');
      if (updated.shortcuts && typeof updated.shortcuts === 'object') {
        setPreferences((prev) => ({ ...prev, shortcuts: updated.shortcuts as Record<string, string> }));
      }
      notify('已恢复默认快捷键');
    } catch (err) {
      notify(err instanceof Error ? err.message : '恢复默认快捷键失败');
    }
  };

  const handleResetDefaults = async () => {
    if (!isDesktopWebView()) {
      setTheme('system');
      setPreferences({ ...INITIAL_PREFERENCES, shortcuts: { ...INITIAL_PREFERENCES.shortcuts } });
      notify('已恢复全部默认设置');
      return;
    }
    try {
      const updated = await desktopRpc<Record<string, unknown>>('settings.resetDefaults');
      if (updated.theme === 'light' || updated.theme === 'dark' || updated.theme === 'system') {
        setTheme(updated.theme);
      }
      setPreferences((prev) => ({
        ...prev,
        language:
          updated.language === 'system' ||
          updated.language === 'zh_CN' ||
          updated.language === 'zh_TW' ||
          updated.language === 'en'
            ? updated.language
            : prev.language,
        ocrLanguage:
          updated.ocrLanguage === 'system' ||
          updated.ocrLanguage === 'zh_CN' ||
          updated.ocrLanguage === 'en'
            ? updated.ocrLanguage
            : prev.ocrLanguage,
        captureOnStartup:
          typeof updated.captureOnStartup === 'boolean'
            ? updated.captureOnStartup
            : prev.captureOnStartup,
        launchAtLogin:
          typeof updated.launchAtLogin === 'boolean' ? updated.launchAtLogin : prev.launchAtLogin,
        fitImageOnOpen:
          typeof updated.fitImageOnOpen === 'boolean' ? updated.fitImageOnOpen : prev.fitImageOnOpen,
        confirmBeforeDiscard:
          typeof updated.confirmBeforeDiscard === 'boolean'
            ? updated.confirmBeforeDiscard
            : prev.confirmBeforeDiscard,
        defaultTool: typeof updated.defaultTool === 'number' ? updated.defaultTool : prev.defaultTool,
        traySingleClick:
          typeof updated.traySingleClick === 'string' ? updated.traySingleClick : prev.traySingleClick,
        trayDoubleClick:
          typeof updated.trayDoubleClick === 'string' ? updated.trayDoubleClick : prev.trayDoubleClick,
        includeOcr: typeof updated.includeOcr === 'boolean' ? updated.includeOcr : prev.includeOcr,
        includeRecognizedText:
          typeof updated.includeRecognizedText === 'boolean'
            ? updated.includeRecognizedText
            : prev.includeRecognizedText,
        includeWindowTitle:
          typeof updated.includeWindowTitle === 'boolean'
            ? updated.includeWindowTitle
            : prev.includeWindowTitle,
        exportMode:
          updated.exportMode === 'embedded' ||
          updated.exportMode === 'none' ||
          updated.exportMode === 'file'
            ? updated.exportMode
            : prev.exportMode,
        copyJsonTextWithImage:
          typeof updated.copyJsonTextWithImage === 'boolean'
            ? updated.copyJsonTextWithImage
            : prev.copyJsonTextWithImage,
        captureShadow:
          typeof updated.captureShadow === 'boolean' ? updated.captureShadow : prev.captureShadow,
        captureCornerRadius:
          typeof updated.captureCornerRadius === 'number'
            ? updated.captureCornerRadius
            : prev.captureCornerRadius,
        feedbackDir: typeof updated.feedbackDir === 'string' ? updated.feedbackDir : prev.feedbackDir,
        scrollAxis: updated.scrollAxis === 'horizontal' ? 'horizontal' : 'vertical',
        scrollAutoCrop:
          typeof updated.scrollAutoCrop === 'boolean' ? updated.scrollAutoCrop : prev.scrollAutoCrop,
        scrollUltraLong:
          typeof updated.scrollUltraLong === 'boolean' ? updated.scrollUltraLong : prev.scrollUltraLong,
        checkUpdatesOnStartup:
          typeof updated.checkUpdatesOnStartup === 'boolean'
            ? updated.checkUpdatesOnStartup
            : prev.checkUpdatesOnStartup,
        updateSource:
          updated.updateSource === 'auto' ||
          updated.updateSource === 'atomgit' ||
          updated.updateSource === 'github' ||
          updated.updateSource === 'gitee'
            ? updated.updateSource
            : prev.updateSource,
        shortcuts:
          updated.shortcuts && typeof updated.shortcuts === 'object'
            ? (updated.shortcuts as Record<string, string>)
            : prev.shortcuts,
      }));
      notify('已恢复全部默认设置');
    } catch (err) {
      notify(err instanceof Error ? err.message : '恢复默认设置失败');
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
    <div className="app-shell">
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
            <CaptureIcon size={14} /> 屏幕截图
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
                <SendIcon size={14} /> 完成并返回 AI
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
            {isDark ? <SunIcon size={16} /> : <MoonIcon size={16} />}
          </button>

          <button
            className={`icon-button ${view === 'settings' ? 'is-active' : ''}`}
            aria-label="设置与关于"
            title="设置与关于 (Ctrl+,)"
            onClick={() => setView((cur) => (cur === 'settings' ? 'workspace' : 'settings'))}
          >
            <SettingsIcon size={16} />
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
              void handleSaveSettingsPatch({ includeOcr: !preferences.includeOcr })
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
                onSavePatch={handleSaveSettingsPatch}
                onResetShortcuts={handleResetShortcuts}
                onResetDefaults={handleResetDefaults}
                diagnosticsInfo={diagnosticsInfo}
                onUpdateDiagnosticsInfo={setDiagnosticsInfo}
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
          <CheckIcon size={14} className="toast-ok" />
          <span>{toast}</span>
          <button className="icon-button" aria-label="关闭提示" onClick={() => setToast('')}>
            <CloseIcon size={14} />
          </button>
        </div>
      )}
    </div>
  );
}
