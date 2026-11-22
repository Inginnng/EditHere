import { useEffect, useRef, useState } from 'react';
import * as Dialog from '@radix-ui/react-dialog';
import * as Tabs from '@radix-ui/react-tabs';
import {
  Code2,
  Copy,
  Download,
  Film,
  ImagePlus,
  Layers2,
  MessageSquarePlus,
  Minus,
  MousePointerClick,
  Pause,
  Play,
  Plus,
  RotateCcw,
  Scan,
  Sparkles,
  Square,
  Trash2,
  X,
} from 'lucide-react';
import { SceneArtwork } from '../components/DemoScenes';
import {
  buildFeedbackPayload,
  INITIAL_VIDEO_MARKERS,
  isBlockTransformed,
} from '../bridge/mockBridge';
import type {
  AnnotationItem,
  ExplodeBlock,
  GalleryPage,
  GroupNote,
  PixelRect,
  ToolMode,
} from '../bridge/types';

type ResizeHandle = 'nw' | 'n' | 'ne' | 'e' | 'se' | 's' | 'sw' | 'w';

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
}

interface WorkspaceViewProps {
  pages: GalleryPage[];
  activePageId: string;
  groupNotes: GroupNote[];
  onSelectPage: (pageId: string) => void;
  onUpdatePages: (updater: (prev: GalleryPage[]) => GalleryPage[]) => void;
  onUpdateGroupNotes: (updater: (prev: GroupNote[]) => GroupNote[]) => void;
  onImportImage: (file: File) => void;
  onAnalyze: () => void;
  onToast: (message: string) => void;
}

