import * as DropdownMenu from '@radix-ui/react-dropdown-menu';
import * as Switch from '@radix-ui/react-switch';
import {
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
import { INITIAL_DOCTOR_SNAPSHOT } from '../bridge/mockBridge';
import type { GeneralPreferences, ThemeMode } from '../bridge/types';

interface SettingsViewProps {
  theme: ThemeMode;
  onChangeTheme: (theme: ThemeMode) => void;
  preferences: GeneralPreferences;
  onChangePreferences: (next: GeneralPreferences) => void;
  onToast: (message: string) => void;
}

export default function SettingsView({
  theme,
  onChangeTheme,
  preferences,
  onChangePreferences,
  onToast,
}: SettingsViewProps) {
  const doctor = INITIAL_DOCTOR_SNAPSHOT;

  const copyMcpSnippet = async () => {
    try {
      await navigator.clipboard.writeText(doctor.unifiedMcpSnippet);
      onToast('已复制统一 MCP 配置 JSON（cli/unified-mcp.mjs）');
    } catch {
      onToast('剪贴板访问受限');
    }
  };

  return (
    <div className="page-container">
      <div className="page-heading">
        <div>
          <h1>设置与统一接入诊断</h1>
          <p>采用单一分层设置列表（替代旧版滚动选项卡与原生下拉框），并直观展示 M2 统一 CLI / MCP 环境状态。</p>
        </div>
      </div>

      <section className="settings-group">
        <h2>外观与界面</h2>
        <div className="settings-card-stack">
          <div className="settings-row">
            <span className="settings-row-icon"><Palette size={19} /></span>
            <div className="settings-row-label">
              <strong>应用主题</strong>
              <p>所有页面与浮层共享同一套语义 Design Token，切换后即时响应。</p>
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
              <p>统一替代旧版系统原生 ComboBox，与当前弹窗语言保持一致。</p>
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
                        onToast(`已选择语言：${lang}（正式版接管 i18n）`);
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
              <p>框选区域自动在本机识别文本写入 <code>ocrtext</code>，帮助 AI 精准锁定目标控件。</p>
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
        <h2>统一 CLI / MCP 与运行环境自检（M2 Doctor）</h2>
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
                {item.status === 'ok' ? '已就绪' : '空壳预览'}
              </span>
            </div>
          ))}

          <div className="settings-row">
            <span className="settings-row-icon"><Terminal size={19} /></span>
            <div className="settings-row-label">
              <strong>统一 MCP 接入配置（自动路由桌面或 Web）</strong>
              <p>执行 <code>node cli/edithere.mjs init</code> 可一键向 Codex / Claude / Cursor / Trae 安装英文单源 Skill。</p>
            </div>
            <button className="button primary sm" onClick={copyMcpSnippet}>
              <Copy size={13} /> 复制 MCP 配置
            </button>
          </div>
        </div>
      </section>
    </div>
  );
}
