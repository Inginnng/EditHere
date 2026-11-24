export type BridgeMode = 'desktop-webview' | 'web-agent' | 'mcp-app' | 'preview-shell';
export type ViewId = 'workspace' | 'analysis' | 'settings';
export type SettingsTabId = 'general' | 'shortcuts' | 'capture' | 'providers' | 'diagnostics';
export type ThemeMode = 'light' | 'dark' | 'system';
export type LanguageCode = 'system' | 'zh_CN' | 'zh_TW' | 'en';
export type OcrLanguageCode = 'system' | 'zh_CN' | 'en';
export type UpdateSourceId = 'auto' | 'atomgit' | 'github' | 'gitee';
export type ToolMode = 'select' | 'pan' | 'smart' | 'point' | 'box' | 'brush' | 'explode' | 'mosaic' | 'crop';
export type AnnotationKind = 'region' | 'point' | 'global';

export interface MosaicRegion {
  id: string;
  rect: PixelRect;
  style: 'pixelate' | 'blur';
  strength: number;
}

export interface BrushStroke {
  id: string;
  points: PixelPoint[];
  color: string;
  width: number;
  opacity?: number;
}

export interface PixelRect {
  x1: number;
  y1: number;
  x2: number;
  y2: number;
}

export interface PixelPoint {
  x: number;
  y: number;
}

export interface ExplodeBlock {
  id: string;
  source: PixelRect;
  dest: PixelRect;
  manual?: boolean;
}

export interface AnnotationItem {
  id: string;
  number: number;
  kind: AnnotationKind;
  text: string;
  rect?: PixelRect;
  point?: PixelPoint;
  blockId?: string;
  movement?: {
    source: PixelRect;
    rect: PixelRect;
    dx: number;
    dy: number;
    scaleX?: number;
    scaleY?: number;
  };
  ocrtext?: string;
}

export interface GroupNote {
  id: string;
  number: number;
  text: string;
}

export interface VideoMarker {
  id: string;
  timestampMs: number;
  label: string;
  note: string;
}

export interface GalleryPage {
  id: string;
  name: string;
  routeHint: string;
  width: number;
  height: number;
  sceneId?: 'home' | 'inventory' | 'settings';
  imageUrl?: string;
  isVideo?: boolean;
  blocks: ExplodeBlock[];
  annotations: AnnotationItem[];
  mosaics?: MosaicRegion[];
  brushStrokes?: BrushStroke[];
}

export interface ProviderPreset {
  id: string;
  name: string;
  company: string;
  model: string;
  endpoint: string;
  protocol: string;
  docs: string;
  regional?: boolean;
}

export interface ProviderConfig {
  id: string;
  presetId: string;
  name: string;
  model: string;
  endpoint: string;
  protocol: string;
  key: string;
  keyPreview?: string;
  keyEdited?: boolean;
  region: string;
  timeout: string;
  note: string;
  saved: boolean;
}

export interface ShortcutDefinitionItem {
  id: string;
  label: string;
  global: boolean;
  defaultKey: string;
}

export interface TrayActionDefinitionItem {
  id: string;
  label: string;
}

export interface GeneralPreferences {
  language: LanguageCode;
  ocrLanguage: OcrLanguageCode;
  captureOnStartup: boolean;
  launchAtLogin: boolean;
  launchAtLoginNotice: string;
  fitImageOnOpen: boolean;
  confirmBeforeDiscard: boolean;
  defaultTool: number;
  traySingleClick: string;
  trayDoubleClick: string;
  trayActions: TrayActionDefinitionItem[];
  includeCoordinates: boolean;
  includeOcr: boolean;
  includeRecognizedText: boolean;
  includeWindowTitle: boolean;
  exportMode: 'file' | 'embedded' | 'none';
  copyJsonTextWithImage: boolean;
  captureShadow: boolean;
  captureCornerRadius: number;
  feedbackDir: string;
  scrollAxis: 'vertical' | 'horizontal';
  scrollAutoCrop: boolean;
  scrollUltraLong: boolean;
  checkUpdatesOnStartup: boolean;
  updateSource: UpdateSourceId;
  shortcuts: Record<string, string>;
  shortcutDefinitions: ShortcutDefinitionItem[];
}

export interface DiagnosticsInfo {
  directory: string;
  fileCount: number;
  lastError: string;
}

export interface UpdateCheckResult {
  status: 'available' | 'upToDate' | 'failed';
  tagName?: string;
  currentVersion?: string;
  url?: string;
  message?: string;
}

export interface DoctorSnapshot {
  activeRoute: 'desktop' | 'web';
  desktopVersion: string;
  nodeVersion: string;
  unifiedMcpSnippet: string;
  checks: Array<{
    id: string;
    label: string;
    detail: string;
    status: 'ok' | 'warn';
  }>;
}

export interface EditHereBridge {
  readonly mode: BridgeMode;
  submitAgentSession(payload: unknown): Promise<{ ok: boolean; message: string }>;
  cancelAgentSession(): Promise<{ ok: boolean; message: string }>;
  getDoctorSnapshot(): Promise<DoctorSnapshot>;
}
