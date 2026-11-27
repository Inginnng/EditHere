import { useI18n } from '../i18n';
import { useState, type FormEvent } from 'react';
import * as DropdownMenu from '@radix-ui/react-dropdown-menu';
import {
  BackIcon,
  CheckIcon,
  ConfigSlidersIcon,
  CopyTextIcon,
  EditIcon,
  HideIcon,
  MoreIcon,
  PlusIcon,
  ResetIcon,
  SearchIcon,
  ShowIcon,
  SpinnerIcon,
  StarIcon,
} from '../components/AppIcons';
import { providerPresets } from '../bridge/mockBridge';
import type { ProviderConfig } from '../bridge/types';

interface ProvidersViewProps {
  providers: ProviderConfig[];
  defaultProviderId: string;
  onSaveProviders: (next: ProviderConfig[], nextDefaultId: string) => Promise<void> | void;
  onTestProvider: (cfg: ProviderConfig) => Promise<{ ok: boolean; message: string }>;
  onToast: (message: string) => void;
  embedded?: boolean;
}

function providerIconSrc(presetId: string): string | null {
  if (presetId === 'custom') return null;
  if (presetId.startsWith('glm')) return './providers/glm.svg';
  if (presetId.startsWith('kimi')) return './providers/kimi.svg';
  if (presetId === 'openai' || presetId === 'claude' || presetId === 'deepseek') {
    return `./providers/${presetId}.svg`;
  }
  return null;
}

