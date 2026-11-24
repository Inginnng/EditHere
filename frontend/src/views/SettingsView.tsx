import * as DropdownMenu from '@radix-ui/react-dropdown-menu';
import * as Switch from '@radix-ui/react-switch';
import {
  ArrowLeft,
  CheckCircle2,
  ChevronDown,
  Copy,
  FileCode2,
  Globe,
  Layers,
  Palette,
  ScanText,
  ShieldCheck,
  SlidersHorizontal,
  Terminal,
} from 'lucide-react';
import { desktopRpc, isDesktopWebView } from '../bridge/desktopBridge';
import type {
  DoctorSnapshot,
  GeneralPreferences,
  ProviderConfig,
  SettingsTabId,
  ThemeMode,
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
  onChangePreferences: (next: GeneralPreferences) => void;
  doctor: DoctorSnapshot;
  desktopConnected?: boolean;
  providers: ProviderConfig[];
  defaultProviderId: string;
  onSaveProviders: (next: ProviderConfig[], nextDefaultId: string) => Promise<void> | void;
  onTestProvider: (cfg: ProviderConfig) => Promise<{ ok: boolean; message: string }>;
  onToast: (message: string) => void;
}

const SHORTCUTS = [
  { key: 'V / G', desc: '选择移动 / 拖动画布（或按住 Space 临时拖拽）' },
  { key: 'R / P / S', desc: '框选批注 / 点批注 / 智能吸附选块' },
  { key: 'B / M / C', desc: '画笔标记 / 马赛克遮罩 / 交互式裁剪' },
  { key: 'E / Esc', desc: '进入视觉大爆炸拆解 / 退出当前选择或裁剪' },
  { key: 'Ctrl + Z / Y', desc: '撤销 / 重做（含马赛克、画笔与裁剪还原）' },
  { key: 'H / Ctrl + I', desc: '显隐批注标记 / 导入本地图片或项目' },
];

