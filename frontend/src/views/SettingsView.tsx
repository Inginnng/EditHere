import { LANGUAGE_LABEL_KEYS, useI18n } from '../i18n';
import { keyEventToPortable } from '../utils/shortcuts';
import { version as productVersion } from '../../package.json';
import { useState } from 'react';
import * as DropdownMenu from '@radix-ui/react-dropdown-menu';
import * as Switch from '@radix-ui/react-switch';
import {
  BackIcon,
  CaptureIcon,
  ChevronDownIcon,
  ClickActionIcon,
  CloseIcon,
  ConfirmDiscardIcon,
  CopyTextIcon,
  CornerRadiusIcon,
  CropToolIcon,
  DeleteIcon,
  EditIcon,
  ExportFileIcon,
  ExternalLinkIcon,
  FitCanvasIcon,
  JsonFileIcon,
  KeyboardIcon,
  LanguageIcon,
  OpenFolderIcon,
  PowerIcon,
  ResetIcon,
  ScrollAxisIcon,
  ScrollCaptureIcon,
  SelectToolIcon,
  ShadowIcon,
  ShieldOkIcon,
  SpinnerIcon,
  StatusOkIcon,
  TerminalIcon,
  ThemeIcon,
  WindowTitleIcon,
} from '../components/AppIcons';
import { desktopRpc, isDesktopWebView } from '../bridge/desktopBridge';
import type {
  DiagnosticsInfo,
  DoctorSnapshot,
  GeneralPreferences,
  LanguageCode,
  ProviderConfig,
  SettingsTabId,
  SkillSyncState,
  SkillSyncStatus,
  ThemeMode,
  UpdateCheckResult,
  UpdateProgress,
  UpdateSourceId,
} from '../bridge/types';
import ProvidersView from './ProvidersView';

interface SettingsViewProps {
  updateStatus: UpdateCheckResult | null;
  onUpdateStatus: (result: UpdateCheckResult | null) => void;
  updateProgress: UpdateProgress;
  activeTab: SettingsTabId;
  onChangeTab: (tab: SettingsTabId) => void;
  onBack: () => void;
  isDark: boolean;
  theme: ThemeMode;
  onChangeTheme: (theme: ThemeMode) => void;
  preferences: GeneralPreferences;
  onSavePatch: (patch: Record<string, unknown>) => Promise<{ ok: boolean; error?: string }>;
  onResetShortcuts: () => Promise<void> | void;
  onResetDefaults: () => Promise<void> | void;
  diagnosticsInfo: DiagnosticsInfo;
  onUpdateDiagnosticsInfo: (info: DiagnosticsInfo) => void;
  doctor: DoctorSnapshot;
  skillsState: SkillSyncState;
  syncingSkills?: boolean;
  onRefreshSkills?: () => Promise<void> | void;
  onSyncSkills?: (opts?: { hostIds?: string[]; onlyUpdateInstalled?: boolean; force?: boolean }) => Promise<void> | void;
  desktopConnected?: boolean;
  providers: ProviderConfig[];
  defaultProviderId: string;
  onSaveProviders: (next: ProviderConfig[], nextDefaultId: string) => Promise<void> | void;
  onTestProvider: (cfg: ProviderConfig) => Promise<{ ok: boolean; message: string }>;
  onToast: (message: string) => void;
}

