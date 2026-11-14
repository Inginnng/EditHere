import { useEffect, useRef, useState, type FormEvent } from 'react';
import * as DropdownMenu from '@radix-ui/react-dropdown-menu';
import * as Switch from '@radix-ui/react-switch';
import { ArrowLeft, ArrowUpRight, Check, ChevronDown, Copy, Eye, EyeOff, Globe2, KeyRound, LoaderCircle, MoreHorizontal, Pencil, Plus, RotateCcw, Search, Settings2, ShieldCheck, Sparkles, Star } from 'lucide-react';
import './provider-settings.css';

type Preset = { id: string; name: string; company: string; model: string; endpoint: string; protocol: string; docs: string; regional?: boolean };
type Configuration = { id: string; presetId: string; name: string; model: string; endpoint: string; protocol: string; key: string; region: string; timeout: string; note: string; saved: boolean };
type Props = { onToast: (message: string) => void; onProviderChange: (provider: string) => void };
const presets: Preset[] = [
  { id: 'openai', name: 'OpenAI', company: 'OpenAI', model: 'gpt-6.1-sol', endpoint: 'https://api.openai.com/v1', protocol: 'responses', docs: 'https://platform.openai.com' },
  { id: 'claude', name: 'Claude', company: 'Anthropic', model: 'claude-sonnet-5-5', endpoint: 'https://api.anthropic.com/v1', protocol: 'anthropic', docs: 'https://platform.claude.com' },
  { id: 'deepseek', name: 'DeepSeek', company: '深度求索', model: 'deepseek-flash', endpoint: 'https://api.deepseek.com', protocol: 'chat', docs: 'https://platform.deepseek.com' },
  { id: 'glm', name: 'GLM', company: '智谱 · Z.AI', model: 'glm-4.6v', endpoint: 'https://open.bigmodel.cn/api/paas/v4', protocol: 'chat', docs: 'https://open.bigmodel.cn', regional: true },
  { id: 'kimi', name: 'Kimi', company: '月之暗面', model: 'kimi-k3', endpoint: 'https://api.moonshot.cn/v1', protocol: 'chat', docs: 'https://platform.moonshot.cn', regional: true },
  { id: 'custom', name: '自定义服务', company: '兼容接口', model: '', endpoint: '', protocol: 'chat', docs: '' },
];
const presetFor = (id: string) => presets.find(preset => preset.id === id) ?? presets[5];
const fromPreset = (preset: Preset, id = preset.id): Configuration => ({ id, presetId: preset.id, name: preset.name, model: preset.model, endpoint: preset.endpoint, protocol: preset.protocol, key: '', region: 'cn', timeout: '120', note: '', saved: false });
const endpointHost = (endpoint: string) => { try { return new URL(endpoint).host; } catch { return '填写你的 API 地址'; } };

function Brand({ id, large = false }: { id: string; large?: boolean }) {
  return <span className={`provider-brand${large ? ' provider-brand-large' : ''}`} data-brand={id}>{id === 'custom' ? <Settings2 size={large ? 24 : 21} strokeWidth={1.65} /> : <img src={`./providers/${id}.svg`} alt="" />}</span>;
}