export default function ProvidersView({
  providers,
  defaultProviderId,
  onSaveProviders,
  onTestProvider,
  onToast,
  embedded = false,
}: ProvidersViewProps) {
  const t = useI18n();
  const presets = providerPresets(t);
  const presetFor = (id: string) => presets.find((preset) => preset.id === id) ?? presets[presets.length - 1];
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
        ? { ...config, key: '', keyEdited: false }
        : {
            id: `custom-${Date.now().toString(36)}`,
            presetId: 'custom',
            name: t("providers.newModelConfiguration"),
            model: 'gpt-6.1-sol',
            endpoint: 'https://api.openai.com/v1',
            protocol: 'responses',
            key: '',
            keyEdited: false,
            region: 'cn',
            timeout: '120',
            note: '',
            saved: false,
          }
    );
    setShowKey(false);
    setAdvancedOpen(!config || config.presetId === 'custom');
    setError('');
    setTesting(false);
  };

  const saveDraft = async (e: FormEvent) => {
    e.preventDefault();
    if (!draft) return;
    if (!draft.name.trim()) {
      setError(t("providers.enterAConfigurationName"));
      return;
    }
    if (!draft.model.trim()) {
      setError(t("providers.enterAModelName"));
      return;
    }
    try {
      const url = new URL(draft.endpoint.trim());
      if (!['http:', 'https:'].includes(url.protocol)) throw new Error('protocol');
    } catch {
      setError(t("providers.enterAValidHttpOrHttpsEndpoint"));
      return;
    }

    const nextSavedFlag = draft.keyEdited ? Boolean(draft.key.trim()) : draft.saved;
    const saved: ProviderConfig = {
      ...draft,
      name: draft.name.trim(),
      model: draft.model.trim(),
      endpoint: draft.endpoint.trim(),
      saved: nextSavedFlag,
    };
    const nextList = creating
      ? [...providers, saved]
      : providers.map((item) => (item.id === saved.id ? saved : item));

    await onSaveProviders(nextList, defaultProviderId);
    setDraft(null);
    onToast(t("providers.saved", { name: saved.name }));
  };

  const runTestConnection = async () => {
    if (!draft) return;
    setTesting(true);
    setError('');
    try {
      const res = await onTestProvider(draft);
      if (res.ok) {
        onToast(res.message);
      } else {
        setError(res.message);
      }
    } finally {
      setTesting(false);
    }
  };

  if (draft) {
    const preset = presetFor(draft.presetId);
    const keyPlaceholder =
      draft.saved && !draft.keyEdited
        ? draft.keyPreview
          ? t("providers.storedInSystemCredentialsLeaveBlankTo", { preview: draft.keyPreview })
          : t("providers.storedInSystemCredentialsLeaveBlankToTooltip")
        : 'sk-...';

    return (
      <div>
        <div className="page-heading" style={embedded ? { marginBottom: 16 } : undefined}>
          <div>
            <h2 style={embedded ? { margin: '0 0 4px', fontSize: 17, fontWeight: 600 } : undefined}>
              {creating ? t("providers.addModelProvider") : t("providers.edit", { name: draft.name })}
            </h2>
          </div>
          <button className="button secondary sm" onClick={() => setDraft(null)}>
            <BackIcon size={14} />{t("providers.backToList")}</button>
        </div>

        <form className="editor-panel" onSubmit={(e) => void saveDraft(e)}>
          <div className="editor-panel-header">
            <strong>{t("providers.providerPreset")}</strong>
            <span className="badge neutral">{preset.company}</span>
          </div>

          <div className="editor-panel-body">
            <div className="preset-grid">
              {presets.map((item) => {
                const icon = providerIconSrc(item.id);
                return (
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
                    {icon ? <img src={icon} alt="" width={16} height={16} /> : <ConfigSlidersIcon size={16} />}
                    <span>{item.name}</span>
                  </button>
                );
              })}
            </div>

            <div className="field-grid">
              <div className="field-block">
                <label htmlFor="cfg-name">{t("providers.configurationName")}</label>
                <input
                  id="cfg-name"
                  value={draft.name}
                  onChange={(e) => setDraft({ ...draft, name: e.target.value })}
                />
              </div>
              <div className="field-block">
                <label htmlFor="cfg-model">{t("providers.visionModel")}</label>
                <input
                  id="cfg-model"
                  value={draft.model}
                  onChange={(e) => setDraft({ ...draft, model: e.target.value })}
                />
              </div>
              <div className="field-block full">
                <label htmlFor="cfg-key">{t("providers.apiKey")}</label>
                <div className="input-with-action">
                  <input
                    id="cfg-key"
                    type={showKey ? 'text' : 'password'}
                    placeholder={keyPlaceholder}
                    value={draft.key}
                    onChange={(e) =>
                      setDraft({ ...draft, key: e.target.value, keyEdited: true })
                    }
                  />
                  <button
                    type="button"
                    className="button secondary"
                    onClick={() => setShowKey((v) => !v)}
                  >
                    {showKey ? <HideIcon size={14} /> : <ShowIcon size={14} />}
                    {showKey ? t("providers.hide") : t("providers.show")}
                  </button>
                  <button
                    type="button"
                    className="button secondary"
                    onClick={() => void runTestConnection()}
                    disabled={testing}
                  >
                    {testing && <SpinnerIcon size={14} className="spin" />}{t("providers.testConnection")}</button>
                </div>
              </div>
            </div>

            <div>
              <button
                type="button"
                className="button ghost sm"
                onClick={() => setAdvancedOpen((v) => !v)}
              >
                {advancedOpen ? t("providers.hideAdvancedSettings") : t("providers.showAdvancedSettingsEndpointProtocolTimeoutPrompt")}
              </button>
            </div>

            {advancedOpen && (
              <div className="field-grid">
                <div className="field-block">
                  <label htmlFor="cfg-endpoint">{t("providers.apiBaseUrl")}</label>
                  <input
                    id="cfg-endpoint"
                    value={draft.endpoint}
                    onChange={(e) => setDraft({ ...draft, endpoint: e.target.value })}
                  />
                </div>
                <div className="field-block">
                  <label htmlFor="cfg-timeout">{t("providers.timeoutSeconds")}</label>
                  <input
                    id="cfg-timeout"
                    value={draft.timeout}
                    onChange={(e) => setDraft({ ...draft, timeout: e.target.value })}
                  />
                </div>
                <div className="field-block full">
                  <label>{t("providers.apiProtocol")}</label>
                  <div className="segmented-control" role="radiogroup" aria-label={t("providers.apiProtocol")}>
                    {([
                      { id: 'responses', label: t("providers.openAiResponses") },
                      { id: 'chat-completions', label: t("providers.chatCompletions") },
                      { id: 'anthropic-messages', label: t("providers.anthropicMessages") },
                    ] as const).map((proto) => (
                      <button
                        key={proto.id}
                        type="button"
                        className={draft.protocol === proto.id ? 'selected' : ''}
                        onClick={() => setDraft({ ...draft, protocol: proto.id })}
                      >
                        {proto.label}
                      </button>
                    ))}
                  </div>
                </div>
                <div className="field-block full">
                  <label htmlFor="cfg-note">{t("providers.additionalSystemInstructions")}</label>
                  <textarea
                    id="cfg-note"
                    rows={3}
                    placeholder={t("providers.exampleKeepTheOutputBriefAndGroup")}
                    value={draft.note}
                    onChange={(e) => setDraft({ ...draft, note: e.target.value })}
                  />
                </div>
              </div>
            )}

            {error && <div style={{ color: 'var(--danger)', fontSize: 12 }}>{error}</div>}
          </div>

          <div className="editor-panel-footer">
            <span style={{ fontSize: 12, color: 'var(--muted)' }}>{t("providers.cancelingDiscardsUnsavedChanges")}</span>
            <div style={{ display: 'flex', gap: 8 }}>
              <button type="button" className="button secondary" onClick={() => setDraft(null)}>{t("app.cancel")}</button>
              <button type="submit" className="button primary">
                <CheckIcon size={14} />{t("providers.saveConfiguration")}</button>
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
    <div>
      {!embedded && (
        <div className="page-heading">
          <div>
            <h1>{t("settings.modelProviders")}</h1>
            <p>{t("providers.optionalAiAnalysisUsesTheseSettingsTo")}</p>
          </div>
          <button className="button primary" onClick={() => openEditor()}>
            <PlusIcon size={14} />{t("providers.addProvider")}</button>
        </div>
      )}

      <div className="toolbar-row">
        <div className="search-field">
          <SearchIcon size={14} />
          <input
            aria-label={t("providers.searchModelConfigurations")}
            placeholder={t("providers.searchProvidersModelsOrEndpointDomains")}
            value={query}
            onChange={(e) => setQuery(e.target.value)}
          />
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <span className="badge neutral">{t("providers.configurationCount", { count: filtered.length })}</span>
          {embedded && (
            <button className="button primary sm" onClick={() => openEditor()}>
              <PlusIcon size={14} />{t("providers.addProvider")}</button>
          )}
        </div>
      </div>

      <div className="provider-list">
        {filtered.map((cfg) => {
          const isDefault = cfg.id === defaultProviderId;
          const icon = providerIconSrc(cfg.presetId);
          let host = t("providers.noEndpoint");
          try {
            if (cfg.endpoint) host = new URL(cfg.endpoint).host;
          } catch {
            host = cfg.endpoint;
          }

          return (
            <div key={cfg.id} className={`provider-card ${isDefault ? 'is-default' : ''}`}>
              <span className="provider-brand">
                {icon ? <img src={icon} alt="" /> : <ConfigSlidersIcon size={18} />}
              </span>

              <div className="provider-info">
                <div className="provider-title-row">
                  <strong>{cfg.name}</strong>
                  {isDefault && <span className="badge accent">{t("providers.default")}</span>}
                  {cfg.saved || cfg.key ? (
                    <span className="badge success">
                      {cfg.keyPreview ? t("providers.apiKeySet", { preview: cfg.keyPreview }) : t("providers.apiKeySetTooltip")}
                    </span>
                  ) : (
                    <span className="badge neutral">{t("providers.noApiKey")}</span>
                  )}
                </div>
                <div className="provider-sub">
                  <code>{cfg.model || t("providers.noModelSpecified")}</code>
                  <span>·</span>
                  <code>{host}</code>
                </div>
              </div>

              <div className="provider-actions">
                {!isDefault && (
                  <button
                    className="button secondary sm"
                    onClick={() => {
                      void onSaveProviders(providers, cfg.id);
                      onToast(t("providers.isNowTheDefaultAnalysisModel", { name: cfg.name }));
                    }}
                  >
                    <StarIcon size={14} />{t("providers.setAsDefault")}</button>
                )}
                <button className="button secondary sm" onClick={() => openEditor(cfg)}>
                  <EditIcon size={14} />{t("providers.editTooltip")}</button>
                <DropdownMenu.Root>
                  <DropdownMenu.Trigger asChild>
                    <button className="icon-button" aria-label={t("workspace.moreActions")}>
                      <MoreIcon size={16} />
                    </button>
                  </DropdownMenu.Trigger>
                  <DropdownMenu.Portal>
                    <DropdownMenu.Content className="dropdown-menu" align="end" sideOffset={6}>
                      <DropdownMenu.Item
                        className="dropdown-item"
                        onSelect={async () => {
                          if (!cfg.endpoint) return;
                          await navigator.clipboard.writeText(cfg.endpoint).catch(() => {});
                          onToast(t("providers.endpointCopied"));
                        }}
                      >
                        <CopyTextIcon size={14} />{t("providers.copyEndpoint")}</DropdownMenu.Item>
                      <DropdownMenu.Item
                        className="dropdown-item"
                        onSelect={() => {
                          const p = presetFor(cfg.presetId);
                          const next = providers.map((x) =>
                            x.id === cfg.id
                              ? { ...x, model: p.model, endpoint: p.endpoint, protocol: p.protocol }
                              : x
                          );
                          void onSaveProviders(next, defaultProviderId);
                          onToast(t("providers.defaultEndpointAndModelRestored"));
                        }}
                      >
                        <ResetIcon size={14} />{t("providers.restoreDefaultParameters")}</DropdownMenu.Item>
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
