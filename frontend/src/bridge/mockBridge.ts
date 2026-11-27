import type { Translate } from '../i18n';
import { version as productVersion } from '../../package.json';
import type {
  DiagnosticsInfo,
  DoctorSnapshot,
  EditHereBridge,
  ExplodeBlock,
  GalleryPage,
  GeneralPreferences,
  GroupNote,
  PixelRect,
  ProviderConfig,
  ProviderPreset,
  ShortcutDefinitionItem,
  SkillSyncState,
  TrayActionDefinitionItem,
  VideoMarker,
} from './types';

function makeBlock(id: string, source: PixelRect): ExplodeBlock {
  return { id, source, dest: { ...source } };
}

export function isBlockTransformed(block: ExplodeBlock): boolean {
  return (
    block.dest.x1 !== block.source.x1 ||
    block.dest.y1 !== block.source.y1 ||
    block.dest.x2 !== block.source.x2 ||
    block.dest.y2 !== block.source.y2
  );
}

export const providerPresets = (t: Translate): ProviderPreset[] => [
  { id: 'openai', name: 'OpenAI', company: 'OpenAI', model: 'gpt-6.1-sol', endpoint: 'https://api.openai.com/v1', protocol: 'responses', docs: 'https://platform.openai.com' },
  { id: 'claude', name: 'Claude', company: 'Anthropic', model: 'claude-sonnet-5-5', endpoint: 'https://api.anthropic.com/v1', protocol: 'anthropic-messages', docs: 'https://platform.claude.com' },
  { id: 'deepseek', name: 'DeepSeek', company: t("preview.deepseek"), model: 'deepseek-flash', endpoint: 'https://api.deepseek.com', protocol: 'chat-completions', docs: 'https://platform.deepseek.com' },
  { id: 'glm-cn', name: t("preview.glmChina"), company: t("preview.zhipuZAi"), model: 'glm-4.6v', endpoint: 'https://open.bigmodel.cn/api/paas/v4', protocol: 'chat-completions', docs: 'https://open.bigmodel.cn', regional: true },
  { id: 'glm-global', name: t("preview.glmZAiGlobal"), company: 'Z.AI', model: 'glm-4.6v', endpoint: 'https://api.z.ai/api/paas/v4', protocol: 'chat-completions', docs: 'https://docs.z.ai', regional: true },
  { id: 'kimi-cn', name: t("preview.kimiChina"), company: t("preview.moonshotAi"), model: 'kimi-k3', endpoint: 'https://api.moonshot.cn/v1', protocol: 'chat-completions', docs: 'https://platform.moonshot.cn', regional: true },
  { id: 'kimi-global', name: t("preview.kimiGlobal"), company: 'Moonshot AI', model: 'kimi-k3', endpoint: 'https://api.moonshot.ai/v1', protocol: 'chat-completions', docs: 'https://platform.kimi.ai', regional: true },
  { id: 'custom', name: t("preview.customEndpoint"), company: t("preview.openaiAnthropicCompatible"), model: '', endpoint: '', protocol: 'chat-completions', docs: '' },
];

export function createInitialProviders(t: Translate): ProviderConfig[] {
  return providerPresets(t).filter((p) => p.id !== 'custom').map((p) => ({
    id: p.id,
    presetId: p.id,
    name: p.name,
    model: p.model,
    endpoint: p.endpoint,
    protocol: p.protocol,
    key: '',
    keyPreview: p.id === 'openai' ? 'sk-demo-••••4291' : '',
    region: 'cn',
    timeout: '120',
    note: '',
    saved: p.id === 'openai',
  }));
}