export default function WorkspaceView({
  pages,
  activePageId,
  groupNotes,
  onSelectPage,
  onUpdatePages,
  onUpdateGroupNotes,
  onImportImage,
  onAnalyze,
  onToast,
}: WorkspaceViewProps) {
  const activePage = pages.find((p) => p.id === activePageId) ?? pages[0];
  const [tool, setTool] = useState<ToolMode>('box');
  const [zoom, setZoom] = useState(100);
  const [selectedNoteId, setSelectedNoteId] = useState<string>(activePage?.annotations[0]?.id ?? '');
  const [editingNoteId, setEditingNoteId] = useState<string | null>(null);
  const [inspectorTab, setInspectorTab] = useState<'page' | 'group'>('page');
  const [hoverBlockId, setHoverBlockId] = useState<string | null>(null);
  const [selectedBlockId, setSelectedBlockId] = useState<string | null>(null);
  const [explosionWaveKey, setExplosionWaveKey] = useState(0);
  const [jsonOpen, setJsonOpen] = useState(false);

  // Box draw & manual Explode cut-draw state (image pixel coords)
  const [draftBox, setDraftBox] = useState<PixelRect | null>(null);
  const [manualCutBox, setManualCutBox] = useState<PixelRect | null>(null);
  const drawStartRef = useRef<{ x: number; y: number } | null>(null);
  const manualCutStartRef = useRef<{ x: number; y: number } | null>(null);

  // Window-tracked Explode move / 8-handle resize session
  const explodeDragRef = useRef<ExplodeDragSession | null>(null);
  const updatePagesRef = useRef(onUpdatePages);
  updatePagesRef.current = onUpdatePages;

  // Video timeline preview shell
  const [videoMode, setVideoMode] = useState(false);
  const [videoPlaying, setVideoPlaying] = useState(false);
  const [videoTimeMs, setVideoTimeMs] = useState(3240);

  const artboardRef = useRef<HTMLDivElement>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    setSelectedNoteId(activePage?.annotations[0]?.id ?? '');
    setEditingNoteId(null);
    setHoverBlockId(null);
    setSelectedBlockId(null);
  }, [activePage?.id]);

  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'i') {
        event.preventDefault();
        fileInputRef.current?.click();
      } else if (event.key === 'Escape' && tool === 'explode') {
        setSelectedBlockId(null);
        setManualCutBox(null);
        manualCutStartRef.current = null;
      }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [tool]);

  useEffect(() => {
    if (!videoMode || !videoPlaying) return;
    const id = setInterval(() => {
      setVideoTimeMs((prev) => (prev + 120 >= 12000 ? 0 : prev + 120));
    }, 120);
    return () => clearInterval(id);
  }, [videoMode, videoPlaying]);

  const nextNoteNumber = (annotations: AnnotationItem[]) =>
    annotations.reduce((max, item) => Math.max(max, item.number), 0) + 1;

  // Window-level pointermove / pointerup for reliable Explode move & 8-handle resize
  useEffect(() => {
    const computeNextDest = (session: ExplodeDragSession, clientX: number, clientY: number): PixelRect => {
      const dxRaw = (clientX - session.startClientX) * session.scaleX;
      const dyRaw = (clientY - session.startClientY) * session.scaleY;
      const o = session.origDest;

      if (session.kind === 'move') {
        const w = o.x2 - o.x1;
        const h = o.y2 - o.y1;
        const x1 = Math.round(Math.max(0, Math.min(session.pageW - w, o.x1 + dxRaw)));
        const y1 = Math.round(Math.max(0, Math.min(session.pageH - h, o.y1 + dyRaw)));
        return { x1, y1, x2: x1 + w, y2: y1 + h };
      }

      const handle = session.handle ?? 'se';
      const minSize = 18;
      let { x1, y1, x2, y2 } = o;

      if (handle.includes('w')) {
        x1 = Math.round(Math.max(0, Math.min(x2 - minSize, o.x1 + dxRaw)));
      }
      if (handle.includes('e')) {
        x2 = Math.round(Math.min(session.pageW, Math.max(x1 + minSize, o.x2 + dxRaw)));
      }
      if (handle.includes('n')) {
        y1 = Math.round(Math.max(0, Math.min(y2 - minSize, o.y1 + dyRaw)));
      }
      if (handle.includes('s')) {
        y2 = Math.round(Math.min(session.pageH, Math.max(y1 + minSize, o.y2 + dyRaw)));
      }
      return { x1, y1, x2, y2 };
    };

    const onWindowPointerMove = (event: PointerEvent) => {
      const session = explodeDragRef.current;
      if (!session) return;
      const nextDest = computeNextDest(session, event.clientX, event.clientY);
      const src = session.source;
      const moveNoteId = `move-${session.blockId}`;
      const dx = nextDest.x1 - src.x1;
      const dy = nextDest.y1 - src.y1;
      const sw = Math.max(1, src.x2 - src.x1);
      const sh = Math.max(1, src.y2 - src.y1);
      const dw = nextDest.x2 - nextDest.x1;
      const dh = nextDest.y2 - nextDest.y1;
      const scaleX = dw / sw;
      const scaleY = dh / sh;
      const significantChange =
        Math.abs(dx) > 2 ||
        Math.abs(dy) > 2 ||
        Math.abs(scaleX - 1) > 0.02 ||
        Math.abs(scaleY - 1) > 0.02;

      updatePagesRef.current((prev) =>
        prev.map((page) => {
          if (page.id !== session.pageId) return page;
          const blocks = page.blocks.map((b) =>
            b.id === session.blockId ? { ...b, dest: nextDest } : b
          );

          const existingIdx = page.annotations.findIndex((a) => a.blockId === session.blockId);
          const annotations = [...page.annotations];

          if (existingIdx >= 0) {
            annotations[existingIdx] = {
              ...annotations[existingIdx],
              rect: nextDest,
              movement: { source: src, rect: nextDest, dx, dy, scaleX, scaleY },
            };
          } else if (significantChange) {
            annotations.push({
              id: moveNoteId,
              number: nextNoteNumber(annotations),
              kind: 'region',
              text: '',
              blockId: session.blockId,
              rect: nextDest,
              movement: { source: src, rect: nextDest, dx, dy, scaleX, scaleY },
            });
          }
          return { ...page, blocks, annotations };
        })
      );

      if (significantChange) {
        setSelectedNoteId(moveNoteId);
      }
    };

    const onWindowPointerUp = () => {
      if (!explodeDragRef.current) return;
      explodeDragRef.current = null;
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
    const newId = `n-${Date.now()}-${Math.random().toString(36).slice(2, 6)}`;
    onUpdatePages((prev) =>
      prev.map((page) => {
        if (page.id !== activePage.id) return page;
        const number = nextNoteNumber(page.annotations);
        const created: AnnotationItem = { id: newId, number, ...partial };
        return { ...page, annotations: [...page.annotations, created] };
      })
    );
    setInspectorTab('page');
    setSelectedNoteId(newId);
    setEditingNoteId(newId);
  };

  const handleArtboardPointerDown = (event: React.PointerEvent<HTMLDivElement>) => {
    if (!activePage) return;
    const pt = clientToPixel(event.clientX, event.clientY);

    if (tool === 'explode') {
      // Clicked empty background outside any block: clear selection & allow manual region cut drag
      setSelectedBlockId(null);
      event.currentTarget.setPointerCapture(event.pointerId);
      manualCutStartRef.current = pt;
      setManualCutBox({ x1: pt.x, y1: pt.y, x2: pt.x, y2: pt.y });
      return;
    }

    if (tool === 'point') {
      appendAnnotation({ kind: 'point', text: '', point: pt });
      return;
    }

    if (tool === 'smart') {
      const hit = activePage.blocks.find(
        (b) =>
          pt.x >= b.dest.x1 &&
          pt.x <= b.dest.x2 &&
          pt.y >= b.dest.y1 &&
          pt.y <= b.dest.y2
      );
      if (hit) {
        appendAnnotation({
          kind: 'region',
          text: '',
          rect: { ...hit.dest },
        });
      } else {
        onToast('未命中检测区域，可切换到「框批注」自由拉框');
      }
      return;
    }

    if (tool === 'box') {
      event.currentTarget.setPointerCapture(event.pointerId);
      drawStartRef.current = pt;
      setDraftBox({ x1: pt.x, y1: pt.y, x2: pt.x, y2: pt.y });
    }
  };

  const handleArtboardPointerMove = (event: React.PointerEvent<HTMLDivElement>) => {
    if (!activePage) return;
    const pt = clientToPixel(event.clientX, event.clientY);

    if (tool === 'smart') {
      const hit = activePage.blocks.find(
        (b) =>
          pt.x >= b.dest.x1 &&
          pt.x <= b.dest.x2 &&
          pt.y >= b.dest.y1 &&
          pt.y <= b.dest.y2
      );
      setHoverBlockId(hit ? hit.id : null);
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

    if (tool === 'box' && drawStartRef.current) {
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

    if (tool === 'box' && drawStartRef.current && draftBox) {
      drawStartRef.current = null;
      const w = draftBox.x2 - draftBox.x1;
      const h = draftBox.y2 - draftBox.y1;
      setDraftBox(null);
      if (w >= 14 && h >= 14) {
        appendAnnotation({ kind: 'region', text: '', rect: draftBox });
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
        onUpdatePages((prev) =>
          prev.map((page) =>
            page.id !== activePage.id
              ? page
              : { ...page, blocks: [...page.blocks, newBlock] }
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
    };
  };

  const resetBlockMovement = (blockId: string) => {
    onUpdatePages((prev) =>
      prev.map((page) =>
        page.id !== activePage.id
          ? page
          : {
              ...page,
              blocks: page.blocks.map((b) =>
                b.id === blockId ? { ...b, dest: { ...b.source } } : b
              ),
              annotations: page.annotations.filter((a) => a.blockId !== blockId),
            }
      )
    );
    onToast('已复位组件布局位置');
  };

  const deleteNote = (note: AnnotationItem) => {
    onUpdatePages((prev) =>
      prev.map((page) =>
        page.id !== activePage.id
          ? page
          : {
              ...page,
              blocks: note.blockId
                ? page.blocks.map((b) =>
                    b.id === note.blockId ? { ...b, dest: { ...b.source } } : b
                  )
                : page.blocks,
              annotations: page.annotations.filter((a) => a.id !== note.id),
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
    if (next === 'explode') {
      setExplosionWaveKey((k) => k + 1);
      onToast('已进入大爆炸模式');
    } else {
      setSelectedBlockId(null);
    }
  };

  const liveJson = JSON.stringify(buildFeedbackPayload(pages, groupNotes), null, 2);

  const copyJson = async () => {
    try {
      await navigator.clipboard.writeText(liveJson);
      onToast('已复制实时 image-set-feedback-1 JSON');
    } catch {
      onToast('浏览器拒绝访问剪贴板，请使用导出功能');
    }
  };

  const downloadJson = () => {
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

  const hoveredBlock = activePage?.blocks.find((b) => b.id === hoverBlockId) ?? null;

  return (
    <div className="workspace-root">
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

      {/* Canvas Column */}
      <div className="canvas-column">
        <div className="canvas-top-hud">
          <div className="canvas-meta-chip">
            <span className="canvas-meta-dot" />
            <strong>{activePage.name}</strong>
            <code>{activePage.width} × {activePage.height}</code>
            <span>·</span>
            <span>{activePage.routeHint}</span>
          </div>

          <div className="canvas-zoom-pill">
            <button
              className="icon-button"
              aria-label="缩小画布"
              onClick={() => setZoom((z) => Math.max(60, z - 15))}
            >
              <Minus size={14} />
            </button>
            <span>{zoom}%</span>
            <button
              className="icon-button"
              aria-label="放大画布"
              onClick={() => setZoom((z) => Math.min(180, z + 15))}
            >
              <Plus size={14} />
            </button>
            <button className="button ghost sm" onClick={() => setZoom(100)}>1:1</button>
          </div>
        </div>

        <div className="canvas-stage-wrap">
          <div className="canvas-stage-inner">
            <div
              ref={artboardRef}
              className={`artboard ${tool === 'box' || tool === 'point' ? 'cursor-crosshair' : ''}`}
              style={{
                aspectRatio: `${activePage.width} / ${activePage.height}`,
                width: `min(100cqw, 820px, calc(100cqh * ${activePage.width} / ${activePage.height}))`,
                transform: `scale(${zoom / 100})`,
              }}
              onPointerDown={handleArtboardPointerDown}
              onPointerMove={handleArtboardPointerMove}
              onPointerUp={handleArtboardPointerUp}
              onPointerLeave={() => setHoverBlockId(null)}
            >
              <div className="artboard-media">
                <SceneArtwork sceneId={activePage.sceneId} imageUrl={activePage.imageUrl} />
              </div>

              {/* Subtle ExplosionWave entry shimmer */}
              {tool === 'explode' && explosionWaveKey > 0 && (
                <div key={explosionWaveKey} className="explosion-wave" aria-hidden="true" />
              )}

              {/* Smart hover highlight (pure geometric dashed outline + pixel size badge, zero fake semantic names) */}
              {tool === 'smart' && hoveredBlock && (
                <div className="smart-block-hover" style={rectStyle(hoveredBlock.dest)}>
                  <span className="smart-block-size">
                    {hoveredBlock.dest.x2 - hoveredBlock.dest.x1} × {hoveredBlock.dest.y2 - hoveredBlock.dest.y1}
                  </span>
                </div>
              )}

              {/* Authentic Visual-Cutout Explode Layer */}
              {tool === 'explode' && (
                <>
                  {/* 1. Original cavities (transparency checkerboard where a slice was lifted) */}
                  {activePage.blocks.map((block) =>
                    isBlockTransformed(block) ? (
                      <div
                        key={`cavity-${block.id}`}
                        className="explode-cavity"
                        style={rectStyle(block.source)}
                      />
                    ) : null
                  )}

                  {/* 2. Physically lifted & resized visual slices */}
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

                  {/* 3. Interactive Explode guides, 8 resize handles & pure-geometry delta pills */}
                  {activePage.blocks.map((block) => {
                    const transformed = isBlockTransformed(block);
                    const isSelected = selectedBlockId === block.id;
                    const isHovered = hoverBlockId === block.id;
                    const linkedNote = activePage.annotations.find((a) => a.blockId === block.id);
                    const dx = block.dest.x1 - block.source.x1;
                    const dy = block.dest.y1 - block.source.y1;
                    const sw = Math.max(1, block.source.x2 - block.source.x1);
                    const sh = Math.max(1, block.source.y2 - block.source.y1);
                    const sx = (block.dest.x2 - block.dest.x1) / sw;
                    const sy = (block.dest.y2 - block.dest.y1) / sh;
                    const scaled = Math.abs(sx - 1) > 0.02 || Math.abs(sy - 1) > 0.02;

                    return (
                      <div
                        key={`guide-${block.id}`}
                        className={`explode-block ${isSelected ? 'selected' : ''} ${isHovered ? 'hovered' : ''} ${transformed ? 'moved' : ''}`}
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
                        {linkedNote && <span className="corner-badge explode">{linkedNote.number}</span>}

                        {(isSelected || isHovered) && transformed && (
                          <span className="explode-delta-pill">
                            Δ {dx >= 0 ? '+' : ''}{dx}, {dy >= 0 ? '+' : ''}{dy}
                            {scaled ? ` · ${sx.toFixed(2)}×` : ''}
                          </span>
                        )}

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

                  {/* 4. Non-blocking Apple Trajectory Arrows (white halo + #007AFF) */}
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

                  {/* 5. Manual cut selection box */}
                  {manualCutBox && (
                    <div className="box-draft-rect" style={rectStyle(manualCutBox)} />
                  )}
                </>
              )}

              {/* Rendered Box & Point Annotations (hide duplicate box rect for blockId while actively in Explode mode) */}
              {activePage.annotations.map((note) => {
                if (note.kind === 'point' && note.point) {
                  return (
                    <button
                      key={note.id}
                      className={`point-pin ${selectedNoteId === note.id ? 'selected' : ''}`}
                      style={{
                        left: pctX(note.point.x),
                        top: pctY(note.point.y),
                      }}
                      onPointerDown={(e) => e.stopPropagation()}
                      onClick={(e) => {
                        e.stopPropagation();
                        setSelectedNoteId(note.id);
                        setInspectorTab('page');
                      }}
                    >
                      {note.number}
                    </button>
                  );
                }
                if (note.kind === 'region' && note.rect) {
                  if (tool === 'explode' && note.blockId) return null;
                  return (
                    <div
                      key={note.id}
                      className={`box-annotation-rect ${selectedNoteId === note.id ? 'selected' : ''}`}
                      style={rectStyle(note.rect)}
                      onPointerDown={(e) => e.stopPropagation()}
                      onClick={(e) => {
                        e.stopPropagation();
                        setSelectedNoteId(note.id);
                        setInspectorTab('page');
                      }}
                    >
                      <span className="corner-badge">{note.number}</span>
                    </div>
                  );
                }
                return null;
              })}

              {/* Live Rubber-Band Box */}
              {draftBox && <div className="box-draft-rect" style={rectStyle(draftBox)} />}
            </div>
          </div>
        </div>

        {/* Explode interaction hint pill (only when not colliding with video timeline) */}
        {tool === 'explode' && !videoMode && (
          <div className="explode-hint-pill">
            单击选块 · 拖动移动 · 拖拽 8 角点缩放 · 空处拉框手动切分 · Esc 取消选中
          </div>
        )}

        {/* Optional Video Timeline Strip */}
        {videoMode && (
          <div className="video-timeline-bar">
            <button
              className="icon-button"
              aria-label={videoPlaying ? '暂停' : '播放'}
              onClick={() => setVideoPlaying((p) => !p)}
            >
              {videoPlaying ? <Pause size={15} /> : <Play size={15} />}
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

        {/* Frosted Apple Capsule Floating Tool Dock */}
        <div className="floating-dock" role="toolbar" aria-label="批注与布局工具">
          <button
            className={`dock-tool ${tool === 'smart' ? 'active' : ''}`}
            onClick={() => setTool('smart')}
            title="悬停自动吸附界面区块"
          >
            <Scan size={15} />
            <span>智能选块</span>
          </button>
          <button
            className={`dock-tool ${tool === 'point' ? 'active' : ''}`}
            onClick={() => setTool('point')}
            title="点击画面精确放点批注"
          >
            <MousePointerClick size={15} />
            <span>点批注</span>
          </button>
          <button
            className={`dock-tool ${tool === 'box' ? 'active' : ''}`}
            onClick={() => setTool('box')}
            title="在画面上按住拖拽拉框"
          >
            <Square size={15} />
            <span>框批注</span>
          </button>
          <span className="dock-sep" />
          <button
            className={`dock-tool explode-btn ${tool === 'explode' ? 'active' : ''}`}
            onClick={toggleExplodeMode}
          >
            <Layers2 size={15} />
            <span>大爆炸</span>
          </button>
          <button
            className={`dock-tool ${videoMode ? 'active' : ''}`}
            onClick={() => setVideoMode((v) => !v)}
            title="切换视频时间轴批注条预览"
          >
            <Film size={15} />
            <span>视频时间轴</span>
          </button>
          <span className="dock-sep" />
          <button
            className="dock-tool"
            onClick={() => fileInputRef.current?.click()}
            title="导入本地图片为新的一页 (Ctrl+I)"
          >
            <ImagePlus size={15} />
            <span>导入图片</span>
          </button>
        </div>
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
        <button className="button ghost sm" onClick={() => fileInputRef.current?.click()}>
          <Plus size={14} /> 添加页面
        </button>
      </div>

      {/* Right Inspector */}
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
              <MessageSquarePlus size={14} /> 全局意见
            </button>
          </div>
          <div className="inspector-list">
            {activePage.annotations.length === 0 ? (
              <div style={{ padding: '32px 16px', textAlign: 'center', color: 'var(--muted)', fontSize: 12, lineHeight: 1.7 }}>
                当前页面暂无批注。<br />选择「框批注」在画布拖拽，或切换「大爆炸」直接切分与调整切片位置。
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
                          <RotateCcw size={13} />
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
                        <Trash2 size={13} />
                      </button>
                    </div>
                  </div>

                  {editingNoteId === note.id ? (
                    <textarea
                      autoFocus
                      className="note-textarea"
                      placeholder="输入具体的修改意见…"
                      value={note.text}
                      onChange={(e) => {
                        const val = e.target.value;
                        onUpdatePages((prev) =>
                          prev.map((p) =>
                            p.id !== activePage.id
                              ? p
                              : {
                                  ...p,
                                  annotations: p.annotations.map((a) =>
                                    a.id === note.id ? { ...a, text: val } : a
                                  ),
                                }
                          )
                        );
                      }}
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
            <span>作用于多页、共享组件或主题 Token</span>
            <button className="button ghost sm" onClick={addGlobalGroupNote}>
              <Plus size={14} /> 添加整组意见
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
                      <Trash2 size={13} />
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
          <Dialog.Root open={jsonOpen} onOpenChange={setJsonOpen}>
            <Dialog.Trigger asChild>
              <button className="button secondary">
                <Code2 size={15} /> 查看 JSON
              </button>
            </Dialog.Trigger>
            <Dialog.Portal>
              <Dialog.Overlay className="dialog-overlay" />
              <Dialog.Content className="dialog-content">
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 8 }}>
                  <Dialog.Title className="dialog-title">实时反馈 JSON（image-set-feedback-1）</Dialog.Title>
                  <Dialog.Close className="icon-button" aria-label="关闭">
                    <X size={16} />
                  </Dialog.Close>
                </div>
                <Dialog.Description className="dialog-desc">
                  仅附带存在批注或布局位移的页面；未批注页面只留在 pageIndex 中作为索引。
                </Dialog.Description>
                <pre className="json-preview-pre">{liveJson}</pre>
                <div style={{ display: 'flex', justifyContent: 'flex-end', gap: 8, marginTop: 14 }}>
                  <button className="button secondary" onClick={downloadJson}>
                    <Download size={14} /> 导出 JSON
                  </button>
                  <button className="button primary" onClick={copyJson}>
                    <Copy size={14} /> 复制 JSON
                  </button>
                </div>
              </Dialog.Content>
            </Dialog.Portal>
          </Dialog.Root>

          <button className="button primary" onClick={onAnalyze}>
            <Sparkles size={15} /> AI 解析
          </button>
        </div>
      </Tabs.Root>
    </div>
  );
}
