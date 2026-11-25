import { useState, type KeyboardEvent } from 'react';
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
  OcrIcon,
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
  OcrLanguageCode,
  ProviderConfig,
  SettingsTabId,
  SkillSyncState,
  SkillSyncStatus,
  ThemeMode,
  UpdateCheckResult,
  UpdateSourceId,
} from '../bridge/types';
import ProvidersView from './ProvidersView';

interface SettingsViewProps {
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

const LANGUAGE_LABELS: Record<LanguageCode, string> = {
  system: '跟随系统',
  zh_CN: '简体中文',
  zh_TW: '繁體中文',
  en: 'English',
};

const OCR_LANGUAGE_LABELS: Record<OcrLanguageCode, string> = {
  system: '跟随系统',
  zh_CN: '简体中文',
  en: 'English',
};

const TOOL_LABELS = ['智能选块', '点批注', '框选批注', '选择与移动'];

const UPDATE_SOURCE_LABELS: Record<UpdateSourceId, string> = {
  auto: '自动',
  atomgit: 'AtomGit',
  github: 'GitHub',
  gitee: 'Gitee',
};

function keyEventToPortable(event: KeyboardEvent<HTMLButtonElement>): string | null {
  const modifierKeys = new Set(['Control', 'Shift', 'Alt', 'Meta', 'OS', 'AltGraph']);
  if (modifierKeys.has(event.key)) return null;

  let keyName = '';
  if (event.code.startsWith('Key') && event.code.length === 4) {
    keyName = event.code.slice(3).toUpperCase();
  } else if (event.code.startsWith('Digit') && event.code.length === 6) {
    keyName = event.code.slice(5);
  } else if (/^F([1-9]|1\d|2[0-4])$/.test(event.key)) {
    keyName = event.key;
  } else {
    const special: Record<string, string> = {
      ArrowUp: 'Up',
      ArrowDown: 'Down',
      ArrowLeft: 'Left',
      ArrowRight: 'Right',
      Delete: 'Del',
      Escape: 'Esc',
      Insert: 'Ins',
      Backspace: 'Backspace',
      Enter: 'Return',
      Tab: 'Tab',
      Home: 'Home',
      End: 'End',
      PageUp: 'PgUp',
      PageDown: 'PgDown',
      PrintScreen: 'Print',
      Pause: 'Pause',
      ' ': 'Space',
    };
    keyName = special[event.key] ?? (event.key.length === 1 ? event.key.toUpperCase() : '');
  }
  if (!keyName) return null;

  const mods: string[] = [];
  if (event.ctrlKey) mods.push('Ctrl');
  if (event.altKey) mods.push('Alt');
  if (event.shiftKey) mods.push('Shift');
  if (event.metaKey) mods.push('Meta');
  return [...mods, keyName].join('+');
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
}: SettingsViewProps) {
  const [recordingId, setRecordingId] = useState<string | null>(null);
  const [shortcutError, setShortcutError] = useState('');
  const [checkingUpdate, setCheckingUpdate] = useState(false);
  const [updateStatus, setUpdateStatus] = useState<UpdateCheckResult | null>(null);
  const [exportingDiag, setExportingDiag] = useState(false);
  const [skillFilter, setSkillFilter] = useState<'detected' | 'installed'>('detected');

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
      onToast(sequence ? `已设置快捷键 ${sequence}` : '已停用该快捷键');
    }
  };

  const handleChooseFeedbackDir = async () => {
    if (!isDesktopWebView()) {
      onToast('请在桌面客户端中选择反馈目录');
      return;
    }
    try {
      const res = await desktopRpc<{ cancelled?: boolean; path?: string }>('settings.chooseFeedbackDir', {}, 0);
      if (!res.cancelled && typeof res.path === 'string') {
        await commitPatch({ feedbackDir: res.path }, '已更新反馈临时目录');
      }
    } catch (err) {
      onToast(err instanceof Error ? err.message : '选择目录失败');
    }
  };

  const handleExportDiagnostics = async () => {
    if (!isDesktopWebView()) {
      onToast('请在桌面客户端中导出诊断日志');
      return;
    }
    setExportingDiag(true);
    try {
      const res = await desktopRpc<{ saved?: boolean; path?: string }>('diagnostics.export', {}, 0);
      if (res.saved && res.path) {
        onToast(`已导出诊断日志：${res.path}`);
      }
      const fresh = await desktopRpc<DiagnosticsInfo>('diagnostics.getInfo').catch(() => null);
      if (fresh) onUpdateDiagnosticsInfo(fresh);
    } catch (err) {
      onToast(err instanceof Error ? err.message : '导出诊断日志失败');
    } finally {
      setExportingDiag(false);
    }
  };

  const handleOpenDiagnosticsFolder = async () => {
    if (!isDesktopWebView()) {
      onToast('请在桌面客户端中打开日志目录');
      return;
    }
    try {
      await desktopRpc('diagnostics.openFolder');
    } catch (err) {
      onToast(err instanceof Error ? err.message : '打开日志目录失败');
    }
  };

  const handleCheckUpdates = async () => {
    if (!isDesktopWebView()) {
      setUpdateStatus({ status: 'upToDate', message: '预览环境已是最新前端资源。' });
      return;
    }
    setCheckingUpdate(true);
    try {
      const res = await desktopRpc<UpdateCheckResult>('updates.check', {}, 35000);
      setUpdateStatus(res);
      if (res.message) onToast(res.message);
    } catch (err) {
      onToast(err instanceof Error ? err.message : '检查更新失败');
    } finally {
      setCheckingUpdate(false);
    }
  };

  const handleOpenReleases = async () => {
    const targetUrl = updateStatus?.url || '';
    if (isDesktopWebView()) {
      await desktopRpc('updates.openReleases', { url: targetUrl }).catch((err) => {
        onToast(err instanceof Error ? err.message : '无法打开发布页');
      });
    } else if (targetUrl) {
      window.open(targetUrl, '_blank', 'noopener,noreferrer');
    }
  };

  const skillStatusBadge = (status: SkillSyncStatus) => {
    switch (status) {
      case 'up-to-date':
        return { label: '已最新', cls: 'success' };
      case 'outdated':
        return { label: '可更新', cls: 'warning' };
      case 'legacy':
        return { label: '旧版', cls: 'warning' };
      case 'path-stale':
        return { label: '路径变更', cls: 'warning' };
      case 'newer':
        return { label: '版本更高', cls: 'accent' };
      default:
        return { label: '未安装', cls: 'neutral' };
    }
  };

  const detectedSkills = skillsState.hosts.filter((h) => h.hostPresent || h.installed);
  const installedSkills = detectedSkills.filter((h) => h.installed);
  const visibleSkills = skillFilter === 'installed' ? installedSkills : detectedSkills;

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
      onToast('已复制 MCP 配置 JSON');
    } catch {
      onToast('剪贴板访问受限');
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
          <h1>设置</h1>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 10, flexWrap: 'wrap' }}>
          <div className="segmented-control" role="tablist" aria-label="设置分页">
            {(
              [
                { id: 'general', label: '常规' },
                { id: 'shortcuts', label: '快捷键' },
                { id: 'capture', label: '截图与导出' },
                { id: 'providers', label: '模型服务' },
                { id: 'diagnostics', label: '诊断与关于' },
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
            <BackIcon size={14} /> 返回工作台
          </button>
        </div>
      </div>

      {activeTab === 'general' && (
        <>
          <section className="settings-group" style={{ marginTop: 0 }}>
            <h2>外观与语言</h2>
            <div className="settings-card-stack">
              <div className="settings-row">
                <span className="settings-row-icon"><ThemeIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>应用主题</strong>
                </div>
                <div className="segmented-control" aria-label="应用主题">
                  {([
                    { id: 'light', label: '浅色' },
                    { id: 'dark', label: '深色' },
                    { id: 'system', label: '跟随系统' },
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
                  <strong>界面语言</strong>
                </div>
                <DropdownMenu.Root>
                  <DropdownMenu.Trigger asChild>
                    <button className="button secondary sm">
                      <span>{LANGUAGE_LABELS[preferences.language]}</span>
                      <ChevronDownIcon size={14} />
                    </button>
                  </DropdownMenu.Trigger>
                  <DropdownMenu.Portal>
                    <DropdownMenu.Content className="dropdown-menu" align="end" sideOffset={6}>
                      {(['system', 'zh_CN', 'zh_TW', 'en'] as LanguageCode[]).map((code) => (
                        <DropdownMenu.Item
                          key={code}
                          className="dropdown-item"
                          onSelect={() => void commitPatch({ language: code }, `已切换界面语言：${LANGUAGE_LABELS[code]}`)}
                        >
                          {LANGUAGE_LABELS[code]}
                        </DropdownMenu.Item>
                      ))}
                    </DropdownMenu.Content>
                  </DropdownMenu.Portal>
                </DropdownMenu.Root>
              </div>

              <div className="settings-row">
                <span className="settings-row-icon"><OcrIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>文字识别语言</strong>
                </div>
                <DropdownMenu.Root>
                  <DropdownMenu.Trigger asChild>
                    <button className="button secondary sm">
                      <span>{OCR_LANGUAGE_LABELS[preferences.ocrLanguage]}</span>
                      <ChevronDownIcon size={14} />
                    </button>
                  </DropdownMenu.Trigger>
                  <DropdownMenu.Portal>
                    <DropdownMenu.Content className="dropdown-menu" align="end" sideOffset={6}>
                      {(['system', 'zh_CN', 'en'] as OcrLanguageCode[]).map((code) => (
                        <DropdownMenu.Item
                          key={code}
                          className="dropdown-item"
                          onSelect={() => void commitPatch({ ocrLanguage: code })}
                        >
                          {OCR_LANGUAGE_LABELS[code]}
                        </DropdownMenu.Item>
                      ))}
                    </DropdownMenu.Content>
                  </DropdownMenu.Portal>
                </DropdownMenu.Root>
              </div>
            </div>
          </section>

          <section className="settings-group">
            <h2>启动与窗口行为</h2>
            <div className="settings-card-stack">
              <div className="settings-row">
                <span className="settings-row-icon"><CaptureIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>启动后立即截图</strong>
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
                  <strong>开机时启动 EditHere</strong>
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
                  <strong>打开图片时自动适应窗口</strong>
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
                  <strong>关闭批注窗口时询问是否保存</strong>
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
                  <strong>默认标注工具</strong>
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
            <h2>系统托盘</h2>
            <div className="settings-card-stack">
              <div className="settings-row">
                <span className="settings-row-icon"><ClickActionIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>托盘图标单击</strong>
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
                  <strong>托盘图标双击</strong>
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
            <span style={{ fontSize: 12.5, color: 'var(--muted)' }}>
              点击录制新快捷键，清空则停用。
            </span>
            <button
              className="button secondary sm"
              onClick={() => {
                setShortcutError('');
                void onResetShortcuts();
              }}
            >
              <ResetIcon size={14} /> 恢复默认快捷键
            </button>
          </div>

          {shortcutError && <div className="settings-error-banner">{shortcutError}</div>}

          {[
            { title: '全局快捷键', list: globalShortcuts },
            { title: '应用内快捷键', list: localShortcuts },
          ].map((group) => (
            <section key={group.title} className="settings-group" style={group.title === '全局快捷键' ? { marginTop: 0 } : undefined}>
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
                            const combo = keyEventToPortable(e);
                            if (!combo) return;
                            setRecordingId(null);
                            void applyShortcut(def.id, combo);
                          }}
                        >
                          {recording ? '请按下组合键…' : val || '未绑定'}
                        </button>
                        {val && (
                          <button
                            type="button"
                            className="icon-button"
                            aria-label={`清空 ${def.label} 快捷键`}
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
            <h2>批注导出与 OCR</h2>
            <div className="settings-card-stack">
              <div className="settings-row">
                <span className="settings-row-icon"><OcrIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>导出时附带区域识别文字</strong>
                </div>
                <Switch.Root
                  className="ui-switch"
                  checked={preferences.includeOcr}
                  onCheckedChange={(v) => void commitPatch({ includeOcr: v, includeRecognizedText: v })}
                >
                  <Switch.Thumb className="ui-switch-thumb" />
                </Switch.Root>
              </div>

              <div className="settings-row">
                <span className="settings-row-icon"><WindowTitleIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>导出时附带窗口标题</strong>
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
                  <strong>默认反馈导出模式</strong>
                </div>
                <div className="segmented-control">
                  <button
                    className={preferences.exportMode === 'file' ? 'selected' : ''}
                    onClick={() => void commitPatch({ exportMode: 'file' })}
                  >
                    JSON + 原图文件
                  </button>
                  <button
                    className={preferences.exportMode === 'embedded' ? 'selected' : ''}
                    onClick={() => void commitPatch({ exportMode: 'embedded' })}
                  >
                    内嵌原图
                  </button>
                  <button
                    className={preferences.exportMode === 'none' ? 'selected' : ''}
                    onClick={() => void commitPatch({ exportMode: 'none' })}
                  >
                    仅 JSON
                  </button>
                </div>
              </div>

              <div className="settings-row">
                <span className="settings-row-icon"><CopyTextIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>复制 JSON 文本时携带原图信息</strong>
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
                  <strong>反馈临时目录</strong>
                  <p>{preferences.feedbackDir || '默认缓存目录'}</p>
                </div>
                <div className="settings-inline-controls">
                  {preferences.feedbackDir && (
                    <button
                      className="icon-button"
                      aria-label="恢复默认目录"
                      onClick={() => void commitPatch({ feedbackDir: '' }, '已恢复默认反馈目录')}
                    >
                      <DeleteIcon size={14} />
                    </button>
                  )}
                  <button className="button secondary sm" onClick={() => void handleChooseFeedbackDir()}>
                    选择目录…
                  </button>
                </div>
              </div>
            </div>
          </section>

          <section className="settings-group">
            <h2>截屏外观与长截图</h2>
            <div className="settings-card-stack">
              <div className="settings-row">
                <span className="settings-row-icon"><ShadowIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>桌面截图柔和投影</strong>
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
                  <strong>截图默认圆角（{preferences.captureCornerRadius} px）</strong>
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
                  <strong>长截图默认滚动方向</strong>
                </div>
                <div className="segmented-control">
                  <button
                    className={preferences.scrollAxis === 'vertical' ? 'selected' : ''}
                    onClick={() => void commitPatch({ scrollAxis: 'vertical' })}
                  >
                    纵向
                  </button>
                  <button
                    className={preferences.scrollAxis === 'horizontal' ? 'selected' : ''}
                    onClick={() => void commitPatch({ scrollAxis: 'horizontal' })}
                  >
                    横向
                  </button>
                </div>
              </div>

              <div className="settings-row">
                <span className="settings-row-icon"><CropToolIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>长截图自动裁掉固定页眉与页脚</strong>
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
                  <strong>超长截图分段拼接模式</strong>
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
            <h2>诊断日志</h2>
            <div className="settings-card-stack">
              <div className="settings-row">
                <span className="settings-row-icon"><ExportFileIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>本机运行日志与错误报告</strong>
                  <p>
                    <code>{diagnosticsInfo.directory || '尚未初始化'}</code> · {diagnosticsInfo.fileCount} 个日志文件
                  </p>
                  {diagnosticsInfo.lastError && (
                    <p style={{ color: 'var(--danger)' }}>最近写入异常：{diagnosticsInfo.lastError}</p>
                  )}
                </div>
                <div className="settings-inline-controls">
                  <button
                    className="button secondary sm"
                    onClick={() => void handleOpenDiagnosticsFolder()}
                  >
                    <OpenFolderIcon size={14} /> 打开日志目录
                  </button>
                  <button
                    className="button primary sm"
                    disabled={exportingDiag}
                    onClick={() => void handleExportDiagnostics()}
                  >
                    <ExportFileIcon size={14} /> {exportingDiag ? '导出中…' : '导出诊断日志…'}
                  </button>
                </div>
              </div>
            </div>
          </section>

          <section className="settings-group">
            <h2>版本与更新</h2>
            <div className="settings-card-stack">
              <div className="settings-row">
                <span className="settings-row-icon"><StatusOkIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>启动时自动检查更新</strong>
                  {updateStatus?.message && <p>{updateStatus.message}</p>}
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
                  <strong>更新源</strong>
                </div>
                <div className="settings-inline-controls">
                  <DropdownMenu.Root>
                    <DropdownMenu.Trigger asChild>
                      <button className="button secondary sm">
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
                            onSelect={() => void commitPatch({ updateSource: src })}
                          >
                            {UPDATE_SOURCE_LABELS[src]}
                          </DropdownMenu.Item>
                        ))}
                      </DropdownMenu.Content>
                    </DropdownMenu.Portal>
                  </DropdownMenu.Root>
                  <button
                    className="button secondary sm"
                    disabled={checkingUpdate}
                    onClick={() => void handleCheckUpdates()}
                  >
                    {checkingUpdate && <SpinnerIcon size={14} className="spin" />}
                    检查更新
                  </button>
                  <button className="button ghost sm" onClick={() => void handleOpenReleases()}>
                    打开发布页 <ExternalLinkIcon size={14} />
                  </button>
                </div>
              </div>
            </div>
          </section>

          <section className="settings-group">
            <h2>运行环境与 MCP 接入</h2>
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
                    {item.status === 'ok' ? '已就绪' : desktopConnected ? '待检查' : '预览模式'}
                  </span>
                </div>
              ))}

              <div className="settings-row">
                <span className="settings-row-icon"><TerminalIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>统一 MCP 配置</strong>
                  <p>执行 <code>node cli/edithere.mjs init</code> 安装 AI 编程助手 Skill。</p>
                </div>
                <button className="button secondary sm" onClick={() => void copyMcpSnippet()}>
                  <CopyTextIcon size={14} /> 复制 MCP 配置
                </button>
              </div>
            </div>
          </section>

          <section className="settings-group">
            <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: 10, gap: 10, flexWrap: 'wrap' }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: 10, flexWrap: 'wrap' }}>
                <h2 style={{ margin: 0 }}>AI 助手 Skill 版本与同步</h2>
                <span className="badge accent">当前程序内置 Skill v{skillsState.bundledSkillVersion}</span>
                <span className="badge neutral">
                  已检测到 {detectedSkills.length} 个（支持 {skillsState.hosts.length} 种）
                </span>
              </div>
              <div style={{ display: 'flex', alignItems: 'center', gap: 8, flexWrap: 'wrap' }}>
                <div className="segmented-control" aria-label="Agent 筛选">
                  <button
                    className={skillFilter === 'detected' ? 'selected' : ''}
                    onClick={() => setSkillFilter('detected')}
                  >
                    已检测到 ({detectedSkills.length})
                  </button>
                  <button
                    className={skillFilter === 'installed' ? 'selected' : ''}
                    onClick={() => setSkillFilter('installed')}
                  >
                    仅已安装 ({installedSkills.length})
                  </button>
                </div>
                <button
                  className="button secondary sm"
                  disabled={syncingSkills}
                  onClick={() => void onRefreshSkills?.()}
                >
                  <ResetIcon size={14} /> 刷新状态
                </button>
                <button
                  className="button primary sm"
                  disabled={syncingSkills || !skillsState.sourceAvailable || actionableSkills.length === 0}
                  onClick={() => void onSyncSkills?.({ onlyUpdateInstalled: true })}
                >
                  {syncingSkills && <SpinnerIcon size={14} className="spin" />}
                  一键同步全部旧版{actionableSkills.length > 0 ? ` (${actionableSkills.length})` : ''}
                </button>
              </div>
            </div>
            <div className="settings-card-stack">
              <div className="settings-row">
                <span className="settings-row-icon"><ShieldOkIcon size={18} /></span>
                <div className="settings-row-label">
                  <strong>软件启动时自动同步旧版 Skill</strong>
                  <p>仅自动更新已安装 EditHere Skill 且版本落后或路径变更的 Agent，不向未安装的 Agent 写入，也不降级覆盖更高版本。</p>
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
                    <strong>未找到内置 Skill 目录（skills/edithere）</strong>
                    <p>请保留完整程序目录下的 skills 文件夹后再执行同步。</p>
                  </div>
                </div>
              )}

              {visibleSkills.length === 0 ? (
                <div className="settings-row">
                  <span className="settings-row-icon"><TerminalIcon size={18} /></span>
                  <div className="settings-row-label">
                    <strong>
                      {skillFilter === 'installed'
                        ? '尚未在已检测到的 Agent 中安装 EditHere Skill'
                        : '当前用户目录下未检测到受支持的 AI 助手'}
                    </strong>
                    <p>
                      {skillFilter === 'installed' && detectedSkills.length > 0
                        ? `已检测到 ${detectedSkills.length} 个 Agent 目录，可切回“已检测到”列表按需点击安装。`
                        : '支持 Codex、Claude Code、Cursor、Antigravity、Windsurf、Kiro、Trae / Trae CN、WorkBuddy、CodeBuddy、Qoder / QoderWork、DSH、Grok、ZCode、OpenCode、Cline 等；安装后点击“刷新状态”即可识别。'}
                    </p>
                  </div>
                  {skillFilter === 'installed' && detectedSkills.length > 0 && (
                    <button className="button secondary sm" onClick={() => setSkillFilter('detected')}>
                      查看已检测 Agent
                    </button>
                  )}
                </div>
              ) : (
                visibleSkills.map((host) => {
                  const badge = skillStatusBadge(host.status);
                  const versionDetail = host.installed
                    ? host.installedVersion
                      ? `已安装 v${host.installedVersion}`
                      : '旧版安装（无版本记录）'
                    : '已检测到 Agent，尚未安装 EditHere Skill';
                  const variantsNote =
                    host.otherVariants && host.otherVariants.length > 0
                      ? ` · 另检测到 ${host.otherVariants.join('、')}`
                      : '';
                  const actionLabel =
                    host.status === 'outdated' || host.status === 'legacy'
                      ? '更新'
                      : host.status === 'path-stale'
                        ? '刷新路径'
                        : host.status === 'not-installed'
                          ? '安装'
                          : host.status === 'newer'
                            ? '强制切换'
                            : '重装';
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
            <h2>关于 EditHere</h2>
            <div className="settings-card-stack" style={{ padding: 20, gap: 14 }}>
              <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', flexWrap: 'wrap', gap: 12 }}>
                <img
                  src={isDark ? './brand/edithere-lockup-light.svg' : './brand/edithere-lockup.svg'}
                  alt="EditHere"
                  style={{ height: 30, width: 'auto' }}
                />
                <span className="badge accent">
                  {desktopConnected ? doctor.desktopVersion : '0.14.1'}
                </span>
              </div>

              <div style={{ display: 'grid', gap: 8 }}>
                {[
                  { label: 'GitHub', url: 'https://github.com/Inginnng/EditHere' },
                  { label: 'AtomGit', url: 'https://atomgit.com/qq_30293075/EditHere' },
                  { label: 'Gitee', url: 'https://gitee.com/InnGing/EditHere' },
                ].map((repo) => (
                  <div
                    key={repo.label}
                    style={{
                      display: 'flex',
                      alignItems: 'center',
                      justifyContent: 'space-between',
                      gap: 10,
                      flexWrap: 'wrap',
                      fontSize: 12.5,
                    }}
                  >
                    <div style={{ display: 'flex', alignItems: 'center', gap: 8, minWidth: 0, flexWrap: 'wrap' }}>
                      <strong style={{ minWidth: 64 }}>{repo.label}</strong>
                      <code style={{ wordBreak: 'break-all' }}>{repo.url}</code>
                    </div>
                    <button
                      className="button ghost sm"
                      onClick={() => openExternalUrl(repo.url)}
                    >
                      访问仓库 <ExternalLinkIcon size={14} />
                    </button>
                  </div>
                ))}
              </div>

              <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'flex-end', flexWrap: 'wrap', gap: 10 }}>
                <button className="button secondary sm" onClick={() => void onResetDefaults()}>
                  <ResetIcon size={14} /> 恢复全部默认设置
                </button>
              </div>
            </div>
          </section>
        </>
      )}
    </div>
  );
}
