import { createTranslator, I18nProvider, LANGUAGE_LABEL_KEYS, resolveLocale } from './i18n';
import { canHandleShortcut, createShortcutMatcher } from './utils/shortcuts';
import { useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react';
import {
  CaptureIcon,
  CheckIcon,
  CloseIcon,
  MoonIcon,
  OpenFolderIcon,
  SendIcon,
  SettingsIcon,
  ShieldOkIcon,
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
  initialDoctorSnapshot,
  initialGroupNotes,
  localizePreviewWorkspace,
  initialPreferences,
  INITIAL_SKILLS_STATE,
  previewShellBridge,
  providerPresets,
} from './bridge/mockBridge';
import type {
  DiagnosticsInfo,
  DoctorSnapshot,
  ExplodeBlock,
  GalleryPage,
  GeneralPreferences,
  GroupNote,
  ProviderConfig,
  SettingsTabId,
  SkillSyncActionResult,
  SkillSyncState,
  ThemeMode,
  UpdateCheckResult,
  UpdateProgress,
  ViewId,
} from './bridge/types';
import { attachRedactedPagesToPayload } from './utils/canvasCompose';
import { detectImageRegions } from './utils/regionDetector';
import WorkspaceView from './views/WorkspaceView';
import AnalysisView from './views/AnalysisView';
import SettingsView from './views/SettingsView';

const VIEW_TITLES = {
  workspace: 'app.workspace',
  analysis: 'app.aiAnalysis',
  settings: 'app.settings',
} as const;

export default function App() {
  const [browserLocale, setBrowserLocale] = useState(() => navigator.language);
  const [preferences, setPreferences] = useState<GeneralPreferences>(() =>
    initialPreferences(createTranslator(resolveLocale('system', undefined, browserLocale)))
  );
  const locale = resolveLocale(preferences.language, preferences.effectiveLanguage, browserLocale);
  const t = useMemo(() => createTranslator(locale), [locale]);
  const localize = useMemo(() => ({
    t,
    list: new Intl.ListFormat(locale.replace('_', '-')),
  }), [locale, t]);
  const localizeRef = useRef(localize);
  localizeRef.current = localize;
  const previewDefaults = useMemo(() => initialPreferences(t), [t]);
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

  const [workspaceState, setWorkspaceState] = useState(() => ({
    pages: isDesktopWebView() ? [] : createInitialPages(t),
    groupNotes: isDesktopWebView() ? [] : initialGroupNotes(t),
  }));
  const { pages, groupNotes } = workspaceState;
  const [activePageId, setActivePageId] = useState(() => isDesktopWebView() ? '' : 'home');
  const workspaceGeneration = useRef(-1);
  const workspaceDirty = useRef(false);
  const workspace = useRef(workspaceState);
  const updateWorkspace = (update: (current: typeof workspaceState) => typeof workspaceState) => {
    workspace.current = update(workspace.current);
    setWorkspaceState(workspace.current);
  };
  const setPages = (update: (current: GalleryPage[]) => GalleryPage[]) =>
    updateWorkspace((current) => ({ ...current, pages: update(current.pages) }));
  const setGroupNotes = (update: (current: GroupNote[]) => GroupNote[]) =>
    updateWorkspace((current) => ({ ...current, groupNotes: update(current.groupNotes) }));

  const reportWorkspaceChange = () => {
    if (isDesktopWebView() && !workspaceDirty.current) {
      workspaceDirty.current = true;
      void desktopRpc('workspace.changed', { generation: workspaceGeneration.current }).catch(() => {});
    }
  };

  const prepareWorkspaceSnapshot = async (media?: HTMLElement | null) => {
    const generation = workspaceGeneration.current;
    const snapshot = workspace.current;
    workspaceDirty.current = false;
    const payload = await attachRedactedPagesToPayload(snapshot.pages, snapshot.groupNotes, media);
    return { ...payload, generation, changedSinceSnapshot: workspaceDirty.current };
  };

  const [updateStatus, setUpdateStatus] = useState<UpdateCheckResult | null>(null);
  const [updateProgress, setUpdateProgress] = useState<UpdateProgress>({ phase: 'idle' });
  const [doctor, setDoctor] = useState<DoctorSnapshot>(() => initialDoctorSnapshot(t));
  const [skillsState, setSkillsState] = useState<SkillSyncState>(INITIAL_SKILLS_STATE);
  const [syncingSkills, setSyncingSkills] = useState(false);
  const [skillBannerDismissed, setSkillBannerDismissed] = useState(false);
  const [diagnosticsInfo, setDiagnosticsInfo] = useState<DiagnosticsInfo>(INITIAL_DIAGNOSTICS_INFO);
  const [desktopConnected, setDesktopConnected] = useState(false);
  const desktopHandshake = useRef<Promise<Record<string, unknown>> | undefined>(undefined);
  const [hasDesktopPages, setHasDesktopPages] = useState(false);

  const [providers, setProviders] = useState<ProviderConfig[]>(() => createInitialProviders(t));
  const [defaultProviderId, setDefaultProviderId] = useState('openai');
  const previewLanguage = useRef(t);

  useLayoutEffect(() => {
    if (isDesktopWebView() || previewLanguage.current === t) return;
    const previous = previewLanguage.current;
    previewLanguage.current = t;
    updateWorkspace((current) => localizePreviewWorkspace(current, previous, t));
    setProviders((current) => {
      const previousNames = new Map(createInitialProviders(previous).map((provider) => [provider.id, provider.name]));
      const nextNames = new Map(createInitialProviders(t).map((provider) => [provider.id, provider.name]));
      return current.map((provider) => provider.name === previousNames.get(provider.id)
        ? { ...provider, name: nextNames.get(provider.id) ?? provider.name }
        : provider);
    });
  }, [t]);

  useEffect(() => {
    const onLanguageChange = () => setBrowserLocale(navigator.language);
    window.addEventListener('languagechange', onLanguageChange);
    return () => window.removeEventListener('languagechange', onLanguageChange);
  }, []);

  const notify = (msg: string, duration = 3200) => {
    clearTimeout(toastTimer.current);
    setToast(msg);
    toastTimer.current = setTimeout(() => setToast(''), duration);
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
    if (isDesktopWebView() && desktopConnected) {
      void desktopRpc('theme.sync', { isDark }).catch(() => {});
    }
  }, [theme, isDark, desktopConnected]);

  const hydratePreferences = (p: Record<string, unknown>) => {
    if (p.theme === 'light' || p.theme === 'dark' || p.theme === 'system') {
      setTheme(p.theme);
    }
    if (p.doctor && typeof p.doctor === 'object') {
      setDoctor(p.doctor as DoctorSnapshot);
    }
    setPreferences((prev) => ({
      ...prev,
      language:
        p.language === 'system' || p.language === 'zh_CN' || p.language === 'zh_TW' || p.language === 'en'
          ? p.language
          : prev.language,
      effectiveLanguage:
        p.effectiveLanguage === 'zh_CN' || p.effectiveLanguage === 'zh_TW' || p.effectiveLanguage === 'en'
          ? p.effectiveLanguage
          : prev.effectiveLanguage,
      includeCoordinates: typeof p.includeCoordinates === 'boolean' ? p.includeCoordinates : prev.includeCoordinates,
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
      autoSyncSkills:
        typeof p.autoSyncSkills === 'boolean' ? p.autoSyncSkills : prev.autoSyncSkills,
      showInstalledSkillsOnly:
        typeof p.showInstalledSkillsOnly === 'boolean' ? p.showInstalledSkillsOnly : prev.showInstalledSkillsOnly,
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

    const navigateFromDesktop = (v: unknown) => {
      if (v === 'workspace' || v === 'analysis' || v === 'settings') {
        setView(v);
      } else if (v === 'providers' || v === 'diagnostics' || v === 'about' || v === 'shortcuts') {
        setSettingsTab(v === 'about' ? 'diagnostics' : v);
        setView('settings');
      }
    };

    const applyDesktopState = (state: Record<string, unknown>) => {
      if (typeof state.agentMode === 'boolean') {
        setAgentMode(state.agentMode);
      }
      if (state.doctor && typeof state.doctor === 'object') {
        setDoctor(state.doctor as DoctorSnapshot);
      }
      if (state.skills && typeof state.skills === 'object') {
        setSkillsState(state.skills as SkillSyncState);
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
      if (Array.isArray(state.pages) && typeof state.workspaceGeneration === 'number') {
        const reset = workspaceGeneration.current !== state.workspaceGeneration;
        workspaceGeneration.current = state.workspaceGeneration;
        if (reset) workspaceDirty.current = false;
        const nextPages = state.pages as GalleryPage[];
        const requestedActive = typeof state.activePageId === 'string' ? state.activePageId : '';
        setPages((current) => {
          if (reset) return nextPages;
          const previous = new Map(current.map((page) => [page.id, page]));
          return nextPages.map((page) => {
            const edited = previous.get(page.id);
            return edited ? { ...page, ...edited, routeHint: page.routeHint,
              blocks: edited.blocks.length > 0 ? edited.blocks : page.blocks } : page;
          });
        });
        setActivePageId(requestedActive || nextPages[0]?.id || '');
        if (reset && Array.isArray(state.groupNotes)) {
          setGroupNotes(() => state.groupNotes as GroupNote[]);
        }
      }
      if (state.update && typeof state.update === 'object' && 'status' in state.update) {
        setUpdateStatus(state.update as UpdateCheckResult);
      }
      navigateFromDesktop(state.view);
    };

    const unsub = subscribeDesktopEvents((eventName, data) => {
      if (eventName === 'session.state') {
        applyDesktopState(data);
      } else if (eventName === 'page.blocksUpdated') {
        const targetId = typeof data.pageId === 'string' ? data.pageId : '';
        const nextBlocks = Array.isArray(data.blocks) ? (data.blocks as ExplodeBlock[]) : [];
        if (targetId && nextBlocks.length > 0) {
          setPages((prev) =>
            prev.map((p) => (p.id === targetId && p.blocks.length === 0 ? { ...p, blocks: nextBlocks } : p))
          );
        }
      } else if (eventName === 'ai.state') {
        applyAiState(data);
      } else if (eventName === 'skills.state') {
        setSkillsState(data as unknown as SkillSyncState);
      } else if (eventName === 'skills.autoSynced') {
        const { t, list } = localizeRef.current;
        const names = Array.isArray(data.updatedHosts)
          ? list.format(data.updatedHosts as string[])
          : '';
        const ver = typeof data.skillVersion === 'string' ? data.skillVersion : '';
        if (names) {
          notify(t("app.skillsAutoSynced", { hosts: names, versionSuffix: ver ? t("app.skillVersionSuffix", { version: ver }) : '' }));
        }
      } else if (eventName === 'workspace.trackChanges') {
        if (data.generation === workspaceGeneration.current) workspaceDirty.current = false;
      } else if (eventName === 'workspace.requestSnapshot') {
        if (typeof data.request !== 'string') return;
        void prepareWorkspaceSnapshot()
          .then((payload) => desktopRpc('workspace.snapshot', { request: data.request, payload }))
          .catch(() => desktopRpc('workspace.snapshot', { request: data.request }).catch(() => {}));
      } else if (eventName === 'updates.result') {
        setUpdateStatus(data as unknown as UpdateCheckResult);
        if (typeof data.message === 'string') notify(data.message);
      } else if (eventName === 'updates.progress') {
        setUpdateProgress(data as unknown as UpdateProgress);
        if (typeof data.message === 'string') notify(data.message);
      } else if (eventName === 'view.navigate') {
        navigateFromDesktop(data.view);
      }
    });

    let active = true;
    desktopHandshake.current ??= desktopRpc<Record<string, unknown>>('bridge.handshake');
    desktopHandshake.current.then((res) => {
        if (!active) return;
        applyDesktopState(res);
        setDesktopConnected(true);
        return desktopRpc<Record<string, unknown>>('ai.getState').then(applyAiState).catch(() => {});
      })
      .catch((err) => {
        console.warn('[EditHere Bridge] handshake failed:', err);
      });

    return () => {
      active = false;
      unsub();
    };
  }, []);

  const previewImportSequence = useRef(0);
  const handleImportImage = (file: File, title?: string) => {
    const objectUrl = URL.createObjectURL(file);
    const img = new Image();
    img.onload = () => {
      const id = `import-${++previewImportSequence.current}`;
      const baseName = title ?? (file.name.replace(/\.[^.]+$/, '') || file.name);
      const names = new Set(workspace.current.pages.map((page) => page.name.toLowerCase()));
      let cleanName = baseName;
      for (let number = 2; names.has(cleanName.toLowerCase()); number++) {
        cleanName = `${baseName} ${number}`;
      }
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
      notify(t("app.imageImported", { name: cleanName, width: naturalW, height: naturalH, count: blocks.length }));
    };
    img.onerror = () => {
      URL.revokeObjectURL(objectUrl);
      notify(t("app.couldNotOpenTheFile"));
    };
    img.src = objectUrl;
  };

  const handleChangeTheme = (nextTheme: ThemeMode) => {
    if (isDesktopWebView()) {
      void handleSaveSettingsPatch({ theme: nextTheme });
    } else {
      setTheme(nextTheme);
    }
  };

  const notifyLanguageChange = (updated: Record<string, unknown>) => {
    const language = updated.language;
    if (language !== 'system' && language !== 'zh_CN' && language !== 'zh_TW' && language !== 'en') return;
    const effective = updated.effectiveLanguage;
    const effectiveLanguage = effective === 'zh_CN' || effective === 'zh_TW' || effective === 'en' ? effective : undefined;
    const nextLocale = resolveLocale(language, effectiveLanguage, browserLocale);
    const nextT = createTranslator(nextLocale);
    notify(nextT("settings.displayLanguageChangedTo", { language: nextT(LANGUAGE_LABEL_KEYS[language]) }));
  };

  const handleSaveSettingsPatch = async (patch: Record<string, unknown>): Promise<{ ok: boolean; error?: string }> => {
    if (!isDesktopWebView()) {
      setPreferences((prev) => ({
        ...prev,
        ...(patch as Partial<GeneralPreferences>),
      }));
      if ('language' in patch) notifyLanguageChange(patch);
      return { ok: true };
    }
    try {
      const updated = await desktopRpc<Record<string, unknown>>('settings.save', patch);
      hydratePreferences(updated);
      if ('language' in patch) notifyLanguageChange(updated);
      return { ok: true };
    } catch (err) {
      const msg = err instanceof Error ? err.message : t("app.couldNotSaveSettings");
      notify(msg);
      return { ok: false, error: msg };
    }
  };

  const handleResetShortcuts = async () => {
    if (!isDesktopWebView()) {
      setPreferences((prev) => ({
        ...prev,
        shortcuts: previewDefaults.shortcuts,
      }));
      notify(t("app.defaultShortcutsRestored"));
      return;
    }
    try {
      const updated = await desktopRpc<Record<string, unknown>>('settings.resetShortcuts');
      hydratePreferences(updated);
      notify(t("app.defaultShortcutsRestored"));
    } catch (err) {
      notify(err instanceof Error ? err.message : t("app.couldNotRestoreDefaultShortcuts"));
    }
  };

  const handleResetDefaults = async () => {
    if (!isDesktopWebView()) {
      setTheme('system');
      setPreferences(previewDefaults);
      const defaultLocale = resolveLocale('system', undefined, browserLocale);
      notify(createTranslator(defaultLocale)("app.allDefaultsRestored"));
      return;
    }
    try {
      const updated = await desktopRpc<Record<string, unknown>>('settings.resetDefaults');
      hydratePreferences(updated);
      const effective = updated.effectiveLanguage;
      const defaultLocale = effective === 'zh_CN' || effective === 'zh_TW' || effective === 'en' ? effective : locale;
      notify(createTranslator(defaultLocale)("app.allDefaultsRestored"));
    } catch (err) {
      notify(err instanceof Error ? err.message : t("app.couldNotRestoreDefaults"));
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
      notify(err instanceof Error ? err.message : t("app.couldNotSaveProviderSettings"));
    }
  };

  const handleTestProvider = async (cfg: ProviderConfig): Promise<{ ok: boolean; message: string }> => {
    if (!isDesktopWebView()) {
      await new Promise((r) => setTimeout(r, 550));
      return { ok: true, message: t("app.previewConnectionTestPassedWithoutUsingAny") };
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
        message: res?.reply ? t("app.connectedSuccessfullyReply", { reply: res.reply }) : t("app.connectionTestPassed"),
      };
    } catch (err) {
      return {
        ok: false,
        message: err instanceof Error ? err.message : t("app.connectionTestFailed"),
      };
    }
  };

  const handleCaptureStart = async (append = false) => {
    if (!isDesktopWebView()) {
      notify(t("app.theDesktopAppHidesThisWindowAnd"));
      return;
    }
    try {
      await desktopRpc('capture.start', { append });
    } catch (err) {
      notify(err instanceof Error ? err.message : t("app.couldNotStartCapture"));
    }
  };

  const selectPage = (pageId: string) => {
    setActivePageId(pageId);
    if (isDesktopWebView()) {
      void desktopRpc('page.select', { pageId }).catch(() => {});
    }
  };

  const removePage = async (pageId: string): Promise<boolean> => {
    if (isDesktopWebView()) {
      try {
        await desktopRpc('page.remove', { pageId });
        return true;
      } catch {
        notify(t("workspace.couldNotRemovePage"));
        return false;
      }
    }

    const current = workspace.current.pages;
    const index = current.findIndex((page) => page.id === pageId);
    if (index === -1) return true;
    const next = current.filter((page) => page.id !== pageId);
    updateWorkspace((state) => ({ pages: next, groupNotes: next.length ? state.groupNotes : [] }));
    setActivePageId((active) => active === pageId ? (next[index] ?? next[index - 1])?.id ?? '' : active);
    const removedUrl = current[index].imageUrl;
    if (removedUrl?.startsWith('blob:')) URL.revokeObjectURL(removedUrl);
    return true;
  };

  const handleRequestOpenFiles = async (): Promise<boolean> => {
    if (!isDesktopWebView()) return false;
    try {
      const result = await desktopRpc<{ cancelled?: boolean }>('file.openDialog', {}, 0);
      if (!result.cancelled) setView('workspace');
      return true;
    } catch (err) {
      notify(err instanceof Error ? err.message : t("app.couldNotOpenTheFile"));
      return true;
    }
  };

  const previewFileInput = useRef<HTMLInputElement>(null);
  const openFiles = async () => {
    if (!await handleRequestOpenFiles()) previewFileInput.current?.click();
  };

  const pasteImage = async () => {
    try {
      if (isDesktopWebView()) {
        const result = await desktopRpc<{ pasted: boolean }>('clipboard.pasteImage');
        if (result.pasted) return;
      } else {
        const items = await navigator.clipboard.read();
        for (const item of items) {
          const type = item.types.find((format) => format.startsWith('image/'));
          if (type) {
            const image = await item.getType(type);
            handleImportImage(new File([image], `clipboard.${type.split('/')[1]}`, { type }), 'Clipboard');
            return;
          }
        }
      }
      notify(t("app.theClipboardHasNoImage"));
    } catch (err) {
      notify(err instanceof Error ? err.message : t("app.couldNotPasteTheClipboardImage"));
    }
  };

  const closeWorkspace = () => {
    if (isDesktopWebView()) void desktopRpc('window.close').catch(() => {});
  };

  const appShortcutActions = useRef({ open: openFiles, paste: pasteImage, close: closeWorkspace });
  appShortcutActions.current = { open: openFiles, paste: pasteImage, close: closeWorkspace };

  const handleViewShortcut = (event: KeyboardEvent) => {
    if (event.defaultPrevented || !(event.ctrlKey || event.metaKey) || event.altKey || event.shiftKey) return;
    const nextView = event.key === '1' ? 'workspace' : event.key === '2' ? 'analysis'
      : event.key === '3' || event.key === ',' ? 'settings' : undefined;
    if (!nextView) return;
    event.preventDefault();
    setView(nextView);
  };

  const workspaceIsOpen = view === 'workspace' && pages.length > 0;
  const workspaceIsEmpty = view === 'workspace' && pages.length === 0;
  useEffect(() => {
    if (workspaceIsOpen) return;
    const matchShortcut = createShortcutMatcher(
      preferences.shortcuts,
      workspaceIsEmpty ? ['open', 'paste', 'close'] as const : ['open', 'paste'] as const,
    );
    const onKeyDown = (event: KeyboardEvent) => {
      if (!canHandleShortcut(event) || event.repeat) return;
      const action = matchShortcut(event);
      if (action) {
        void appShortcutActions.current[action]();
        return;
      }
      handleViewShortcut(event);
    };
    window.addEventListener('keydown', onKeyDown, true);
    return () => window.removeEventListener('keydown', onKeyDown, true);
  }, [preferences.shortcuts, workspaceIsEmpty, workspaceIsOpen]);

  const handleCompleteAgent = async () => {
    if (isDesktopWebView()) {
      try {
        const redactedPayload = await attachRedactedPagesToPayload(pages, groupNotes);
        const res = await desktopRpc<{ annotations?: number }>(
          'agent.complete',
          { payload: redactedPayload },
          0
        );
        notify(t("app.returnedAnnotationsToTheAi", { count: res.annotations ?? totalNotesCount }));
      } catch (err) {
        notify(err instanceof Error ? err.message : t("app.couldNotReturnTheAnnotations"));
      }
      return;
    }
    const payload = buildFeedbackPayload(pages, groupNotes);
    const receipt = await previewShellBridge(t).submitAgentSession(payload);
    notify(t("app.annotatedPagesGroupNotes", {
      message: receipt.message,
      pages: 'pages' in payload ? payload.pages.length : Number(payload.objects.length > 0),
      notes: 'groupNotes' in payload ? payload.groupNotes.length : 0,
    }));
  };

  const handleCancelAgent = async () => {
    if (isDesktopWebView()) {
      await desktopRpc('agent.cancel').catch(() => {});
      notify(t("app.aiSessionCanceled"));
      return;
    }
    const receipt = await previewShellBridge(t).cancelAgentSession();
    notify(receipt.message);
  };

  const handleRefreshSkills = async () => {
    if (!isDesktopWebView()) {
      onToastSkillSync(t("app.skillStatusRefreshedInPreview"));
      return;
    }
    try {
      const res = await desktopRpc<SkillSyncState>('skills.getStatus');
      if (res && Array.isArray(res.hosts)) {
        setSkillsState(res);
      }
    } catch (err) {
      console.warn('Failed to read Skill status', err);
      notify(t("app.couldNotLoadSkillStatusTryAgain"));
    }
  };

  const onToastSkillSync = (msg: string) => notify(msg);

  const handleSyncSkills = async (
    opts: { hostIds?: string[]; onlyUpdateInstalled?: boolean; force?: boolean } = {}
  ) => {
    if (!isDesktopWebView()) {
      setSkillsState((prev) => ({
        ...prev,
        outdatedCount: 0,
        hosts: prev.hosts.map((h) => {
          if (opts.hostIds?.length && !opts.hostIds.includes(h.id)) return h;
          if ((opts.onlyUpdateInstalled ?? true) && h.status === 'not-installed') return h;
          return {
            ...h,
            installed: true,
            installedVersion: prev.bundledSkillVersion,
            status: 'up-to-date',
          };
        }),
      }));
      notify(t("app.previewSkillSyncedToV", { version: skillsState.bundledSkillVersion }));
      return;
    }
    setSyncingSkills(true);
    try {
      const res = await desktopRpc<{
        results?: SkillSyncActionResult[];
        skills?: SkillSyncState;
      }>('skills.sync', {
        hostIds: opts.hostIds ?? [],
        onlyUpdateInstalled: opts.onlyUpdateInstalled ?? true,
        force: opts.force ?? false,
      });
      if (res?.skills) {
        setSkillsState(res.skills);
      }
      const list = Array.isArray(res?.results) ? res.results : [];
      const changed = list.filter((r) => r.state === 'updated' || r.state === 'installed');
      const failed = list.filter((r) => r.state === 'failed');
      if (failed.length > 0) {
        notify(failed[0].error || t("app.skillSyncFailedFor", { host: failed[0].name }), 7000);
      } else if (changed.length > 0) {
        const names = localizeRef.current.list.format(changed.map((r) => r.name));
        const ver = res?.skills?.bundledSkillVersion ?? skillsState.bundledSkillVersion;
        notify(t("app.syncedTheSkillForToV", { hosts: names, version: ver }));
      } else if (list.some((r) => r.state === 'skipped-newer')) {
        notify(t("app.skippedAiAssistantsWithANewerSkill"));
      } else {
        notify(t("app.assistantSkillsAreUpToDateAnd"));
      }
    } catch (err) {
      console.warn('Failed to get Skill sync results', err);
      notify(t("app.couldNotLoadSkillSyncResultsRefresh"), 7000);
    } finally {
      setSyncingSkills(false);
    }
  };

  const actionableSkillHosts = skillsState.hosts.filter(
    (h) => h.status === 'outdated' || h.status === 'legacy' || h.status === 'path-stale'
  );

  const presets = providerPresets(t);
  const visibleProviders = isDesktopWebView() ? providers.map((provider) => {
    const preset = presets.find((item) => item.id === provider.id);
    return preset ? { ...provider, name: preset.name } : provider;
  }) : providers;
  const defaultProvider = visibleProviders.find((p) => p.id === defaultProviderId) ?? visibleProviders[0];
  const totalNotesCount = pages.reduce((acc, p) => acc + p.annotations.length, 0) + groupNotes.length;

  const viewTitle = t(VIEW_TITLES[view]);
  return (
    <I18nProvider locale={locale} viewTitle={viewTitle}>
      <div className="app-shell">
        {!isDesktopWebView() && (
          <input
            ref={previewFileInput}
            type="file"
            accept="image/*"
            multiple
            hidden
            onChange={(event) => {
              for (const file of Array.from(event.currentTarget.files ?? [])) handleImportImage(file);
              event.currentTarget.value = '';
            }}
          />
        )}
        {/* Full-Width Seamless Translucent Window Header (Zero Left Sidebar) */}
        <header className="topbar">
          <div className="topbar-left">
            <button
              className="brand-lockup-btn"
              aria-label={t("app.edithereWorkspace")}
              title={t("app.returnToWorkspace")}
              onClick={() => setView('workspace')}
            >
              <img className="brand-squircle" src="./brand/edithere.svg" alt={t("app.edithereIcon")} />
              <img
                className="brand-wordmark"
                src={isDark ? './brand/edithere-wordmark-light.svg' : './brand/edithere-wordmark.svg'}
                alt="EditHere"
              />
            </button>
            <span className="topbar-divider" />
            <span className="topbar-view-title">{viewTitle}</span>
            <span className="topbar-meta">
              <span>{t("app.pageCount", { count: pages.length })}</span>
              <span>·</span>
              <span>{t("app.noteCount", { count: totalNotesCount })}</span>
            </span>
          </div>

          <div className="topbar-right">
            <button className="button secondary sm" onClick={() => void handleCaptureStart()} title={t("app.captureTheScreen")}>
              <CaptureIcon size={14} />{t("app.screenshot")}</button>

            <div className="segmented-control" aria-label={t("app.workMode")}>
              <button className={agentMode ? 'selected' : ''} onClick={() => setAgentMode(true)}>{t("app.agentSession")}</button>
              <button className={!agentMode ? 'selected' : ''} onClick={() => setAgentMode(false)}>{t("app.standalone")}</button>
            </div>

            {agentMode && (
              <>
                <span className="session-pill">
                  <span className="session-dot" />{t("app.aiIsWaitingForYourAnnotations")}</span>
                <button className="button secondary sm" onClick={handleCancelAgent}>{t("app.cancel")}</button>
                <button className="button primary sm" onClick={handleCompleteAgent}>
                  <SendIcon size={14} />{t("app.finishAndReturnToAi")}</button>
              </>
            )}

            <span className="topbar-divider" />

            <button
              className="icon-button"
              aria-label={isDark ? t("app.switchToLight") : t("app.switchToDark")}
              title={isDark ? t("app.switchToLightAppearance") : t("app.switchToDarkAppearance")}
              onClick={() => handleChangeTheme(isDark ? 'light' : 'dark')}
            >
              {isDark ? <SunIcon size={16} /> : <MoonIcon size={16} />}
            </button>

            <button
              className={`icon-button ${view === 'settings' ? 'is-active' : ''}`}
              aria-label={t("app.settingsAndAbout")}
              title={t("app.settingsAndAboutCtrl")}
              onClick={() => setView((cur) => (cur === 'settings' ? 'workspace' : 'settings'))}
            >
              <SettingsIcon size={16} />
            </button>
          </div>
        </header>

        {/* Main Content Area */}
        <main className={`app-main ${view === 'workspace' ? 'is-workspace' : 'is-page-view'}`}>
          {!agentMode && !skillBannerDismissed && skillsState.sourceAvailable && actionableSkillHosts.length > 0 && (
            <div className="skill-update-banner" role="region" aria-label={t("app.skillSyncReminder")}>
              <div className="skill-update-banner-main">
                <ShieldOkIcon size={15} style={{ color: 'var(--warning)' }} />
                <span>{t("app.skillReminderDetail", {
                  count: actionableSkillHosts.length,
                  version: skillsState.bundledSkillVersion,
                  hosts: localize.list.format(actionableSkillHosts.map((host) => host.name)),
                })}
                </span>
              </div>
              <div className="skill-update-banner-actions">
                <button
                  className="button primary sm"
                  disabled={syncingSkills}
                  onClick={() => void handleSyncSkills({ onlyUpdateInstalled: true })}
                >
                  {syncingSkills ? t("app.syncing") : t("app.syncAll")}
                </button>
                <button
                  className="button secondary sm"
                  onClick={() => {
                    setSettingsTab('diagnostics');
                    setView('settings');
                  }}
                >{t("app.manage")}</button>
                <button
                  className="icon-button"
                  aria-label={t("app.dismissSkillReminder")}
                  onClick={() => setSkillBannerDismissed(true)}
                >
                  <CloseIcon size={14} />
                </button>
              </div>
            </div>
          )}
          {view === 'workspace' && pages.length === 0 && (
            <section className="canvas-surface workspace-empty" aria-label={t("app.emptyWorkspace")}>
              <div>
                <button className="button primary" onClick={() => void handleCaptureStart()}>
                  <CaptureIcon size={16} />{t("app.screenshotTooltip")}</button>
                <button className="button secondary" onClick={() => void openFiles()}>
                  <OpenFolderIcon size={16} />{t("app.openImageOrProject")}</button>
              </div>
            </section>
          )}
          {view === 'workspace' && pages.length > 0 && (
            <WorkspaceView
              key={workspaceGeneration.current}
              prepareWorkspaceSnapshot={prepareWorkspaceSnapshot}
              pages={pages}
              activePageId={activePageId}
              groupNotes={groupNotes}
              fitImageOnOpen={preferences.fitImageOnOpen}
              defaultTool={preferences.defaultTool}
              shortcuts={preferences.shortcuts}
              onCapture={() => void handleCaptureStart()}
              onAddPageCapture={() => void handleCaptureStart(true)}
              onClose={closeWorkspace}
              onSelectPage={selectPage}
              onRemovePage={removePage}
              onUpdatePages={(update) => {
                reportWorkspaceChange();
                setPages(update);
              }}
              onUpdateGroupNotes={(update) => {
                reportWorkspaceChange();
                setGroupNotes(update);
              }}
              onOpenFiles={openFiles}
              onPasteImage={pasteImage}
              onViewShortcut={handleViewShortcut}
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
                  preferences={isDesktopWebView() ? preferences : {
                    ...preferences,
                    trayActions: previewDefaults.trayActions,
                    shortcutDefinitions: previewDefaults.shortcutDefinitions,
                  }}
                  onSavePatch={handleSaveSettingsPatch}
                  onResetShortcuts={handleResetShortcuts}
                  onResetDefaults={handleResetDefaults}
                  updateStatus={updateStatus}
                  onUpdateStatus={setUpdateStatus}
                  updateProgress={updateProgress}
                  diagnosticsInfo={diagnosticsInfo}
                  onUpdateDiagnosticsInfo={setDiagnosticsInfo}
                  doctor={isDesktopWebView() ? doctor : initialDoctorSnapshot(t)}
                  skillsState={skillsState}
                  syncingSkills={syncingSkills}
                  onRefreshSkills={handleRefreshSkills}
                  onSyncSkills={handleSyncSkills}
                  desktopConnected={desktopConnected}
                  providers={visibleProviders}
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
            <button className="icon-button" aria-label={t("app.dismissNotification")} onClick={() => setToast('')}>
              <CloseIcon size={14} />
            </button>
          </div>
        )}
      </div>
    </I18nProvider>
  );
}