export function createInitialPages(t: Translate): GalleryPage[] {
  return [
    {
      id: 'home',
      name: 'Landing Page Hero',
      routeHint: '/ (Morrow Home)',
      width: 800,
      height: 480,
      sceneId: 'home',
      blocks: [
        makeBlock('home-nav', { x1: 24, y1: 12, x2: 776, y2: 54 }),
        makeBlock('home-hero-title', { x1: 36, y1: 126, x2: 360, y2: 298 }),
        makeBlock('home-hero-cta', { x1: 43, y1: 316, x2: 177, y2: 353 }),
        makeBlock('home-hero-chair', { x1: 498, y1: 63, x2: 795, y2: 415 }),
      ],
      annotations: [
        {
          id: 'h-1',
          number: 1,
          kind: 'region',
          text: t("preview.increaseTheHeadingOneSizeWhileKeeping"),
          rect: { x1: 36, y1: 126, x2: 360, y2: 298 },
        },
        {
          id: 'h-2',
          number: 2,
          kind: 'region',
          text: t("preview.makeThePrimaryButtonDarkGreenWith"),
          rect: { x1: 43, y1: 316, x2: 177, y2: 353 },
        },
      ],
    },
    {
      id: 'inventory',
      name: 'Game Inventory HUD',
      routeHint: '/hud/inventory',
      width: 800,
      height: 480,
      sceneId: 'inventory',
      blocks: [
        makeBlock('inv-stats', { x1: 28, y1: 76, x2: 262, y2: 404 }),
        makeBlock('inv-grid', { x1: 280, y1: 76, x2: 772, y2: 344 }),
        makeBlock('inv-actions', { x1: 280, y1: 360, x2: 772, y2: 404 }),
      ],
      annotations: [
        {
          id: 'inv-1',
          number: 1,
          kind: 'region',
          text: t("preview.increaseTheSpacingBetweenInventorySlotsAnd"),
          rect: { x1: 280, y1: 76, x2: 772, y2: 344 },
        },
        {
          id: 'inv-2',
          number: 2,
          kind: 'point',
          text: t("preview.moveQuickSortFartherFromBulkSalvage"),
          point: { x: 642, y: 382 },
        },
      ],
    },
    {
      id: 'settings',
      name: 'Application Settings',
      routeHint: '/settings',
      width: 800,
      height: 480,
      sceneId: 'settings',
      blocks: [
        makeBlock('set-nav', { x1: 20, y1: 20, x2: 185, y2: 460 }),
        makeBlock('set-cards', { x1: 212, y1: 70, x2: 764, y2: 370 }),
        makeBlock('set-footer', { x1: 212, y1: 408, x2: 764, y2: 452 }),
      ],
      annotations: [],
    },
  ];
}

export const initialGroupNotes = (t: Translate): GroupNote[] => [
  {
    id: 'g-1',
    number: 1,
    text: t("preview.useConsistentCornerRadii6Px8"),
  },
];

export function localizePreviewWorkspace(
  workspace: { pages: GalleryPage[]; groupNotes: GroupNote[] },
  previous: Translate,
  next: Translate,
) {
  const previousPages = createInitialPages(previous);
  const nextPages = createInitialPages(next);
  const previousGroup = initialGroupNotes(previous);
  const nextGroup = initialGroupNotes(next);
  return {
    pages: workspace.pages.map((page) => {
      const before = previousPages.find((item) => item.id === page.id);
      const after = nextPages.find((item) => item.id === page.id);
      if (!before || !after) return page;
      return {
        ...page,
        annotations: page.annotations.map((note) => {
          const oldText = before.annotations.find((item) => item.id === note.id)?.text;
          const newText = after.annotations.find((item) => item.id === note.id)?.text;
          return note.text === oldText && newText !== undefined ? { ...note, text: newText } : note;
        }),
      };
    }),
    groupNotes: workspace.groupNotes.map((note) => {
      const oldText = previousGroup.find((item) => item.id === note.id)?.text;
      const newText = nextGroup.find((item) => item.id === note.id)?.text;
      return note.text === oldText && newText !== undefined ? { ...note, text: newText } : note;
    }),
  };
}

export const initialVideoMarkers = (t: Translate): VideoMarker[] => [
  { id: 'vm-1', timestampMs: 1240, label: t("preview.fadeIn"), note: t("preview.000124StartTheHeadingAnd") },
  { id: 'vm-2', timestampMs: 3240, label: t("preview.hoverTransition"), note: t("preview.000324EaseTheCardS") },
  { id: 'vm-3', timestampMs: 8420, label: t("preview.dialogDismissal"), note: t("preview.000842TheConfirmationDialogFlickers") },
];

