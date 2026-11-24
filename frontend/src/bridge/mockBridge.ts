import type {
  DoctorSnapshot,
  EditHereBridge,
  ExplodeBlock,
  GalleryPage,
  GroupNote,
  PixelRect,
  ProviderConfig,
  ProviderPreset,
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

export const PROVIDER_PRESETS: ProviderPreset[] = [
  { id: 'openai', name: 'OpenAI', company: 'OpenAI', model: 'gpt-6.1-sol', endpoint: 'https://api.openai.com/v1', protocol: 'responses', docs: 'https://platform.openai.com' },
  { id: 'claude', name: 'Claude', company: 'Anthropic', model: 'claude-sonnet-5-5', endpoint: 'https://api.anthropic.com/v1', protocol: 'anthropic-messages', docs: 'https://platform.claude.com' },
  { id: 'deepseek', name: 'DeepSeek', company: '深度求索', model: 'deepseek-flash', endpoint: 'https://api.deepseek.com', protocol: 'chat-completions', docs: 'https://platform.deepseek.com' },
  { id: 'glm-cn', name: 'GLM（中国）', company: '智谱 · Z.AI', model: 'glm-4.6v', endpoint: 'https://open.bigmodel.cn/api/paas/v4', protocol: 'chat-completions', docs: 'https://open.bigmodel.cn', regional: true },
  { id: 'glm-global', name: 'GLM / Z.AI（全球）', company: 'Z.AI', model: 'glm-4.6v', endpoint: 'https://api.z.ai/api/paas/v4', protocol: 'chat-completions', docs: 'https://docs.z.ai', regional: true },
  { id: 'kimi-cn', name: 'Kimi（中国）', company: '月之暗面', model: 'kimi-k3', endpoint: 'https://api.moonshot.cn/v1', protocol: 'chat-completions', docs: 'https://platform.moonshot.cn', regional: true },
  { id: 'kimi-global', name: 'Kimi（全球）', company: 'Moonshot AI', model: 'kimi-k3', endpoint: 'https://api.moonshot.ai/v1', protocol: 'chat-completions', docs: 'https://platform.kimi.ai', regional: true },
  { id: 'custom', name: '自定义接口', company: 'OpenAI / Anthropic 兼容', model: '', endpoint: '', protocol: 'chat-completions', docs: '' },
];

export function createInitialProviders(): ProviderConfig[] {
  return PROVIDER_PRESETS.filter((p) => p.id !== 'custom').map((p) => ({
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

export function createInitialPages(): GalleryPage[] {
  return [
    {
      id: 'home',
      name: '首页首屏',
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
          text: '主标题字号再放大一档，保留两行排版；下方副标题颜色调浅拉开阅读层级。',
          rect: { x1: 36, y1: 126, x2: 360, y2: 298 },
          ocrtext: 'A quieter kind of home.',
        },
        {
          id: 'h-2',
          number: 2,
          kind: 'region',
          text: '主按钮改为深绿色，圆角收小到 4px，文案替换为「探索系列」。',
          rect: { x1: 43, y1: 316, x2: 177, y2: 353 },
          ocrtext: 'EXPLORE THE EDIT',
        },
      ],
    },
    {
      id: 'inventory',
      name: '游戏物品栏 HUD',
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
          text: '物品格间距太紧，适当增加网格 gap 并突出已装备槽位的描边高亮。',
          rect: { x1: 280, y1: 76, x2: 772, y2: 344 },
          ocrtext: 'INVENTORY · 18 / 24',
        },
        {
          id: 'inv-2',
          number: 2,
          kind: 'point',
          text: '「一键整理」按钮建议与「批量分解」拉开间距，避免误触。',
          point: { x: 642, y: 382 },
        },
      ],
    },
    {
      id: 'settings',
      name: '应用设置页',
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

export const INITIAL_GROUP_NOTES: GroupNote[] = [
  {
    id: 'g-1',
    number: 1,
    text: '全站与各子页面保持统一的圆角（6px / 8px）和间距刻度；不要使用生硬纯黑边框。',
  },
];

export const INITIAL_VIDEO_MARKERS: VideoMarker[] = [
  { id: 'vm-1', timestampMs: 1240, label: '入场淡入', note: '00:01.24 · 主标题入场与副标题同步，延迟 80ms 更柔和' },
  { id: 'vm-2', timestampMs: 3240, label: '悬停态切换', note: '00:03.24 · 卡片悬停阴影过渡略生硬，改为 180ms ease-out' },
  { id: 'vm-3', timestampMs: 8420, label: '弹窗收起', note: '00:08.42 · 确认弹窗关闭瞬间有轻微闪烁' },
];

export const INITIAL_DOCTOR_SNAPSHOT: DoctorSnapshot = {
  activeRoute: 'desktop',
  desktopVersion: '0.14.0-dev (Qt6 Native Core)',
  nodeVersion: 'v23.9.0',
  unifiedMcpSnippet: JSON.stringify(
    {
      mcpServers: {
        edithere: {
          command: 'node',
          args: ['E:/Project/EditHere总包/EditHere-0.14/cli/unified-mcp.mjs'],
        },
      },
    },
    null,
    2
  ),
  checks: [
    { id: 'core', label: '原生截图 / OCR / 长截图内核', detail: 'C++20 + Qt6 · 保留零延迟遮罩与 Windows HDR 支持', status: 'ok' },
    {
      id: 'bridge',
      label: 'B2 前端与大爆炸分割',
      detail: 'M3 交互壳使用轻量端侧像素分割检测；M4 WebView2 Bridge 将接入 C++ detector.cpp 多层级选区与本地 OCR',
      status: 'warn',
    },
    { id: 'mcp', label: '统一 CLI & MCP 路由', detail: 'cli/edithere.mjs · cli/unified-mcp.mjs（桌面优先，Web 自动回退）', status: 'ok' },
    { id: 'skills', label: '英文单源 Skills 构建', detail: '5 端同步零漂移 · 含局部/设计系统分层与代码影响面换图规范', status: 'ok' },
  ],
};

/**
 * Build a live `image-set-feedback-1` JSON matching EditHere's protocol:
 * `pages[]` only includes pages that actually have annotations or layout movements.
 */
export function buildFeedbackPayload(pages: GalleryPage[], groupNotes: GroupNote[]) {
  const annotatedPages = pages.filter(
    (p) => p.annotations.length > 0 || p.blocks.some(isBlockTransformed)
  );

  return {
    schemaVersion: 'image-set-feedback-1',
    tool: { name: 'EditHere', version: '0.14.0-b2-shell' },
    annotationSpace: 'result',
    set: {
      name: '示例项目组图',
      pageCount: pages.length,
      annotatedPageCount: annotatedPages.length,
    },
    pageIndex: pages.map((p, index) => ({
      id: p.id,
      name: p.name,
      index: index + 1,
      width: p.width,
      height: p.height,
      annotated: annotatedPages.some((ap) => ap.id === p.id),
    })),
    pages: annotatedPages.map((p, index) => ({
      id: p.id,
      name: p.name,
      index: index + 1,
      width: p.width,
      height: p.height,
      feedback: {
        schemaVersion: 'feedback-minimal-4',
        tool: { name: 'EditHere', version: '0.14.0-b2-shell' },
        annotationSpace: 'result',
        image: {
          mode: 'none',
          mimeType: 'image/png',
          width: p.width,
          height: p.height,
          devicePixelRatio: null,
        },
        capture: {
          source: 'gallery',
          page: p.name,
          pageSource: 'agent',
        },
        objects: p.annotations.map((ann) => {
          const base: Record<string, unknown> = {
            id: ann.id,
            kind: ann.kind,
          };
          if (ann.kind === 'region' && ann.rect) {
            base.source = ann.movement ? ann.movement.source : ann.rect;
          } else if (ann.kind === 'point' && ann.point) {
            base.point = ann.point;
          }
          if (ann.movement) {
            const mv: Record<string, unknown> = {
              rect: ann.movement.rect,
              dx: ann.movement.dx,
              dy: ann.movement.dy,
            };
            if (typeof ann.movement.scaleX === 'number' && Math.abs(ann.movement.scaleX - 1) > 0.005) {
              mv.scaleX = Number(ann.movement.scaleX.toFixed(3));
            }
            if (typeof ann.movement.scaleY === 'number' && Math.abs(ann.movement.scaleY - 1) > 0.005) {
              mv.scaleY = Number(ann.movement.scaleY.toFixed(3));
            }
            base.movement = mv;
          }
          if (ann.ocrtext) {
            base.ocrtext = ann.ocrtext;
          }
          base.annotations = [{ number: ann.number, text: ann.text || '' }];
          return base;
        }),
      },
    })),
    groupNotes: groupNotes.map((g) => ({ number: g.number, text: g.text })),
  };
}

export function generateAnalysisText(pages: GalleryPage[], groupNotes: GroupNote[]): string {
  const sections: string[] = ['# EditHere 修改清单（基于当前工作台实时批注生成）', ''];

  if (groupNotes.length > 0) {
    sections.push('## 整组设计系统规范');
    for (const g of groupNotes) {
      sections.push(`- [整组 ${g.number}] ${g.text}`);
    }
    sections.push('');
  }

  const annotatedPages = pages.filter((p) => p.annotations.length > 0);
  if (annotatedPages.length === 0) {
    sections.push('> 当前各页面暂无批注；在工作台框选、点选或启用「大爆炸」调整组件布局后，此处会自动生成结构化修改说明。');
  } else {
    for (const page of annotatedPages) {
      sections.push(`## 页面：${page.name}（id: \`${page.id}\` · ${page.width}×${page.height}）`);
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
            parts.push(`缩放 ${sx.toFixed(2)}× / ${sy.toFixed(2)}×`);
          }
          moveHint = ` [布局调整 ${parts.join(', ')}]`;
        }
        const text = note.text.trim() || '按图中位置调整组件布局（无补充文字）';
        sections.push(`${note.number}. **${note.kind === 'point' ? '点定位' : note.kind === 'global' ? '全局' : '区域'}${moveHint}**：${text}`);
      }
      sections.push('');
    }
  }

  return sections.join('\n').trim();
}

export const previewShellBridge: EditHereBridge = {
  mode: 'preview-shell',
  async submitAgentSession(payload) {
    console.info('[EditHere B2 Shell] submitAgentSession payload:', payload);
    return {
      ok: true,
      message: '已生成并回传 image-set-feedback-1 反馈（当前为 B2 前端确认空壳）',
    };
  },
  async cancelAgentSession() {
    return {
      ok: true,
      message: '已取消本轮会话（当前为 B2 前端确认空壳）',
    };
  },
  async getDoctorSnapshot() {
    return INITIAL_DOCTOR_SNAPSHOT;
  },
};
