import { useI18n } from '../i18n';
import { canHandleShortcut, createShortcutMatcher, TEXT_INPUT_SELECTOR } from '../utils/shortcuts';
import { useEffect, useLayoutEffect, useRef, useState } from 'react';
import * as Dialog from '@radix-ui/react-dialog';
import * as DropdownMenu from '@radix-ui/react-dropdown-menu';
import * as Tabs from '@radix-ui/react-tabs';
import {
  AiSparkleIcon,
  BrushToolIcon,
  CaptureIcon,
  CloseIcon,
  CopyBundleIcon,
  CopyImageIcon,
  CopyTextIcon,
  CropToolIcon,
  DeleteIcon,
  EditIcon,
  ExplodeIcon,
  ExportFileIcon,
  FitCanvasIcon,
  HideIcon,
  ImportImageIcon,
  InspectorCloseIcon,
  InspectorOpenIcon,
  JsonFileIcon,
  MagnifierIcon,
  MinusIcon,
  MoreIcon,
  MosaicToolIcon,
  NoteAddIcon,
  PanToolIcon,
  PauseIcon,
  PinIcon,
  PlayIcon,
  PlusIcon,
  PointToolIcon,
  RectToolIcon,
  RedoIcon,
  ResetIcon,
  SaveImageIcon,
  SaveProjectIcon,
  SelectToolIcon,
  ShieldOkIcon,
  ShowIcon,
  SmartSelectIcon,
  UndoIcon,
  VideoIcon,
} from '../components/AppIcons';
import { desktopRpc, isDesktopWebView } from '../bridge/desktopBridge';
import { SceneArtwork } from '../components/DemoScenes';
import {
  buildFeedbackPayload,
  initialVideoMarkers,
  isBlockTransformed,
} from '../bridge/mockBridge';
import type {
  AnnotationItem,
  BrushStroke,
  ExplodeBlock,
  GalleryPage,
  GroupNote,
  MosaicRegion,
  PixelPoint,
  PixelRect,
  ToolMode,
} from '../bridge/types';
import {
  attachRedactedPagesToPayload,
  composePageToPng,
  cropPageBitmap,
  drawMosaicsOnContext,
  loadBitmapForPage,
  sampleLoupeGrid,
  strokeToSvgPath,
} from '../utils/canvasCompose';
import { detectImageRegions } from '../utils/regionDetector';

type ResizeHandle = 'nw' | 'n' | 'ne' | 'e' | 'se' | 's' | 'sw' | 'w';
type CropRatio = 'free' | '16:9' | '4:3' | '1:1';

const RESIZE_HANDLES: Array<{ id: ResizeHandle; cursor: string }> = [
  { id: 'nw', cursor: 'nwse-resize' },
  { id: 'n', cursor: 'ns-resize' },
  { id: 'ne', cursor: 'nesw-resize' },
  { id: 'e', cursor: 'ew-resize' },
  { id: 'se', cursor: 'nwse-resize' },
  { id: 's', cursor: 'ns-resize' },
  { id: 'sw', cursor: 'nesw-resize' },
  { id: 'w', cursor: 'ew-resize' },
];

const BRUSH_COLORS = ['#FF453A', '#FF9F0A', '#30D158', '#0A84FF', '#BF5AF2', '#FFFFFF'];
const MIN_ZOOM = 25;
const MAX_ZOOM = 1000;
const ZOOM_STEP = 1.2;
const WORKSPACE_SHORTCUT_IDS = [
  'open', 'paste', 'save', 'export', 'copy', 'copyJson', 'saveImage', 'hideAnnotations', 'addGlobalNote',
  'undo', 'redo', 'fit', 'delete', 'close', 'smart', 'point', 'rectangle', 'adjust', 'explode', 'component',
] as const;
const DEFAULT_TOOLS: readonly ToolMode[] = ['smart', 'point', 'box', 'select'];
const FIT_VIEW = { zoom: 100, panOffset: { x: 0, y: 0 } };

function clampZoom(zoom: number) {
  return Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, zoom));
}

interface ExplodeDragSession {
  kind: 'move' | 'resize';
  handle?: ResizeHandle;
  blockId: string;
  pageId: string;
  startClientX: number;
  startClientY: number;
  origDest: PixelRect;
  source: PixelRect;
  pageW: number;
  pageH: number;
  scaleX: number;
  scaleY: number;
  snapshot: GalleryPage[];
  moved: boolean;
}

interface AnnotationDragSession {
  kind: 'region' | 'point';
  noteId: string;
  pageId: string;
  startClientX: number;
  startClientY: number;
  origRect?: PixelRect;
  origPoint?: PixelPoint;
  pageW: number;
  pageH: number;
  scaleX: number;
  scaleY: number;
  snapshot: GalleryPage[];
  moved: boolean;
}

interface CropDragSession {
  kind: 'move' | 'resize';
  handle?: ResizeHandle;
  startClientX: number;
  startClientY: number;
  origRect: PixelRect;
  pageW: number;
  pageH: number;
  scaleX: number;
  scaleY: number;
}

interface LoupeState {
  px: number;
  py: number;
  pctX: number;
  pctY: number;
  dataUrl: string;
  hex: string;
  rgb: string;
}

interface WorkspaceViewProps {
  pages: GalleryPage[];
  activePageId: string;
  groupNotes: GroupNote[];
  fitImageOnOpen: boolean;
  defaultTool: number;
  shortcuts: Record<string, string>;
  onCapture?: () => void;
  onAddPageCapture: () => void;
  onClose: () => void;
  onSelectPage: (pageId: string) => void;
  onRemovePage: (pageId: string) => Promise<boolean>;
  prepareWorkspaceSnapshot: (media: HTMLElement | null) => Promise<
    Awaited<ReturnType<typeof attachRedactedPagesToPayload>> & { generation: number; changedSinceSnapshot: boolean }
  >;
  onUpdatePages: (updater: (prev: GalleryPage[]) => GalleryPage[]) => void;
  onUpdateGroupNotes: (updater: (prev: GroupNote[]) => GroupNote[]) => void;
  onOpenFiles: () => void;
  onPasteImage: () => void;
  onViewShortcut: (event: KeyboardEvent) => void;
  onAnalyze: () => void;
  onToast: (message: string) => void;
}

function clampRect(r: PixelRect, maxW: number, maxH: number): PixelRect {
  const x1 = Math.max(0, Math.min(maxW - 1, Math.round(r.x1)));
  const y1 = Math.max(0, Math.min(maxH - 1, Math.round(r.y1)));
  const x2 = Math.max(x1 + 1, Math.min(maxW, Math.round(r.x2)));
  const y2 = Math.max(y1 + 1, Math.min(maxH, Math.round(r.y2)));
  return { x1, y1, x2, y2 };
}

function shiftRectAfterCrop(r: PixelRect, crop: PixelRect): PixelRect | null {
  const cw = crop.x2 - crop.x1;
  const ch = crop.y2 - crop.y1;
  const x1 = Math.max(0, Math.min(cw, r.x1 - crop.x1));
  const y1 = Math.max(0, Math.min(ch, r.y1 - crop.y1));
  const x2 = Math.max(0, Math.min(cw, r.x2 - crop.x1));
  const y2 = Math.max(0, Math.min(ch, r.y2 - crop.y1));
  if (x2 - x1 < 8 || y2 - y1 < 8) return null;
  return { x1, y1, x2, y2 };
}

function ratioValue(ratio: CropRatio): number | null {
  if (ratio === '16:9') return 16 / 9;
  if (ratio === '4:3') return 4 / 3;
  if (ratio === '1:1') return 1;
  return null;
}

