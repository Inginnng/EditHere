import { useState, type FormEvent } from 'react';
import * as DropdownMenu from '@radix-ui/react-dropdown-menu';
import {
  ArrowLeft,
  Check,
  Copy,
  Eye,
  EyeOff,
  LoaderCircle,
  MoreHorizontal,
  Pencil,
  Plus,
  RotateCcw,
  Search,
  Settings2,
  Star,
} from 'lucide-react';
import { PROVIDER_PRESETS } from '../bridge/mockBridge';
import type { ProviderConfig, ProviderPreset } from '../bridge/types';

interface ProvidersViewProps {
  providers: ProviderConfig[];
  defaultProviderId: string;
  onUpdateProviders: (next: ProviderConfig[]) => void;
  onChangeDefault: (id: string) => void;
  onToast: (message: string) => void;
}

const presetFor = (id: string): ProviderPreset =>
  PROVIDER_PRESETS.find((p) => p.id === id) ?? PROVIDER_PRESETS[PROVIDER_PRESETS.length - 1];

export default function ProvidersView({
  providers,
  defaultProviderId,
  onUpdateProviders,
  onChangeDefault,
  onToast,
}: ProvidersViewProps) {
  const [query, setQuery] = useState('');
  const [draft, setDraft] = useState<ProviderConfig | null>(null);
  const [creating, setCreating] = useState(false);
  const [showKey, setShowKey] = useState(false);
  const [advancedOpen, setAdvancedOpen] = useState(false);
  const [error, setError] = useState('');
  const [testing, setTesting] = useState(false);

  const openEditor = (config?: ProviderConfig) => {
    setCreating(!config);
    setDraft(
      config
        ? { ...config }
        : {
            id: `custom-${Date.now()}`,
            presetId: 'openai',
            name: '新模型配置',
            model: 'gpt-6.1-sol',
            endpoint: 'https://api.openai.com/v1',
            protocol: 'responses',
            key: '',
            region: 'cn',
            timeout: '120',
            note: '',
            saved: false,
          }
    );
    setShowKey(false);
    setAdvancedOpen(false);
    setError('');
    setTesting(false);
  };

  const saveDraft = (e: FormEvent) => {
    e.preventDefault();
    if (!draft) return;
    if (!draft.name.trim()) {
      setError('请填写配置名称。');
      return;
    }
    if (!draft.model.trim()) {
      setError('请填写模型名称。');
      return;
    }
    try {
      const url = new URL(draft.endpoint);
      if (!['http:', 'https:'].includes(url.protocol)) throw new Error('protocol');
    } catch {
      setError('请输入有效的 HTTP / HTTPS 接口地址。');
      return;
    }
    const saved: ProviderConfig = {
      ...draft,
      name: draft.name.trim(),
      model: draft.model.trim(),
      endpoint: draft.endpoint.trim(),
      saved: Boolean(draft.key.trim()),
    };
    onUpdateProviders(
      creating
        ? [...providers, saved]
        : providers.map((item) => (item.id === saved.id ? saved : item))
    );
    setDraft(null);
    onToast(`已更新「${saved.name}」配置`);
  };

  const runDemoTest = () => {
    setTesting(true);
    setTimeout(() => {
      setTesting(false);
      onToast('连通性演示校验通过（未消耗真实额度）');
    }, 650);
  };

  if (draft) {
    const preset = presetFor(draft.presetId);
    return (
      <div className="page-container">
        <div className="page-heading">
          <div>
            <h1>{creating ? '添加模型服务' : `编辑 · ${draft.name}`}</h1>
            <p>配置可选「AI 解析」使用的视觉模型接口；密钥在真实桌面端存入系统凭据库。</p>
          </div>
          <button className="button secondary" onClick={() => setDraft(null)}>
            <ArrowLeft size={14} /> 返回列表
          </button>
        </div>

        <form className="editor-panel" onSubmit={saveDraft}>
          <div className="editor-panel-header">
            <strong>服务商预设</strong>
            <span className="badge neutral">{preset.company}</span>
          </div>

          <div className="editor-panel-body">
            <div className="preset-grid">
              {PROVIDER_PRESETS.map((item) => (
                <button
                  key={item.id}
                  type="button"
                  className={`preset-chip ${draft.presetId === item.id ? 'selected' : ''}`}
                  onClick={() =>
                    setDraft((prev) =>
                      prev
                        ? {
                            ...prev,
                            presetId: item.id,
                            name: item.name,
                            model: item.model,
                            endpoint: item.endpoint,
                            protocol: item.protocol,
                          }
                        : null
                    )
                  }
                >
                  {item.id === 'custom' ? (
                    <Settings2 size={15} />
                  ) : (
                    <img src={`./providers/${item.id}.svg`} alt="" width={16} height={16} />
                  )}
                  <span>{item.name}</span>
                </button>
              ))}
            </div>

            <div className="field-grid">
              <div className="field-block">
                <label htmlFor="cfg-name">配置名称</label>
                <input
                  id="cfg-name"
                  value={draft.name}
                  onChange={(e) => setDraft({ ...draft, name: e.target.value })}
                />
              </div>
              <div className="field-block">
                <label htmlFor="cfg-model">视觉模型</label>
                <input
                  id="cfg-model"
                  value={draft.model}
                  onChange={(e) => setDraft({ ...draft, model: e.target.value })}
                />
              </div>
              <div className="field-block full">
                <label htmlFor="cfg-key">API Key</label>
                <div className="input-with-action">
                  <input
                    id="cfg-key"
                    type={showKey ? 'text' : 'password'}
                    placeholder="sk-..."
                    value={draft.key}
                    onChange={(e) => setDraft({ ...draft, key: e.target.value })}
                  />
                  <button
                    type="button"
                    className="button secondary"
                    onClick={() => setShowKey((v) => !v)}
                  >
                    {showKey ? <EyeOff size={14} /> : <Eye size={14} />}
                    {showKey ? '隐藏' : '显示'}
                  </button>
                  <button type="button" className="button secondary" onClick={runDemoTest} disabled={testing}>
                    {testing && <LoaderCircle size={14} className="spin" />}
                    测试连接
                  </button>
                </div>
              </div>
            </div>

            <div>
              <button
                type="button"
                className="button ghost sm"
                onClick={() => setAdvancedOpen((v) => !v)}
              >
                {advancedOpen ? '收起高级设置 ▲' : '展开高级设置（地址 / 超时 / 补充提示词）▼'}
              </button>
            </div>

            {advancedOpen && (
              <div className="field-grid">
                <div className="field-block">
                  <label htmlFor="cfg-endpoint">API Base URL</label>
                  <input
                    id="cfg-endpoint"
                    value={draft.endpoint}
                    onChange={(e) => setDraft({ ...draft, endpoint: e.target.value })}
                  />
                </div>
                <div className="field-block">
                  <label htmlFor="cfg-timeout">超时（秒）</label>
                  <input
                    id="cfg-timeout"
                    value={draft.timeout}
                    onChange={(e) => setDraft({ ...draft, timeout: e.target.value })}
                  />
                </div>
                <div className="field-block full">
                  <label htmlFor="cfg-note">补充系统要求</label>
                  <textarea
                    id="cfg-note"
                    rows={3}
                    placeholder="例如：输出保持简短；优先按页面与组件分层列出…"
                    value={draft.note}
                    onChange={(e) => setDraft({ ...draft, note: e.target.value })}
                  />
                </div>
              </div>
            )}

            {error && <div style={{ color: 'var(--danger)', fontSize: 12 }}>{error}</div>}
          </div>

          <div className="editor-panel-footer">
            <span style={{ fontSize: 12, color: 'var(--muted)' }}>
              取消将丢弃本次未保存草稿
            </span>
            <div style={{ display: 'flex', gap: 8 }}>
              <button type="button" className="button secondary" onClick={() => setDraft(null)}>
                取消
              </button>
              <button type="submit" className="button primary">
                <Check size={14} /> 保存配置
              </button>
            </div>
          </div>
        </form>
      </div>
    );
  }

  const filtered = providers.filter((p) =>
    `${p.name} ${p.model} ${p.endpoint}`.toLowerCase().includes(query.toLowerCase())
  );

  return (
    <div className="page-container">
      <div className="page-heading">
        <div>
          <h1>模型服务</h1>
          <p>仅用于可选「AI 解析」把批注整理为纯文本；直接让 Codex / Claude Code 等 Agent 读图时无需配置。</p>
        </div>
        <button className="button primary" onClick={() => openEditor()}>
          <Plus size={14} /> 添加服务
        </button>
      </div>

      <div className="toolbar-row">
        <div className="search-field">
          <Search size={14} />
          <input
            aria-label="搜索模型配置"
            placeholder="搜索服务商、模型或接口域名…"
            value={query}
            onChange={(e) => setQuery(e.target.value)}
          />
        </div>
        <span className="badge neutral">共 {filtered.length} 项配置</span>
      </div>

      <div className="provider-list">
        {filtered.map((cfg) => {
          const isDefault = cfg.id === defaultProviderId;
          let host = '未填地址';
          try {
            if (cfg.endpoint) host = new URL(cfg.endpoint).host;
          } catch {
            host = cfg.endpoint;
          }

          return (
            <div key={cfg.id} className={`provider-card ${isDefault ? 'is-default' : ''}`}>
              <span className="provider-brand">
                {cfg.presetId === 'custom' ? (
                  <Settings2 size={18} />
                ) : (
                  <img src={`./providers/${cfg.presetId}.svg`} alt="" />
                )}
              </span>

              <div className="provider-info">
                <div className="provider-title-row">
                  <strong>{cfg.name}</strong>
                  {isDefault && <span className="badge accent">当前默认</span>}
                  {cfg.saved || cfg.key ? (
                    <span className="badge success">Key 已配置</span>
                  ) : (
                    <span className="badge neutral">未填 Key</span>
                  )}
                </div>
                <div className="provider-sub">
                  <code>{cfg.model || '未指定模型'}</code>
                  <span>·</span>
                  <code>{host}</code>
                </div>
              </div>

              <div className="provider-actions">
                {!isDefault && (
                  <button
                    className="button secondary sm"
                    onClick={() => {
                      onChangeDefault(cfg.id);
                      onToast(`已将「${cfg.name}」设为默认解析模型`);
                    }}
                  >
                    <Star size={13} /> 设为默认
                  </button>
                )}
                <button className="button secondary sm" onClick={() => openEditor(cfg)}>
                  <Pencil size={13} /> 编辑
                </button>
                <DropdownMenu.Root>
                  <DropdownMenu.Trigger asChild>
                    <button className="icon-button" aria-label="更多操作">
                      <MoreHorizontal size={16} />
                    </button>
                  </DropdownMenu.Trigger>
                  <DropdownMenu.Portal>
                    <DropdownMenu.Content className="dropdown-menu" align="end" sideOffset={6}>
                      <DropdownMenu.Item
                        className="dropdown-item"
                        onSelect={async () => {
                          if (!cfg.endpoint) return;
                          await navigator.clipboard.writeText(cfg.endpoint).catch(() => {});
                          onToast('已复制接口地址');
                        }}
                      >
                        <Copy size={13} /> 复制接口地址
                      </DropdownMenu.Item>
                      <DropdownMenu.Item
                        className="dropdown-item"
                        onSelect={() => {
                          const p = presetFor(cfg.presetId);
                          onUpdateProviders(
                            providers.map((x) =>
                              x.id === cfg.id ? { ...x, model: p.model, endpoint: p.endpoint, protocol: p.protocol } : x
                            )
                          );
                          onToast('已恢复默认端点与模型');
                        }}
                      >
                        <RotateCcw size={13} /> 恢复默认参数
                      </DropdownMenu.Item>
                    </DropdownMenu.Content>
                  </DropdownMenu.Portal>
                </DropdownMenu.Root>
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
}