export const defaultShortcutDefinitions = (t: Translate): ShortcutDefinitionItem[] => [
  { id: 'capture', label: t("app.screenshotTooltip"), global: true, defaultKey: 'Alt+Shift+2' },
  { id: 'annotate', label: t("preview.newAnnotationEmptyWindow"), global: true, defaultKey: 'Alt+Shift+1' },
  { id: 'open', label: t("app.openImageOrProject"), global: false, defaultKey: 'Ctrl+O' },
  { id: 'paste', label: t("preview.pasteImage"), global: false, defaultKey: 'Ctrl+V' },
  { id: 'save', label: t("preview.saveProject"), global: false, defaultKey: 'Ctrl+S' },
  { id: 'export', label: t("preview.viewJson"), global: false, defaultKey: 'Ctrl+E' },
  { id: 'copy', label: t("preview.copyAnnotatedImage"), global: false, defaultKey: 'Ctrl+C' },
  { id: 'copyJson', label: t("workspace.copyJsonImageTooltip"), global: false, defaultKey: '' },
  { id: 'saveImage', label: t("workspace.saveImage"), global: false, defaultKey: '' },
  { id: 'hideAnnotations', label: t("preview.showOrHideAnnotationMarkers"), global: false, defaultKey: '' },
  { id: 'addGlobalNote', label: t("preview.addGlobalNote"), global: false, defaultKey: '' },
  { id: 'undo', label: t("workspace.undo"), global: false, defaultKey: 'Ctrl+Z' },
  { id: 'redo', label: t("workspace.redo"), global: false, defaultKey: 'Ctrl+Y' },
  { id: 'fit', label: t("preview.fitToWindow"), global: false, defaultKey: 'Ctrl+0' },
  { id: 'delete', label: t("preview.deleteSelectedAnnotation"), global: false, defaultKey: 'Del' },
  { id: 'close', label: t("preview.cancelActionOrCloseCapture"), global: false, defaultKey: 'Esc' },
  { id: 'smart', label: t("settings.smartSelect"), global: false, defaultKey: 'B' },
  { id: 'point', label: t("preview.pointAnnotation"), global: false, defaultKey: 'P' },
  { id: 'rectangle', label: t("preview.boxAnnotation"), global: false, defaultKey: 'R' },
  { id: 'adjust', label: t("preview.adjustAnnotation"), global: false, defaultKey: 'V' },
  { id: 'explode', label: t("preview.toggleExplodedView"), global: false, defaultKey: 'E' },
  { id: 'component', label: t("preview.adjustComponent"), global: false, defaultKey: 'M' },
];

export const defaultTrayActions = (t: Translate): TrayActionDefinitionItem[] => [
  { id: 'capture', label: t("app.screenshotTooltip") },
  { id: 'annotate', label: t("preview.newAnnotationEmptyWindow") },
  { id: 'show', label: t("preview.showCurrentWindow") },
  { id: 'open', label: t("app.openImageOrProject") },
  { id: 'none', label: t("preview.doNothing") },
];

export const initialPreferences = (t: Translate): GeneralPreferences => ({
  language: 'system',
  ocrLanguage: 'system',
  captureOnStartup: true,
  launchAtLogin: false,
  launchAtLoginNotice: '',
  fitImageOnOpen: true,
  confirmBeforeDiscard: true,
  defaultTool: 0,
  traySingleClick: 'capture',
  trayDoubleClick: 'annotate',
  trayActions: defaultTrayActions(t),
  includeCoordinates: true,
  includeOcr: true,
  includeRecognizedText: true,
  includeWindowTitle: true,
  exportMode: 'file',
  copyJsonTextWithImage: false,
  captureShadow: true,
  captureCornerRadius: 10,
  feedbackDir: '',
  scrollAxis: 'vertical',
  scrollAutoCrop: false,
  scrollUltraLong: false,
  checkUpdatesOnStartup: true,
  autoSyncSkills: false,
  showInstalledSkillsOnly: false,
  updateSource: 'auto',
  shortcuts: Object.fromEntries(defaultShortcutDefinitions(t).map((d) => [d.id, d.defaultKey])),
  shortcutDefinitions: defaultShortcutDefinitions(t),
});

export const INITIAL_DIAGNOSTICS_INFO: DiagnosticsInfo = {
  directory: 'C:\\Users\\Local\\AppData\\Local\\EditHere\\logs',
  fileCount: 2,
  lastError: '',
};