export default function SettingsView({
  activeTab,
  onChangeTab,
  onBack,
  isDark,
  theme,
  onChangeTheme,
  preferences,
  onSavePatch,
  onResetShortcuts,
  onResetDefaults,
  diagnosticsInfo,
  onUpdateDiagnosticsInfo,
  doctor,
  skillsState,
  syncingSkills = false,
  onRefreshSkills,
  onSyncSkills,
  desktopConnected = false,
  providers,
  defaultProviderId,
  onSaveProviders,
  onTestProvider,
  onToast,
  updateStatus,
  onUpdateStatus,
  updateProgress,
}: SettingsViewProps) {
  const t = useI18n();
  const TOOL_LABELS = [t("settings.smartSelect"), t("settings.pointAnnotation"), t("settings.boxAnnotation"), t("settings.selectAndMove")];

  const UPDATE_SOURCE_LABELS: Record<UpdateSourceId, string> = {
    auto: t("settings.automatic"),
    atomgit: 'AtomGit',
    github: 'GitHub',
    gitee: 'Gitee',
  };

  const [recordingId, setRecordingId] = useState<string | null>(null);
  const [shortcutError, setShortcutError] = useState('');
  const [checkingUpdate, setCheckingUpdate] = useState(false);
  const applyingUpdate = updateProgress.phase !== 'idle';
  const updatePercent = updateProgress.total && updateProgress.total > 0
    ? Math.min(100, Math.round(100 * (updateProgress.received ?? 0) / updateProgress.total))
    : undefined;
  const [exportingDiag, setExportingDiag] = useState(false);

  const openExternalUrl = (url: string) => {
    if (isDesktopWebView()) {
      void desktopRpc('shell.openExternal', { url });
    } else {
      window.open(url, '_blank', 'noopener,noreferrer');
    }
  };

  const commitPatch = async (patch: Record<string, unknown>, okToast?: string) => {
    const res = await onSavePatch(patch);
    if (res.ok) {
      setShortcutError('');
      if (okToast) onToast(okToast);
    }
    return res;
  };

  const applyShortcut = async (id: string, sequence: string) => {
    const res = await onSavePatch({
      shortcuts: { ...preferences.shortcuts, [id]: sequence },
    });
    if (!res.ok && res.error) {
      setShortcutError(res.error);
    } else {
      setShortcutError('');
      onToast(sequence ? t("settings.shortcutSetTo", { keys: sequence }) : t("settings.shortcutDisabled"));
    }
  };

  const handleChooseFeedbackDir = async () => {
    if (!isDesktopWebView()) {
      onToast(t("settings.chooseTheFeedbackFolderInTheDesktop"));
      return;
    }
    try {
      const res = await desktopRpc<{ cancelled?: boolean; path?: string }>('settings.chooseFeedbackDir', {}, 0);
      if (!res.cancelled && typeof res.path === 'string') {
        await commitPatch({ feedbackDir: res.path }, t("settings.feedbackFolderUpdated"));
      }
    } catch (err) {
      onToast(err instanceof Error ? err.message : t("settings.couldNotSelectTheFolder"));
    }
  };

  const handleExportDiagnostics = async () => {
    if (!isDesktopWebView()) {
      onToast(t("settings.exportDiagnosticLogsInTheDesktopApp"));
      return;
    }
    setExportingDiag(true);
    try {
      const res = await desktopRpc<{ saved?: boolean; path?: string }>('diagnostics.export', {}, 0);
      if (res.saved && res.path) {
        onToast(t("settings.diagnosticLogsExportedTo", { path: res.path }));
      }
      const fresh = await desktopRpc<DiagnosticsInfo>('diagnostics.getInfo').catch(() => null);
      if (fresh) onUpdateDiagnosticsInfo(fresh);
    } catch (err) {
      onToast(err instanceof Error ? err.message : t("settings.couldNotExportDiagnosticLogs"));
    } finally {
      setExportingDiag(false);
    }
  };

  const handleOpenDiagnosticsFolder = async () => {
    if (!isDesktopWebView()) {
      onToast(t("settings.openTheLogFolderInTheDesktop"));
      return;
    }
    try {
      await desktopRpc('diagnostics.openFolder');
    } catch (err) {
      onToast(err instanceof Error ? err.message : t("settings.couldNotOpenTheLogFolder"));
    }
  };

  const handleCheckUpdates = async () => {
    if (!isDesktopWebView()) {
      onUpdateStatus({ status: 'upToDate', message: t("settings.thePreviewIsUpToDate") });
      return;
    }
    setCheckingUpdate(true);
    try {
      const res = await desktopRpc<UpdateCheckResult>('updates.check', {}, 35000);
      onUpdateStatus(res);
      if (res.message) onToast(res.message);
    } catch (err) {
      onToast(err instanceof Error ? err.message : t("settings.couldNotCheckForUpdates"));
    } finally {
      setCheckingUpdate(false);
    }
  };

  const runUpdateAction = async (method: 'updates.downloadAndInstall' | 'updates.cancel') => {
    try {
      await desktopRpc(method);
    } catch (err) {
      onToast(err instanceof Error ? err.message : t("settings.updateFailed"));
    }
  };

  const handleOpenReleases = async () => {
    const targetUrl = updateStatus?.url || '';
    if (isDesktopWebView()) {
      await desktopRpc('updates.openReleases', { url: targetUrl }).catch((err) => {
        onToast(err instanceof Error ? err.message : t("settings.couldNotOpenTheReleasesPage"));
      });
    } else if (targetUrl) {
      window.open(targetUrl, '_blank', 'noopener,noreferrer');
    }
  };

  const skillStatusBadge = (status: SkillSyncStatus) => {
    switch (status) {
      case 'up-to-date':
        return { label: t("settings.upToDate"), cls: 'success' };
      case 'outdated':
        return { label: t("settings.updateAvailable"), cls: 'warning' };
      case 'legacy':
        return { label: t("settings.legacy"), cls: 'warning' };
      case 'path-stale':
        return { label: t("settings.pathChanged"), cls: 'warning' };
      case 'newer':
        return { label: t("settings.newerVersion"), cls: 'accent' };
      default:
        return { label: t("settings.notInstalled"), cls: 'neutral' };
    }
  };

  const detectedSkills = skillsState.hosts.filter((h) => h.hostPresent || h.installed);
  const installedSkills = detectedSkills.filter((h) => h.installed);
  const { showInstalledSkillsOnly } = preferences;
  const visibleSkills = showInstalledSkillsOnly ? installedSkills : detectedSkills;

  const actionableSkills = skillsState.hosts.filter(
    (h) => h.status === 'outdated' || h.status === 'legacy' || h.status === 'path-stale'
  );

  const copyMcpSnippet = async () => {
    try {
      if (isDesktopWebView()) {
        await desktopRpc('clipboard.copyText', { text: doctor.unifiedMcpSnippet });
      } else {
        await navigator.clipboard.writeText(doctor.unifiedMcpSnippet);
      }
      onToast(t("settings.mcpConfigurationCopied"));
    } catch {
      onToast(t("settings.clipboardAccessRestricted"));
    }
  };

  const trayLabel = (id: string) =>
    preferences.trayActions.find((a) => a.id === id)?.label ?? id;

  const globalShortcuts = preferences.shortcutDefinitions.filter((d) => d.global);
  const localShortcuts = preferences.shortcutDefinitions.filter((d) => !d.global);

  return (
    <div className="page-container">
      <div className="page-heading">
        <div>
          <h1>{t("app.settings")}</h1>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 10, flexWrap: 'wrap' }}>
          <div className="segmented-control" role="tablist" aria-label={t("settings.settingsTabs")}>
            {(
              [
                { id: 'general', label: t("settings.general") },
                { id: 'shortcuts', label: t("settings.shortcuts") },
                { id: 'capture', label: t("settings.captureAndExport") },
                { id: 'providers', label: t("settings.modelProviders") },
                { id: 'diagnostics', label: t("settings.diagnosticsAndAbout") },
              ] as const
            ).map((tab) => (
              <button
                key={tab.id}
                className={activeTab === tab.id ? 'selected' : ''}
                onClick={() => onChangeTab(tab.id)}
              >
                {tab.label}
              </button>
            ))}
          </div>
          <button className="button secondary sm" onClick={onBack}>
            <BackIcon size={14} />{t("settings.backToWorkspace")}</button>
        </div>
      </div>

      {activeTab === 'general' && (
        <>
          <section className="settings-group" style={{ marginTop: 0 }}>
            <h2>{t("settings.appearanceAndLanguage")}</h2>
            <div className="settings-card-stack">
              <div className="settings-row">
                <span className="settings-row-icon"><ThemeIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>{t("settings.appTheme")}</strong>
                </div>
                <div className="segmented-control" aria-label={t("settings.appTheme")}>
                  {([
                    { id: 'light', label: t("settings.light") },
                    { id: 'dark', label: t("settings.dark") },
                    { id: 'system', label: t("settings.followSystem") },
                  ] as const).map((opt) => (
                    <button
                      key={opt.id}
                      className={theme === opt.id ? 'selected' : ''}
                      onClick={() => onChangeTheme(opt.id)}
                    >
                      {opt.label}
                    </button>
                  ))}
                </div>
              </div>

              <div className="settings-row">
                <span className="settings-row-icon"><LanguageIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>{t("settings.displayLanguage")}</strong>
                </div>
                <DropdownMenu.Root>
                  <DropdownMenu.Trigger asChild>
                    <button className="button secondary sm">
                      <span>{t(LANGUAGE_LABEL_KEYS[preferences.language])}</span>
                      <ChevronDownIcon size={14} />
                    </button>
                  </DropdownMenu.Trigger>
                  <DropdownMenu.Portal>
                    <DropdownMenu.Content className="dropdown-menu" align="end" sideOffset={6}>
                      {(['system', 'zh_CN', 'zh_TW', 'en'] as LanguageCode[]).map((code) => (
                        <DropdownMenu.Item
                          key={code}
                          className="dropdown-item"
                          onSelect={() => void commitPatch({ language: code })}
                        >
                          {t(LANGUAGE_LABEL_KEYS[code])}
                        </DropdownMenu.Item>
                      ))}
                    </DropdownMenu.Content>
                  </DropdownMenu.Portal>
                </DropdownMenu.Root>
              </div>

            </div>
          </section>

          <section className="settings-group">
            <h2>{t("settings.startupAndWindowBehavior")}</h2>
            <div className="settings-card-stack">
              <div className="settings-row">
                <span className="settings-row-icon"><CaptureIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>{t("settings.captureOnStartup")}</strong>
                </div>
                <Switch.Root
                  className="ui-switch"
                  checked={preferences.captureOnStartup}
                  onCheckedChange={(v) => void commitPatch({ captureOnStartup: v })}
                >
                  <Switch.Thumb className="ui-switch-thumb" />
                </Switch.Root>
              </div>

              <div className="settings-row">
                <span className="settings-row-icon"><PowerIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>{t("settings.launchEdithereAtLogin")}</strong>
                  {preferences.launchAtLoginNotice && <p>{preferences.launchAtLoginNotice}</p>}
                </div>
                <Switch.Root
                  className="ui-switch"
                  checked={preferences.launchAtLogin}
                  onCheckedChange={(v) => void commitPatch({ launchAtLogin: v })}
                >
                  <Switch.Thumb className="ui-switch-thumb" />
                </Switch.Root>
              </div>

              <div className="settings-row">
                <span className="settings-row-icon"><FitCanvasIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>{t("settings.fitImagesToWindowOnOpen")}</strong>
                </div>
                <Switch.Root
                  className="ui-switch"
                  checked={preferences.fitImageOnOpen}
                  onCheckedChange={(v) => void commitPatch({ fitImageOnOpen: v })}
                >
                  <Switch.Thumb className="ui-switch-thumb" />
                </Switch.Root>
              </div>

              <div className="settings-row">
                <span className="settings-row-icon"><ConfirmDiscardIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>{t("settings.askToSaveBeforeClosingTheAnnotation")}</strong>
                </div>
                <Switch.Root
                  className="ui-switch"
                  checked={preferences.confirmBeforeDiscard}
                  onCheckedChange={(v) => void commitPatch({ confirmBeforeDiscard: v })}
                >
                  <Switch.Thumb className="ui-switch-thumb" />
                </Switch.Root>
              </div>

              <div className="settings-row">
                <span className="settings-row-icon"><EditIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>{t("settings.defaultAnnotationTool")}</strong>
                </div>
                <DropdownMenu.Root>
                  <DropdownMenu.Trigger asChild>
                    <button className="button secondary sm">
                      <span>{TOOL_LABELS[preferences.defaultTool] ?? TOOL_LABELS[0]}</span>
                      <ChevronDownIcon size={14} />
                    </button>
                  </DropdownMenu.Trigger>
                  <DropdownMenu.Portal>
                    <DropdownMenu.Content className="dropdown-menu" align="end" sideOffset={6}>
                      {TOOL_LABELS.map((label, idx) => (
                        <DropdownMenu.Item
                          key={label}
                          className="dropdown-item"
                          onSelect={() => void commitPatch({ defaultTool: idx })}
                        >
                          {label}
                        </DropdownMenu.Item>
                      ))}
                    </DropdownMenu.Content>
                  </DropdownMenu.Portal>
                </DropdownMenu.Root>
              </div>
            </div>
          </section>

          <section className="settings-group">
            <h2>{t("settings.systemTray")}</h2>
            <div className="settings-card-stack">
              <div className="settings-row">
                <span className="settings-row-icon"><ClickActionIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>{t("settings.singleClickTrayIcon")}</strong>
                </div>
                <DropdownMenu.Root>
                  <DropdownMenu.Trigger asChild>
                    <button className="button secondary sm">
                      <span>{trayLabel(preferences.traySingleClick)}</span>
                      <ChevronDownIcon size={14} />
                    </button>
                  </DropdownMenu.Trigger>
                  <DropdownMenu.Portal>
                    <DropdownMenu.Content className="dropdown-menu" align="end" sideOffset={6}>
                      {preferences.trayActions.map((act) => (
                        <DropdownMenu.Item
                          key={act.id}
                          className="dropdown-item"
                          onSelect={() => void commitPatch({ traySingleClick: act.id })}
                        >
                          {act.label}
                        </DropdownMenu.Item>
                      ))}
                    </DropdownMenu.Content>
                  </DropdownMenu.Portal>
                </DropdownMenu.Root>
              </div>

              <div className="settings-row">
                <span className="settings-row-icon"><SelectToolIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>{t("settings.doubleClickTrayIcon")}</strong>
                </div>
                <DropdownMenu.Root>
                  <DropdownMenu.Trigger asChild>
                    <button className="button secondary sm">
                      <span>{trayLabel(preferences.trayDoubleClick)}</span>
                      <ChevronDownIcon size={14} />
                    </button>
                  </DropdownMenu.Trigger>
                  <DropdownMenu.Portal>
                    <DropdownMenu.Content className="dropdown-menu" align="end" sideOffset={6}>
                      {preferences.trayActions.map((act) => (
                        <DropdownMenu.Item
                          key={act.id}
                          className="dropdown-item"
                          onSelect={() => void commitPatch({ trayDoubleClick: act.id })}
                        >
                          {act.label}
                        </DropdownMenu.Item>
                      ))}
                    </DropdownMenu.Content>
                  </DropdownMenu.Portal>
                </DropdownMenu.Root>
              </div>
            </div>
          </section>
        </>
      )}

      {activeTab === 'shortcuts' && (
        <>
          <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: 14, gap: 12 }}>
            <span style={{ fontSize: 12.5, color: 'var(--muted)' }}>{t("settings.clickToRecordAShortcutClearIt")}</span>
            <button
              className="button secondary sm"
              onClick={() => {
                setShortcutError('');
                void onResetShortcuts();
              }}
            >
              <ResetIcon size={14} />{t("settings.restoreDefaultShortcuts")}</button>
          </div>

          {shortcutError && <div className="settings-error-banner">{shortcutError}</div>}

          {[
            { title: t("settings.globalShortcuts"), list: globalShortcuts },
            { title: t("settings.inAppShortcuts"), list: localShortcuts },
          ].map((group) => (
            <section key={group.title} className="settings-group" style={group.title === t("settings.globalShortcuts") ? { marginTop: 0 } : undefined}>
              <h2>{group.title}</h2>
              <div className="settings-card-stack">
                {group.list.map((def) => {
                  const val = preferences.shortcuts[def.id] ?? '';
                  const recording = recordingId === def.id;
                  return (
                    <div key={def.id} className="settings-row">
                      <span className="settings-row-icon"><KeyboardIcon size={18} /></span>
                      <div className="settings-row-label">
                        <strong>{def.label}</strong>
                      </div>
                      <div className="settings-inline-controls">
                        <button
                          type="button"
                          className={`shortcut-capture-btn ${recording ? 'recording' : ''} ${!val ? 'empty' : ''}`}
                          onClick={() => setRecordingId(recording ? null : def.id)}
                          onBlur={() => setRecordingId((cur) => (cur === def.id ? null : cur))}
                          onKeyDown={(e) => {
                            if (!recording) return;
                            e.preventDefault();
                            e.stopPropagation();
                            if (e.key === 'Escape' && def.id !== 'close') {
                              setRecordingId(null);
                              return;
                            }
                            if (e.key === 'Backspace' && !e.ctrlKey && !e.altKey && !e.shiftKey && !e.metaKey) {
                              setRecordingId(null);
                              void applyShortcut(def.id, '');
                              return;
                            }
                            const combo = keyEventToPortable(e.nativeEvent);
                            if (!combo) return;
                            setRecordingId(null);
                            void applyShortcut(def.id, combo);
                          }}
                        >
                          {recording ? t("settings.pressAShortcut") : val || t("settings.unassigned")}
                        </button>
                        {val && (
                          <button
                            type="button"
                            className="icon-button"
                            aria-label={t("settings.clearTheShortcut", { label: def.label })}
                            onClick={() => void applyShortcut(def.id, '')}
                          >
                            <CloseIcon size={14} />
                          </button>
                        )}
                      </div>
                    </div>
                  );
                })}
              </div>
            </section>
          ))}
        </>
      )}

      {activeTab === 'capture' && (
        <>
          <section className="settings-group" style={{ marginTop: 0 }}>
            <h2>{t("settings.annotationExport")}</h2>
            <div className="settings-card-stack">

              <div className="settings-row">
                <span className="settings-row-icon"><WindowTitleIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>{t("settings.includeWindowTitleInExports")}</strong>
                </div>
                <Switch.Root
                  className="ui-switch"
                  checked={preferences.includeWindowTitle}
                  onCheckedChange={(v) => void commitPatch({ includeWindowTitle: v })}
                >
                  <Switch.Thumb className="ui-switch-thumb" />
                </Switch.Root>
              </div>

              <div className="settings-row">
                <span className="settings-row-icon"><JsonFileIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>{t("settings.defaultFeedbackExportMode")}</strong>
                </div>
                <div className="segmented-control">
                  <button
                    className={preferences.exportMode === 'file' ? 'selected' : ''}
                    onClick={() => void commitPatch({ exportMode: 'file' })}
                  >{t("settings.jsonImageFile")}</button>
                  <button
                    className={preferences.exportMode === 'embedded' ? 'selected' : ''}
                    onClick={() => void commitPatch({ exportMode: 'embedded' })}
                  >{t("settings.embedImage")}</button>
                  <button
                    className={preferences.exportMode === 'none' ? 'selected' : ''}
                    onClick={() => void commitPatch({ exportMode: 'none' })}
                  >{t("settings.jsonOnly")}</button>
                </div>
              </div>

              <div className="settings-row">
                <span className="settings-row-icon"><CopyTextIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>{t("settings.embedTheOriginalImageWhenCopyingJson")}</strong>
                </div>
                <Switch.Root
                  className="ui-switch"
                  checked={preferences.copyJsonTextWithImage}
                  onCheckedChange={(v) => void commitPatch({ copyJsonTextWithImage: v })}
                >
                  <Switch.Thumb className="ui-switch-thumb" />
                </Switch.Root>
              </div>

              <div className="settings-row">
                <span className="settings-row-icon"><OpenFolderIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>{t("settings.temporaryFeedbackFolder")}</strong>
                  <p>{preferences.feedbackDir || t("settings.defaultCacheFolder")}</p>
                </div>
                <div className="settings-inline-controls">
                  {preferences.feedbackDir && (
                    <button
                      className="icon-button"
                      aria-label={t("settings.restoreDefaultFolder")}
                      onClick={() => void commitPatch({ feedbackDir: '' }, t("settings.defaultFeedbackFolderRestored"))}
                    >
                      <DeleteIcon size={14} />
                    </button>
                  )}
                  <button className="button secondary sm" onClick={() => void handleChooseFeedbackDir()}>{t("settings.chooseFolder")}</button>
                </div>
              </div>
            </div>
          </section>

          <section className="settings-group">
            <h2>{t("settings.captureAppearanceAndScrollingCapture")}</h2>
            <div className="settings-card-stack">
              <div className="settings-row">
                <span className="settings-row-icon"><ShadowIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>{t("settings.softShadowOnDesktopCaptures")}</strong>
                </div>
                <Switch.Root
                  className="ui-switch"
                  checked={preferences.captureShadow}
                  onCheckedChange={(v) => void commitPatch({ captureShadow: v })}
                >
                  <Switch.Thumb className="ui-switch-thumb" />
                </Switch.Root>
              </div>

              <div className="settings-row">
                <span className="settings-row-icon"><CornerRadiusIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>{t("settings.cornerRadius", { radius: preferences.captureCornerRadius })}</strong>
                </div>
                <input
                  type="range"
                  min={0}
                  max={32}
                  step={2}
                  value={preferences.captureCornerRadius}
                  onChange={(e) => void commitPatch({ captureCornerRadius: Number(e.target.value) })}
                  style={{ width: 140, accentColor: 'var(--accent)' }}
                />
              </div>

              <div className="settings-row">
                <span className="settings-row-icon"><ScrollAxisIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>{t("settings.scrollingCaptureDirection")}</strong>
                </div>
                <div className="segmented-control">
                  <button
                    className={preferences.scrollAxis === 'vertical' ? 'selected' : ''}
                    onClick={() => void commitPatch({ scrollAxis: 'vertical' })}
                  >{t("settings.vertical")}</button>
                  <button
                    className={preferences.scrollAxis === 'horizontal' ? 'selected' : ''}
                    onClick={() => void commitPatch({ scrollAxis: 'horizontal' })}
                  >{t("settings.horizontal")}</button>
                </div>
              </div>

              <div className="settings-row">
                <span className="settings-row-icon"><CropToolIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>{t("settings.removeFixedHeadersAndFootersFromScrolling")}</strong>
                </div>
                <Switch.Root
                  className="ui-switch"
                  checked={preferences.scrollAutoCrop}
                  onCheckedChange={(v) => void commitPatch({ scrollAutoCrop: v })}
                >
                  <Switch.Thumb className="ui-switch-thumb" />
                </Switch.Root>
              </div>

              <div className="settings-row">
                <span className="settings-row-icon"><ScrollCaptureIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>{t("settings.stitchExtraLongCapturesInSegments")}</strong>
                </div>
                <Switch.Root
                  className="ui-switch"
                  checked={preferences.scrollUltraLong}
                  onCheckedChange={(v) => void commitPatch({ scrollUltraLong: v })}
                >
                  <Switch.Thumb className="ui-switch-thumb" />
                </Switch.Root>
              </div>
            </div>
          </section>
        </>
      )}

      {activeTab === 'providers' && (
        <ProvidersView
          embedded
          providers={providers}
          defaultProviderId={defaultProviderId}
          onSaveProviders={onSaveProviders}
          onTestProvider={onTestProvider}
          onToast={onToast}
        />
      )}

      {activeTab === 'diagnostics' && (
        <>
          <section className="settings-group" style={{ marginTop: 0 }}>
            <h2>{t("settings.diagnosticLogs")}</h2>
            <div className="settings-card-stack">
              <div className="settings-row">
                <span className="settings-row-icon"><ExportFileIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>{t("settings.localLogsAndErrorReports")}</strong>
                  <p>
                    <code>{diagnosticsInfo.directory || t("settings.notInitialized")}</code> · {t("settings.logCount", { count: diagnosticsInfo.fileCount })}</p>
                  {diagnosticsInfo.lastError && (
                    <p style={{ color: 'var(--danger)' }}>{t("settings.lastLogError", { message: diagnosticsInfo.lastError })}</p>
                  )}
                </div>
                <div className="settings-inline-controls">
                  <button
                    className="button secondary sm"
                    onClick={() => void handleOpenDiagnosticsFolder()}
                  >
                    <OpenFolderIcon size={14} />{t("settings.openLogFolder")}</button>
                  <button
                    className="button primary sm"
                    disabled={exportingDiag}
                    onClick={() => void handleExportDiagnostics()}
                  >
                    <ExportFileIcon size={14} /> {exportingDiag ? t("settings.exporting") : t("settings.exportDiagnosticLogs")}
                  </button>
                </div>
              </div>
            </div>
          </section>

          <section className="settings-group">
            <h2>{t("settings.versionAndUpdates")}</h2>
            <div className="settings-card-stack">
              <div className="settings-row">
                <span className="settings-row-icon"><StatusOkIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>{t("settings.checkForUpdatesOnStartup")}</strong>
                  {updateStatus?.message && <p>{updateStatus.message}</p>}
                  {applyingUpdate && (
                    <p role="status">
                      {updateProgress.phase === 'preparing' ? t("settings.checkingInstallerRequirements")
                        : updatePercent === undefined ? t("settings.downloadingInstaller") : t("settings.downloadingInstallerTooltip", { percent: updatePercent })}
                    </p>
                  )}
                  {!applyingUpdate && updateProgress.message && <p role="status">{updateProgress.message}</p>}
                </div>
                <Switch.Root
                  className="ui-switch"
                  checked={preferences.checkUpdatesOnStartup}
                  onCheckedChange={(v) => void commitPatch({ checkUpdatesOnStartup: v })}
                >
                  <Switch.Thumb className="ui-switch-thumb" />
                </Switch.Root>
              </div>

              <div className="settings-row">
                <span className="settings-row-icon"><LanguageIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>{t("settings.updateSource")}</strong>
                </div>
                <div className="settings-inline-controls">
                  <DropdownMenu.Root>
                    <DropdownMenu.Trigger asChild>
                      <button className="button secondary sm" disabled={checkingUpdate || applyingUpdate}>
                        <span>{UPDATE_SOURCE_LABELS[preferences.updateSource]}</span>
                        <ChevronDownIcon size={14} />
                      </button>
                    </DropdownMenu.Trigger>
                    <DropdownMenu.Portal>
                      <DropdownMenu.Content className="dropdown-menu" align="end" sideOffset={6}>
                        {(['auto', 'atomgit', 'github', 'gitee'] as UpdateSourceId[]).map((src) => (
                          <DropdownMenu.Item
                            key={src}
                            className="dropdown-item"
                            onSelect={() => {
                              onUpdateStatus(null);
                              void commitPatch({ updateSource: src });
                            }}
                          >
                            {UPDATE_SOURCE_LABELS[src]}
                          </DropdownMenu.Item>
                        ))}
                      </DropdownMenu.Content>
                    </DropdownMenu.Portal>
                  </DropdownMenu.Root>
                  <button
                    className="button secondary sm"
                    disabled={checkingUpdate || applyingUpdate}
                    onClick={() => void handleCheckUpdates()}
                  >
                    {checkingUpdate && <SpinnerIcon size={14} className="spin" />}{t("settings.checkForUpdates")}</button>
                  {applyingUpdate ? (
                    <button className="button secondary sm" onClick={() => void runUpdateAction('updates.cancel')}>{t("settings.cancelUpdate")}</button>
                  ) : updateStatus?.canInstall && (
                    <button className="button primary sm" disabled={checkingUpdate}
                      onClick={() => void runUpdateAction('updates.downloadAndInstall')}>{t("settings.updateNow")}</button>
                  )}
                  <button className="button ghost sm" onClick={() => void handleOpenReleases()}>{t("settings.openReleasesPage")}<ExternalLinkIcon size={14} />
                  </button>
                </div>
              </div>
            </div>
          </section>

          <section className="settings-group">
            <h2>{t("settings.runtimeAndMcpSetup")}</h2>
            <div className="settings-card-stack">
              {doctor.checks.map((item) => (
                <div key={item.id} className="settings-row">
                  <span className="settings-row-icon">
                    {item.status === 'ok' ? (
                      <StatusOkIcon size={18} style={{ color: 'var(--success)' }} />
                    ) : (
                      <ShieldOkIcon size={18} style={{ color: 'var(--warning)' }} />
                    )}
                  </span>
                  <div className="settings-row-label">
                    <strong>{item.label}</strong>
                    <p>{item.detail}</p>
                  </div>
                  <span className={`badge ${item.status === 'ok' ? 'success' : 'warning'}`}>
                    {item.status === 'ok' ? t("settings.ready") : desktopConnected ? t("settings.needsCheck") : t("settings.preview")}
                  </span>
                </div>
              ))}

              <div className="settings-row">
                <span className="settings-row-icon"><TerminalIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>{t("settings.unifiedMcpConfiguration")}</strong>
                  <p>{t("settings.installSkillCommand")} <code>node cli/edithere.mjs init</code></p>
                </div>
                <button className="button secondary sm" onClick={() => void copyMcpSnippet()}>
                  <CopyTextIcon size={14} />{t("settings.copyMcpConfiguration")}</button>
              </div>
            </div>
          </section>

          <section className="settings-group">
            <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: 10, gap: 10, flexWrap: 'wrap' }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: 10, flexWrap: 'wrap' }}>
                <h2 style={{ margin: 0 }}>{t("settings.aiAssistantSkillVersionsAndSync")}</h2>
                <span className="badge accent">{t("settings.bundledSkillVersion", { version: skillsState.bundledSkillVersion })}</span>
                <span className="badge neutral">{t("settings.detectedSkillsCount", { count: detectedSkills.length, total: skillsState.hosts.length })}</span>
              </div>
              <div style={{ display: 'flex', alignItems: 'center', gap: 8, flexWrap: 'wrap' }}>
                <div className="segmented-control" aria-label={t("settings.filterAssistants")}>
                  <button
                    className={showInstalledSkillsOnly ? '' : 'selected'}
                    aria-pressed={!showInstalledSkillsOnly}
                    onClick={() => void commitPatch({ showInstalledSkillsOnly: false })}
                  >{t("settings.detectedFilter", { count: detectedSkills.length })}
                  </button>
                  <button
                    className={showInstalledSkillsOnly ? 'selected' : ''}
                    aria-pressed={showInstalledSkillsOnly}
                    onClick={() => void commitPatch({ showInstalledSkillsOnly: true })}
                  >{t("settings.installedFilter", { count: installedSkills.length })}
                  </button>
                </div>
                <button
                  className="button secondary sm"
                  disabled={syncingSkills}
                  onClick={() => void onRefreshSkills?.()}
                >
                  <ResetIcon size={14} />{t("settings.refreshStatus")}</button>
                <button
                  className="button primary sm"
                  disabled={syncingSkills || !skillsState.sourceAvailable || actionableSkills.length === 0}
                  onClick={() => void onSyncSkills?.({ onlyUpdateInstalled: true })}
                >
                  {syncingSkills && <SpinnerIcon size={14} className="spin" />}{t("settings.syncAllOutdatedSkills")}{actionableSkills.length > 0 ? ` (${actionableSkills.length})` : ''}
                </button>
              </div>
            </div>
            <div className="settings-card-stack">
              <div className="settings-row">
                <span className="settings-row-icon"><ShieldOkIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>{t("settings.syncOutdatedSkillsOnStartup")}</strong>
                  <p>{t("settings.automaticallyUpdatesInstalledEdithereSkillsWhenTheir")}</p>
                </div>
                <Switch.Root
                  className="ui-switch"
                  checked={preferences.autoSyncSkills}
                  onCheckedChange={(v) => void commitPatch({ autoSyncSkills: v })}
                >
                  <Switch.Thumb className="ui-switch-thumb" />
                </Switch.Root>
              </div>

              {!skillsState.sourceAvailable && (
                <div className="settings-row">
                  <span className="settings-row-icon"><ShieldOkIcon size={18} style={{ color: 'var(--warning)' }} /></span>
                  <div className="settings-row-label">
                    <strong>{t("settings.bundledSkillFolderSkillsEdithereMissing")}</strong>
                    <p>{t("settings.keepTheSkillsFolderInTheApp")}</p>
                  </div>
                </div>
              )}

              {visibleSkills.length === 0 ? (
                <div className="settings-row">
                  <span className="settings-row-icon"><TerminalIcon size={18} /></span>
                  <div className="settings-row-label">
                    <strong>
                      {showInstalledSkillsOnly
                        ? t("settings.theEdithereSkillIsMissingFromAll")
                        : t("settings.noSupportedAiAssistantsFoundInThe")}
                    </strong>
                    <p>
                      {showInstalledSkillsOnly && detectedSkills.length > 0
                        ? t("settings.foundAssistantFoldersSwitchToDetectedTo", { count: detectedSkills.length })
                        : t("settings.supportedAssistantsIncludeCodexClaudeCodeCursor")}
                    </p>
                  </div>
                  {showInstalledSkillsOnly && detectedSkills.length > 0 && (
                    <button className="button secondary sm" onClick={() => void commitPatch({ showInstalledSkillsOnly: false })}>{t("settings.viewDetectedAssistants")}</button>
                  )}
                </div>
              ) : (
                visibleSkills.map((host) => {
                  const badge = skillStatusBadge(host.status);
                  const versionDetail = host.installed
                    ? host.installedVersion
                      ? t("settings.installedV", { version: host.installedVersion })
                      : t("settings.legacyInstallationVersionUnknown")
                    : t("settings.assistantDetectedEdithereSkillIsMissing");
                  const variantsNote =
                    host.otherVariants && host.otherVariants.length > 0
                      ? t("settings.alsoFound", { variants: host.otherVariants.join('、') })
                      : '';
                  const actionLabel =
                    host.status === 'outdated' || host.status === 'legacy'
                      ? t("settings.update")
                      : host.status === 'path-stale'
                        ? t("settings.refreshPath")
                        : host.status === 'not-installed'
                          ? t("settings.install")
                          : host.status === 'newer'
                            ? t("settings.forceSwitch")
                            : t("settings.reinstall");
                  const actionOpts =
                    host.status === 'outdated' || host.status === 'legacy' || host.status === 'path-stale'
                      ? { hostIds: [host.id], onlyUpdateInstalled: true, force: false }
                      : host.status === 'not-installed'
                        ? { hostIds: [host.id], onlyUpdateInstalled: false, force: false }
                        : { hostIds: [host.id], onlyUpdateInstalled: false, force: true };
                  return (
                    <div key={host.id} className="settings-row">
                      <span className="settings-row-icon"><TerminalIcon size={18} /></span>
                      <div className="settings-row-label">
                        <strong>{host.name}</strong>
                        <p>
                          {versionDetail}
                          {variantsNote} · <code>{host.skillDir}</code>
                        </p>
                      </div>
                      <div className="settings-inline-controls">
                        <span className={`badge ${badge.cls}`}>{badge.label}</span>
                        <button
                          className={
                            host.status === 'outdated' || host.status === 'legacy' || host.status === 'path-stale'
                              ? 'button primary sm'
                              : 'button secondary sm'
                          }
                          disabled={syncingSkills || !skillsState.sourceAvailable}
                          onClick={() => void onSyncSkills?.(actionOpts)}
                        >
                          {actionLabel}
                        </button>
                      </div>
                    </div>
                  );
                })
              )}
            </div>
          </section>

          <section className="settings-group">
            <h2>{t("settings.aboutEdithere")}</h2>
            <div className="settings-card-stack" style={{ padding: 20, gap: 14 }}>
              <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', flexWrap: 'wrap', gap: 12 }}>
                <img
                  src={isDark ? './brand/edithere-lockup-light.svg' : './brand/edithere-lockup.svg'}
                  alt="EditHere"
                  style={{ height: 30, width: 'auto' }}
                />
                <span className="badge accent">
                  {desktopConnected ? doctor.desktopVersion : productVersion}
                </span>
              </div>

              <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', flexWrap: 'wrap', gap: 10 }}>
                <div style={{ display: 'flex', alignItems: 'center', flexWrap: 'wrap', gap: 8 }}>
                  {[
                    { label: 'GitHub', url: 'https://github.com/Inginnng/EditHere' },
                    { label: 'AtomGit', url: 'https://atomgit.com/Inginnng/EditHere' },
                    { label: 'Gitee', url: 'https://gitee.com/InnGing/EditHere' },
                  ].map((repo) => (
                    <button
                      key={repo.label}
                      className="button secondary sm"
                      title={t("settings.openTheRepository", { repository: repo.label })}
                      onClick={() => openExternalUrl(repo.url)}
                    >
                      {repo.label} <ExternalLinkIcon size={14} />
                    </button>
                  ))}
                </div>
                <button className="button ghost sm" onClick={() => void onResetDefaults()}>
                  <ResetIcon size={14} />{t("settings.restoreAllDefaults")}</button>
              </div>
            </div>
          </section>
        </>
      )}
    </div>
  );
}
