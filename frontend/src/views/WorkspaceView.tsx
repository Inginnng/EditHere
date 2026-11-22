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
import { buildFeedbackPayload, INITIAL_VIDEO_MARKERS } from '../bridge/mockBridge';
import type {
  AnnotationItem,
  ExplodeBlock,
  GalleryPage,
  GroupNote,
  PixelRect,
  ToolMode,
} from '../bridge/types';

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
  const [jsonOpen, setJsonOpen] = useState(false);

  // Box draw state (in image pixel coordinates)
  const [draftBox, setDraftBox] = useState<PixelRect | null>(null);
  const drawStartRef = useRef<{ x: number; y: number } | null>(null);

  // Explode drag state
  const dragStateRef = useRef<{
    blockId: string;
    startClientX: number;
    startClientY: number;
    origDx: number;
    origDy: number;
    rectScaleX: number;
    rectScaleY: number;
  } | null>(null);

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
  }, [activePage?.id]);

  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'i') {
        event.preventDefault();
        fileInputRef.current?.click();
      }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, []);

  useEffect(() => {
    if (!videoMode || !videoPlaying) return;
    const id = setInterval(() => {
      setVideoTimeMs((prev) => (prev + 120 >= 12000 ? 0 : prev + 120));
    }, 120);
    return () => clearInterval(id);
  }, [videoMode, videoPlaying]);

  const clientToPixel = (clientX: number, clientY: number) => {
    const el = artboardRef.current;
    if (!el || !activePage) return { x: 0, y: 0 };
    const rect = el.getBoundingClientRect();
    const x = Math.round(Math.max(0, Math.min(activePage.width, ((clientX - rect.left) / rect.width) * activePage.width)));
    const y = Math.round(Math.max(0, Math.min(activePage.height, ((clientY - rect.top) / rect.height) * activePage.height)));
    return { x, y };
  };

  const nextNoteNumber = (annotations: AnnotationItem[]) =>
    annotations.reduce((max, item) => Math.max(max, item.number), 0) + 1;

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
    if (!activePage || tool === 'explode') return;
    const pt = clientToPixel(event.clientX, event.clientY);

    if (tool === 'point') {
      appendAnnotation({ kind: 'point', text: '', point: pt });
      return;
    }

    if (tool === 'smart') {
      const hit = activePage.blocks.find(
        (b) =>
          pt.x >= b.source.x1 + b.dx &&
          pt.x <= b.source.x2 + b.dx &&
          pt.y >= b.source.y1 + b.dy &&
          pt.y <= b.source.y2 + b.dy
      );
      if (hit) {
        appendAnnotation({
          kind: 'region',
          text: '',
          rect: {
            x1: hit.source.x1 + hit.dx,
            y1: hit.source.y1 + hit.dy,
            x2: hit.source.x2 + hit.dx,
            y2: hit.source.y2 + hit.dy,
          },
          ocrtext: hit.label,
        });
      } else {
        onToast('未命中预设区块，请切换到「框批注」自由拉框');
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
          pt.x >= b.source.x1 + b.dx &&
          pt.x <= b.source.x2 + b.dx &&
          pt.y >= b.source.y1 + b.dy &&
          pt.y <= b.source.y2 + b.dy
      );
      setHoverBlockId(hit ? hit.id : null);
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

    if (tool === 'explode' && dragStateRef.current) {
      const d = dragStateRef.current;
      const dx = Math.round(d.origDx + (event.clientX - d.startClientX) * d.rectScaleX);
      const dy = Math.round(d.origDy + (event.clientY - d.startClientY) * d.rectScaleY);
      onUpdatePages((prev) =>
        prev.map((page) => {
          if (page.id !== activePage.id) return page;
          const blocks = page.blocks.map((b) => (b.id === d.blockId ? { ...b, dx, dy } : b));
          const block = blocks.find((b) => b.id === d.blockId);
          if (!block) return { ...page, blocks };
          const movedRect: PixelRect = {
            x1: block.source.x1 + dx,
            y1: block.source.y1 + dy,
            x2: block.source.x2 + dx,
            y2: block.source.y2 + dy,
          };
          const existingIdx = page.annotations.findIndex((a) => a.blockId === block.id);
          let annotations = [...page.annotations];
          if (existingIdx >= 0) {
            annotations[existingIdx] = {
              ...annotations[existingIdx],
              rect: movedRect,
              movement: { source: block.source, rect: movedRect, dx, dy },
            };
          } else if (Math.abs(dx) > 3 || Math.abs(dy) > 3) {
            const created: AnnotationItem = {
              id: `move-${block.id}-${Date.now()}`,
              number: nextNoteNumber(annotations),
              kind: 'region',
              text: '',
              blockId: block.id,
              rect: movedRect,
              movement: { source: block.source, rect: movedRect, dx, dy },
              ocrtext: block.label,
            };
            annotations.push(created);
            setSelectedNoteId(created.id);
          }
          return { ...page, blocks, annotations };
        })
      );
    }
  };

  const handleArtboardPointerUp = (event: React.PointerEvent<HTMLDivElement>) => {
    if (tool === 'box' && drawStartRef.current && draftBox) {
      if (event.currentTarget.hasPointerCapture(event.pointerId)) {
        event.currentTarget.releasePointerCapture(event.pointerId);
      }
      drawStartRef.current = null;
      const w = draftBox.x2 - draftBox.x1;
      const h = draftBox.y2 - draftBox.y1;
      setDraftBox(null);
      if (w >= 14 && h >= 14) {
        appendAnnotation({ kind: 'region', text: '', rect: draftBox });
      }
    }
    if (dragStateRef.current) {
      dragStateRef.current = null;
    }
  };

  const startDraggingBlock = (event: React.PointerEvent<HTMLDivElement>, block: ExplodeBlock) => {
    event.stopPropagation();
    const el = artboardRef.current;
    if (!el || !activePage) return;
    const rect = el.getBoundingClientRect();
    dragStateRef.current = {
      blockId: block.id,
      startClientX: event.clientX,
      startClientY: event.clientY,
      origDx: block.dx,
      origDy: block.dy,
      rectScaleX: activePage.width / rect.width,
      rectScaleY: activePage.height / rect.height,
    };
  };

  const resetBlockMovement = (blockId: string) => {
    onUpdatePages((prev) =>
      prev.map((page) =>
        page.id !== activePage.id
          ? page
          : {
              ...page,
              blocks: page.blocks.map((b) => (b.id === blockId ? { ...b, dx: 0, dy: 0 } : b)),
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
                ? page.blocks.map((b) => (b.id === note.blockId ? { ...b, dx: 0, dy: 0 } : b))
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

  const pct = (px: number, max: number) => `${(px / max) * 100}%`;
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
            <button className="icon-button" aria-label="缩小画布" onClick={() => setZoom((z) => Math.max(60, z - 15))}>
              <Minus size={14} />
            </button>
            <span>{zoom}%</span>
            <button className="icon-button" aria-label="放大画布" onClick={() => setZoom((z) => Math.min(180, z + 15))}>
              <Plus size={14} />
            </button>
            <button className="button ghost sm" onClick={() => setZoom(100)}>1:1</button>
          </div>
        </div>

        <div className="canvas-stage-wrap">
          <div
            ref={artboardRef}
            className={`artboard ${tool === 'box' || tool === 'point' ? 'cursor-crosshair' : ''}`}
            style={{ transform: `scale(${zoom / 100})` }}
            onPointerDown={handleArtboardPointerDown}
            onPointerMove={handleArtboardPointerMove}
            onPointerUp={handleArtboardPointerUp}
            onPointerLeave={() => setHoverBlockId(null)}
          >
            <div className="artboard-media">
              <SceneArtwork sceneId={activePage.sceneId} imageUrl={activePage.imageUrl} />
            </div>

            {/* Smart hover highlight */}
            {tool === 'smart' && hoveredBlock && (
              <div
                className="smart-block-hover"
                style={{
                  left: pct(hoveredBlock.source.x1 + hoveredBlock.dx, activePage.width),
                  top: pct(hoveredBlock.source.y1 + hoveredBlock.dy, activePage.height),
                  width: pct(hoveredBlock.source.x2 - hoveredBlock.source.x1, activePage.width),
                  height: pct(hoveredBlock.source.y2 - hoveredBlock.source.y1, activePage.height),
                }}
              >
                <span className="smart-block-tag">{hoveredBlock.label}</span>
              </div>
            )}

            {/* Explode draggable blocks + original position ghost */}
            {tool === 'explode' &&
              activePage.blocks.map((block) => {
                const moved = block.dx !== 0 || block.dy !== 0;
                return (
                  <div key={block.id}>
                    {moved && (
                      <div
                        className="explode-ghost"
                        style={{
                          left: pct(block.source.x1, activePage.width),
                          top: pct(block.source.y1, activePage.height),
                          width: pct(block.source.x2 - block.source.x1, activePage.width),
                          height: pct(block.source.y2 - block.source.y1, activePage.height),
                        }}
                      />
                    )}
                    <div
                      className={`explode-block ${moved ? 'moved' : ''}`}
                      style={{
                        left: pct(block.source.x1 + block.dx, activePage.width),
                        top: pct(block.source.y1 + block.dy, activePage.height),
                        width: pct(block.source.x2 - block.source.x1, activePage.width),
                        height: pct(block.source.y2 - block.source.y1, activePage.height),
                      }}
                      onPointerDown={(e) => startDraggingBlock(e, block)}
                    >
                      <span className="explode-block-label">{block.label}</span>
                      {moved && (
                        <span className="explode-delta-pill">
                          {block.dx >= 0 ? '+' : ''}{block.dx}, {block.dy >= 0 ? '+' : ''}{block.dy}
                        </span>
                      )}
                    </div>
                  </div>
                );
              })}

            {/* Rendered Box & Point Annotations */}
            {activePage.annotations.map((note) => {
              if (note.kind === 'point' && note.point) {
                return (
                  <button
                    key={note.id}
                    className={`point-pin ${selectedNoteId === note.id ? 'selected' : ''}`}
                    style={{
                      left: pct(note.point.x, activePage.width),
                      top: pct(note.point.y, activePage.height),
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
                return (
                  <div
                    key={note.id}
                    className={`box-annotation-rect ${selectedNoteId === note.id ? 'selected' : ''}`}
                    style={{
                      left: pct(note.rect.x1, activePage.width),
                      top: pct(note.rect.y1, activePage.height),
                      width: pct(note.rect.x2 - note.rect.x1, activePage.width),
                      height: pct(note.rect.y2 - note.rect.y1, activePage.height),
                    }}
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
            {draftBox && (
              <div
                className="box-draft-rect"
                style={{
                  left: pct(draftBox.x1, activePage.width),
                  top: pct(draftBox.y1, activePage.height),
                  width: pct(draftBox.x2 - draftBox.x1, activePage.width),
                  height: pct(draftBox.y2 - draftBox.y1, activePage.height),
                }}
              />
            )}
          </div>
        </div>

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

        {/* Centered Floating Tool Dock */}
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
            onClick={() => {
              const next = tool === 'explode' ? 'box' : 'explode';
              setTool(next);
              if (next === 'explode') {
                onToast('已进入「大爆炸」模式：直接拖动画面上的区块生成位移批注');
              }
            }}
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
          <button className="dock-tool" onClick={() => fileInputRef.current?.click()} title="导入本地图片为新的一页 (Ctrl+I)">
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
        <Tabs.List className="inspector-tabs-list" aria-label="批注面板视图">
          <Tabs.Trigger className="inspector-tab-trigger" value="page">
            本页批注 <span className="badge neutral">{activePage.annotations.length}</span>
          </Tabs.Trigger>
          <Tabs.Trigger className="inspector-tab-trigger" value="group">
            整组意见 <span className="badge neutral">{groupNotes.length}</span>
          </Tabs.Trigger>
        </Tabs.List>

        <Tabs.Content value="page" style={{ display: 'flex', flexDirection: 'column', flex: 1, minHeight: 0 }}>
          <div className="inspector-subbar">
            <span>在画面拉框、放点或拖动组件</span>
            <button
              className="button ghost sm"
              onClick={() => appendAnnotation({ kind: 'global', text: '' })}
            >
              <MessageSquarePlus size={14} /> 全局意见
            </button>
          </div>
          <div className="inspector-list">
            {activePage.annotations.length === 0 ? (
              <div style={{ padding: '28px 14px', textAlign: 'center', color: 'var(--muted)', fontSize: 12 }}>
                当前页面暂无批注。<br />选择「框批注」在画布拖拽，或切换「大爆炸」直接拖动组件。
              </div>
            ) : (
              activePage.annotations.map((note) => (
                <article
                  key={note.id}
                  className={`note-card ${selectedNoteId === note.id ? 'selected' : ''}`}
                  onClick={() => setSelectedNoteId(note.id)}
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
                              : { ...p, annotations: p.annotations.map((a) => (a.id === note.id ? { ...a, text: val } : a)) }
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
                      onUpdateGroupNotes((prev) => prev.map((item) => (item.id === g.id ? { ...item, text: val } : item)));
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
