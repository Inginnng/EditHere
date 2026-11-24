export type BridgeMode = 'desktop-webview' | 'web-agent' | 'mcp-app' | 'preview-shell';
export type ViewId = 'workspace' | 'analysis' | 'settings';
export type SettingsTabId = 'general' | 'providers' | 'about';
export type ThemeMode = 'light' | 'dark' | 'system';
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

export interface GeneralPreferences {
  includeCoordinates: boolean;
  includeOcr: boolean;
  captureShadow: boolean;
  compactDensity: boolean;
  exportMode: 'file' | 'embedded';
  language: '简体中文' | '繁體中文' | 'English';
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