export default function SettingsView({
  activeTab,
  onChangeTab,
  onBack,
  isDark,
  theme,
  onChangeTheme,
  preferences,
  onChangePreferences,
  doctor,
  desktopConnected = false,
  providers,
  defaultProviderId,
  onSaveProviders,
  onTestProvider,
  onToast,
}: SettingsViewProps) {
  const copyMcpSnippet = async () => {
    try {
      if (isDesktopWebView()) {
        await desktopRpc('clipboard.copyText', { text: doctor.unifiedMcpSnippet });
      } else {
        await navigator.clipboard.writeText(doctor.unifiedMcpSnippet);
      }
      onToast('已复制统一 MCP 配置 JSON（cli/unified-mcp.mjs）');
    } catch {
      onToast('剪贴板访问受限');
    }
  };

  return (
    <div className="page-container">
      <div className="page-heading">
        <div>
          <h1>设置与系统中心</h1>
          <p>统一管理外观、模型服务凭据、MCP/CLI 环境自检与关于信息。</p>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 10, flexWrap: 'wrap' }}>
          <div className="segmented-control" role="tablist" aria-label="设置分页">
            <button
              className={activeTab === 'general' ? 'selected' : ''}
              onClick={() => onChangeTab('general')}
            >
              常规与诊断
            </button>
            <button
              className={activeTab === 'providers' ? 'selected' : ''}
              onClick={() => onChangeTab('providers')}
            >
              模型服务
            </button>
            <button
              className={activeTab === 'about' ? 'selected' : ''}
              onClick={() => onChangeTab('about')}
            >
              关于 EditHere
            </button>
          </div>
          <button className="button secondary sm" onClick={onBack}>
            <ArrowLeft size={14} /> 返回工作台
          </button>
        </div>
      </div>

      {activeTab === 'general' && (
        <>
          <section className="settings-group" style={{ marginTop: 0 }}>
            <h2>外观与界面</h2>
            <div className="settings-card-stack">
              <div className="settings-row">
                <span className="settings-row-icon"><Palette size={19} /></span>
                <div className="settings-row-label">
                  <strong>应用主题</strong>
                  <p>所有页面与截屏浮层共享同一套建筑感几何语义 Token。</p>
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
                <span className="settings-row-icon"><SlidersHorizontal size={19} /></span>
                <div className="settings-row-label">
                  <strong>紧凑布局密度</strong>
                  <p>收窄卡片上下留白，在小尺寸屏幕下容纳更多设置行与批注条目。</p>
                </div>
                <Switch.Root
                  className="ui-switch"
                  checked={preferences.compactDensity}
                  onCheckedChange={(v) => onChangePreferences({ ...preferences, compactDensity: v })}
                >
                  <Switch.Thumb className="ui-switch-thumb" />
                </Switch.Root>
              </div>

              <div className="settings-row">
                <span className="settings-row-icon"><Globe size={19} /></span>
                <div className="settings-row-label">
                  <strong>界面语言</strong>
                  <p>与桌面端多语言词库同步。</p>
                </div>
                <DropdownMenu.Root>
                  <DropdownMenu.Trigger asChild>
                    <button className="button secondary sm">
                      <span>{preferences.language}</span>
                      <ChevronDown size={14} />
                    </button>
                  </DropdownMenu.Trigger>
                  <DropdownMenu.Portal>
                    <DropdownMenu.Content className="dropdown-menu" align="end" sideOffset={6}>
                      {(['简体中文', '繁體中文', 'English'] as const).map((lang) => (
                        <DropdownMenu.Item
                          key={lang}
                          className="dropdown-item"
                          onSelect={() => {
                            onChangePreferences({ ...preferences, language: lang });
                            onToast(`已选择语言：${lang}`);
                          }}
                        >
                          {lang}
                        </DropdownMenu.Item>
                      ))}
                    </DropdownMenu.Content>
                  </DropdownMenu.Portal>
                </DropdownMenu.Root>
              </div>
            </div>
          </section>

          <section className="settings-group">
            <h2>批注、OCR 与反馈导出</h2>
            <div className="settings-card-stack">
              <div className="settings-row">
                <span className="settings-row-icon"><ScanText size={19} /></span>
                <div className="settings-row-label">
                  <strong>导出时附带区域 OCR 文字</strong>
                  <p>框选区域自动在本机识别文本写入 <code>ocrtext</code>；已被马赛克遮挡区域会自动擦除，不泄漏隐私。</p>
                </div>
                <Switch.Root
                  className="ui-switch"
                  checked={preferences.includeOcr}
                  onCheckedChange={(v) => onChangePreferences({ ...preferences, includeOcr: v })}
                >
                  <Switch.Thumb className="ui-switch-thumb" />
                </Switch.Root>
              </div>

              <div className="settings-row">
                <span className="settings-row-icon"><Layers size={19} /></span>
                <div className="settings-row-label">
                  <strong>桌面截图柔和投影与圆角</strong>
                  <p>保留原图真实像素，仅在复制带效果图片或贴图窗口时应用柔和边缘。</p>
                </div>
                <Switch.Root
                  className="ui-switch"
                  checked={preferences.captureShadow}
                  onCheckedChange={(v) => onChangePreferences({ ...preferences, captureShadow: v })}
                >
                  <Switch.Thumb className="ui-switch-thumb" />
                </Switch.Root>
              </div>

              <div className="settings-row">
                <span className="settings-row-icon"><FileCode2 size={19} /></span>
                <div className="settings-row-label">
                  <strong>默认反馈导出模式</strong>
                  <p>Agent 会话默认使用 JSON + 同目录原图，减少上下文 Base64 占用。</p>
                </div>
                <div className="segmented-control">
                  <button
                    className={preferences.exportMode === 'file' ? 'selected' : ''}
                    onClick={() => onChangePreferences({ ...preferences, exportMode: 'file' })}
                  >
                    JSON + 同目录原图
                  </button>
                  <button
                    className={preferences.exportMode === 'embedded' ? 'selected' : ''}
                    onClick={() => onChangePreferences({ ...preferences, exportMode: 'embedded' })}
                  >
                    内嵌原图 DataURL
                  </button>
                </div>
              </div>
            </div>
          </section>

          <section className="settings-group">
            <h2>统一 CLI / MCP 与运行环境自检</h2>
            <div className="settings-card-stack">
              {doctor.checks.map((item) => (
                <div key={item.id} className="settings-row">
                  <span className="settings-row-icon">
                    {item.status === 'ok' ? (
                      <CheckCircle2 size={19} style={{ color: 'var(--success)' }} />
                    ) : (
                      <ShieldCheck size={19} style={{ color: 'var(--warning)' }} />
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
                <span className="settings-row-icon"><Terminal size={19} /></span>
                <div className="settings-row-label">
                  <strong>统一 MCP 接入配置（自动路由桌面或 Web）</strong>
                  <p>执行 <code>node cli/edithere.mjs init</code> 可一键向 Codex / Claude / Cursor / Trae 安装 Skill。</p>
                </div>
                <button className="button primary sm" onClick={copyMcpSnippet}>
                  <Copy size={13} /> 复制 MCP 配置
                </button>
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

      {activeTab === 'about' && (
        <div className="settings-card-stack" style={{ padding: 24, gap: 18 }}>
          <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', flexWrap: 'wrap', gap: 12 }}>
            <img
              src={isDark ? './brand/edithere-lockup-light.svg' : './brand/edithere-lockup.svg'}
              alt="EditHere"
              style={{ height: 32, width: 'auto' }}
            />
            <span className="badge accent">
              {desktopConnected ? doctor.desktopVersion : '0.14 B2 Web Shell'}
            </span>
          </div>

          <div>
            <h2 style={{ margin: '0 0 6px', fontSize: 16, fontWeight: 600 }}>EditHere · 改这里</h2>
            <p style={{ margin: 0, fontSize: 13, lineHeight: 1.75, color: 'var(--secondary)' }}>
              C++20 / Qt6 原生 HDR 截屏、8 锚点智能吸附选区、WinRT 本机离线 OCR、置顶贴图 + Edge WebView2 协同工作台。
              支持视觉大爆炸组件级拆解与位移跟踪、AI 不可见马赛克脱敏遮罩、画笔自由标注、交互式裁剪与无边栏双模批注（行内直达气泡 + 右侧抽屉）。
            </p>
          </div>

          <section className="settings-group" style={{ marginTop: 0 }}>
            <h2>常用快捷键一览</h2>
            <div className="settings-card-stack">
              {SHORTCUTS.map((s) => (
                <div key={s.key} className="settings-row" style={{ minHeight: 44, padding: '8px 14px' }}>
                  <code style={{
                    fontFamily: 'var(--mono)',
                    fontSize: 11.5,
                    fontWeight: 600,
                    padding: '3px 8px',
                    borderRadius: 'var(--r-micro)',
                    background: 'var(--fill)',
                    color: 'var(--ink)',
                    minWidth: 110,
                    textAlign: 'center',
                  }}>
                    {s.key}
                  </code>
                  <span style={{ fontSize: 12.5, color: 'var(--secondary)' }}>{s.desc}</span>
                </div>
              ))}
            </div>
          </section>
        </div>
      )}
    </div>
  );
}