export default function ProviderSettings({ onToast, onProviderChange }: Props) {
  const [configurations, setConfigurations] = useState<Configuration[]>(() => presets.map(preset => fromPreset(preset)));
  const [defaultId, setDefaultId] = useState('openai');
  const [tab, setTab] = useState<'providers' | 'preferences'>('providers');
  const [query, setQuery] = useState('');
  const [draft, setDraft] = useState<Configuration | null>(null);
  const [creating, setCreating] = useState(false);
  const [showKey, setShowKey] = useState(false);
  const [advanced, setAdvanced] = useState(false);
  const [error, setError] = useState('');
  const [testing, setTesting] = useState(false);
  const testGeneration = useRef(0);
  const testTimer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);
  useEffect(() => () => { testGeneration.current++; clearTimeout(testTimer.current); }, []);
  const invalidateTest = () => { testGeneration.current++; clearTimeout(testTimer.current); setTesting(false); setTestResult(''); };
  const [testResult, setTestResult] = useState('');
  const [preferences, setPreferences] = useState({ coordinates: true, quotes: true, language: '简体中文' });
  const currentPreset = draft ? presetFor(draft.presetId) : presets[0];
  const openEditor = (configuration?: Configuration) => {
    invalidateTest();
    setCreating(!configuration); setDraft(configuration ? { ...configuration } : fromPreset(presets[0], `configuration-${Date.now()}`));
    setShowKey(false); setAdvanced(false); setError(''); setTestResult('');
  };
  const closeEditor = () => { invalidateTest(); setDraft(null); setError(''); };
  const update = (field: keyof Configuration, value: string) => { invalidateTest(); setDraft(previous => previous ? { ...previous, [field]: value } : previous); setError(''); };
  const selectPreset = (preset: Preset) => { invalidateTest(); setDraft(previous => previous ? fromPreset(preset, previous.id) : null); };
  const save = (event: FormEvent) => {
    event.preventDefault(); if (!draft) return;
    if (!draft.name.trim()) { setError('请填写配置名称。'); return; }
    if (!draft.model.trim()) { setError('请填写模型名称。'); return; }
    try { if (!['http:', 'https:'].includes(new URL(draft.endpoint).protocol)) throw new Error(); }
    catch { setError('请输入有效的 HTTP 或 HTTPS 地址。'); return; }
    if (!Number.isFinite(Number(draft.timeout)) || Number(draft.timeout) < 10 || Number(draft.timeout) > 600) { setError('请求超时应为 10 至 600 秒。'); return; }
    const saved = { ...draft, name: draft.name.trim(), model: draft.model.trim(), endpoint: draft.endpoint.trim(), saved: true };
    setConfigurations(previous => creating ? [...previous, saved] : previous.map(item => item.id === draft.id ? saved : item));
    if (defaultId === saved.id) onProviderChange(presetFor(saved.presetId).name);
    closeEditor(); onToast(`${saved.name} 配置已保存到本次会话`);
  };
  const makeDefault = (configuration: Configuration) => {
    setDefaultId(configuration.id); onProviderChange(presetFor(configuration.presetId).name); onToast(`已将 ${configuration.name} 设为默认解析服务`);
  };
  const copyEndpoint = async (configuration: Configuration) => {
    if (!configuration.endpoint) { onToast('这份配置尚未填写 API 地址'); return; }
    try { await navigator.clipboard.writeText(configuration.endpoint); onToast('API 地址已复制'); } catch { onToast('浏览器未允许复制，请在编辑页手动复制'); }
  };
  const resetConfiguration = (configuration: Configuration) => {
    setConfigurations(previous => previous.map(item => item.id === configuration.id ? { ...fromPreset(presetFor(item.presetId), item.id), name: item.name, key: item.key, saved: item.saved } : item));
    onToast('模型、地址和协议已恢复预设；Key 已保留');
  };
  const changeRegion = (region: string) => {
    if (!draft) return;
    invalidateTest();
    const endpoint = draft.presetId === 'glm' ? (region === 'cn' ? 'https://open.bigmodel.cn/api/paas/v4' : 'https://api.z.ai/api/paas/v4') : (region === 'cn' ? 'https://api.moonshot.cn/v1' : 'https://api.moonshot.ai/v1');
    setDraft({ ...draft, region, endpoint });
  };
  const test = () => {
    invalidateTest();
    const generation = testGeneration.current;
    setTesting(true);
    testTimer.current = setTimeout(() => {
      if (generation !== testGeneration.current) return;
      setTesting(false); setTestResult('本地演示已完成。未向服务商发送请求。');
    }, 650);
  };
  const filtered = configurations.filter(configuration => `${configuration.name} ${configuration.model} ${configuration.endpoint}`.toLowerCase().includes(query.toLowerCase()));

  return <section className="provider-page">
    {draft ? <form className="provider-edit-page" onSubmit={save}>
      <button className="provider-back-button" type="button" onClick={closeEditor}><ArrowLeft size={16} />模型服务</button>
      <header className="provider-edit-heading"><Brand id={draft.presetId} large /><div><h1>{creating ? '添加配置' : `编辑 ${draft.name}`}</h1><p>{creating ? '选择服务商，填写你的连接信息。' : '更新模型、API 地址和访问密钥。'}</p></div></header>
      {creating && <section className="provider-edit-section"><h2>选择预设</h2><div className="provider-preset-grid">{presets.map(preset => <button key={preset.id} type="button" onClick={() => selectPreset(preset)} aria-pressed={draft.presetId === preset.id} className={`provider-preset-tile${draft.presetId === preset.id ? ' provider-preset-selected' : ''}`}><Brand id={preset.id} /><span>{preset.name}</span>{draft.presetId === preset.id && <Check size={13} />}</button>)}</div></section>}
      <section className="provider-edit-section"><h2>基本信息</h2><div className="provider-form-group"><div className="provider-form-row"><div className="provider-form-caption"><label htmlFor="provider-config-name">配置名称</label><p>在服务商列表中显示的名称。</p></div><input id="provider-config-name" maxLength={48} value={draft.name} onChange={event => update('name', event.target.value)} placeholder="例如：我的 OpenAI" /></div><div className="provider-form-row"><div className="provider-form-caption"><span>服务商</span></div><div className="provider-readonly-value"><Brand id={draft.presetId} /><span>{currentPreset.name}</span>{currentPreset.docs && <a href={currentPreset.docs} target="_blank" rel="noreferrer">开放平台<ArrowUpRight size={13} /></a>}</div></div></div></section>
      <section className="provider-edit-section"><h2>连接设置</h2><div className="provider-form-group">
        {currentPreset.regional && <div className="provider-form-row"><div className="provider-form-caption"><label htmlFor="provider-region">服务区域</label><p>地区变更会同步更新默认地址。</p></div><div className="provider-select"><select id="provider-region" value={draft.region} onChange={event => changeRegion(event.target.value)}><option value="cn">中国大陆</option><option value="global">全球站</option></select><ChevronDown size={14} /></div></div>}
        <div className="provider-form-row"><div className="provider-form-caption"><label htmlFor="provider-model">模型名称</label><p>使用支持图片输入的模型。</p></div><input id="provider-model" value={draft.model} onChange={event => update('model', event.target.value)} placeholder="输入模型 ID" list={`provider-models-${draft.presetId}`} spellCheck={false} autoComplete="off" /><datalist id={`provider-models-${draft.presetId}`}>{currentPreset.model && <option value={currentPreset.model} />}</datalist></div>
        <div className="provider-form-row"><div className="provider-form-caption"><label htmlFor="provider-protocol">API 协议</label></div><div className="provider-select"><select id="provider-protocol" value={draft.protocol} onChange={event => update('protocol', event.target.value)}><option value="responses">OpenAI Responses</option><option value="chat">Chat Completions</option><option value="anthropic">Anthropic Messages</option></select><ChevronDown size={14} /></div></div>
        <div className="provider-form-row"><div className="provider-form-caption"><label htmlFor="provider-endpoint">API 地址</label><p>填写基础地址，无需追加请求路径。</p></div><div className="provider-endpoint-input"><Globe2 size={15} /><input id="provider-endpoint" value={draft.endpoint} onChange={event => update('endpoint', event.target.value)} placeholder="https://api.example.com/v1" spellCheck={false} autoComplete="off" /></div></div>
        <div className="provider-form-row"><div className="provider-form-caption"><label htmlFor="provider-key">API Key</label><p>只保留在当前预览会话中。</p></div><div className="provider-key-input"><input id="provider-key" type={showKey ? 'text' : 'password'} value={draft.key} onChange={event => update('key', event.target.value)} autoComplete="off" placeholder="输入 API Key" spellCheck={false} /><button type="button" onClick={() => setShowKey(value => !value)} aria-label={showKey ? '隐藏 API Key' : '显示 API Key'}>{showKey ? <EyeOff size={16} /> : <Eye size={16} />}</button></div></div>
      </div></section>
      <section className="provider-edit-section"><button type="button" className="provider-advanced-toggle" aria-expanded={advanced} onClick={() => setAdvanced(value => !value)}><Settings2 size={16} /><span>高级选项</span><ChevronDown size={15} className={advanced ? 'provider-chevron-open' : ''} /></button>{advanced && <div className="provider-form-group provider-advanced-group"><div className="provider-form-row"><div className="provider-form-caption"><label htmlFor="provider-timeout">请求超时</label><p>单位：秒，范围 10–600。</p></div><input id="provider-timeout" type="number" min={10} max={600} value={draft.timeout} onChange={event => update('timeout', event.target.value)} /></div><div className="provider-form-row provider-form-row-textarea"><div className="provider-form-caption"><label htmlFor="provider-note">补充指令</label><p>在内置提示词之后添加。</p></div><textarea id="provider-note" rows={3} value={draft.note} onChange={event => update('note', event.target.value)} placeholder="例如：保留组件名称，以列表输出修改意见。" /></div></div>}</section>
      {error && <p className="provider-validation-error" role="alert">{error}</p>}{testResult && <p className="provider-test-result" role="status"><Check size={14} />{testResult}</p>}
      <footer className="provider-editor-footer"><button type="button" className="provider-button provider-button-secondary" disabled={testing} onClick={test}>{testing ? <LoaderCircle className="provider-spin" size={15} /> : <Sparkles size={15} />}{testing ? '演示中…' : '演示连接'}</button><div><button type="button" className="provider-button provider-button-secondary" onClick={closeEditor}>取消</button><button type="submit" className="provider-button provider-button-primary"><Check size={15} />保存配置</button></div></footer>
    </form> : <>
      <header className="provider-page-heading"><div><h1>模型服务</h1><p>管理解析截图和批注时使用的 AI 服务。</p></div><button className="provider-button provider-button-primary" onClick={() => openEditor()}><Plus size={16} />添加配置</button></header>
      <div className="provider-toolbar"><div className="provider-tabs" role="tablist" aria-label="模型设置分类"><button role="tab" aria-selected={tab === 'providers'} className={tab === 'providers' ? 'provider-tab-active' : ''} onClick={() => setTab('providers')}>服务商<span>{configurations.length}</span></button><button role="tab" aria-selected={tab === 'preferences'} className={tab === 'preferences' ? 'provider-tab-active' : ''} onClick={() => setTab('preferences')}>解析偏好</button></div>{tab === 'providers' && <label className="provider-search"><Search size={15} /><input placeholder="搜索名称或模型" aria-label="搜索服务商" value={query} onChange={event => setQuery(event.target.value)} /></label>}</div>
      {tab === 'providers' ? <><div className="provider-list-summary"><span>{configurations.length} 份配置</span><span><KeyRound size={13} />{configurations.filter(configuration => configuration.key.trim()).length} 份已填写 Key</span></div><div className="provider-list" aria-label="服务商列表">{filtered.map(configuration => <article key={configuration.id} className={`provider-configuration-card${configuration.id === defaultId ? ' provider-card-default' : ''}`}>
        <Brand id={configuration.presetId} /><div className="provider-card-identity"><div><h2>{configuration.name}</h2>{configuration.id === defaultId && <span className="provider-default-badge">默认解析</span>}</div><p><span>{configuration.model || '待选择模型'}</span><span className="provider-meta-dot">·</span><span>{endpointHost(configuration.endpoint)}</span></p></div>
        <span className={`provider-key-status${configuration.key.trim() ? ' provider-key-filled' : ''}`}><span />{configuration.key.trim() ? '本次会话已配置' : '未填写 Key'}</span>
        <div className="provider-card-actions">{configuration.id !== defaultId && <button className="provider-default-button" onClick={() => makeDefault(configuration)} aria-label={`将 ${configuration.name} 设为默认`} title="设为默认解析服务"><Star size={15} /></button>}<button className="provider-button provider-button-secondary provider-configure-button" onClick={() => openEditor(configuration)}><Pencil size={14} />{configuration.saved ? '编辑' : '配置'}</button><DropdownMenu.Root><DropdownMenu.Trigger asChild><button className="provider-more-button" aria-label={`${configuration.name} 更多操作`}><MoreHorizontal size={18} /></button></DropdownMenu.Trigger><DropdownMenu.Portal><DropdownMenu.Content className="provider-menu" sideOffset={6} align="end">{configuration.id !== defaultId && <DropdownMenu.Item className="provider-menu-item" onSelect={() => makeDefault(configuration)}><Star size={14} />设为默认</DropdownMenu.Item>}<DropdownMenu.Item className="provider-menu-item" onSelect={() => void copyEndpoint(configuration)}><Copy size={14} />复制 API 地址</DropdownMenu.Item><DropdownMenu.Separator className="provider-menu-separator" /><DropdownMenu.Item className="provider-menu-item" onSelect={() => resetConfiguration(configuration)}><RotateCcw size={14} />恢复预设</DropdownMenu.Item></DropdownMenu.Content></DropdownMenu.Portal></DropdownMenu.Root></div>
      </article>)}{filtered.length === 0 && <div className="provider-empty"><Search size={23} /><h2>没有匹配的配置</h2><p>试试其他服务商名称或模型名称。</p><button className="provider-button provider-button-secondary" onClick={() => setQuery('')}>清除搜索</button></div>}</div></> : <div className="provider-preferences"><h2>解析内容</h2><div className="provider-form-group"><div className="provider-preference-row"><div><strong>保留精确坐标</strong><p>保留批注中的移动距离、尺寸和定位数值。</p></div><Switch.Root className="provider-switch" checked={preferences.coordinates} onCheckedChange={value => setPreferences(previous => ({ ...previous, coordinates: value }))} aria-label="保留精确坐标"><Switch.Thumb className="provider-switch-thumb" /></Switch.Root></div><div className="provider-preference-row"><div><strong>保留批注原话</strong><p>引用原始批注，减少理解过程中的信息损失。</p></div><Switch.Root className="provider-switch" checked={preferences.quotes} onCheckedChange={value => setPreferences(previous => ({ ...previous, quotes: value }))} aria-label="保留批注原话"><Switch.Thumb className="provider-switch-thumb" /></Switch.Root></div><div className="provider-preference-row"><div><label htmlFor="provider-language">输出语言</label><p>修改意见默认使用的语言。</p></div><div className="provider-select"><select id="provider-language" value={preferences.language} onChange={event => setPreferences(previous => ({ ...previous, language: event.target.value }))}><option>简体中文</option><option>English</option><option>跟随批注语言</option></select><ChevronDown size={14} /></div></div></div><footer className="provider-preferences-footer"><button className="provider-button provider-button-primary" onClick={() => onToast('解析偏好已保存到本次会话')}><Check size={15} />保存偏好</button></footer></div>}
    </>}
    <p className="provider-preview-note"><ShieldCheck size={13} />本地交互预览 · 密钥仅保留在当前会话 · 不发送真实 API 请求</p>
  </section>;
}