export const initialDoctorSnapshot = (t: Translate): DoctorSnapshot => ({
  activeRoute: 'desktop',
  desktopVersion: `${productVersion} (Qt6 Native Core)`,
  nodeVersion: 'v23.9.0',
  unifiedMcpSnippet: JSON.stringify(
    {
      mcpServers: {
        edithere: {
          command: 'node',
          args: ['./cli/unified-mcp.mjs'],
        },
      },
    },
    null,
    2
  ),
  checks: [
    { id: 'core', label: t("preview.nativeAndScrollingCapture"), detail: t("preview.c20Qt6WindowsHdrDwmAnd"), status: 'ok' },
    { id: 'mcp', label: t("preview.unifiedCliAndMcpRouting"), detail: 'cli/edithere.mjs · cli/unified-mcp.mjs', status: 'ok' },
    { id: 'skills', label: t("preview.multiAssistantSkillSync"), detail: t("preview.syncConfigurationAcrossFiveAssistants"), status: 'ok' },
  ],
});

export const INITIAL_SKILLS_STATE: SkillSyncState = {
  bundledSkillVersion: '1.0.0',
  minCompatibleSkillVersion: '1.0.0',
  appVersion: productVersion,
  sourceAvailable: true,
  sourceSkillDir: 'skills/edithere',
  cliPath: 'edithere-cli.exe',
  unifiedCliPath: 'cli/edithere.mjs',
  unifiedMcpPath: 'cli/unified-mcp.mjs',
  detectedCount: 3,
  installedCount: 1,
  outdatedCount: 0,
  hosts: [
    {
      id: 'codex',
      name: 'Codex',
      hostRoot: '~/.codex',
      skillDir: '~/.codex/skills/edithere',
      hostPresent: true,
      installed: true,
      installedVersion: '1.0.0',
      status: 'up-to-date',
    },
    {
      id: 'workbuddy',
      name: 'WorkBuddy',
      hostRoot: '~/.workbuddy',
      skillDir: '~/.workbuddy/skills/edithere',
      hostPresent: true,
      installed: false,
      installedVersion: '',
      status: 'not-installed',
      otherVariants: ['edithere-web'],
    },
    {
      id: 'dsh',
      name: 'DeepSeek Harness (DSH)',
      hostRoot: '~/.dsh',
      skillDir: '~/.dsh/skills/edithere',
      hostPresent: true,
      installed: false,
      installedVersion: '',
      status: 'not-installed',
      otherVariants: ['edithere-dsh'],
    },
  ],
};

