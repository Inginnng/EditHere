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
  OcrIcon,
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
  INITIAL_VIDEO_MARKERS,
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
  includeOcr?: boolean;
  onToggleOcr?: () => void;
  onCapture?: () => void;
  onSelectPage: (pageId: string) => void;
  prepareWorkspaceSnapshot: (media: HTMLElement | null) => Promise<
    Awaited<ReturnType<typeof attachRedactedPagesToPayload>> & { generation: number; changedSinceSnapshot: boolean }
  >;
  onUpdatePages: (updater: (prev: GalleryPage[]) => GalleryPage[]) => void;
  onUpdateGroupNotes: (updater: (prev: GroupNote[]) => GroupNote[]) => void;
  onImportImage: (file: File) => void;
  onRequestImport?: () => Promise<boolean>;
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
  includeOcr = true,
  onToggleOcr,
  onCapture,
  onSelectPage,
  prepareWorkspaceSnapshot,
  onUpdatePages,
  onUpdateGroupNotes,
  onImportImage,
  onRequestImport,
  onAnalyze,
  onToast,
}: WorkspaceViewProps) {
  const activePage = pages.find((p) => p.id === activePageId) ?? pages[0];
  const [tool, setTool] = useState<ToolMode>('box');
  const [zoom, setZoom] = useState(100);
  const [panOffset, setPanOffset] = useState<{ x: number; y: number }>({ x: 0, y: 0 });
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
  const artboardRef = useRef<HTMLDivElement>(null);
  const artboardMediaRef = useRef<HTMLDivElement>(null);
  const mosaicCanvasRef = useRef<HTMLCanvasElement>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);
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
    onToast('已撤销');
  };

  const handleRedo = () => {
    if (!redoStackRef.current.length) return;
    const nextSnap = redoStackRef.current[redoStackRef.current.length - 1];
    redoStackRef.current = redoStackRef.current.slice(0, -1);
    undoStackRef.current = [...undoStackRef.current.slice(-39), pagesRef.current];
    onUpdatePages(() => nextSnap);
    setHistoryTick((t) => t + 1);
    void syncDesktopPageAfterUndoRedo(nextSnap);
    onToast('已重做');
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
    setPanOffset({ x: 0, y: 0 });
    setVideoMode(false);
    setTool((t) => (t === 'crop' ? 'box' : t));
  }, [activePage?.id, activePage?.isVideo]);

  const triggerImport = () => {
    if (onRequestImport) {
      onRequestImport().then((handled) => {
        if (!handled) fileInputRef.current?.click();
      });
    } else {
      fileInputRef.current?.click();
    }
  };

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
      onToast('当前页面位图尚未就绪，请稍后再试');
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
    setPanOffset({ x: 0, y: 0 });
    setTool('box');
    onToast(`已裁剪画布至 ${cropped.width} × ${cropped.height}`);
  };

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      const target = event.target as HTMLElement | null;
      const inInput = Boolean(target?.closest('input, textarea, [contenteditable="true"]'));

      if (event.code === 'Space' && !inInput) {
        event.preventDefault();
        setSpacePressed(true);
        return;
      }

      if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'i') {
        event.preventDefault();
        triggerImport();
        return;
      }
      if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'z' && !inInput) {
        event.preventDefault();
        if (event.shiftKey) handleRedo();
        else handleUndo();
        return;
      }
      if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'y' && !inInput) {
        event.preventDefault();
        handleRedo();
        return;
      }
      if (inInput) return;

      const lower = event.key.toLowerCase();
      if (event.key === 'Escape') {
        setInlineEditNoteId(null);
        setHoveredNoteId(null);
        if (tool === 'crop') {
          setCropRect(null);
          setTool('box');
        } else if (tool === 'explode') {
          setSelectedBlockId(null);
          setManualCutBox(null);
          manualCutStartRef.current = null;
        }
      } else if (event.key === 'Enter' && tool === 'crop' && cropRect) {
        event.preventDefault();
        void confirmCrop();
      } else if ((event.key === 'Delete' || event.key === 'Backspace') && activePage) {
        if (selectedMosaicId) {
          commitPages((prev) =>
            prev.map((p) =>
              p.id !== activePage.id
                ? p
                : { ...p, mosaics: (p.mosaics ?? []).filter((m) => m.id !== selectedMosaicId) }
            )
          );
          setSelectedMosaicId(null);
        } else if (selectedNoteId) {
          const targetNote = activePage.annotations.find((a) => a.id === selectedNoteId);
          if (targetNote) {
            deleteNote(targetNote);
          }
        }
      } else if (!event.ctrlKey && !event.metaKey && !event.altKey) {
        if (lower === 'v') setTool('select');
        else if (lower === 'g') setTool('pan');
        else if (lower === 's') setTool('smart');
        else if (lower === 'p') setTool('point');
        else if (lower === 'r') setTool('box');
        else if (lower === 'b') setTool('brush');
        else if (lower === 'e') toggleExplodeMode();
        else if (lower === 'm') setTool('mosaic');
        else if (lower === 'c') enterCropMode();
        else if (lower === 'h') setHideAnnotations((h) => !h);
      }
    };
    const onKeyUp = (event: KeyboardEvent) => {
      if (event.code === 'Space') setSpacePressed(false);
    };
    window.addEventListener('keydown', onKeyDown);
    window.addEventListener('keyup', onKeyUp);
    return () => {
      window.removeEventListener('keydown', onKeyDown);
      window.removeEventListener('keyup', onKeyUp);
    };
  }, [tool, cropRect, activePage?.id, selectedMosaicId, selectedNoteId]);

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
        setPanOffset({
          x: Math.round(pd.origX + (event.clientX - pd.startClientX)),
          y: Math.round(pd.origY + (event.clientY - pd.startClientY)),
        });
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

    if (partial.kind === 'region' && partial.rect && includeOcr && isDesktopWebView()) {
      desktopRpc<{ ok?: boolean; text?: string }>(
        'ocr.recognizeRect',
        {
          pageId: targetPageId,
          rect: partial.rect,
          mosaics: (activePage.mosaics ?? []).map((m) => m.rect),
        },
        30000
      )
        .then((res) => {
          const recognized = (res?.text || '').trim();
          if (!recognized) return;
          onUpdatePages((prev) =>
            prev.map((page) =>
              page.id !== targetPageId
                ? page
                : {
                    ...page,
                    annotations: page.annotations.map((a) =>
                      a.id === newId ? { ...a, ocrtext: recognized } : a
                    ),
                  }
            )
          );
        })
        .catch(() => {});
    }
  };

  const handleStagePointerDown = (event: React.PointerEvent<HTMLDivElement>) => {
    if (tool === 'pan' || spacePressed || event.button === 1) {
      event.preventDefault();
      panDragRef.current = {
        startClientX: event.clientX,
        startClientY: event.clientY,
        origX: panOffset.x,
        origY: panOffset.y,
      };
      setIsPanning(true);
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
        onToast('未命中检测区域，可切换到「框批注」自由拉框');
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
        onToast('已手动切分选区，可直接拖动或拖拽角点缩放');
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
    onToast('已复位组件布局位置');
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
    onToast('已复位本页所有切片位置');
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
      onToast('已进入大爆炸布局拆解模式');
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
      onToast('当前画布图像暂不可编码');
      return;
    }
    if (isDesktopWebView()) {
      try {
        await desktopRpc('clipboard.copyImage', { pngBase64: rendered.pngBase64 });
        onToast('已复制带批注图片');
      } catch (err) {
        onToast(err instanceof Error ? err.message : '复制图片失败');
      }
      return;
    }
    try {
      const res = await fetch(rendered.dataUrl);
      const blob = await res.blob();
      await navigator.clipboard.write([new ClipboardItem({ 'image/png': blob })]);
      onToast('已复制合成图片到剪贴板');
    } catch {
      const a = document.createElement('a');
      a.href = rendered.dataUrl;
      a.download = `${activePage.name || 'edithere'}-annotated.png`;
      a.click();
      onToast('已直接下载合成 PNG 图片');
    }
  };

  const handleSaveAnnotatedImage = async () => {
    const rendered = await buildRenderedPngForActivePage();
    if (!rendered || !rendered.pngBase64) {
      onToast('没有可保存的图像内容');
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
        if (res.saved) onToast('已保存带批注 PNG 图片');
      } catch (err) {
        onToast(err instanceof Error ? err.message : '保存图片失败');
      }
      return;
    }
    const a = document.createElement('a');
    a.href = rendered.dataUrl;
    a.download = defaultName;
    a.click();
    onToast('已导出合成 PNG 图片');
  };

  const handlePinCurrentPage = async () => {
    if (!isDesktopWebView()) {
      onToast('请在 Windows 桌面端中使用置顶贴图窗');
      return;
    }
    const rendered = await buildRenderedPngForActivePage();
    try {
      await desktopRpc('pin.page', {
        pageId: activePage.id,
        pngBase64: rendered?.pngBase64 ?? '',
      });
      onToast('已生成桌面置顶贴图窗');
    } catch (err) {
      onToast(err instanceof Error ? err.message : '当前页面无法贴图');
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
        if (res.saved) onToast('已保存 EditHere 项目文件');
      } catch (err) {
        onToast(err instanceof Error ? err.message : '保存项目失败');
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
    onToast('已导出项目 JSON');
  };

  const liveJson = JSON.stringify(buildFeedbackPayload(pages, groupNotes), null, 2);

  const copyJson = async () => {
    try {
      if (isDesktopWebView()) {
        await desktopRpc('clipboard.copyText', { text: liveJson });
      } else {
        await navigator.clipboard.writeText(liveJson);
      }
      onToast('已复制实时 image-set-feedback-1 JSON');
    } catch {
      onToast('浏览器拒绝访问剪贴板，请使用导出功能');
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
      onToast(`已复制 JSON 与原图（共 ${res.files ?? 2} 项）`);
    } catch (err) {
      onToast(err instanceof Error ? err.message : '复制 JSON 文件包失败');
    }
  };

  const downloadJson = async () => {
    if (isDesktopWebView()) {
      try {
        const res = await desktopRpc<{ saved?: boolean }>(
          'file.saveText',
          { defaultName: 'edithere-gallery-feedback.json', text: liveJson },
          0
        );
        if (res.saved) onToast('已导出实时反馈 JSON');
      } catch (err) {
        onToast(err instanceof Error ? err.message : '导出失败');
      }
      return;
    }
    const url = URL.createObjectURL(new Blob([liveJson], { type: 'application/json' }));
    const a = document.createElement('a');
    a.href = url;
    a.download = 'edithere-gallery-feedback.json';
    a.click();
    URL.revokeObjectURL(url);
    onToast('已下载实时反馈 JSON');
  };

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
              ? `位移 ${note.movement.dx >= 0 ? '+' : ''}${note.movement.dx}, ${note.movement.dy >= 0 ? '+' : ''}${note.movement.dy}`
              : note.kind === 'point'
                ? '点定位'
                : '框选批注'}
          </span>
          <div className="inline-note-actions">
            <button
              className="icon-button"
              title="编辑批注"
              onMouseDown={(e) => e.preventDefault()}
              onClick={() => setInlineEditNoteId(isEditing ? null : note.id)}
            >
              <EditIcon size={12} />
            </button>
            {note.blockId && (
              <button
                className="icon-button"
                title="复位组件位移"
                onMouseDown={(e) => e.preventDefault()}
                onClick={() => resetBlockMovement(note.blockId!)}
              >
                <ResetIcon size={12} />
              </button>
            )}
            <button
              className="icon-button"
              title="删除批注"
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
            placeholder="输入修改说明，回车或点击空白保存…"
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
            {note.text || '点击直接输入修改意见…'}
          </button>
        )}

        {note.ocrtext && <div className="inline-note-ocr">OCR: {note.ocrtext}</div>}
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
      <input
        ref={fileInputRef}
        type="file"
        accept="image/*"
        hidden
        onChange={(e) => {
          const file = e.target.files?.[0];
          if (file) onImportImage(file);
          e.target.value = '';
        }}
      />

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
                  aria-label="撤销"
                  {...tipProps('撤销 (Ctrl+Z)', 'below')}
                >
                  <UndoIcon size={14} />
                </button>
                <button
                  className="icon-button"
                  disabled={!canRedo}
                  onClick={handleRedo}
                  aria-label="重做"
                  {...tipProps('重做 (Ctrl+Y)', 'below')}
                >
                  <RedoIcon size={14} />
                </button>
                <button
                  className={`icon-button ${hideAnnotations ? 'is-active' : ''}`}
                  onClick={() => setHideAnnotations((h) => !h)}
                  aria-label={hideAnnotations ? '显示批注' : '隐藏批注'}
                  {...tipProps(hideAnnotations ? '显示批注标记 (H)' : '隐藏批注标记 (H)', 'below')}
                >
                  {hideAnnotations ? <HideIcon size={14} /> : <ShowIcon size={14} />}
                </button>
              </div>

              <div className="canvas-zoom-pill">
                <button
                  className="icon-button"
                  aria-label="缩小画布"
                  onClick={() => setZoom((z) => Math.max(60, z - 15))}
                  {...tipProps('缩小画布', 'below')}
                >
                  <MinusIcon size={14} />
                </button>
                <span>{zoom}%</span>
                <button
                  className="icon-button"
                  aria-label="放大画布"
                  onClick={() => setZoom((z) => Math.min(180, z + 15))}
                  {...tipProps('放大画布', 'below')}
                >
                  <PlusIcon size={14} />
                </button>
                <button
                  className="button ghost sm"
                  onClick={() => {
                    setZoom(100);
                    setPanOffset({ x: 0, y: 0 });
                  }}
                  {...tipProps('适应窗口并复位画布位置', 'below')}
                >
                  适应
                </button>
              </div>

              <div className="canvas-zoom-pill">
                <button
                  className={`inspector-toggle-btn ${inspectorOpen ? 'is-open' : ''}`}
                  aria-label={inspectorOpen ? '收起右侧批注栏' : '展开右侧批注栏'}
                  onClick={() => {
                    setInspectorOpen((o) => !o);
                    setInlineEditNoteId(null);
                  }}
                  {...tipProps(inspectorOpen ? '收起右侧批注栏' : '展开右侧批注栏', 'below')}
                >
                  {inspectorOpen ? <InspectorCloseIcon size={14} /> : <InspectorOpenIcon size={14} />}
                  <span>批注栏</span>
                  <span className="inspector-toggle-count">{activePage.annotations.length}</span>
                </button>
              </div>
            </div>
          </div>

          <div
            className={`canvas-stage-wrap ${canPanActive ? 'can-pan' : ''} ${isPanning ? 'is-panning' : ''}`}
            onPointerDown={handleStagePointerDown}
          >
            <div className="canvas-stage-inner">
              <div
                ref={artboardRef}
                className={`artboard ${isPanning ? 'is-panning' : ''} ${
                  isDrawingTool ? 'cursor-crosshair drawing-tool-active' : ''
                }`}
                style={{
                  aspectRatio: `${activePage.width} / ${activePage.height}`,
                  width: `min(100cqw, 820px, calc(100cqh * ${activePage.width} / ${activePage.height}))`,
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
                    <img className="pixel-loupe-img" src={loupe.dataUrl} alt="像素放大镜" />
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
              aria-label="画笔工具选项"
              style={subbarLeft !== null ? { left: subbarLeft, transform: 'none' } : undefined}
            >
              <div className="segmented-control">
                <button
                  className={brushMode === 'pen' ? 'selected' : ''}
                  onClick={() => setBrushMode('pen')}
                >
                  画笔
                </button>
                <button
                  className={brushMode === 'highlighter' ? 'selected' : ''}
                  onClick={() => setBrushMode('highlighter')}
                >
                  荧光笔
                </button>
              </div>
              <span className="dock-subbar-sep" />
              <div style={{ display: 'inline-flex', alignItems: 'center', gap: 5 }}>
                {BRUSH_COLORS.map((hex) => (
                  <button
                    key={hex}
                    type="button"
                    className={`brush-swatch-btn ${brushColor === hex ? 'active' : ''}`}
                    style={{ background: hex }}
                    aria-label={`画笔颜色 ${hex}`}
                    onClick={() => setBrushColor(hex)}
                  />
                ))}
              </div>
              <span className="dock-subbar-sep" />
              <div className="segmented-control">
                {([
                  { w: 2, label: '细' },
                  { w: 4, label: '中' },
                  { w: 8, label: '粗' },
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
                  <span>{pageStrokes.length} 笔</span>
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
                  >
                    撤销一笔
                  </button>
                  <button
                    className="button ghost sm"
                    onClick={() => {
                      commitPages((prev) =>
                        prev.map((p) => (p.id !== activePage.id ? p : { ...p, brushStrokes: [] }))
                      );
                      onToast('已清空本页画笔笔迹');
                    }}
                  >
                    清空
                  </button>
                </>
              )}
            </div>
          )}

          {!videoMode && tool === 'mosaic' && (
            <div
              ref={subbarRef}
              className="dock-subbar"
              role="region"
              aria-label="马赛克工具选项"
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
                >
                  像素块
                </button>
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
                >
                  高斯模糊
                </button>
              </div>
              <span className="dock-subbar-sep" />
              <span>强度 {mosaicStrength}</span>
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
                <ShieldOkIcon size={12} /> AI 不可见
              </span>
              {pageMosaics.length > 0 && (
                <>
                  <span className="dock-subbar-sep" />
                  <span>{pageMosaics.length} 处</span>
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
                    >
                      删除选中
                    </button>
                  )}
                  <button
                    className="button ghost sm"
                    onClick={() => {
                      commitPages((prev) =>
                        prev.map((p) => (p.id !== activePage.id ? p : { ...p, mosaics: [] }))
                      );
                      setSelectedMosaicId(null);
                      onToast('已清空本页马赛克');
                    }}
                  >
                    清空
                  </button>
                </>
              )}
            </div>
          )}

          {!videoMode && tool === 'crop' && cropRect && (
            <div
              ref={subbarRef}
              className="dock-subbar"
              role="region"
              aria-label="裁剪选项"
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
                    {r === 'free' ? '自由' : r}
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
              >
                取消
              </button>
              <button className="button primary sm" onClick={() => void confirmCrop()}>
                确认裁剪
              </button>
            </div>
          )}

          {!videoMode && tool === 'explode' && (
            <div className="dock-subbar" role="region" aria-label="大爆炸操作提示">
              <span>选块 · 拖动 · 8角缩放 · 空处拉框手动切分</span>
              {hasTransformedBlocks && (
                <>
                  <span className="dock-subbar-sep" />
                  <button className="button ghost sm" onClick={resetAllExplodeBlocks}>
                    <ResetIcon size={12} /> 复位全部
                  </button>
                </>
              )}
            </div>
          )}

          {/* Video Timeline Strip (only shown when active page is video) */}
          {isVideoPage && videoMode && (
            <div className="video-timeline-bar">
              <button
                className="icon-button"
                aria-label={videoPlaying ? '暂停' : '播放'}
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
                {INITIAL_VIDEO_MARKERS.map((m) => (
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
          <div className="floating-dock" role="toolbar" aria-label="工具栏">
            {/* Group 1: Selection & Navigation */}
            <button
              className={`dock-tool icon-only ${tool === 'select' ? 'active' : ''}`}
              onClick={() => setTool('select')}
              aria-label="选择与移动"
              {...tipProps('选择与移动 (V)')}
            >
              <SelectToolIcon size={16} />
            </button>
            <button
              className={`dock-tool icon-only ${tool === 'pan' ? 'active' : ''}`}
              onClick={() => setTool('pan')}
              aria-label="拖动画布"
              {...tipProps('拖动画布 (G / Space)')}
            >
              <PanToolIcon size={16} />
            </button>

            <span className="dock-sep" />

            {/* Group 2: Image Edit & Annotations */}
            <button
              className={`dock-tool icon-only ${tool === 'smart' ? 'active' : ''}`}
              onClick={() => setTool('smart')}
              aria-label="智能选块"
              {...tipProps('智能选块 (S)')}
            >
              <SmartSelectIcon size={16} />
            </button>
            <button
              className={`dock-tool icon-only ${tool === 'point' ? 'active' : ''}`}
              onClick={() => setTool('point')}
              aria-label="点批注"
              {...tipProps('点批注 (P)')}
            >
              <PointToolIcon size={16} />
            </button>
            <button
              className={`dock-tool icon-only ${tool === 'box' ? 'active' : ''}`}
              onClick={() => setTool('box')}
              aria-label="框批注"
              {...tipProps('框批注 (R)')}
            >
              <RectToolIcon size={16} />
            </button>
            <button
              ref={brushBtnRef}
              className={`dock-tool icon-only ${tool === 'brush' ? 'active' : ''}`}
              onClick={() => setTool((t) => (t === 'brush' ? 'box' : 'brush'))}
              aria-label="画笔"
              {...tipProps('画笔与荧光笔 (B)')}
            >
              <BrushToolIcon size={16} />
            </button>
            <button
              className={`dock-tool explode-btn ${tool === 'explode' ? 'active' : ''}`}
              onClick={toggleExplodeMode}
              {...tipProps('大爆炸布局拆解 (E)')}
            >
              <ExplodeIcon size={16} />
              <span>大爆炸</span>
            </button>
            <button
              ref={mosaicBtnRef}
              className={`dock-tool icon-only ${tool === 'mosaic' ? 'active' : ''}`}
              onClick={() => setTool((t) => (t === 'mosaic' ? 'box' : 'mosaic'))}
              aria-label="马赛克"
              {...tipProps('马赛克 / 模糊 (M)')}
            >
              <MosaicToolIcon size={16} />
            </button>
            <button
              ref={cropBtnRef}
              className={`dock-tool icon-only ${tool === 'crop' ? 'active' : ''}`}
              onClick={() => (tool === 'crop' ? (setCropRect(null), setTool('box')) : enterCropMode())}
              aria-label="裁剪"
              {...tipProps('裁剪画布 (C)')}
            >
              <CropToolIcon size={16} />
            </button>
            <button
              className={`dock-tool icon-only ${magnifierEnabled ? 'active' : ''}`}
              onClick={() => {
                setMagnifierEnabled((v) => !v);
                setLoupe(null);
              }}
              aria-label="像素放大镜"
              {...tipProps('像素放大镜与取色')}
            >
              <MagnifierIcon size={16} />
            </button>
            <button
              className={`dock-tool icon-only ${includeOcr ? 'active' : ''}`}
              onClick={() => {
                onToggleOcr?.();
                onToast(includeOcr ? '已关闭自动 OCR' : '已开启自动 OCR');
              }}
              aria-label="切换自动 OCR"
              {...tipProps(includeOcr ? '区域自动 OCR：开启' : '区域自动 OCR：关闭')}
            >
              <OcrIcon size={16} />
            </button>
            {onCapture && (
              <button
                className="dock-tool icon-only"
                onClick={onCapture}
                aria-label="重新截图"
                {...tipProps('重新截图')}
              >
                <CaptureIcon size={16} />
              </button>
            )}
            {isVideoPage && (
              <button
                className={`dock-tool icon-only ${videoMode ? 'active' : ''}`}
                onClick={() => setVideoMode((v) => !v)}
                aria-label="视频时间轴"
                {...tipProps('视频时间轴批注条')}
              >
                <VideoIcon size={16} />
              </button>
            )}

            <span className="dock-sep" />

            {/* Group 3: Export, Share & Direct CTAs */}
            <button
              className="dock-tool icon-only"
              onClick={() => void handleSaveAnnotatedImage()}
              aria-label="保存图片"
              {...tipProps('保存图片 (PNG)')}
            >
              <SaveImageIcon size={16} />
            </button>
            <button
              className="dock-tool icon-only"
              onClick={() => void handlePinCurrentPage()}
              aria-label="贴图"
              {...tipProps('桌面置顶贴图窗')}
            >
              <PinIcon size={16} />
            </button>
            <button
              className="dock-tool icon-only"
              onClick={triggerImport}
              aria-label="导入图片"
              {...tipProps('导入图片或项目 (Ctrl+I)')}
            >
              <ImportImageIcon size={16} />
            </button>

            <DropdownMenu.Root>
              <DropdownMenu.Trigger asChild>
                <button
                  className="dock-tool icon-only"
                  aria-label="更多操作"
                  {...tipProps('更多操作')}
                >
                  <MoreIcon size={16} />
                </button>
              </DropdownMenu.Trigger>
              <DropdownMenu.Portal>
                <DropdownMenu.Content className="dropdown-menu" side="top" sideOffset={8} align="end">
                  <DropdownMenu.Item className="dropdown-item" onSelect={() => void handleSaveProject()}>
                    <SaveProjectIcon size={14} /> 保存项目 (.edithere)
                  </DropdownMenu.Item>
                  <DropdownMenu.Item className="dropdown-item" onSelect={() => void handleCopyFeedbackBundle()}>
                    <CopyBundleIcon size={14} /> 复制 JSON 与原图
                  </DropdownMenu.Item>
                  <DropdownMenu.Item className="dropdown-item" onSelect={() => void copyJson()}>
                    <CopyTextIcon size={14} /> 复制 JSON 文本
                  </DropdownMenu.Item>
                  <DropdownMenu.Item className="dropdown-item" onSelect={() => setJsonOpen(true)}>
                    <JsonFileIcon size={14} /> 查看 / 导出 JSON
                  </DropdownMenu.Item>
                  <DropdownMenu.Separator className="dropdown-separator" />
                  <DropdownMenu.Item
                    className="dropdown-item"
                    onSelect={() => {
                      setZoom(100);
                      setPanOffset({ x: 0, y: 0 });
                    }}
                  >
                    <FitCanvasIcon size={14} /> 适应并归位画布
                  </DropdownMenu.Item>
                </DropdownMenu.Content>
              </DropdownMenu.Portal>
            </DropdownMenu.Root>

            <button
              className="dock-tool copy-cta"
              onClick={() => void handleCopyAnnotatedImage()}
              {...tipProps('复制带批注与图片到剪贴板')}
            >
              <CopyImageIcon size={15} />
              <span>复制带批注与图片</span>
            </button>

            <button
              className="dock-tool analyze-cta"
              onClick={onAnalyze}
              {...tipProps('打开 AI 解析视图')}
            >
              <AiSparkleIcon size={15} />
              <span>AI 解析</span>
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
        <div className="gallery-strip" role="tablist" aria-label="项目组图页面">
          <span className="badge neutral">组图 · {pages.length} 页</span>
          {pages.map((page) => {
            const count = page.annotations.length;
            return (
              <button
                key={page.id}
                className={`gallery-thumb-btn ${page.id === activePage.id ? 'active' : ''}`}
                onClick={() => onSelectPage(page.id)}
              >
                <span>{page.name}</span>
                <span className="gallery-count-pill">{count}</span>
              </button>
            );
          })}
          <button className="button ghost sm" onClick={triggerImport}>
            <PlusIcon size={14} /> 添加页面
          </button>
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
            <Tabs.List className="inspector-tabs-list" aria-label="批注面板视图">
              <Tabs.Trigger className="inspector-tab-trigger" value="page">
                本页批注 <span className="tab-count">{activePage.annotations.length}</span>
              </Tabs.Trigger>
              <Tabs.Trigger className="inspector-tab-trigger" value="group">
                整组意见 <span className="tab-count">{groupNotes.length}</span>
              </Tabs.Trigger>
            </Tabs.List>
          </div>

          <Tabs.Content value="page" style={{ display: 'flex', flexDirection: 'column', flex: 1, minHeight: 0 }}>
            <div className="inspector-subbar">
              <span>在画面拉框、放点或拖动切片</span>
              <button
                className="button ghost sm"
                onClick={() => appendAnnotation({ kind: 'global', text: '' })}
              >
                <NoteAddIcon size={14} /> 全局意见
              </button>
            </div>
            <div className="inspector-list">
              {activePage.annotations.length === 0 ? (
                <div style={{ padding: '32px 16px', textAlign: 'center', color: 'var(--muted)', fontSize: 12, lineHeight: 1.7 }}>
                  当前页面暂无批注。<br />选择「框批注」或「画笔 / 马赛克 / 裁剪」直接编辑画面。
                </div>
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
                          ? `位移 ${note.movement.dx >= 0 ? '+' : ''}${note.movement.dx}, ${note.movement.dy >= 0 ? '+' : ''}${note.movement.dy}`
                          : note.kind === 'point'
                            ? '点定位'
                            : note.kind === 'global'
                              ? '整页意见'
                              : '框选区域'}
                      </span>
                      <div className="note-card-actions">
                        {note.blockId && (
                          <button
                            className="icon-button"
                            title="复位组件位移"
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
                          title="删除批注"
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
                        placeholder="输入具体的修改意见…"
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
                        {note.text || '点击补充文字说明（仅拖动位置也是有效布局意见）'}
                      </button>
                    )}

                    {(note.rect || note.point || note.ocrtext) && (
                      <div className="note-meta-foot">
                        <span>
                          {note.rect
                            ? `(${note.rect.x1},${note.rect.y1})→(${note.rect.x2},${note.rect.y2})`
                            : note.point
                              ? `(${note.point.x},${note.point.y})`
                              : ''}
                        </span>
                        {note.ocrtext && <span>OCR: {note.ocrtext.slice(0, 18)}</span>}
                      </div>
                    )}
                  </article>
                ))
              )}
            </div>
          </Tabs.Content>

          <Tabs.Content value="group" style={{ display: 'flex', flexDirection: 'column', flex: 1, minHeight: 0 }}>
            <div className="inspector-subbar">
              <span>跨页面共用修改意见</span>
              <button className="button ghost sm" onClick={addGlobalGroupNote}>
                <PlusIcon size={14} /> 添加整组意见
              </button>
            </div>
            <div className="inspector-list">
              {groupNotes.map((g) => (
                <article key={g.id} className="note-card">
                  <div className="note-card-head">
                    <span className="note-num">{g.number}</span>
                    <span className="badge accent">整组规范</span>
                    <div className="note-card-actions">
                      <button
                        className="icon-button"
                        title="删除整组意见"
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
                      placeholder="输入跨页面统一修改意见…"
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
                      {g.text || '点击填写整组设计或布局意见…'}
                    </button>
                  )}
                </article>
              ))}
            </div>
          </Tabs.Content>

          <div className="inspector-footer">
            <button className="button secondary" onClick={() => void handleCopyFeedbackBundle()}>
              <CopyBundleIcon size={15} /> 复制 JSON 与图片
            </button>

            <button className="button primary" onClick={onAnalyze}>
              <AiSparkleIcon size={15} /> AI 解析
            </button>
          </div>
        </Tabs.Root>
      )}

      <Dialog.Root open={jsonOpen} onOpenChange={setJsonOpen}>
        <Dialog.Portal>
          <Dialog.Overlay className="dialog-overlay" />
          <Dialog.Content className="dialog-content">
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 8 }}>
              <Dialog.Title className="dialog-title">实时反馈 JSON</Dialog.Title>
              <Dialog.Close className="icon-button" aria-label="关闭">
                <CloseIcon size={16} />
              </Dialog.Close>
            </div>
            <Dialog.Description className="dialog-desc">
              仅导出包含批注或布局调整的页面。
            </Dialog.Description>
            <pre className="json-preview-pre">{liveJson}</pre>
            <div style={{ display: 'flex', justifyContent: 'flex-end', gap: 8, marginTop: 14 }}>
              <button className="button secondary" onClick={downloadJson}>
                <ExportFileIcon size={14} /> 导出 JSON
              </button>
              <button className="button primary" onClick={copyJson}>
                <CopyTextIcon size={14} /> 复制 JSON
              </button>
            </div>
          </Dialog.Content>
        </Dialog.Portal>
      </Dialog.Root>
    </div>
  );
}