export default function WorkspaceView({
  pages,
  activePageId,
  groupNotes,
  fitImageOnOpen,
  defaultTool,
  shortcuts,
  onCapture,
  onAddPageCapture,
  onClose,
  onSelectPage,
  onRemovePage,
  prepareWorkspaceSnapshot,
  onUpdatePages,
  onUpdateGroupNotes,
  onOpenFiles,
  onPasteImage,
  onViewShortcut,
  onAnalyze,
  onToast,
}: WorkspaceViewProps) {
  const t = useI18n();
  const shortcutLabel = (label: string, id: string) => {
    const keys = shortcuts[id];
    return keys ? t("workspace.shortcutTooltip", { label, keys }) : label;
  };
  const activePage = pages.find((p) => p.id === activePageId) ?? pages[0];
  const [addPageOpen, setAddPageOpen] = useState(false);
  const [removePageId, setRemovePageId] = useState<string | null>(null);
  const [removingPage, setRemovingPage] = useState(false);
  const removePageTarget = pages.find((page) => page.id === removePageId);
  const removeTriggerRef = useRef<HTMLButtonElement | null>(null);
  const removeCancelRef = useRef<HTMLButtonElement>(null);
  const addPageTriggerRef = useRef<HTMLButtonElement>(null);
  const confirmRemovePage = async () => {
    if (!removePageTarget || removingPage) return;
    setRemovingPage(true);
    if (await onRemovePage(removePageTarget.id)) setRemovePageId(null);
    setRemovingPage(false);
  };
  const [tool, setTool] = useState<ToolMode>(() => DEFAULT_TOOLS[defaultTool] ?? 'smart');
  const [{ zoom, panOffset }, setCanvasView] = useState(FIT_VIEW);
  const [isPanning, setIsPanning] = useState(false);
  const [spacePressed, setSpacePressed] = useState(false);

  const [selectedNoteId, setSelectedNoteId] = useState<string>(activePage?.annotations[0]?.id ?? '');
  const [hoveredNoteId, setHoveredNoteId] = useState<string | null>(null);
  const [inlineEditNoteId, setInlineEditNoteId] = useState<string | null>(null);
  const [selectedMosaicId, setSelectedMosaicId] = useState<string | null>(null);
  const [editingNoteId, setEditingNoteId] = useState<string | null>(null);
  const [inspectorTab, setInspectorTab] = useState<'page' | 'group'>('page');
  const [inspectorOpen, setInspectorOpen] = useState(() =>
    typeof window !== 'undefined' ? window.innerWidth > 960 : true
  );
  const [hoverBlockId, setHoverBlockId] = useState<string | null>(null);
  const [selectedBlockId, setSelectedBlockId] = useState<string | null>(null);
  const [explosionWaveKey, setExplosionWaveKey] = useState(0);
  const [showExplosionWave, setShowExplosionWave] = useState(false);
  const explosionWaveTimerRef = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);
  const [jsonOpen, setJsonOpen] = useState(false);

  // Instant custom tooltip state (Point 2)
  const [dockTip, setDockTip] = useState<{
    text: string;
    left: number;
    top: number;
    placement: 'above' | 'below';
  } | null>(null);
  const tipDelayRef = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);
  const tipVisibleRef = useRef(false);
  tipVisibleRef.current = Boolean(dockTip);

  // Anchored contextual subbar offset (Point 6)
  const [subbarLeft, setSubbarLeft] = useState<number | null>(null);

  // Tool options & Brush state
  const [hideAnnotations, setHideAnnotations] = useState(false);
  const [magnifierEnabled, setMagnifierEnabled] = useState(false);
  const [mosaicStyle, setMosaicStyle] = useState<'pixelate' | 'blur'>('pixelate');
  const [mosaicStrength, setMosaicStrength] = useState(12);
  const [brushMode, setBrushMode] = useState<'pen' | 'highlighter'>('pen');
  const [brushColor, setBrushColor] = useState('#FF453A');
  const [brushWidth, setBrushWidth] = useState(4);
  const [activeStroke, setActiveStroke] = useState<BrushStroke | null>(null);
  const activeStrokeRef = useRef<BrushStroke | null>(null);
  activeStrokeRef.current = activeStroke;

  // Interactive crop state
  const [cropRect, setCropRect] = useState<PixelRect | null>(null);
  const [cropRatio, setCropRatio] = useState<CropRatio>('free');
  const cropRatioRef = useRef<CropRatio>(cropRatio);
  cropRatioRef.current = cropRatio;

  // Loupe hover state
  const [loupe, setLoupe] = useState<LoupeState | null>(null);
  const loupeRafRef = useRef<number | null>(null);
  const hoverCloseTimerRef = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);

  // Draft box / mosaic draw / manual Explode cut-draw state
  const [draftBox, setDraftBox] = useState<PixelRect | null>(null);
  const [manualCutBox, setManualCutBox] = useState<PixelRect | null>(null);
  const drawStartRef = useRef<{ x: number; y: number } | null>(null);
  const manualCutStartRef = useRef<{ x: number; y: number } | null>(null);

  // Undo / Redo stacks
  const undoStackRef = useRef<GalleryPage[][]>([]);
  const redoStackRef = useRef<GalleryPage[][]>([]);
  const [, setHistoryTick] = useState(0);
  const pagesRef = useRef(pages);
  pagesRef.current = pages;

  // Window-tracked drag sessions
  const explodeDragRef = useRef<ExplodeDragSession | null>(null);
  const annDragRef = useRef<AnnotationDragSession | null>(null);
  const cropDragRef = useRef<CropDragSession | null>(null);
  const panDragRef = useRef<{ startClientX: number; startClientY: number; origX: number; origY: number } | null>(null);
  const updatePagesRef = useRef(onUpdatePages);
  updatePagesRef.current = onUpdatePages;
  const requestedDetectKeysRef = useRef<Set<string>>(new Set());

  // Video timeline preview shell
  const [videoMode, setVideoMode] = useState(false);
  const [videoPlaying, setVideoPlaying] = useState(false);
  const [videoTimeMs, setVideoTimeMs] = useState(3240);

  const canvasColRef = useRef<HTMLDivElement>(null);
  const brushBtnRef = useRef<HTMLButtonElement>(null);
  const mosaicBtnRef = useRef<HTMLButtonElement>(null);
  const cropBtnRef = useRef<HTMLButtonElement>(null);
  const subbarRef = useRef<HTMLDivElement>(null);
  const stageRef = useRef<HTMLDivElement>(null);
  const artboardRef = useRef<HTMLDivElement>(null);
  const artboardMediaRef = useRef<HTMLDivElement>(null);
  const mosaicCanvasRef = useRef<HTMLCanvasElement>(null);
  const sourceBitmapRef = useRef<HTMLImageElement | null>(null);
  const [bitmapReadyTick, setBitmapReadyTick] = useState(0);

  // Auto collapse inspector on narrow viewports (<960px) so the canvas never gets squished vertically
  useEffect(() => {
    const mq = window.matchMedia('(max-width: 960px)');
    const onChange = (e: MediaQueryListEvent) => {
      if (e.matches) setInspectorOpen(false);
    };
    mq.addEventListener('change', onChange);
    return () => mq.removeEventListener('change', onChange);
  }, []);

  const activePageIdForDetect = activePage?.id ?? '';
  const activePageUrlForDetect = activePage?.imageUrl ?? '';
  const activePageBlockCount = activePage?.blocks.length ?? 0;
  const activePageIsVideo = Boolean(activePage?.isVideo);

  useEffect(() => {
    if (
      tool !== 'explode' ||
      !isDesktopWebView() ||
      !activePageIdForDetect ||
      activePageIsVideo ||
      activePageBlockCount > 0
    ) {
      return;
    }
    const detectKey = `${activePageIdForDetect}|${activePageUrlForDetect}`;
    if (requestedDetectKeysRef.current.has(detectKey)) return;
    requestedDetectKeysRef.current.add(detectKey);

    desktopRpc<{ blocks?: ExplodeBlock[] }>('detect.regions', { pageId: activePageIdForDetect })
      .then((res) => {
        const blocks = Array.isArray(res?.blocks) ? res.blocks : [];
        if (blocks.length === 0) return;
        updatePagesRef.current((prev) =>
          prev.map((p) =>
            p.id === activePageIdForDetect && p.blocks.length === 0 ? { ...p, blocks } : p
          )
        );
      })
      .catch(() => {
        requestedDetectKeysRef.current.delete(detectKey);
      });
  }, [tool, activePageIdForDetect, activePageUrlForDetect, activePageBlockCount, activePageIsVideo]);

  useEffect(
    () => () => {
      clearTimeout(hoverCloseTimerRef.current);
      clearTimeout(tipDelayRef.current);
      clearTimeout(explosionWaveTimerRef.current);
    },
    []
  );

  const showTip = (target: HTMLElement, text: string, placement: 'above' | 'below' = 'above') => {
    clearTimeout(tipDelayRef.current);
    const col = canvasColRef.current;
    if (!col) return;
    const colRect = col.getBoundingClientRect();
    const btnRect = target.getBoundingClientRect();
    const left = Math.max(54, Math.min(colRect.width - 54, btnRect.left - colRect.left + btnRect.width / 2));
    const top = placement === 'above' ? btnRect.top - colRect.top - 8 : btnRect.bottom - colRect.top + 8;
    if (tipVisibleRef.current) {
      setDockTip({ text, left, top, placement });
    } else {
      tipDelayRef.current = setTimeout(() => {
        setDockTip({ text, left, top, placement });
      }, 65);
    }
  };

  const hideTip = () => {
    clearTimeout(tipDelayRef.current);
    setDockTip(null);
  };

  const tipProps = (text: string, placement: 'above' | 'below' = 'above') => ({
    onPointerEnter: (e: React.PointerEvent<HTMLElement>) => showTip(e.currentTarget, text, placement),
    onPointerLeave: hideTip,
    onPointerDown: hideTip,
  });

  useLayoutEffect(() => {
    const computeSubbarPos = () => {
      const col = canvasColRef.current;
      const bar = subbarRef.current;
      if (!col || !bar) {
        setSubbarLeft(null);
        return;
      }
      const anchor =
        tool === 'brush'
          ? brushBtnRef.current
          : tool === 'mosaic'
            ? mosaicBtnRef.current
            : tool === 'crop'
              ? cropBtnRef.current
              : null;
      if (!anchor) {
        setSubbarLeft(null);
        return;
      }
      const colRect = col.getBoundingClientRect();
      const anchorRect = anchor.getBoundingClientRect();
      const subW = bar.offsetWidth;
      if (colRect.width <= 0 || subW <= 0) {
        setSubbarLeft(null);
        return;
      }
      const desired = anchorRect.left - colRect.left + anchorRect.width / 2 - subW / 2;
      const clamped = Math.round(Math.max(12, Math.min(colRect.width - subW - 12, desired)));
      setSubbarLeft(clamped);
    };
    computeSubbarPos();
    window.addEventListener('resize', computeSubbarPos);
    return () => window.removeEventListener('resize', computeSubbarPos);
  }, [tool, inspectorOpen, pages.length, cropRect !== null, activePage?.mosaics?.length, activePage?.brushStrokes?.length]);

  const scheduleHoverOpen = (noteId: string) => {
    clearTimeout(hoverCloseTimerRef.current);
    setHoveredNoteId(noteId);
  };

  const scheduleHoverClose = (noteId: string) => {
    clearTimeout(hoverCloseTimerRef.current);
    hoverCloseTimerRef.current = setTimeout(() => {
      setHoveredNoteId((cur) => (cur === noteId && inlineEditNoteId !== noteId ? null : cur));
    }, 260);
  };

  const pushUndoSnapshot = (snapshot: GalleryPage[]) => {
    undoStackRef.current = [...undoStackRef.current.slice(-39), snapshot];
    redoStackRef.current = [];
    setHistoryTick((t) => t + 1);
  };

  const commitPages = (updater: (prev: GalleryPage[]) => GalleryPage[]) => {
    const snap = pagesRef.current;
    pushUndoSnapshot(snap);
    onUpdatePages(updater);
  };

  const syncDesktopPageAfterUndoRedo = async (restoredPages: GalleryPage[]) => {
    if (!isDesktopWebView() || !activePage) return;
    const restored = restoredPages.find((p) => p.id === activePage.id);
    if (!restored) return;
    if (
      restored.width !== activePage.width ||
      restored.height !== activePage.height ||
      restored.imageUrl !== activePage.imageUrl
    ) {
      if (!restored.imageUrl) return;
      const img = await loadBitmapForPage(restored, artboardMediaRef.current);
      if (!img) return;
      const c = document.createElement('canvas');
      c.width = restored.width;
      c.height = restored.height;
      const ctx = c.getContext('2d');
      if (!ctx) return;
      ctx.drawImage(img, 0, 0, restored.width, restored.height);
      const b64 = c.toDataURL('image/png').split(',')[1] ?? '';
      if (!b64) return;
      desktopRpc('image.updatePage', { pageId: restored.id, pngBase64: b64 }).catch(() => {});
    }
  };

  const handleUndo = () => {
    if (!undoStackRef.current.length) return;
    const prevSnap = undoStackRef.current[undoStackRef.current.length - 1];
    undoStackRef.current = undoStackRef.current.slice(0, -1);
    redoStackRef.current = [...redoStackRef.current.slice(-39), pagesRef.current];
    onUpdatePages(() => prevSnap);
    setHistoryTick((t) => t + 1);
    void syncDesktopPageAfterUndoRedo(prevSnap);
    onToast(t("workspace.undone"));
  };

  const handleRedo = () => {
    if (!redoStackRef.current.length) return;
    const nextSnap = redoStackRef.current[redoStackRef.current.length - 1];
    redoStackRef.current = redoStackRef.current.slice(0, -1);
    undoStackRef.current = [...undoStackRef.current.slice(-39), pagesRef.current];
    onUpdatePages(() => nextSnap);
    setHistoryTick((t) => t + 1);
    void syncDesktopPageAfterUndoRedo(nextSnap);
    onToast(t("workspace.redone"));
  };

  useEffect(() => {
    let cancelled = false;
    if (!activePage) return;
    loadBitmapForPage(activePage, artboardMediaRef.current).then((img) => {
      if (cancelled) return;
      sourceBitmapRef.current = img;
      setBitmapReadyTick((t) => t + 1);
    });
    return () => {
      cancelled = true;
    };
  }, [activePage?.id, activePage?.imageUrl, activePage?.sceneId, activePage?.width, activePage?.height]);

  useEffect(() => {
    const canvas = mosaicCanvasRef.current;
    const img = sourceBitmapRef.current;
    if (!canvas || !activePage) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    const regions = activePage.mosaics ?? [];
    if (!img || regions.length === 0) return;
    drawMosaicsOnContext(ctx, img, regions, activePage.width, activePage.height, canvas.width, canvas.height);
  }, [activePage?.id, activePage?.mosaics, activePage?.width, activePage?.height, bitmapReadyTick]);

  useEffect(() => {
    setSelectedNoteId(activePage?.annotations[0]?.id ?? '');
    setHoveredNoteId(null);
    setInlineEditNoteId(null);
    setSelectedMosaicId(null);
    setEditingNoteId(null);
    setHoverBlockId(null);
    setSelectedBlockId(null);
    setLoupe(null);
    setCropRect(null);
    setCanvasView((view) => ({ ...view, panOffset: FIT_VIEW.panOffset }));
    setVideoMode(false);
    setTool((t) => (t === 'crop' ? 'box' : t));
  }, [activePage?.id, activePage?.isVideo]);

  useLayoutEffect(() => {
    setTool(DEFAULT_TOOLS[defaultTool] ?? 'smart');
  }, [defaultTool]);

  useLayoutEffect(() => {
    const container = artboardRef.current?.parentElement;
    const width = activePage?.width;
    const height = activePage?.height;
    if (fitImageOnOpen || !container || !width || !height) {
      setCanvasView(FIT_VIEW);
      return;
    }

    // The artboard fits the image in this container before applying the zoom transform.
    const fitScale = Math.min(container.clientWidth / width, container.clientHeight / height);
    setCanvasView(fitScale > 0 ? { ...FIT_VIEW, zoom: 100 / fitScale } : FIT_VIEW);
  }, [activePage?.id, activePage?.width, activePage?.height, fitImageOnOpen]);

  const enterCropMode = () => {
    if (!activePage) return;
    const padX = Math.max(0, Math.round(activePage.width * 0.08));
    const padY = Math.max(0, Math.round(activePage.height * 0.08));
    const w = activePage.width;
    const h = activePage.height;
    setCropRatio('free');
    setCropRect(
      w > 96 && h > 96
        ? { x1: padX, y1: padY, x2: w - padX, y2: h - padY }
        : { x1: 0, y1: 0, x2: w, y2: h }
    );
    setTool('crop');
  };

  const applyCropRatioPreset = (ratio: CropRatio) => {
    setCropRatio(ratio);
    if (!activePage || !cropRect) return;
    const targetRatio = ratioValue(ratio);
    if (!targetRatio) return;
    const cx = (cropRect.x1 + cropRect.x2) / 2;
    const cy = (cropRect.y1 + cropRect.y2) / 2;
    let cw = Math.max(32, cropRect.x2 - cropRect.x1);
    let ch = Math.round(cw / targetRatio);
    if (ch > activePage.height) {
      ch = activePage.height;
      cw = Math.round(ch * targetRatio);
    }
    if (cw > activePage.width) {
      cw = activePage.width;
      ch = Math.round(cw / targetRatio);
    }
    const x1 = Math.round(Math.max(0, Math.min(activePage.width - cw, cx - cw / 2)));
    const y1 = Math.round(Math.max(0, Math.min(activePage.height - ch, cy - ch / 2)));
    setCropRect({ x1, y1, x2: x1 + cw, y2: y1 + ch });
  };

  const confirmCrop = async () => {
    if (!activePage || !cropRect) return;
    const bmp =
      sourceBitmapRef.current ?? (await loadBitmapForPage(activePage, artboardMediaRef.current));
    if (!bmp) {
      onToast(t("workspace.thisImageIsStillLoadingTryAgain"));
      return;
    }
    const cropped = cropPageBitmap(bmp, activePage.width, activePage.height, cropRect);
    let nextUrl = cropped.dataUrl;
    let nextBlocks: ExplodeBlock[] = [];

    if (isDesktopWebView() && !activePage.sceneId) {
      try {
        const res = await desktopRpc<{
          mediaUrl?: string;
          blocks?: ExplodeBlock[];
        }>('image.updatePage', {
          pageId: activePage.id,
          pngBase64: cropped.pngBase64,
        });
        if (res?.mediaUrl) nextUrl = res.mediaUrl;
        if (Array.isArray(res?.blocks)) nextBlocks = res.blocks;
      } catch {
        // fallback cleanly
      }
    }

    if (nextBlocks.length === 0) {
      const tmpImg = new Image();
      tmpImg.src = cropped.dataUrl;
      await new Promise<void>((resolve) => {
        if (tmpImg.complete) resolve();
        else tmpImg.onload = () => resolve();
      });
      nextBlocks = detectImageRegions(tmpImg, cropped.width, cropped.height);
    }

    const nextAnnotations: AnnotationItem[] = activePage.annotations
      .map((note): AnnotationItem | null => {
        if (note.kind === 'global') return note;
        if (note.kind === 'point' && note.point) {
          const px = note.point.x - cropRect.x1;
          const py = note.point.y - cropRect.y1;
          if (px < 0 || py < 0 || px > cropped.width || py > cropped.height) return null;
          return { ...note, point: { x: Math.round(px), y: Math.round(py) } };
        }
        if (note.kind === 'region' && note.rect) {
          const shifted = shiftRectAfterCrop(note.rect, cropRect);
          if (!shifted) return null;
          return { ...note, rect: shifted, blockId: undefined, movement: undefined };
        }
        return note;
      })
      .filter((x): x is AnnotationItem => x !== null)
      .map((n, i) => ({ ...n, number: i + 1 }));

    const nextMosaics: MosaicRegion[] = (activePage.mosaics ?? [])
      .map((m) => {
        const shifted = shiftRectAfterCrop(m.rect, cropRect);
        return shifted ? { ...m, rect: shifted } : null;
      })
      .filter((x): x is MosaicRegion => x !== null);

    const nextStrokes: BrushStroke[] = (activePage.brushStrokes ?? [])
      .map((s) => ({
        ...s,
        points: s.points
          .map((pt) => ({ x: pt.x - cropRect.x1, y: pt.y - cropRect.y1 }))
          .filter((pt) => pt.x >= 0 && pt.y >= 0 && pt.x <= cropped.width && pt.y <= cropped.height),
      }))
      .filter((s) => s.points.length >= 2);

    commitPages((prev) =>
      prev.map((p) =>
        p.id !== activePage.id
          ? p
          : {
              ...p,
              width: cropped.width,
              height: cropped.height,
              imageUrl: nextUrl,
              sceneId: undefined,
              blocks: nextBlocks,
              annotations: nextAnnotations,
              mosaics: nextMosaics,
              brushStrokes: nextStrokes,
            }
      )
    );
    setCropRect(null);
    setCanvasView((view) => ({ ...view, panOffset: FIT_VIEW.panOffset }));
    setTool('box');
    onToast(t("workspace.canvasCroppedTo", { width: cropped.width, height: cropped.height }));
  };

  const stepZoom = (factor: number) => {
    setCanvasView((view) => ({ ...view, zoom: clampZoom(view.zoom * factor) }));
  };

  const resetCanvasView = () => {
    setCanvasView(FIT_VIEW);
  };

  const hasActivePage = Boolean(activePage);
  useEffect(() => {
    const stage = stageRef.current;
    const inner = artboardRef.current?.parentElement;
    if (!stage || !inner) return;

    const onWheel = (event: WheelEvent) => {
      if (!event.ctrlKey && !event.metaKey) return;
      event.preventDefault();

      const bounds = inner.getBoundingClientRect();
      const x = event.clientX - (bounds.left + bounds.width / 2);
      const y = event.clientY - (bounds.top + bounds.height / 2);
      const deltaUnit = event.deltaMode;
      const deltaY = event.deltaY * (deltaUnit === WheelEvent.DOM_DELTA_LINE ? 16 :
        deltaUnit === WheelEvent.DOM_DELTA_PAGE ? stage.clientHeight : 1);
      const factor = ZOOM_STEP ** (-deltaY / 100);

      setCanvasView((view) => {
        const nextZoom = clampZoom(view.zoom * factor);
        if (nextZoom === view.zoom) return view;
        const ratio = nextZoom / view.zoom;
        return {
          zoom: nextZoom,
          panOffset: {
            x: x - (x - view.panOffset.x) * ratio,
            y: y - (y - view.panOffset.y) * ratio,
          },
        };
      });
    };

    stage.addEventListener('wheel', onWheel, { passive: false });
    return () => stage.removeEventListener('wheel', onWheel);
  }, [hasActivePage]);

  useEffect(() => {
    if (!videoMode || !videoPlaying) return;
    const id = setInterval(() => {
      setVideoTimeMs((prev) => (prev + 120 >= 12000 ? 0 : prev + 120));
    }, 120);
    return () => clearInterval(id);
  }, [videoMode, videoPlaying]);

  const nextNoteNumber = (annotations: AnnotationItem[]) =>
    annotations.reduce((max, item) => Math.max(max, item.number), 0) + 1;

  // Window-level pointermove / pointerup for Explode, Select/Move, Crop, and Canvas Pan drags
  useEffect(() => {
    const computeResizeRect = (
      o: PixelRect,
      handle: ResizeHandle,
      dxRaw: number,
      dyRaw: number,
      pageW: number,
      pageH: number,
      minSize: number,
      lockedRatio: number | null = null
    ): PixelRect => {
      let { x1, y1, x2, y2 } = o;
      if (handle.includes('w')) x1 = Math.round(Math.max(0, Math.min(x2 - minSize, o.x1 + dxRaw)));
      if (handle.includes('e')) x2 = Math.round(Math.min(pageW, Math.max(x1 + minSize, o.x2 + dxRaw)));
      if (handle.includes('n')) y1 = Math.round(Math.max(0, Math.min(y2 - minSize, o.y1 + dyRaw)));
      if (handle.includes('s')) y2 = Math.round(Math.min(pageH, Math.max(y1 + minSize, o.y2 + dyRaw)));

      if (lockedRatio) {
        let w = Math.max(minSize, x2 - x1);
        let h = Math.round(w / lockedRatio);
        if (handle.includes('n') && !handle.includes('s')) {
          y1 = Math.max(0, y2 - h);
          h = y2 - y1;
          w = Math.round(h * lockedRatio);
          if (handle.includes('w')) x1 = Math.max(0, x2 - w);
          else x2 = Math.min(pageW, x1 + w);
        } else {
          if (y1 + h > pageH) {
            h = pageH - y1;
            w = Math.round(h * lockedRatio);
          }
          if (handle.includes('w') && !handle.includes('e')) x1 = Math.max(0, x2 - w);
          else x2 = Math.min(pageW, x1 + w);
          y2 = Math.min(pageH, y1 + h);
        }
      }
      return { x1, y1, x2, y2 };
    };

    const onWindowPointerMove = (event: PointerEvent) => {
      if (panDragRef.current) {
        const pd = panDragRef.current;
        setCanvasView((view) => ({
          ...view,
          panOffset: {
            x: pd.origX + event.clientX - pd.startClientX,
            y: pd.origY + event.clientY - pd.startClientY,
          },
        }));
        return;
      }

      const ex = explodeDragRef.current;
      if (ex) {
        const dxRaw = (event.clientX - ex.startClientX) * ex.scaleX;
        const dyRaw = (event.clientY - ex.startClientY) * ex.scaleY;
        let nextDest: PixelRect;
        if (ex.kind === 'move') {
          const w = ex.origDest.x2 - ex.origDest.x1;
          const h = ex.origDest.y2 - ex.origDest.y1;
          const x1 = Math.round(Math.max(0, Math.min(ex.pageW - w, ex.origDest.x1 + dxRaw)));
          const y1 = Math.round(Math.max(0, Math.min(ex.pageH - h, ex.origDest.y1 + dyRaw)));
          nextDest = { x1, y1, x2: x1 + w, y2: y1 + h };
        } else {
          nextDest = computeResizeRect(ex.origDest, ex.handle ?? 'se', dxRaw, dyRaw, ex.pageW, ex.pageH, 18);
        }
        if (
          Math.abs(nextDest.x1 - ex.origDest.x1) > 1 ||
          Math.abs(nextDest.y1 - ex.origDest.y1) > 1 ||
          Math.abs(nextDest.x2 - ex.origDest.x2) > 1 ||
          Math.abs(nextDest.y2 - ex.origDest.y2) > 1
        ) {
          ex.moved = true;
        }
        const src = ex.source;
        const moveNoteId = `move-${ex.blockId}`;
        const dx = nextDest.x1 - src.x1;
        const dy = nextDest.y1 - src.y1;
        const sw = Math.max(1, src.x2 - src.x1);
        const sh = Math.max(1, src.y2 - src.y1);
        const scaleX = (nextDest.x2 - nextDest.x1) / sw;
        const scaleY = (nextDest.y2 - nextDest.y1) / sh;
        const significant =
          Math.abs(dx) > 2 || Math.abs(dy) > 2 || Math.abs(scaleX - 1) > 0.02 || Math.abs(scaleY - 1) > 0.02;

        updatePagesRef.current((prev) =>
          prev.map((page) => {
            if (page.id !== ex.pageId) return page;
            const blocks = page.blocks.map((b) => (b.id === ex.blockId ? { ...b, dest: nextDest } : b));
            const idx = page.annotations.findIndex((a) => a.blockId === ex.blockId);
            const annotations = [...page.annotations];
            if (idx >= 0) {
              annotations[idx] = {
                ...annotations[idx],
                rect: nextDest,
                movement: { source: src, rect: nextDest, dx, dy, scaleX, scaleY },
              };
            } else if (significant) {
              annotations.push({
                id: moveNoteId,
                number: nextNoteNumber(annotations),
                kind: 'region',
                text: '',
                blockId: ex.blockId,
                rect: nextDest,
                movement: { source: src, rect: nextDest, dx, dy, scaleX, scaleY },
              });
            }
            return { ...page, blocks, annotations };
          })
        );
        if (significant) setSelectedNoteId(moveNoteId);
        return;
      }

      const ad = annDragRef.current;
      if (ad) {
        const dx = Math.round((event.clientX - ad.startClientX) * ad.scaleX);
        const dy = Math.round((event.clientY - ad.startClientY) * ad.scaleY);
        if (Math.abs(dx) > 1 || Math.abs(dy) > 1) ad.moved = true;

        updatePagesRef.current((prev) =>
          prev.map((page) => {
            if (page.id !== ad.pageId) return page;
            return {
              ...page,
              annotations: page.annotations.map((note) => {
                if (note.id !== ad.noteId) return note;
                if (ad.kind === 'region' && ad.origRect) {
                  const w = ad.origRect.x2 - ad.origRect.x1;
                  const h = ad.origRect.y2 - ad.origRect.y1;
                  const nx1 = Math.max(0, Math.min(ad.pageW - w, ad.origRect.x1 + dx));
                  const ny1 = Math.max(0, Math.min(ad.pageH - h, ad.origRect.y1 + dy));
                  return { ...note, rect: { x1: nx1, y1: ny1, x2: nx1 + w, y2: ny1 + h } };
                }
                if (ad.kind === 'point' && ad.origPoint) {
                  const nx = Math.max(0, Math.min(ad.pageW, ad.origPoint.x + dx));
                  const ny = Math.max(0, Math.min(ad.pageH, ad.origPoint.y + dy));
                  return { ...note, point: { x: nx, y: ny } };
                }
                return note;
              }),
            };
          })
        );
        return;
      }

      const cd = cropDragRef.current;
      if (cd) {
        const dxRaw = (event.clientX - cd.startClientX) * cd.scaleX;
        const dyRaw = (event.clientY - cd.startClientY) * cd.scaleY;
        if (cd.kind === 'move') {
          const w = cd.origRect.x2 - cd.origRect.x1;
          const h = cd.origRect.y2 - cd.origRect.y1;
          const x1 = Math.round(Math.max(0, Math.min(cd.pageW - w, cd.origRect.x1 + dxRaw)));
          const y1 = Math.round(Math.max(0, Math.min(cd.pageH - h, cd.origRect.y1 + dyRaw)));
          setCropRect({ x1, y1, x2: x1 + w, y2: y1 + h });
        } else {
          setCropRect(
            computeResizeRect(
              cd.origRect,
              cd.handle ?? 'se',
              dxRaw,
              dyRaw,
              cd.pageW,
              cd.pageH,
              32,
              ratioValue(cropRatioRef.current)
            )
          );
        }
      }
    };

    const onWindowPointerUp = () => {
      if (panDragRef.current) {
        panDragRef.current = null;
        setIsPanning(false);
      }
      if (explodeDragRef.current) {
        const ex = explodeDragRef.current;
        explodeDragRef.current = null;
        if (ex.moved) pushUndoSnapshot(ex.snapshot);
      }
      if (annDragRef.current) {
        const ad = annDragRef.current;
        annDragRef.current = null;
        if (ad.moved) pushUndoSnapshot(ad.snapshot);
      }
      if (cropDragRef.current) {
        cropDragRef.current = null;
      }
    };

    window.addEventListener('pointermove', onWindowPointerMove);
    window.addEventListener('pointerup', onWindowPointerUp);
    return () => {
      window.removeEventListener('pointermove', onWindowPointerMove);
      window.removeEventListener('pointerup', onWindowPointerUp);
    };
  }, []);

  const clientToPixel = (clientX: number, clientY: number) => {
    const el = artboardRef.current;
    if (!el || !activePage) return { x: 0, y: 0 };
    const rect = el.getBoundingClientRect();
    const x = Math.round(
      Math.max(0, Math.min(activePage.width, ((clientX - rect.left) / rect.width) * activePage.width))
    );
    const y = Math.round(
      Math.max(0, Math.min(activePage.height, ((clientY - rect.top) / rect.height) * activePage.height))
    );
    return { x, y };
  };

  const appendAnnotation = (partial: Omit<AnnotationItem, 'id' | 'number'>) => {
    const targetPageId = activePage.id;
    const newId = `n-${Date.now()}-${Math.random().toString(36).slice(2, 6)}`;
    commitPages((prev) =>
      prev.map((page) => {
        if (page.id !== targetPageId) return page;
        const number = nextNoteNumber(page.annotations);
        const created: AnnotationItem = { id: newId, number, ...partial };
        return { ...page, annotations: [...page.annotations, created] };
      })
    );
    setInspectorTab('page');
    setSelectedNoteId(newId);
    if (inspectorOpen) {
      setEditingNoteId(newId);
    } else if (partial.kind !== 'global') {
      setHoveredNoteId(newId);
      setInlineEditNoteId(newId);
    }
  };

  const startPan = (event: React.PointerEvent<HTMLDivElement>) => {
    panDragRef.current = {
      startClientX: event.clientX,
      startClientY: event.clientY,
      origX: panOffset.x,
      origY: panOffset.y,
    };
    setIsPanning(true);
  };

  const handleStagePointerDownCapture = (event: React.PointerEvent<HTMLDivElement>) => {
    if (event.button !== 1) return;
    if ((event.target as Element).closest(`${TEXT_INPUT_SELECTOR}, .inline-note-popover:has(textarea)`)) return;
    event.preventDefault();
    event.stopPropagation();
    event.currentTarget.setPointerCapture(event.pointerId);
    startPan(event);
  };

  const preventMiddleClickDefault = (event: React.MouseEvent<HTMLDivElement>) => {
    if (event.button === 1) event.preventDefault();
  };

  const handleStagePointerDown = (event: React.PointerEvent<HTMLDivElement>) => {
    if (event.button === 1) return;
    if (tool === 'pan' || spacePressed) {
      event.preventDefault();
      startPan(event);
      return;
    }
    if (event.target === event.currentTarget || (event.target as HTMLElement).classList.contains('canvas-stage-inner')) {
      setInlineEditNoteId(null);
      setHoveredNoteId(null);
    }
  };

  const handleArtboardPointerDown = (event: React.PointerEvent<HTMLDivElement>) => {
    if (!activePage || tool === 'crop') return;
    if (tool === 'pan' || spacePressed || event.button === 1) {
      return; // handled by parent stage-wrap pan handler
    }
    const pt = clientToPixel(event.clientX, event.clientY);

    if (tool === 'select') {
      setSelectedMosaicId(null);
      setSelectedNoteId('');
      setInlineEditNoteId(null);
      setHoveredNoteId(null);
      return;
    }

    if (tool === 'explode') {
      setSelectedBlockId(null);
      event.currentTarget.setPointerCapture(event.pointerId);
      manualCutStartRef.current = pt;
      setManualCutBox({ x1: pt.x, y1: pt.y, x2: pt.x, y2: pt.y });
      return;
    }

    if (tool === 'brush') {
      event.currentTarget.setPointerCapture(event.pointerId);
      const stroke: BrushStroke = {
        id: `b-${Date.now()}-${Math.random().toString(36).slice(2, 6)}`,
        points: [pt],
        color: brushColor,
        width: brushMode === 'highlighter' ? brushWidth * 2.6 : brushWidth,
        opacity: brushMode === 'highlighter' ? 0.38 : 1,
      };
      setActiveStroke(stroke);
      return;
    }

    if (tool === 'point') {
      appendAnnotation({ kind: 'point', text: '', point: pt });
      return;
    }

    if (tool === 'smart') {
      const hit = activePage.blocks.find(
        (b) => pt.x >= b.dest.x1 && pt.x <= b.dest.x2 && pt.y >= b.dest.y1 && pt.y <= b.dest.y2
      );
      if (hit) {
        appendAnnotation({ kind: 'region', text: '', rect: { ...hit.dest } });
      } else {
        onToast(t("workspace.noDetectedRegionHereSwitchToBox"));
      }
      return;
    }

    if (tool === 'box' || tool === 'mosaic') {
      setSelectedMosaicId(null);
      event.currentTarget.setPointerCapture(event.pointerId);
      drawStartRef.current = pt;
      setDraftBox({ x1: pt.x, y1: pt.y, x2: pt.x, y2: pt.y });
    }
  };

  const updateLoupeAtPoint = (pt: PixelPoint) => {
    if (!magnifierEnabled || !activePage || !sourceBitmapRef.current || tool === 'explode') {
      if (loupe) setLoupe(null);
      return;
    }
    if (loupeRafRef.current !== null) return;
    loupeRafRef.current = requestAnimationFrame(() => {
      loupeRafRef.current = null;
      const bmp = sourceBitmapRef.current;
      if (!bmp || !activePage) return;
      const sampled = sampleLoupeGrid(bmp, activePage.width, activePage.height, pt.x, pt.y, 6);
      if (!sampled) return;
      setLoupe({
        px: pt.x,
        py: pt.y,
        pctX: (pt.x / Math.max(1, activePage.width)) * 100,
        pctY: (pt.y / Math.max(1, activePage.height)) * 100,
        ...sampled,
      });
    });
  };

  const handleArtboardPointerMove = (event: React.PointerEvent<HTMLDivElement>) => {
    if (!activePage || panDragRef.current) return;
    const pt = clientToPixel(event.clientX, event.clientY);

    if (magnifierEnabled) {
      updateLoupeAtPoint(pt);
    }

    if (tool === 'smart') {
      const hit = activePage.blocks.find(
        (b) => pt.x >= b.dest.x1 && pt.x <= b.dest.x2 && pt.y >= b.dest.y1 && pt.y <= b.dest.y2
      );
      setHoverBlockId(hit ? hit.id : null);
    }

    if (tool === 'brush' && activeStrokeRef.current) {
      setActiveStroke((prev) => {
        if (!prev) return null;
        const last = prev.points[prev.points.length - 1];
        if (last && Math.hypot(pt.x - last.x, pt.y - last.y) < 2) return prev;
        return { ...prev, points: [...prev.points, pt] };
      });
      return;
    }

    if (tool === 'explode' && manualCutStartRef.current) {
      const s = manualCutStartRef.current;
      setManualCutBox({
        x1: Math.min(s.x, pt.x),
        y1: Math.min(s.y, pt.y),
        x2: Math.max(s.x, pt.x),
        y2: Math.max(s.y, pt.y),
      });
    }

    if ((tool === 'box' || tool === 'mosaic') && drawStartRef.current) {
      const s = drawStartRef.current;
      setDraftBox({
        x1: Math.min(s.x, pt.x),
        y1: Math.min(s.y, pt.y),
        x2: Math.max(s.x, pt.x),
        y2: Math.max(s.y, pt.y),
      });
    }
  };

  const handleArtboardPointerUp = (event: React.PointerEvent<HTMLDivElement>) => {
    if (event.currentTarget.hasPointerCapture(event.pointerId)) {
      event.currentTarget.releasePointerCapture(event.pointerId);
    }

    if (tool === 'brush' && activeStrokeRef.current) {
      const stroke = activeStrokeRef.current;
      setActiveStroke(null);
      if (stroke.points.length >= 1) {
        commitPages((prev) =>
          prev.map((p) =>
            p.id !== activePage.id ? p : { ...p, brushStrokes: [...(p.brushStrokes ?? []), stroke] }
          )
        );
      }
      return;
    }

    if (tool === 'box' && drawStartRef.current && draftBox) {
      drawStartRef.current = null;
      const w = draftBox.x2 - draftBox.x1;
      const h = draftBox.y2 - draftBox.y1;
      setDraftBox(null);
      if (w >= 14 && h >= 14) {
        appendAnnotation({ kind: 'region', text: '', rect: clampRect(draftBox, activePage.width, activePage.height) });
      }
    }

    if (tool === 'mosaic' && drawStartRef.current && draftBox) {
      drawStartRef.current = null;
      const w = draftBox.x2 - draftBox.x1;
      const h = draftBox.y2 - draftBox.y1;
      const clickPt = { x: draftBox.x1, y: draftBox.y1 };
      const finalBox = clampRect(draftBox, activePage.width, activePage.height);
      setDraftBox(null);
      if (w >= 10 && h >= 10) {
        const mId = `m-${Date.now()}-${Math.random().toString(36).slice(2, 6)}`;
        const region: MosaicRegion = {
          id: mId,
          rect: finalBox,
          style: mosaicStyle,
          strength: mosaicStrength,
        };
        commitPages((prev) =>
          prev.map((p) =>
            p.id !== activePage.id ? p : { ...p, mosaics: [...(p.mosaics ?? []), region] }
          )
        );
        setSelectedMosaicId(mId);
      } else {
        const hit = [...(activePage.mosaics ?? [])]
          .reverse()
          .find(
            (m) =>
              clickPt.x >= m.rect.x1 &&
              clickPt.x <= m.rect.x2 &&
              clickPt.y >= m.rect.y1 &&
              clickPt.y <= m.rect.y2
          );
        setSelectedMosaicId(hit ? hit.id : null);
      }
    }

    if (tool === 'explode' && manualCutStartRef.current && manualCutBox) {
      manualCutStartRef.current = null;
      const w = manualCutBox.x2 - manualCutBox.x1;
      const h = manualCutBox.y2 - manualCutBox.y1;
      setManualCutBox(null);
      if (w >= 18 && h >= 18) {
        const manualId = `manual-${Date.now()}`;
        const newBlock: ExplodeBlock = {
          id: manualId,
          source: { ...manualCutBox },
          dest: { ...manualCutBox },
          manual: true,
        };
        commitPages((prev) =>
          prev.map((page) =>
            page.id !== activePage.id ? page : { ...page, blocks: [...page.blocks, newBlock] }
          )
        );
        setSelectedBlockId(manualId);
        onToast(t("workspace.regionSplitDragItToMoveIt"));
      }
    }
  };

  const startExplodeSession = (
    event: React.PointerEvent<HTMLElement>,
    block: ExplodeBlock,
    kind: 'move' | 'resize',
    handle?: ResizeHandle
  ) => {
    if (tool === 'pan' || spacePressed) return;
    event.stopPropagation();
    const el = artboardRef.current;
    if (!el || !activePage) return;
    const rect = el.getBoundingClientRect();
    setSelectedBlockId(block.id);
    const linked = activePage.annotations.find((a) => a.blockId === block.id);
    if (linked) setSelectedNoteId(linked.id);

    explodeDragRef.current = {
      kind,
      handle,
      blockId: block.id,
      pageId: activePage.id,
      startClientX: event.clientX,
      startClientY: event.clientY,
      origDest: { ...block.dest },
      source: { ...block.source },
      pageW: activePage.width,
      pageH: activePage.height,
      scaleX: activePage.width / rect.width,
      scaleY: activePage.height / rect.height,
      snapshot: pagesRef.current,
      moved: false,
    };
  };

  const startAnnotationDrag = (event: React.PointerEvent<HTMLElement>, note: AnnotationItem) => {
    if (tool !== 'select' || spacePressed) return;
    event.stopPropagation();
    const el = artboardRef.current;
    if (!el || !activePage) return;
    const rect = el.getBoundingClientRect();
    setSelectedNoteId(note.id);
    setSelectedMosaicId(null);
    setInspectorTab('page');
    if (note.kind === 'region' && note.rect) {
      annDragRef.current = {
        kind: 'region',
        noteId: note.id,
        pageId: activePage.id,
        startClientX: event.clientX,
        startClientY: event.clientY,
        origRect: { ...note.rect },
        pageW: activePage.width,
        pageH: activePage.height,
        scaleX: activePage.width / rect.width,
        scaleY: activePage.height / rect.height,
        snapshot: pagesRef.current,
        moved: false,
      };
    } else if (note.kind === 'point' && note.point) {
      annDragRef.current = {
        kind: 'point',
        noteId: note.id,
        pageId: activePage.id,
        startClientX: event.clientX,
        startClientY: event.clientY,
        origPoint: { ...note.point },
        pageW: activePage.width,
        pageH: activePage.height,
        scaleX: activePage.width / rect.width,
        scaleY: activePage.height / rect.height,
        snapshot: pagesRef.current,
        moved: false,
      };
    }
  };

  const startCropSession = (event: React.PointerEvent<HTMLElement>, kind: 'move' | 'resize', handle?: ResizeHandle) => {
    if (spacePressed) return;
    event.stopPropagation();
    const el = artboardRef.current;
    if (!el || !activePage || !cropRect) return;
    const rect = el.getBoundingClientRect();
    cropDragRef.current = {
      kind,
      handle,
      startClientX: event.clientX,
      startClientY: event.clientY,
      origRect: { ...cropRect },
      pageW: activePage.width,
      pageH: activePage.height,
      scaleX: activePage.width / rect.width,
      scaleY: activePage.height / rect.height,
    };
  };

  const resetBlockMovement = (blockId: string) => {
    commitPages((prev) =>
      prev.map((page) =>
        page.id !== activePage.id
          ? page
          : {
              ...page,
              blocks: page.blocks.map((b) => (b.id === blockId ? { ...b, dest: { ...b.source } } : b)),
              annotations: page.annotations.filter((a) => a.blockId !== blockId),
            }
      )
    );
    onToast(t("workspace.componentPositionReset"));
  };

  const resetAllExplodeBlocks = () => {
    commitPages((prev) =>
      prev.map((page) =>
        page.id !== activePage.id
          ? page
          : {
              ...page,
              blocks: page.blocks.map((b) => ({ ...b, dest: { ...b.source } })),
              annotations: page.annotations.filter((a) => !a.blockId),
            }
      )
    );
    onToast(t("workspace.allComponentsOnThisPageReset"));
  };

  const deleteNote = (note: AnnotationItem) => {
    if (inlineEditNoteId === note.id) setInlineEditNoteId(null);
    if (hoveredNoteId === note.id) setHoveredNoteId(null);
    commitPages((prev) =>
      prev.map((page) =>
        page.id !== activePage.id
          ? page
          : {
              ...page,
              blocks: note.blockId
                ? page.blocks.map((b) => (b.id === note.blockId ? { ...b, dest: { ...b.source } } : b))
                : page.blocks,
              annotations: page.annotations.filter((a) => a.id !== note.id),
            }
      )
    );
  };

  const updateNoteText = (noteId: string, val: string) => {
    onUpdatePages((prev) =>
      prev.map((p) =>
        p.id !== activePage.id
          ? p
          : {
              ...p,
              annotations: p.annotations.map((a) => (a.id === noteId ? { ...a, text: val } : a)),
            }
      )
    );
  };

  const addGlobalGroupNote = () => {
    const id = `g-${Date.now()}`;
    const nextNum = groupNotes.reduce((m, g) => Math.max(m, g.number), 0) + 1;
    onUpdateGroupNotes((prev) => [...prev, { id, number: nextNum, text: '' }]);
    setInspectorTab('group');
    setEditingNoteId(id);
  };

  const toggleExplodeMode = () => {
    const next = tool === 'explode' ? 'box' : 'explode';
    setTool(next);
    setHoverBlockId(null);
    clearTimeout(explosionWaveTimerRef.current);
    if (next === 'explode') {
      setExplosionWaveKey((k) => k + 1);
      setShowExplosionWave(true);
      explosionWaveTimerRef.current = setTimeout(() => setShowExplosionWave(false), 950);
      onToast(t("workspace.explodedViewEnabled"));
    } else {
      setShowExplosionWave(false);
      setSelectedBlockId(null);
    }
  };

  const buildRenderedPngForActivePage = async (): Promise<{ dataUrl: string; pngBase64: string } | null> => {
    if (!activePage) return null;
    const bmp = sourceBitmapRef.current ?? (await loadBitmapForPage(activePage, artboardMediaRef.current));
    if (!bmp) return null;
    return composePageToPng(activePage, bmp, !hideAnnotations);
  };

  const handleCopyAnnotatedImage = async () => {
    const rendered = await buildRenderedPngForActivePage();
    if (!rendered || !rendered.pngBase64) {
      onToast(t("workspace.thisCanvasImageCouldnTBeEncoded"));
      return;
    }
    if (isDesktopWebView()) {
      try {
        await desktopRpc('clipboard.copyImage', { pngBase64: rendered.pngBase64 });
        onToast(t("workspace.annotatedImageCopied"));
      } catch (err) {
        onToast(err instanceof Error ? err.message : t("workspace.couldNotCopyTheImage"));
      }
      return;
    }
    try {
      const res = await fetch(rendered.dataUrl);
      const blob = await res.blob();
      await navigator.clipboard.write([new ClipboardItem({ 'image/png': blob })]);
      onToast(t("workspace.compositedImageCopiedToTheClipboard"));
    } catch {
      const a = document.createElement('a');
      a.href = rendered.dataUrl;
      a.download = `${activePage.name || 'edithere'}-annotated.png`;
      a.click();
      onToast(t("workspace.compositedPngDownloaded"));
    }
  };

  const handleSaveAnnotatedImage = async () => {
    const rendered = await buildRenderedPngForActivePage();
    if (!rendered || !rendered.pngBase64) {
      onToast(t("workspace.noImageToSave"));
      return;
    }
    const defaultName = `${activePage.name || 'edithere'}-annotated.png`;
    if (isDesktopWebView()) {
      try {
        const res = await desktopRpc<{ saved?: boolean }>(
          'file.saveImage',
          { defaultName, pngBase64: rendered.pngBase64 },
          0
        );
        if (res.saved) onToast(t("workspace.annotatedPngSaved"));
      } catch (err) {
        onToast(err instanceof Error ? err.message : t("workspace.couldNotSaveTheImage"));
      }
      return;
    }
    const a = document.createElement('a');
    a.href = rendered.dataUrl;
    a.download = defaultName;
    a.click();
    onToast(t("workspace.compositedPngExported"));
  };

  const handlePinCurrentPage = async () => {
    if (!isDesktopWebView()) {
      onToast(t("workspace.pinningImagesIsAvailableInTheWindows"));
      return;
    }
    const rendered = await buildRenderedPngForActivePage();
    try {
      await desktopRpc('pin.page', {
        pageId: activePage.id,
        pngBase64: rendered?.pngBase64 ?? '',
      });
      onToast(t("workspace.imagePinnedInAFloatingWindow"));
    } catch (err) {
      onToast(err instanceof Error ? err.message : t("workspace.couldNotPinThisPage"));
    }
  };

  const handleSaveProject = async () => {
    const defaultName = `${activePage?.name || 'edithere-project'}.edithere`;
    if (isDesktopWebView()) {
      try {
        const payload = await prepareWorkspaceSnapshot(artboardMediaRef.current);
        const res = await desktopRpc<{ saved?: boolean }>(
          'file.saveProject',
          { defaultName, payload },
          0
        );
        if (res.saved) onToast(t("workspace.edithereProjectSaved"));
      } catch (err) {
        onToast(err instanceof Error ? err.message : t("workspace.couldNotSaveTheProject"));
      }
      return;
    }
    const json = JSON.stringify(buildFeedbackPayload(pages, groupNotes), null, 2);
    const url = URL.createObjectURL(new Blob([json], { type: 'application/json' }));
    const a = document.createElement('a');
    a.href = url;
    a.download = `${activePage?.name || 'edithere-project'}.json`;
    a.click();
    URL.revokeObjectURL(url);
    onToast(t("workspace.projectJsonExported"));
  };

  const liveJson = JSON.stringify(buildFeedbackPayload(pages, groupNotes), null, 2);

  const copyJson = async () => {
    try {
      if (isDesktopWebView()) {
        const payload = await prepareWorkspaceSnapshot(artboardMediaRef.current);
        await desktopRpc('clipboard.copyJsonText', { payload });
      } else {
        await navigator.clipboard.writeText(liveJson);
      }
      onToast(t("workspace.feedbackJsonCopied"));
    } catch (err) {
      onToast(isDesktopWebView()
        ? (err instanceof Error ? err.message : t("workspace.copyJsonTextFailed"))
        : t("workspace.clipboardAccessWasBlockedUseExportTo"));
    }
  };

  const handleCopyFeedbackBundle = async () => {
    if (!isDesktopWebView()) {
      await copyJson();
      return;
    }
    try {
      const redactedPayload = await attachRedactedPagesToPayload(pages, groupNotes, artboardMediaRef.current);
      const res = await desktopRpc<{ files?: number }>(
        'clipboard.copyFeedbackBundle',
        { payload: redactedPayload }
      );
      onToast(t("workspace.copiedJsonImageItems", { count: res.files ?? 2 }));
    } catch (err) {
      onToast(err instanceof Error ? err.message : t("workspace.couldNotCopyTheJsonBundle"));
    }
  };

  const downloadJson = async () => {
    if (isDesktopWebView()) {
      try {
        const payload = await prepareWorkspaceSnapshot(artboardMediaRef.current);
        const res = await desktopRpc<{ saved?: boolean }>(
          'file.saveText',
          { payload, defaultName: 'feedback.json' },
          0
        );
        if (res.saved) onToast(t("workspace.feedbackJsonExported"));
      } catch (err) {
        onToast(err instanceof Error ? err.message : t("workspace.exportFailed"));
      }
      return;
    }
    const url = URL.createObjectURL(new Blob([liveJson], { type: 'application/json' }));
    const a = document.createElement('a');
    a.href = url;
    a.download = 'edithere-gallery-feedback.json';
    a.click();
    URL.revokeObjectURL(url);
    onToast(t("workspace.feedbackJsonDownloaded"));
  };

  const shortcutActions: Record<(typeof WORKSPACE_SHORTCUT_IDS)[number], () => void> = {
    open: onOpenFiles,
    paste: onPasteImage,
    save: () => void handleSaveProject(),
    export: () => setJsonOpen(true),
    copy: () => void handleCopyAnnotatedImage(),
    copyJson: () => void handleCopyFeedbackBundle(),
    saveImage: () => void handleSaveAnnotatedImage(),
    hideAnnotations: () => setHideAnnotations((hidden) => !hidden),
    addGlobalNote: addGlobalGroupNote,
    undo: handleUndo,
    redo: handleRedo,
    fit: resetCanvasView,
    delete: () => {
      if (selectedMosaicId) {
        commitPages((prev) => prev.map((page) => page.id === activePage.id
          ? { ...page, mosaics: (page.mosaics ?? []).filter((mosaic) => mosaic.id !== selectedMosaicId) }
          : page));
        setSelectedMosaicId(null);
        return;
      }
      const note = activePage.annotations.find((item) => item.id === selectedNoteId);
      if (note) deleteNote(note);
    },
    close: () => {
      if (jsonOpen) {
        setJsonOpen(false);
      } else if (inlineEditNoteId || hoveredNoteId || editingNoteId) {
        setInlineEditNoteId(null);
        setHoveredNoteId(null);
        setEditingNoteId(null);
      } else if (tool === 'crop') {
        setCropRect(null);
        setTool('box');
      } else if (tool === 'explode') {
        if (selectedBlockId || manualCutBox) {
          setSelectedBlockId(null);
          setManualCutBox(null);
          manualCutStartRef.current = null;
        } else {
          toggleExplodeMode();
        }
      } else {
        onClose();
      }
    },
    smart: () => setTool('smart'),
    point: () => setTool('point'),
    rectangle: () => setTool('box'),
    adjust: () => setTool('select'),
    explode: toggleExplodeMode,
    component: () => {
      if (tool === 'explode') {
        setSelectedBlockId(hoverBlockId ?? selectedBlockId ?? activePage.blocks[0]?.id ?? null);
      }
    },
  };

  const shortcutActionsRef = useRef(shortcutActions);
  shortcutActionsRef.current = shortcutActions;
  const shortcutDialogOpenRef = useRef(jsonOpen);
  shortcutDialogOpenRef.current = jsonOpen;
  const galleryDialogOpenRef = useRef(false);
  galleryDialogOpenRef.current = addPageOpen || removePageTarget !== undefined;
  const handleCanvasKeyRef = useRef<(event: KeyboardEvent) => void>(() => {});
  handleCanvasKeyRef.current = (event) => {
    if (jsonOpen) return;
    onViewShortcut(event);
    if (event.defaultPrevented) return;
    if (event.code === 'Space' && !event.ctrlKey && !event.altKey && !event.metaKey && !event.shiftKey) {
      event.preventDefault();
      setSpacePressed(true);
    } else if ((event.ctrlKey || event.metaKey) && ['+', '=', '-'].includes(event.key)) {
      event.preventDefault();
      stepZoom(event.key === '-' ? 1 / ZOOM_STEP : ZOOM_STEP);
    } else if (event.key === 'Enter' && tool === 'crop' && cropRect) {
      event.preventDefault();
      void confirmCrop();
    }
  };

  useEffect(() => {
    const matchShortcut = createShortcutMatcher(shortcuts, WORKSPACE_SHORTCUT_IDS);
    const matchDialogShortcut = createShortcutMatcher(shortcuts, ['close']);
    const onKeyDown = (event: KeyboardEvent) => {
      if (galleryDialogOpenRef.current || !canHandleShortcut(event) || event.repeat) return;
      if ((event.key === 'Enter' || event.code === 'Space') &&
          event.target instanceof Element && event.target.closest('.gallery-strip button')) return;
      const action = shortcutDialogOpenRef.current ? matchDialogShortcut(event) : matchShortcut(event);
      if (action) shortcutActionsRef.current[action]();
      else if (!event.defaultPrevented) handleCanvasKeyRef.current(event);
    };
    const onKeyUp = (event: KeyboardEvent) => {
      if (event.code === 'Space') setSpacePressed(false);
    };
    const onBlur = () => {
      setSpacePressed(false);
      panDragRef.current = null;
      setIsPanning(false);
    };
    window.addEventListener('keydown', onKeyDown, true);
    window.addEventListener('keyup', onKeyUp);
    window.addEventListener('blur', onBlur);
    return () => {
      window.removeEventListener('keydown', onKeyDown, true);
      window.removeEventListener('keyup', onKeyUp);
      window.removeEventListener('blur', onBlur);
    };
  }, [shortcuts]);

  const pctX = (px: number) => `${(px / activePage.width) * 100}%`;
  const pctY = (px: number) => `${(px / activePage.height) * 100}%`;
  const rectStyle = (r: PixelRect) => ({
    left: pctX(r.x1),
    top: pctY(r.y1),
    width: pctX(r.x2 - r.x1),
    height: pctY(r.y2 - r.y1),
  });

  const renderInlinePopover = (note: AnnotationItem, anchorX: number) => {
    const placeLeft = anchorX > activePage.width * 0.58;
    const isEditing = inlineEditNoteId === note.id;

    return (
      <div
        className={`inline-note-popover ${placeLeft ? 'place-left' : ''}`}
        onPointerDown={(e) => e.stopPropagation()}
        onClick={(e) => e.stopPropagation()}
        onPointerEnter={() => scheduleHoverOpen(note.id)}
        onPointerLeave={() => {
          if (!isEditing) scheduleHoverClose(note.id);
        }}
      >
        <div className="inline-note-head">
          <span className={`note-num ${note.movement ? 'explode' : ''}`} style={{ width: 18, height: 18, fontSize: 10 }}>
            {note.number}
          </span>
          <span className="badge neutral">
            {note.movement
              ? t("workspace.offset", { xSign: note.movement.dx >= 0 ? '+' : '', x: note.movement.dx, ySign: note.movement.dy >= 0 ? '+' : '', y: note.movement.dy })
              : note.kind === 'point'
                ? t("workspace.point")
                : t("settings.boxAnnotation")}
          </span>
          <div className="inline-note-actions">
            <button
              className="icon-button"
              title={t("workspace.editAnnotation")}
              onMouseDown={(e) => e.preventDefault()}
              onClick={() => setInlineEditNoteId(isEditing ? null : note.id)}
            >
              <EditIcon size={12} />
            </button>
            {note.blockId && (
              <button
                className="icon-button"
                title={t("workspace.resetComponentPosition")}
                onMouseDown={(e) => e.preventDefault()}
                onClick={() => resetBlockMovement(note.blockId!)}
              >
                <ResetIcon size={12} />
              </button>
            )}
            <button
              className="icon-button"
              title={t("workspace.deleteAnnotation")}
              onMouseDown={(e) => e.preventDefault()}
              onClick={() => deleteNote(note)}
            >
              <DeleteIcon size={12} />
            </button>
          </div>
        </div>

        {isEditing ? (
          <textarea
            autoFocus
            className="note-textarea"
            style={{ minHeight: 58 }}
            placeholder={t("workspace.enterANotePressEnterOrClick")}
            value={note.text}
            onChange={(e) => updateNoteText(note.id, e.target.value)}
            onKeyDown={(e) => {
              if (e.key === 'Enter' && !e.shiftKey) {
                e.preventDefault();
                setInlineEditNoteId(null);
              } else if (e.key === 'Escape') {
                e.stopPropagation();
                setInlineEditNoteId(null);
              }
            }}
            onBlur={() => setInlineEditNoteId(null)}
          />
        ) : (
          <button
            className={`inline-note-body ${!note.text ? 'placeholder' : ''}`}
            onClick={() => setInlineEditNoteId(note.id)}
          >
            {note.text || t("workspace.clickToEnterFeedback")}
          </button>
        )}

      </div>
    );
  };

  const hoveredBlock = activePage?.blocks.find((b) => b.id === hoverBlockId) ?? null;
  const pageMosaics = activePage?.mosaics ?? [];
  const pageStrokes = activePage?.brushStrokes ?? [];
  const hasTransformedBlocks = Boolean(activePage?.blocks.some((b) => isBlockTransformed(b)));
  const canUndo = undoStackRef.current.length > 0;
  const canRedo = redoStackRef.current.length > 0;
  const canPanActive = tool === 'pan' || spacePressed;
  const isDrawingTool =
    !canPanActive && (tool === 'box' || tool === 'point' || tool === 'mosaic' || tool === 'brush');
  const isVideoPage = Boolean(activePage?.isVideo);

  return (
    <div className={`workspace-root ${!inspectorOpen ? 'inspector-collapsed' : ''}`}>
      {/* Main Canvas Architecture */}
      <section className="canvas-surface">
        <div className="canvas-column" ref={canvasColRef}>
          <div className="canvas-top-hud">
            <div className="canvas-meta-chip">
              <span className="canvas-meta-dot" />
              <strong>{activePage.name}</strong>
              <code>{activePage.width} × {activePage.height}</code>
              <span>·</span>
              <span>{activePage.routeHint}</span>
            </div>

            <div className="canvas-top-right-hud">
              <div className="canvas-zoom-pill">
                <button
                  className="icon-button"
                  disabled={!canUndo}
                  onClick={handleUndo}
                  aria-label={t("workspace.undo")}
                  {...tipProps(shortcutLabel(t("workspace.undoTooltip"), 'undo'), 'below')}
                >
                  <UndoIcon size={14} />
                </button>
                <button
                  className="icon-button"
                  disabled={!canRedo}
                  onClick={handleRedo}
                  aria-label={t("workspace.redo")}
                  {...tipProps(shortcutLabel(t("workspace.redoTooltip"), 'redo'), 'below')}
                >
                  <RedoIcon size={14} />
                </button>
                <button
                  className={`icon-button ${hideAnnotations ? 'is-active' : ''}`}
                  onClick={() => setHideAnnotations((h) => !h)}
                  aria-label={hideAnnotations ? t("workspace.showAnnotations") : t("workspace.hideAnnotations")}
                  {...tipProps(shortcutLabel(hideAnnotations ? t("workspace.showAnnotationMarkers") : t("workspace.hideAnnotationMarkers"), 'hideAnnotations'), 'below')}
                >
                  {hideAnnotations ? <HideIcon size={14} /> : <ShowIcon size={14} />}
                </button>
              </div>

              <div className="canvas-zoom-pill">
                <button
                  className="icon-button"
                  aria-label={t("workspace.zoomOut")}
                  onClick={() => stepZoom(1 / ZOOM_STEP)}
                  {...tipProps(t("workspace.zoomOut"), 'below')}
                >
                  <MinusIcon size={14} />
                </button>
                <span>{Math.round(zoom)}%</span>
                <button
                  className="icon-button"
                  aria-label={t("workspace.zoomIn")}
                  onClick={() => stepZoom(ZOOM_STEP)}
                  {...tipProps(t("workspace.zoomIn"), 'below')}
                >
                  <PlusIcon size={14} />
                </button>
                <button
                  className="button ghost sm"
                  onClick={resetCanvasView}
                  {...tipProps(shortcutLabel(t("workspace.fitAndCenterCanvas"), 'fit'), 'below')}
                >{t("workspace.fit")}</button>
              </div>

              <div className="canvas-zoom-pill">
                <button
                  className={`inspector-toggle-btn ${inspectorOpen ? 'is-open' : ''}`}
                  aria-label={inspectorOpen ? t("workspace.collapseAnnotationPanel") : t("workspace.expandAnnotationPanel")}
                  onClick={() => {
                    setInspectorOpen((o) => !o);
                    setInlineEditNoteId(null);
                  }}
                  {...tipProps(inspectorOpen ? t("workspace.collapseAnnotationPanel") : t("workspace.expandAnnotationPanel"), 'below')}
                >
                  {inspectorOpen ? <InspectorCloseIcon size={14} /> : <InspectorOpenIcon size={14} />}
                  <span>{t("workspace.annotationPanel")}</span>
                  <span className="inspector-toggle-count">{activePage.annotations.length}</span>
                </button>
              </div>
            </div>
          </div>

          <div
            ref={stageRef}
            className={`canvas-stage-wrap ${canPanActive ? 'can-pan' : ''} ${isPanning ? 'is-panning' : ''}`}
            onPointerDownCapture={handleStagePointerDownCapture}
            onPointerDown={handleStagePointerDown}
            onMouseDownCapture={preventMiddleClickDefault}
            onAuxClickCapture={preventMiddleClickDefault}
          >
            <div className="canvas-stage-inner">
              <div
                ref={artboardRef}
                className={`artboard ${isPanning ? 'is-panning' : ''} ${
                  isDrawingTool ? 'cursor-crosshair drawing-tool-active' : ''
                }`}
                style={{
                  aspectRatio: `${activePage.width} / ${activePage.height}`,
                  width: `min(100cqw, calc(100cqh * ${activePage.width} / ${activePage.height}))`,
                  transform: `translate(${panOffset.x}px, ${panOffset.y}px) scale(${zoom / 100})`,
                }}
                onPointerDown={handleArtboardPointerDown}
                onPointerMove={handleArtboardPointerMove}
                onPointerUp={handleArtboardPointerUp}
                onPointerLeave={() => {
                  setHoverBlockId(null);
                  setLoupe(null);
                }}
              >
                <div className="artboard-media" ref={artboardMediaRef}>
                  <SceneArtwork sceneId={activePage.sceneId} imageUrl={activePage.imageUrl} />
                </div>

                {/* Live Mosaic Redaction Layer */}
                <canvas
                  ref={mosaicCanvasRef}
                  className="mosaic-canvas"
                  width={activePage.width}
                  height={activePage.height}
                />

                {/* Freehand Vector Brush Layer */}
                {(pageStrokes.length > 0 || activeStroke) && (
                  <svg
                    className="brush-svg-layer"
                    viewBox={`0 0 ${activePage.width} ${activePage.height}`}
                    preserveAspectRatio="none"
                    aria-hidden="true"
                  >
                    {pageStrokes.map((s) => (
                      <path
                        key={s.id}
                        d={strokeToSvgPath(s.points)}
                        fill="none"
                        stroke={s.color}
                        strokeWidth={s.width}
                        strokeOpacity={s.opacity ?? 1}
                        strokeLinecap="round"
                        strokeLinejoin="round"
                        vectorEffect="non-scaling-stroke"
                      />
                    ))}
                    {activeStroke && (
                      <path
                        d={strokeToSvgPath(activeStroke.points)}
                        fill="none"
                        stroke={activeStroke.color}
                        strokeWidth={activeStroke.width}
                        strokeOpacity={activeStroke.opacity ?? 1}
                        strokeLinecap="round"
                        strokeLinejoin="round"
                        vectorEffect="non-scaling-stroke"
                      />
                    )}
                  </svg>
                )}

                {/* Selectable Mosaic Outlines in Mosaic or Select tool */}
                {(tool === 'mosaic' || tool === 'select') &&
                  pageMosaics.map((m) => (
                    <div
                      key={m.id}
                      className={`mosaic-region-outline ${selectedMosaicId === m.id ? 'selected' : ''}`}
                      style={rectStyle(m.rect)}
                      onPointerDown={(e) => {
                        if (spacePressed) return;
                        e.stopPropagation();
                        setSelectedMosaicId(m.id);
                      }}
                    />
                  ))}

                {/* Brief Entry Rainbow Ripple Only (Explode) */}
                {tool === 'explode' && showExplosionWave && explosionWaveKey > 0 && (
                  <div key={explosionWaveKey} className="explosion-wave" aria-hidden="true" />
                )}

                {/* Smart hover highlight */}
                {!hideAnnotations && tool === 'smart' && hoveredBlock && (
                  <div className="smart-block-hover" style={rectStyle(hoveredBlock.dest)}>
                    <span className="smart-block-size">
                      {hoveredBlock.dest.x2 - hoveredBlock.dest.x1} × {hoveredBlock.dest.y2 - hoveredBlock.dest.y1}
                    </span>
                  </div>
                )}

                {/* Authentic Visual-Cutout Explode Layer */}
                {tool === 'explode' && (
                  <>
                    {activePage.blocks.map((block) =>
                      isBlockTransformed(block) ? (
                        <div
                          key={`cavity-${block.id}`}
                          className="explode-cavity"
                          style={rectStyle(block.source)}
                        />
                      ) : null
                    )}

                    {activePage.blocks.map((block) => {
                      if (!isBlockTransformed(block)) return null;
                      const sw = Math.max(1, block.source.x2 - block.source.x1);
                      const sh = Math.max(1, block.source.y2 - block.source.y1);
                      const isSelected = selectedBlockId === block.id;
                      return (
                        <div
                          key={`slice-${block.id}`}
                          className={`explode-piece ${isSelected ? 'is-selected' : ''}`}
                          style={rectStyle(block.dest)}
                        >
                          <div
                            className="explode-piece-inner"
                            style={{
                              width: `${(activePage.width / sw) * 100}%`,
                              height: `${(activePage.height / sh) * 100}%`,
                              transform: `translate(${-(block.source.x1 / activePage.width) * 100}%, ${-(block.source.y1 / activePage.height) * 100}%)`,
                            }}
                          >
                            <SceneArtwork
                              sceneId={activePage.sceneId}
                              imageUrl={activePage.imageUrl}
                              stretch
                            />
                          </div>
                        </div>
                      );
                    })}

                    {activePage.blocks.map((block) => {
                      const transformed = isBlockTransformed(block);
                      const isSelected = selectedBlockId === block.id;
                      const isHovered = hoverBlockId === block.id;
                      const linkedNote = activePage.annotations.find((a) => a.blockId === block.id);
                      const showPopover = Boolean(
                        !hideAnnotations &&
                          !inspectorOpen &&
                          linkedNote &&
                          (hoveredNoteId === linkedNote.id || inlineEditNoteId === linkedNote.id)
                      );
                      const dx = block.dest.x1 - block.source.x1;
                      const dy = block.dest.y1 - block.source.y1;
                      void dx;
                      void dy;

                      return (
                        <div
                          key={`guide-${block.id}`}
                          className={`explode-block ${isSelected ? 'selected' : ''} ${isHovered ? 'hovered' : ''} ${transformed ? 'moved' : ''} ${showPopover ? 'has-popover' : ''}`}
                          style={rectStyle(block.dest)}
                          onPointerEnter={() => setHoverBlockId(block.id)}
                          onPointerLeave={() =>
                            setHoverBlockId((cur) => (cur === block.id ? null : cur))
                          }
                          onPointerDown={(e) => startExplodeSession(e, block, 'move')}
                          onDoubleClick={(e) => {
                            e.stopPropagation();
                            if (transformed) resetBlockMovement(block.id);
                          }}
                        >
                          {!hideAnnotations && linkedNote && (
                            <span
                              className={`corner-badge explode ${!inspectorOpen ? 'interactive' : ''}`}
                              onPointerEnter={() => {
                                if (!inspectorOpen) scheduleHoverOpen(linkedNote.id);
                              }}
                              onPointerLeave={() => {
                                if (!inspectorOpen && inlineEditNoteId !== linkedNote.id) {
                                  scheduleHoverClose(linkedNote.id);
                                }
                              }}
                              onPointerDown={(e) => {
                                if (!inspectorOpen) e.stopPropagation();
                              }}
                              onClick={(e) => {
                                if (!inspectorOpen) {
                                  e.stopPropagation();
                                  setSelectedNoteId(linkedNote.id);
                                  scheduleHoverOpen(linkedNote.id);
                                  setInlineEditNoteId(linkedNote.id);
                                }
                              }}
                            >
                              {linkedNote.number}
                            </span>
                          )}

                          {!hideAnnotations &&
                            !inspectorOpen &&
                            linkedNote &&
                            (hoveredNoteId === linkedNote.id || inlineEditNoteId === linkedNote.id) &&
                            renderInlinePopover(linkedNote, block.dest.x1)}

                          {isSelected &&
                            RESIZE_HANDLES.map((h) => (
                              <div
                                key={h.id}
                                className={`explode-handle handle-${h.id}`}
                                style={{ cursor: h.cursor }}
                                onPointerDown={(e) => startExplodeSession(e, block, 'resize', h.id)}
                              />
                            ))}
                        </div>
                      );
                    })}

                    {!hideAnnotations && (
                      <svg
                        className="explode-trajectories"
                        viewBox={`0 0 ${activePage.width} ${activePage.height}`}
                        preserveAspectRatio="none"
                        aria-hidden="true"
                      >
                        {activePage.blocks.map((block) => {
                          if (!isBlockTransformed(block)) return null;
                          const x1 = (block.source.x1 + block.source.x2) / 2;
                          const y1 = (block.source.y1 + block.source.y2) / 2;
                          const x2 = (block.dest.x1 + block.dest.x2) / 2;
                          const y2 = (block.dest.y1 + block.dest.y2) / 2;
                          const len = Math.hypot(x2 - x1, y2 - y1);
                          if (len < 6) return null;
                          const ux = (x2 - x1) / len;
                          const uy = (y2 - y1) / len;
                          const nx = -uy;
                          const ny = ux;
                          const head = Math.min(11, Math.max(6, len * 0.25));
                          const p1x = x2 - ux * head + nx * head * 0.45;
                          const p1y = y2 - uy * head + ny * head * 0.45;
                          const p2x = x2 - ux * head - nx * head * 0.45;
                          const p2y = y2 - uy * head - ny * head * 0.45;
                          const active = selectedBlockId === block.id;

                          return (
                            <g key={`traj-${block.id}`} opacity={active ? 0.96 : 0.72}>
                              <line
                                x1={x1}
                                y1={y1}
                                x2={x2}
                                y2={y2}
                                stroke="rgba(255,255,255,0.85)"
                                strokeWidth={active ? 4 : 3.2}
                                strokeLinecap="round"
                                vectorEffect="non-scaling-stroke"
                              />
                              <line
                                x1={x1}
                                y1={y1}
                                x2={x2}
                                y2={y2}
                                stroke="var(--accent)"
                                strokeWidth={active ? 2 : 1.5}
                                strokeLinecap="round"
                                vectorEffect="non-scaling-stroke"
                              />
                              <polyline
                                points={`${p1x},${p1y} ${x2},${y2} ${p2x},${p2y}`}
                                fill="none"
                                stroke="var(--accent)"
                                strokeWidth={active ? 2 : 1.5}
                                strokeLinecap="round"
                                strokeLinejoin="round"
                                vectorEffect="non-scaling-stroke"
                              />
                              <circle
                                cx={x1}
                                cy={y1}
                                r={Math.max(2.5, activePage.width / 240)}
                                fill="#ffffff"
                                stroke="var(--accent)"
                                strokeWidth={1.5}
                                vectorEffect="non-scaling-stroke"
                              />
                            </g>
                          );
                        })}
                      </svg>
                    )}

                    {manualCutBox && <div className="box-draft-rect" style={rectStyle(manualCutBox)} />}
                  </>
                )}

                {/* Rendered Box & Point Annotations + Inline On-Frame Hover/Edit Bubbles */}
                {!hideAnnotations &&
                  activePage.annotations.map((note) => {
                    const showPopover =
                      !inspectorOpen && (hoveredNoteId === note.id || inlineEditNoteId === note.id);

                    if (note.kind === 'point' && note.point) {
                      return (
                        <div
                          key={note.id}
                          className={`inline-note-anchor ${showPopover ? 'has-popover' : ''}`}
                          style={{ left: pctX(note.point.x), top: pctY(note.point.y) }}
                        >
                          <button
                            className={`point-pin ${selectedNoteId === note.id ? 'selected' : ''} ${tool === 'select' ? 'draggable' : ''}`}
                            style={{ left: 0, top: 0 }}
                            onPointerEnter={() => {
                              if (!inspectorOpen) scheduleHoverOpen(note.id);
                            }}
                            onPointerLeave={() => {
                              if (!inspectorOpen && inlineEditNoteId !== note.id) {
                                scheduleHoverClose(note.id);
                              }
                            }}
                            onPointerDown={(e) => startAnnotationDrag(e, note)}
                            onClick={(e) => {
                              e.stopPropagation();
                              setSelectedNoteId(note.id);
                              setInspectorTab('page');
                              if (!inspectorOpen) {
                                scheduleHoverOpen(note.id);
                                setInlineEditNoteId(note.id);
                              }
                            }}
                          >
                            {note.number}
                          </button>
                          {showPopover && renderInlinePopover(note, note.point.x)}
                        </div>
                      );
                    }
                    if (note.kind === 'region' && note.rect) {
                      if (tool === 'explode' && note.blockId) return null;
                      return (
                        <div
                          key={note.id}
                          className={`box-annotation-rect ${selectedNoteId === note.id ? 'selected' : ''} ${tool === 'select' ? 'draggable' : ''} ${showPopover ? 'has-popover' : ''}`}
                          style={rectStyle(note.rect)}
                          onPointerDown={(e) => startAnnotationDrag(e, note)}
                          onClick={(e) => {
                            e.stopPropagation();
                            setSelectedNoteId(note.id);
                            setInspectorTab('page');
                          }}
                        >
                          <span
                            className={`corner-badge ${!inspectorOpen ? 'interactive' : ''}`}
                            onPointerEnter={() => {
                              if (!inspectorOpen) scheduleHoverOpen(note.id);
                            }}
                            onPointerLeave={() => {
                              if (!inspectorOpen && inlineEditNoteId !== note.id) {
                                scheduleHoverClose(note.id);
                              }
                            }}
                            onPointerDown={(e) => {
                              if (!inspectorOpen) e.stopPropagation();
                            }}
                            onClick={(e) => {
                              e.stopPropagation();
                              setSelectedNoteId(note.id);
                              if (!inspectorOpen) {
                                scheduleHoverOpen(note.id);
                                setInlineEditNoteId(note.id);
                              }
                            }}
                          >
                            {note.number}
                          </span>
                          {showPopover && renderInlinePopover(note, note.rect.x1)}
                        </div>
                      );
                    }
                    return null;
                  })}

                {/* Interactive Crop Overlay */}
                {tool === 'crop' && cropRect && (
                  <div className="crop-overlay-layer" onPointerDown={(e) => e.stopPropagation()}>
                    <div
                      className="crop-window"
                      style={rectStyle(cropRect)}
                      onPointerDown={(e) => startCropSession(e, 'move')}
                    >
                      <div className="crop-thirds-grid" />
                      <span className="crop-size-tag">
                        {cropRect.x2 - cropRect.x1} × {cropRect.y2 - cropRect.y1} px
                      </span>
                      {RESIZE_HANDLES.map((h) => (
                        <div
                          key={h.id}
                          className={`explode-handle handle-${h.id}`}
                          style={{ cursor: h.cursor }}
                          onPointerDown={(e) => startCropSession(e, 'resize', h.id)}
                        />
                      ))}
                    </div>
                  </div>
                )}

                {/* Live Rubber-Band Box / Mosaic Draft */}
                {draftBox && <div className="box-draft-rect" style={rectStyle(draftBox)} />}

                {/* Pixel Loupe Overlay */}
                {magnifierEnabled && loupe && tool !== 'explode' && (
                  <div
                    className="pixel-loupe"
                    style={{
                      left: loupe.pctX > 66 ? `calc(${loupe.pctX}% - 144px)` : `calc(${loupe.pctX}% + 16px)`,
                      top: loupe.pctY > 64 ? `calc(${loupe.pctY}% - 160px)` : `calc(${loupe.pctY}% + 16px)`,
                    }}
                  >
                    <img className="pixel-loupe-img" src={loupe.dataUrl} alt={t("workspace.pixelMagnifier")} />
                    <div className="pixel-loupe-meta">
                      <span>({loupe.px}, {loupe.py})</span>
                      <span style={{ display: 'inline-flex', alignItems: 'center', gap: 4 }}>
                        <span className="pixel-loupe-swatch" style={{ background: loupe.hex }} />
                        {loupe.hex}
                      </span>
                    </div>
                  </div>
                )}
              </div>
            </div>
          </div>

          {/* Two-Tier Contextual Sub-Bars (anchored above source tool button) */}
          {!videoMode && tool === 'brush' && (
            <div
              ref={subbarRef}
              className="dock-subbar"
              role="region"
              aria-label={t("workspace.brushOptions")}
              style={subbarLeft !== null ? { left: subbarLeft, transform: 'none' } : undefined}
            >
              <div className="segmented-control">
                <button
                  className={brushMode === 'pen' ? 'selected' : ''}
                  onClick={() => setBrushMode('pen')}
                >{t("workspace.brush")}</button>
                <button
                  className={brushMode === 'highlighter' ? 'selected' : ''}
                  onClick={() => setBrushMode('highlighter')}
                >{t("workspace.highlighter")}</button>
              </div>
              <span className="dock-subbar-sep" />
              <div style={{ display: 'inline-flex', alignItems: 'center', gap: 5 }}>
                {BRUSH_COLORS.map((hex) => (
                  <button
                    key={hex}
                    type="button"
                    className={`brush-swatch-btn ${brushColor === hex ? 'active' : ''}`}
                    style={{ background: hex }}
                    aria-label={t("workspace.brushColor", { color: hex })}
                    onClick={() => setBrushColor(hex)}
                  />
                ))}
              </div>
              <span className="dock-subbar-sep" />
              <div className="segmented-control">
                {([
                  { w: 2, label: t("workspace.fine") },
                  { w: 4, label: t("workspace.medium") },
                  { w: 8, label: t("workspace.thick") },
                ] as const).map((sz) => (
                  <button
                    key={sz.w}
                    className={brushWidth === sz.w ? 'selected' : ''}
                    onClick={() => setBrushWidth(sz.w)}
                  >
                    {sz.label}
                  </button>
                ))}
              </div>
              {pageStrokes.length > 0 && (
                <>
                  <span className="dock-subbar-sep" />
                  <span>{t("workspace.strokeCount", { count: pageStrokes.length })}</span>
                  <button
                    className="button ghost sm"
                    onClick={() =>
                      commitPages((prev) =>
                        prev.map((p) =>
                          p.id !== activePage.id
                            ? p
                            : { ...p, brushStrokes: (p.brushStrokes ?? []).slice(0, -1) }
                        )
                      )
                    }
                  >{t("workspace.undoLastStroke")}</button>
                  <button
                    className="button ghost sm"
                    onClick={() => {
                      commitPages((prev) =>
                        prev.map((p) => (p.id !== activePage.id ? p : { ...p, brushStrokes: [] }))
                      );
                      onToast(t("workspace.brushStrokesClearedFromThisPage"));
                    }}
                  >{t("workspace.clear")}</button>
                </>
              )}
            </div>
          )}

          {!videoMode && tool === 'mosaic' && (
            <div
              ref={subbarRef}
              className="dock-subbar"
              role="region"
              aria-label={t("workspace.mosaicOptions")}
              style={subbarLeft !== null ? { left: subbarLeft, transform: 'none' } : undefined}
            >
              <div className="segmented-control">
                <button
                  className={mosaicStyle === 'pixelate' ? 'selected' : ''}
                  onClick={() => {
                    setMosaicStyle('pixelate');
                    if (selectedMosaicId) {
                      commitPages((prev) =>
                        prev.map((p) =>
                          p.id !== activePage.id
                            ? p
                            : {
                                ...p,
                                mosaics: (p.mosaics ?? []).map((m) =>
                                  m.id === selectedMosaicId ? { ...m, style: 'pixelate' } : m
                                ),
                              }
                        )
                      );
                    }
                  }}
                >{t("workspace.pixelate")}</button>
                <button
                  className={mosaicStyle === 'blur' ? 'selected' : ''}
                  onClick={() => {
                    setMosaicStyle('blur');
                    if (selectedMosaicId) {
                      commitPages((prev) =>
                        prev.map((p) =>
                          p.id !== activePage.id
                            ? p
                            : {
                                ...p,
                                mosaics: (p.mosaics ?? []).map((m) =>
                                  m.id === selectedMosaicId ? { ...m, style: 'blur' } : m
                                ),
                              }
                        )
                      );
                    }
                  }}
                >{t("workspace.gaussianBlur")}</button>
              </div>
              <span className="dock-subbar-sep" />
              <span>{t("workspace.strength")}{mosaicStrength}</span>
              <input
                className="dock-subbar-slider"
                type="range"
                min={6}
                max={28}
                step={2}
                value={mosaicStrength}
                onChange={(e) => setMosaicStrength(Number(e.target.value))}
              />
              <span className="badge success">
                <ShieldOkIcon size={12} />{t("workspace.hiddenFromAi")}</span>
              {pageMosaics.length > 0 && (
                <>
                  <span className="dock-subbar-sep" />
                  <span>{t("workspace.regionCount", { count: pageMosaics.length })}</span>
                  {selectedMosaicId && (
                    <button
                      className="button ghost sm"
                      onClick={() => {
                        commitPages((prev) =>
                          prev.map((p) =>
                            p.id !== activePage.id
                              ? p
                              : {
                                  ...p,
                                  mosaics: (p.mosaics ?? []).filter((m) => m.id !== selectedMosaicId),
                                }
                          )
                        );
                        setSelectedMosaicId(null);
                      }}
                    >{t("workspace.deleteSelected")}</button>
                  )}
                  <button
                    className="button ghost sm"
                    onClick={() => {
                      commitPages((prev) =>
                        prev.map((p) => (p.id !== activePage.id ? p : { ...p, mosaics: [] }))
                      );
                      setSelectedMosaicId(null);
                      onToast(t("workspace.mosaicRegionsClearedFromThisPage"));
                    }}
                  >{t("workspace.clear")}</button>
                </>
              )}
            </div>
          )}

          {!videoMode && tool === 'crop' && cropRect && (
            <div
              ref={subbarRef}
              className="dock-subbar"
              role="region"
              aria-label={t("workspace.cropOptions")}
              style={subbarLeft !== null ? { left: subbarLeft, transform: 'none' } : undefined}
            >
              <code style={{ fontFamily: 'var(--mono)' }}>
                {cropRect.x2 - cropRect.x1} × {cropRect.y2 - cropRect.y1} px
              </code>
              <span className="dock-subbar-sep" />
              <div className="segmented-control">
                {(['free', '16:9', '4:3', '1:1'] as CropRatio[]).map((r) => (
                  <button
                    key={r}
                    className={cropRatio === r ? 'selected' : ''}
                    onClick={() => applyCropRatioPreset(r)}
                  >
                    {r === 'free' ? t("workspace.free") : r}
                  </button>
                ))}
              </div>
              <span className="dock-subbar-sep" />
              <button
                className="button ghost sm"
                onClick={() => {
                  setCropRect(null);
                  setTool('box');
                }}
              >{t("app.cancel")}</button>
              <button className="button primary sm" onClick={() => void confirmCrop()}>{t("workspace.applyCrop")}</button>
            </div>
          )}

          {!videoMode && tool === 'explode' && (
            <div className="dock-subbar" role="region" aria-label={t("workspace.explodedViewControls")}>
              <span>{t("workspace.selectOrDragAComponentResizeWith")}</span>
              {hasTransformedBlocks && (
                <>
                  <span className="dock-subbar-sep" />
                  <button className="button ghost sm" onClick={resetAllExplodeBlocks}>
                    <ResetIcon size={12} />{t("workspace.resetAll")}</button>
                </>
              )}
            </div>
          )}

          {/* Video Timeline Strip (only shown when active page is video) */}
          {isVideoPage && videoMode && (
            <div className="video-timeline-bar">
              <button
                className="icon-button"
                aria-label={videoPlaying ? t("workspace.pause") : t("workspace.play")}
                onClick={() => setVideoPlaying((p) => !p)}
              >
                {videoPlaying ? <PauseIcon size={15} /> : <PlayIcon size={15} />}
              </button>
              <code style={{ fontSize: 11, fontFamily: 'var(--mono)' }}>
                {(videoTimeMs / 1000).toFixed(2)}s / 12.00s
              </code>
              <div className="video-scrubber">
                <input
                  type="range"
                  min={0}
                  max={12000}
                  step={40}
                  value={videoTimeMs}
                  onChange={(e) => {
                    setVideoPlaying(false);
                    setVideoTimeMs(Number(e.target.value));
                  }}
                />
              </div>
              <div className="video-markers-row">
                {initialVideoMarkers(t).map((m) => (
                  <button
                    key={m.id}
                    className={`video-marker-chip ${Math.abs(videoTimeMs - m.timestampMs) < 350 ? 'active' : ''}`}
                    onClick={() => {
                      setVideoPlaying(false);
                      setVideoTimeMs(m.timestampMs);
                      onToast(m.note);
                    }}
                  >
                    {m.label}
                  </button>
                ))}
              </div>
            </div>
          )}

          {/* Reorganized Concentric Bottom Dock by Category */}
          <div className="floating-dock" role="toolbar" aria-label={t("workspace.toolbar")}>
            {/* Group 1: Selection & Navigation */}
            <button
              className={`dock-tool icon-only ${tool === 'select' ? 'active' : ''}`}
              onClick={() => setTool('select')}
              aria-label={t("settings.selectAndMove")}
              {...tipProps(shortcutLabel(t("workspace.selectAndMove"), 'adjust'))}
            >
              <SelectToolIcon size={16} />
            </button>
            <button
              className={`dock-tool icon-only ${tool === 'pan' ? 'active' : ''}`}
              onClick={() => setTool('pan')}
              aria-label={t("workspace.pan")}
              {...tipProps(t("workspace.panSpace"))}
            >
              <PanToolIcon size={16} />
            </button>

            <span className="dock-sep" />

            {/* Group 2: Image Edit & Annotations */}
            <button
              className={`dock-tool icon-only ${tool === 'smart' ? 'active' : ''}`}
              onClick={() => setTool('smart')}
              aria-label={t("settings.smartSelect")}
              {...tipProps(shortcutLabel(t("workspace.smartSelect"), 'smart'))}
            >
              <SmartSelectIcon size={16} />
            </button>
            <button
              className={`dock-tool icon-only ${tool === 'point' ? 'active' : ''}`}
              onClick={() => setTool('point')}
              aria-label={t("settings.pointAnnotation")}
              {...tipProps(shortcutLabel(t("workspace.pointAnnotation"), 'point'))}
            >
              <PointToolIcon size={16} />
            </button>
            <button
              className={`dock-tool icon-only ${tool === 'box' ? 'active' : ''}`}
              onClick={() => setTool('box')}
              aria-label={t("workspace.boxAnnotation")}
              {...tipProps(shortcutLabel(t("workspace.boxAnnotationTooltip"), 'rectangle'))}
            >
              <RectToolIcon size={16} />
            </button>
            <button
              ref={brushBtnRef}
              className={`dock-tool icon-only ${tool === 'brush' ? 'active' : ''}`}
              onClick={() => setTool((t) => (t === 'brush' ? 'box' : 'brush'))}
              aria-label={t("workspace.brush")}
              {...tipProps(t("workspace.brushAndHighlighter"))}
            >
              <BrushToolIcon size={16} />
            </button>
            <button
              className={`dock-tool explode-btn ${tool === 'explode' ? 'active' : ''}`}
              onClick={toggleExplodeMode}
              {...tipProps(shortcutLabel(t("workspace.explodedView"), 'explode'))}
            >
              <ExplodeIcon size={16} />
              <span>{t("workspace.explodedViewTooltip")}</span>
            </button>
            <button
              ref={mosaicBtnRef}
              className={`dock-tool icon-only ${tool === 'mosaic' ? 'active' : ''}`}
              onClick={() => setTool((t) => (t === 'mosaic' ? 'box' : 'mosaic'))}
              aria-label={t("workspace.mosaic")}
              {...tipProps(t("workspace.pixelateOrBlur"))}
            >
              <MosaicToolIcon size={16} />
            </button>
            <button
              ref={cropBtnRef}
              className={`dock-tool icon-only ${tool === 'crop' ? 'active' : ''}`}
              onClick={() => (tool === 'crop' ? (setCropRect(null), setTool('box')) : enterCropMode())}
              aria-label={t("workspace.crop")}
              {...tipProps(t("workspace.cropCanvas"))}
            >
              <CropToolIcon size={16} />
            </button>
            <button
              className={`dock-tool icon-only ${magnifierEnabled ? 'active' : ''}`}
              onClick={() => {
                setMagnifierEnabled((v) => !v);
                setLoupe(null);
              }}
              aria-label={t("workspace.pixelMagnifier")}
              {...tipProps(t("workspace.pixelMagnifierAndColorPicker"))}
            >
              <MagnifierIcon size={16} />
            </button>
            {onCapture && (
              <button
                className="dock-tool icon-only"
                onClick={onCapture}
                aria-label={t("workspace.retakeScreenshot")}
                {...tipProps(t("workspace.retakeScreenshot"))}
              >
                <CaptureIcon size={16} />
              </button>
            )}
            {isVideoPage && (
              <button
                className={`dock-tool icon-only ${videoMode ? 'active' : ''}`}
                onClick={() => setVideoMode((v) => !v)}
                aria-label={t("workspace.videoTimeline")}
                {...tipProps(t("workspace.videoTimelineAnnotations"))}
              >
                <VideoIcon size={16} />
              </button>
            )}

            <span className="dock-sep" />

            {/* Group 3: Export, Share & Direct CTAs */}
            <button
              className="dock-tool icon-only"
              onClick={() => void handleSaveAnnotatedImage()}
              aria-label={t("workspace.saveImage")}
              {...tipProps(shortcutLabel(t("workspace.saveImagePng"), 'saveImage'))}
            >
              <SaveImageIcon size={16} />
            </button>
            <button
              className="dock-tool icon-only"
              onClick={() => void handlePinCurrentPage()}
              aria-label={t("workspace.pinImage")}
              {...tipProps(t("workspace.pinImageInAFloatingWindow"))}
            >
              <PinIcon size={16} />
            </button>
            <button
              className="dock-tool icon-only"
              onClick={onOpenFiles}
              aria-label={t("workspace.importImage")}
              {...tipProps(shortcutLabel(t("workspace.openImageOrProject"), 'open'))}
            >
              <ImportImageIcon size={16} />
            </button>

            <DropdownMenu.Root>
              <DropdownMenu.Trigger asChild>
                <button
                  className="dock-tool icon-only"
                  aria-label={t("workspace.moreActions")}
                  {...tipProps(t("workspace.moreActions"))}
                >
                  <MoreIcon size={16} />
                </button>
              </DropdownMenu.Trigger>
              <DropdownMenu.Portal>
                <DropdownMenu.Content className="dropdown-menu" side="top" sideOffset={8} align="end">
                  <DropdownMenu.Item className="dropdown-item" onSelect={() => void handleSaveProject()}>
                    <SaveProjectIcon size={14} />{t("workspace.saveProjectEdithere")}</DropdownMenu.Item>
                  <DropdownMenu.Item className="dropdown-item" onSelect={() => void handleCopyFeedbackBundle()}>
                    <CopyBundleIcon size={14} />{t("workspace.copyJsonImage")}</DropdownMenu.Item>
                  <DropdownMenu.Item className="dropdown-item" onSelect={() => void copyJson()}>
                    <CopyTextIcon size={14} />{t("workspace.copyJsonText")}</DropdownMenu.Item>
                  <DropdownMenu.Item className="dropdown-item" onSelect={() => setJsonOpen(true)}>
                    <JsonFileIcon size={14} />{t("workspace.viewOrExportJson")}</DropdownMenu.Item>
                  <DropdownMenu.Separator className="dropdown-separator" />
                  <DropdownMenu.Item
                    className="dropdown-item"
                    onSelect={resetCanvasView}
                  >
                    <FitCanvasIcon size={14} />{t("workspace.fitAndCenterCanvasTooltip")}</DropdownMenu.Item>
                </DropdownMenu.Content>
              </DropdownMenu.Portal>
            </DropdownMenu.Root>

            <button
              className="dock-tool copy-cta"
              onClick={() => void handleCopyAnnotatedImage()}
              {...tipProps(shortcutLabel(t("workspace.copyAnnotatedImageToClipboard"), 'copy'))}
            >
              <CopyImageIcon size={15} />
              <span>{t("workspace.copyAnnotatedImage")}</span>
            </button>

            <button
              className="dock-tool analyze-cta"
              onClick={onAnalyze}
              {...tipProps(t("workspace.openAiAnalysis"))}
            >
              <AiSparkleIcon size={15} />
              <span>{t("app.aiAnalysis")}</span>
            </button>
          </div>

          {/* Instant Responsive Tooltip Overlay */}
          {dockTip && (
            <div
              className={`instant-tooltip ${dockTip.placement}`}
              style={{ left: dockTip.left, top: dockTip.top }}
              role="tooltip"
            >
              {dockTip.text}
            </div>
          )}
        </div>

        {/* Multi-Page Gallery Strip */}
        <div className="gallery-strip" role="group" aria-label={t("workspace.projectPages")}>
          <span className="badge neutral">{t("workspace.projectPageCount", { count: pages.length })}</span>
          {pages.map((page) => {
            const count = page.annotations.length;
            return (
              <div className="gallery-thumb" key={page.id}>
                <button
                  className={`gallery-thumb-btn ${page.id === activePage.id ? 'active' : ''}`}
                  aria-pressed={page.id === activePage.id}
                  title={page.name}
                  onClick={() => onSelectPage(page.id)}
                >
                  <span>{page.name}</span>
                  <span className="gallery-count-pill">{count}</span>
                </button>
                <button
                  className="gallery-remove-btn"
                  aria-label={t("workspace.removePageLabel", { name: page.name })}
                  aria-haspopup="dialog"
                  onClick={(event) => {
                    event.stopPropagation();
                    removeTriggerRef.current = event.currentTarget;
                    setRemovePageId(page.id);
                  }}
                >
                  <CloseIcon size={12} />
                </button>
              </div>
            );
          })}
          <Dialog.Root open={addPageOpen} onOpenChange={setAddPageOpen}>
            <Dialog.Trigger ref={addPageTriggerRef} className="button ghost sm">
              <PlusIcon size={14} />{t("workspace.addPage")}
            </Dialog.Trigger>
            <Dialog.Portal>
              <Dialog.Overlay className="dialog-overlay" />
              <Dialog.Content className="dialog-content gallery-dialog" aria-describedby={undefined}>
                <div className="gallery-dialog-header">
                  <Dialog.Title className="dialog-title">{t("workspace.addPage")}</Dialog.Title>
                  <Dialog.Close className="icon-button" aria-label={t("workspace.close")}>
                    <CloseIcon size={16} />
                  </Dialog.Close>
                </div>
                <div className="gallery-add-options">
                  <Dialog.Close className="gallery-add-option" onClick={onOpenFiles}>
                    <ImportImageIcon size={20} />
                    <span>
                      <strong>{t("workspace.importImage")}</strong>
                      <span>{t("workspace.addPageImportDescription")}</span>
                    </span>
                  </Dialog.Close>
                  <Dialog.Close className="gallery-add-option" onClick={onAddPageCapture}>
                    <CaptureIcon size={20} />
                    <span>
                      <strong>{t("workspace.addPageCapture")}</strong>
                      <span>{t("workspace.addPageCaptureDescription")}</span>
                    </span>
                  </Dialog.Close>
                </div>
              </Dialog.Content>
            </Dialog.Portal>
          </Dialog.Root>
        </div>
      </section>

      {/* Right Inspector */}
      {inspectorOpen && (
        <Tabs.Root
          className="inspector-column"
          value={inspectorTab}
          onValueChange={(v) => setInspectorTab(v as 'page' | 'group')}
        >
          <div className="inspector-tabs-wrap">
            <Tabs.List className="inspector-tabs-list" aria-label={t("workspace.annotationPanelView")}>
              <Tabs.Trigger className="inspector-tab-trigger" value="page">{t("workspace.pageNotes")}<span className="tab-count">{activePage.annotations.length}</span>
              </Tabs.Trigger>
              <Tabs.Trigger className="inspector-tab-trigger" value="group">{t("workspace.groupNotes")}<span className="tab-count">{groupNotes.length}</span>
              </Tabs.Trigger>
            </Tabs.List>
          </div>

          <Tabs.Content value="page" style={{ display: 'flex', flexDirection: 'column', flex: 1, minHeight: 0 }}>
            <div className="inspector-subbar">
              <span>{t("workspace.drawABoxPlaceAPointOr")}</span>
              <button
                className="button ghost sm"
                onClick={() => appendAnnotation({ kind: 'global', text: '' })}
              >
                <NoteAddIcon size={14} />{t("workspace.generalNotes")}</button>
            </div>
            <div className="inspector-list">
              {activePage.annotations.length === 0 ? (
                <div style={{ padding: '32px 16px', textAlign: 'center', color: 'var(--muted)', fontSize: 12, lineHeight: 1.7 }}>{t("workspace.thisPageHasNoAnnotationsYet")}<br />{t("workspace.useBoxAnnotationBrushMosaicOrCrop")}</div>
              ) : (
                activePage.annotations.map((note) => (
                  <article
                    key={note.id}
                    className={`note-card ${selectedNoteId === note.id ? 'selected' : ''}`}
                    onClick={() => {
                      setSelectedNoteId(note.id);
                      if (tool === 'explode' && note.blockId) {
                        setSelectedBlockId(note.blockId);
                      }
                    }}
                  >
                    <div className="note-card-head">
                      <span className={`note-num ${note.movement ? 'explode' : ''}`}>{note.number}</span>
                      <span className="badge neutral">
                        {note.movement
                          ? t("workspace.offset", { xSign: note.movement.dx >= 0 ? '+' : '', x: note.movement.dx, ySign: note.movement.dy >= 0 ? '+' : '', y: note.movement.dy })
                          : note.kind === 'point'
                            ? t("workspace.point")
                            : note.kind === 'global'
                              ? t("workspace.pageWideNote")
                              : t("workspace.selectedRegion")}
                      </span>
                      <div className="note-card-actions">
                        {note.blockId && (
                          <button
                            className="icon-button"
                            title={t("workspace.resetComponentPosition")}
                            onClick={(e) => {
                              e.stopPropagation();
                              resetBlockMovement(note.blockId!);
                            }}
                          >
                            <ResetIcon size={14} />
                          </button>
                        )}
                        <button
                          className="icon-button"
                          title={t("workspace.deleteAnnotation")}
                          onClick={(e) => {
                            e.stopPropagation();
                            deleteNote(note);
                          }}
                        >
                          <DeleteIcon size={14} />
                        </button>
                      </div>
                    </div>

                    {editingNoteId === note.id ? (
                      <textarea
                        autoFocus
                        className="note-textarea"
                        placeholder={t("workspace.enterSpecificFeedback")}
                        value={note.text}
                        onChange={(e) => updateNoteText(note.id, e.target.value)}
                        onBlur={() => setEditingNoteId(null)}
                      />
                    ) : (
                      <button
                        className={`note-body-btn ${!note.text ? 'note-body-placeholder' : ''}`}
                        onClick={(e) => {
                          e.stopPropagation();
                          setSelectedNoteId(note.id);
                          setEditingNoteId(note.id);
                        }}
                      >
                        {note.text || t("workspace.clickToAddANoteAPosition")}
                      </button>
                    )}

                    {(note.rect || note.point) && (
                      <div className="note-meta-foot">
                        <span>
                          {note.rect
                            ? `(${note.rect.x1},${note.rect.y1})→(${note.rect.x2},${note.rect.y2})`
                            : note.point
                              ? `(${note.point.x},${note.point.y})`
                              : ''}
                        </span>
                      </div>
                    )}
                  </article>
                ))
              )}
            </div>
          </Tabs.Content>

          <Tabs.Content value="group" style={{ display: 'flex', flexDirection: 'column', flex: 1, minHeight: 0 }}>
            <div className="inspector-subbar">
              <span>{t("workspace.feedbackThatAppliesAcrossPages")}</span>
              <button className="button ghost sm" onClick={addGlobalGroupNote}>
                <PlusIcon size={14} />{t("workspace.addGroupNote")}</button>
            </div>
            <div className="inspector-list">
              {groupNotes.map((g) => (
                <article key={g.id} className="note-card">
                  <div className="note-card-head">
                    <span className="note-num">{g.number}</span>
                    <span className="badge accent">{t("workspace.groupGuidelines")}</span>
                    <div className="note-card-actions">
                      <button
                        className="icon-button"
                        title={t("workspace.deleteGroupNote")}
                        onClick={() => onUpdateGroupNotes((prev) => prev.filter((item) => item.id !== g.id))}
                      >
                        <DeleteIcon size={14} />
                      </button>
                    </div>
                  </div>
                  {editingNoteId === g.id ? (
                    <textarea
                      autoFocus
                      className="note-textarea"
                      placeholder={t("workspace.enterFeedbackThatAppliesAcrossPages")}
                      value={g.text}
                      onChange={(e) => {
                        const val = e.target.value;
                        onUpdateGroupNotes((prev) =>
                          prev.map((item) => (item.id === g.id ? { ...item, text: val } : item))
                        );
                      }}
                      onBlur={() => setEditingNoteId(null)}
                    />
                  ) : (
                    <button
                      className={`note-body-btn ${!g.text ? 'note-body-placeholder' : ''}`}
                      onClick={() => setEditingNoteId(g.id)}
                    >
                      {g.text || t("workspace.clickToEnterGroupDesignOrLayout")}
                    </button>
                  )}
                </article>
              ))}
            </div>
          </Tabs.Content>

          <div className="inspector-footer">
            <button className="button secondary" onClick={() => void handleCopyFeedbackBundle()}>
              <CopyBundleIcon size={15} />{t("workspace.copyJsonImageTooltip")}</button>

            <button className="button primary" onClick={onAnalyze}>
              <AiSparkleIcon size={15} />{t("app.aiAnalysis")}</button>
          </div>
        </Tabs.Root>
      )}

      <Dialog.Root open={Boolean(removePageTarget)} onOpenChange={(open) => {
        if (!open && !removingPage) setRemovePageId(null);
      }}>
        <Dialog.Portal>
          <Dialog.Overlay className="dialog-overlay" />
          <Dialog.Content
            className="dialog-content gallery-dialog"
            onOpenAutoFocus={(event) => {
              event.preventDefault();
              removeCancelRef.current?.focus();
            }}
            onCloseAutoFocus={(event) => {
              event.preventDefault();
              const trigger = removeTriggerRef.current;
              if (trigger?.isConnected) trigger.focus();
              else addPageTriggerRef.current?.focus();
            }}
          >
            <Dialog.Title className="dialog-title">{t("workspace.removePageTitle")}</Dialog.Title>
            <Dialog.Description className="dialog-desc gallery-remove-description">
              {removePageTarget && t("workspace.removePageDescription", { name: removePageTarget.name })}
              {removePageTarget && removePageTarget.annotations.length > 0 && (
                <span>{t("workspace.removePageAnnotationCount", { count: removePageTarget.annotations.length })}</span>
              )}
            </Dialog.Description>
            <div className="gallery-dialog-actions">
              <Dialog.Close ref={removeCancelRef} className="button secondary" disabled={removingPage}>
                {t("app.cancel")}
              </Dialog.Close>
              <button className="button gallery-confirm-remove" disabled={removingPage} onClick={() => void confirmRemovePage()}>
                {t("workspace.confirmRemovePage")}
              </button>
            </div>
          </Dialog.Content>
        </Dialog.Portal>
      </Dialog.Root>

      <Dialog.Root open={jsonOpen} onOpenChange={setJsonOpen}>
        <Dialog.Portal>
          <Dialog.Overlay className="dialog-overlay" />
          <Dialog.Content className="dialog-content" onEscapeKeyDown={(event) => event.preventDefault()}>
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 8 }}>
              <Dialog.Title className="dialog-title">{t("workspace.liveFeedbackJson")}</Dialog.Title>
              <Dialog.Close className="icon-button" aria-label={t("workspace.close")}>
                <CloseIcon size={16} />
              </Dialog.Close>
            </div>
            <Dialog.Description className="dialog-desc">{t("workspace.exportsPagesWithAnnotationsOrLayoutAdjustments")}</Dialog.Description>
            <pre className="json-preview-pre">{liveJson}</pre>
            <div style={{ display: 'flex', justifyContent: 'flex-end', gap: 8, marginTop: 14 }}>
              <button className="button secondary" onClick={downloadJson}>
                <ExportFileIcon size={14} />{t("workspace.exportJson")}</button>
              <button className="button primary" onClick={copyJson}>
                <CopyTextIcon size={14} />{t("workspace.copyJson")}</button>
            </div>
          </Dialog.Content>
        </Dialog.Portal>
      </Dialog.Root>
    </div>
  );
}