export function buildFeedbackPayload(pages: GalleryPage[], groupNotes: GroupNote[]) {
  const exportedAt = new Date().toISOString();
  const metadata = { tool: { name: 'EditHere', version: productVersion }, annotationSpace: 'result' as const };
  const pageFeedback = pages.map((page, index) => {
    const objects: Record<string, unknown>[] = [];
    for (const ann of page.annotations) {
      const text = ann.text.trim();
      if (!text && !ann.movement) continue;
      const object: Record<string, unknown> = {
        id: ann.id,
        kind: ann.kind,
        annotations: text ? [{ number: ann.number, text }] : [],
      };
      if (ann.kind === 'region' && ann.rect) {
        object.source = ann.movement?.source ?? ann.rect;
      } else if (ann.kind === 'point' && ann.point) {
        object.point = ann.point;
      }
      if (ann.movement) {
        const { rect, dx, dy, scaleX, scaleY } = ann.movement;
        const movement: Record<string, unknown> = { rect, dx, dy };
        if (scaleX !== undefined && Math.abs(scaleX - 1) > 0.005) movement.scaleX = Number(scaleX.toFixed(3));
        if (scaleY !== undefined && Math.abs(scaleY - 1) > 0.005) movement.scaleY = Number(scaleY.toFixed(3));
        object.movement = movement;
      }
      objects.push(object);
    }
    for (const block of page.blocks) {
      if (!isBlockTransformed(block) || page.annotations.some((ann) => ann.blockId === block.id)) continue;
      objects.push({
        id: `move-${block.id}`,
        kind: 'region',
        source: block.source,
        movement: {
          rect: block.dest,
          dx: block.dest.x1 - block.source.x1,
          dy: block.dest.y1 - block.source.y1,
        },
        annotations: [],
      });
    }
    const safeId = page.id.replace(/[^a-zA-Z0-9._-]/g, '-').slice(0, 80) || `p${index + 1}`;
    const imageFile = `page-${index + 1}-${safeId}-feedback.png`;
    return {
      id: page.id,
      name: page.name,
      index: index + 1,
      width: page.width,
      height: page.height,
      imageFile,
      feedback: {
        schemaVersion: 'feedback-minimal-4' as const,
        ...metadata,
        image: {
          mode: 'none',
          mimeType: 'image/png',
          width: page.width,
          height: page.height,
          devicePixelRatio: null,
          sha256: '0'.repeat(64),
        },
        capture: { source: 'demo', capturedAt: exportedAt, page: page.name, pageSource: 'agent' },
        objects,
      },
    };
  });
  const annotatedPages = pageFeedback.filter(({ feedback }) => feedback.objects.length > 0);
  const notes = groupNotes.map(({ text }) => text.trim()).filter(Boolean);
  if (notes.length === 0) {
    if (pageFeedback.length === 1) return pageFeedback[0].feedback;
    if (annotatedPages.length === 1) return annotatedPages[0].feedback;
  }

  return {
    $schema: 'urn:edithere:image-set-feedback:1' as const,
    schemaVersion: 'image-set-feedback-1' as const,
    ...metadata,
    exportedAt,
    set: {
      name: 'EditHere Image Set',
      pageCount: pages.length,
      annotatedPageCount: annotatedPages.length,
    },
    pageIndex: pageFeedback.map(({ feedback, imageFile, ...page }) =>
      feedback.objects.length > 0 ? { ...page, imageFile, annotated: true } : { ...page, annotated: false }
    ),
    pages: annotatedPages,
    groupNotes: notes.map((text, index) => ({ number: index + 1, text })),
  };
}

export function generateAnalysisText(pages: GalleryPage[], groupNotes: GroupNote[], t: Translate): string {
  const sections: string[] = [t("preview.checklistHeading"), ''];

  if (groupNotes.length > 0) {
    sections.push(t("preview.groupGuidelinesHeading"));
    for (const g of groupNotes) {
      sections.push(t("preview.groupNoteLine", { number: g.number, text: g.text }));
    }
    sections.push('');
  }

  const annotatedPages = pages.filter((p) => p.annotations.length > 0);
  if (annotatedPages.length === 0) {
    sections.push(t("preview.thereAreNoAnnotationsYetDrawA"));
  } else {
    for (const page of annotatedPages) {
      sections.push(t("preview.pageHeading", { name: page.name, id: page.id, width: page.width, height: page.height }));
      for (const note of page.annotations) {
        let moveHint = '';
        if (note.movement) {
          const parts = [
            `Δx=${note.movement.dx >= 0 ? '+' : ''}${note.movement.dx}px`,
            `Δy=${note.movement.dy >= 0 ? '+' : ''}${note.movement.dy}px`,
          ];
          const sx = note.movement.scaleX ?? 1;
          const sy = note.movement.scaleY ?? 1;
          if (Math.abs(sx - 1) > 0.01 || Math.abs(sy - 1) > 0.01) {
            parts.push(t("preview.scale", { x: sx.toFixed(2), y: sy.toFixed(2) }));
          }
          moveHint = t("preview.layoutChange", { change: parts.join(', ') });
        }
        const text = note.text.trim() || t("preview.adjustTheComponentAtTheIndicatedPosition");
        sections.push(`${note.number}. **${note.kind === 'point' ? t("workspace.point") : note.kind === 'global' ? t("preview.global") : t("preview.region")}${moveHint}**: ${text}`);
      }
      sections.push('');
    }
  }

  return sections.join('\n').trim();
}

export const previewShellBridge = (t: Translate): EditHereBridge => ({
  mode: 'preview-shell',
  async submitAgentSession(payload) {
    console.info('[EditHere B2 Shell] submitAgentSession payload:', payload);
    return {
      ok: true,
      message: t("preview.feedbackGenerated"),
    };
  },
  async cancelAgentSession() {
    return {
      ok: true,
      message: t("preview.sessionCanceledInPreview"),
    };
  },
  async getDoctorSnapshot() {
    return initialDoctorSnapshot(t);
  },
});
