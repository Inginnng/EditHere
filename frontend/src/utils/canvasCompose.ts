import type { BrushStroke, GalleryPage, GroupNote, MosaicRegion, PixelPoint, PixelRect } from '../bridge/types';

function loadImageElement(src: string): Promise<HTMLImageElement | null> {
  return new Promise((resolve) => {
    const img = new Image();
    if (src.startsWith('http://') || src.startsWith('https://')) {
      img.crossOrigin = 'anonymous';
    }
    img.onload = () => resolve(img);
    img.onerror = () => resolve(null);
    img.src = src;
  });
}

export async function loadBitmapForPage(
  page: GalleryPage,
  mediaContainer?: HTMLElement | null
): Promise<HTMLImageElement | null> {
  if (page.imageUrl) {
    const img = await loadImageElement(page.imageUrl);
    if (img) return img;
  }

  const svg = mediaContainer?.querySelector('svg.artboard-svg') as SVGSVGElement | null;
  if (!svg) return null;

  const clone = svg.cloneNode(true) as SVGSVGElement;
  clone.setAttribute('xmlns', 'http://www.w3.org/2000/svg');
  clone.setAttribute('width', String(page.width));
  clone.setAttribute('height', String(page.height));
  const serialized = new XMLSerializer().serializeToString(clone);
  const blob = new Blob([serialized], { type: 'image/svg+xml;charset=utf-8' });
  const blobUrl = URL.createObjectURL(blob);
  try {
    return await loadImageElement(blobUrl);
  } finally {
    URL.revokeObjectURL(blobUrl);
  }
}

export function drawMosaicsOnContext(
  ctx: CanvasRenderingContext2D,
  sourceImg: HTMLImageElement,
  mosaics: MosaicRegion[],
  pageWidth: number,
  pageHeight: number,
  destWidth = pageWidth,
  destHeight = pageHeight
): void {
  if (!mosaics.length) return;
  const imgW = sourceImg.naturalWidth || pageWidth;
  const imgH = sourceImg.naturalHeight || pageHeight;
  const imgScaleX = imgW / Math.max(1, pageWidth);
  const imgScaleY = imgH / Math.max(1, pageHeight);
  const dstScaleX = destWidth / Math.max(1, pageWidth);
  const dstScaleY = destHeight / Math.max(1, pageHeight);

  const sampleCanvas = document.createElement('canvas');
  const sampleCtx = sampleCanvas.getContext('2d');

  for (const region of mosaics) {
    const rx = Math.max(0, Math.min(pageWidth, region.rect.x1));
    const ry = Math.max(0, Math.min(pageHeight, region.rect.y1));
    const rw = Math.max(1, Math.min(pageWidth - rx, region.rect.x2 - rx));
    const rh = Math.max(1, Math.min(pageHeight - ry, region.rect.y2 - ry));

    const sx = rx * imgScaleX;
    const sy = ry * imgScaleY;
    const sw = rw * imgScaleX;
    const sh = rh * imgScaleY;

    const dx = rx * dstScaleX;
    const dy = ry * dstScaleY;
    const dw = rw * dstScaleX;
    const dh = rh * dstScaleY;

    if (region.style === 'pixelate' && sampleCtx) {
      const block = Math.max(4, Math.round(region.strength || 12));
      const cellsW = Math.max(1, Math.round(rw / block));
      const cellsH = Math.max(1, Math.round(rh / block));
      sampleCanvas.width = cellsW;
      sampleCanvas.height = cellsH;
      sampleCtx.clearRect(0, 0, cellsW, cellsH);
      sampleCtx.drawImage(sourceImg, sx, sy, sw, sh, 0, 0, cellsW, cellsH);

      ctx.save();
      ctx.beginPath();
      ctx.rect(dx, dy, dw, dh);
      ctx.clip();
      ctx.imageSmoothingEnabled = false;
      ctx.drawImage(sampleCanvas, 0, 0, cellsW, cellsH, dx, dy, dw, dh);
      ctx.restore();
    } else {
      const pad = 16;
      const padSx = Math.max(0, sx - pad);
      const padSy = Math.max(0, sy - pad);
      const padSw = Math.min(imgW - padSx, sw + pad * 2);
      const padSh = Math.min(imgH - padSy, sh + pad * 2);
      const padDx = dx - (sx - padSx) * (dstScaleX / imgScaleX);
      const padDy = dy - (sy - padSy) * (dstScaleY / imgScaleY);
      const padDw = padSw * (dstScaleX / imgScaleX);
      const padDh = padSh * (dstScaleY / imgScaleY);

      ctx.save();
      ctx.beginPath();
      ctx.rect(dx, dy, dw, dh);
      ctx.clip();
      ctx.filter = `blur(${Math.max(4, Math.round((region.strength || 12) * 0.85))}px)`;
      ctx.drawImage(sourceImg, padSx, padSy, padSw, padSh, padDx, padDy, padDw, padDh);
      ctx.restore();
    }
  }
}

export function cropPageBitmap(
  sourceImg: HTMLImageElement,
  pageWidth: number,
  pageHeight: number,
  cropRect: PixelRect
): { dataUrl: string; pngBase64: string; width: number; height: number } {
  const x1 = Math.max(0, Math.min(pageWidth - 1, Math.round(cropRect.x1)));
  const y1 = Math.max(0, Math.min(pageHeight - 1, Math.round(cropRect.y1)));
  const x2 = Math.max(x1 + 1, Math.min(pageWidth, Math.round(cropRect.x2)));
  const y2 = Math.max(y1 + 1, Math.min(pageHeight, Math.round(cropRect.y2)));
  const width = x2 - x1;
  const height = y2 - y1;

  const imgW = sourceImg.naturalWidth || pageWidth;
  const imgH = sourceImg.naturalHeight || pageHeight;
  const sx = (x1 / Math.max(1, pageWidth)) * imgW;
  const sy = (y1 / Math.max(1, pageHeight)) * imgH;
  const sw = (width / Math.max(1, pageWidth)) * imgW;
  const sh = (height / Math.max(1, pageHeight)) * imgH;

  const canvas = document.createElement('canvas');
  canvas.width = width;
  canvas.height = height;
  const ctx = canvas.getContext('2d');
  if (ctx) {
    ctx.drawImage(sourceImg, sx, sy, sw, sh, 0, 0, width, height);
  }
  const dataUrl = canvas.toDataURL('image/png');
  const pngBase64 = dataUrl.split(',')[1] ?? '';
  return { dataUrl, pngBase64, width, height };
}

function drawBadgeCircle(
  ctx: CanvasRenderingContext2D,
  cx: number,
  cy: number,
  num: number,
  fill: string
) {
  ctx.save();
  ctx.shadowColor = 'rgba(0,0,0,0.28)';
  ctx.shadowBlur = 5;
  ctx.shadowOffsetY = 2;
  ctx.beginPath();
  ctx.arc(cx, cy, 11, 0, Math.PI * 2);
  ctx.fillStyle = fill;
  ctx.fill();
  ctx.shadowColor = 'transparent';
  ctx.lineWidth = 2;
  ctx.strokeStyle = '#ffffff';
  ctx.stroke();

  ctx.fillStyle = '#ffffff';
  ctx.font = '600 11px ui-monospace, SF Mono, Consolas, monospace';
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  ctx.fillText(String(num), cx, cy + 0.5);
  ctx.restore();
}

export function composePageToPng(
  page: GalleryPage,
  sourceImg: HTMLImageElement,
  includeAnnotations: boolean
): { dataUrl: string; pngBase64: string } {
  const w = Math.max(1, page.width);
  const h = Math.max(1, page.height);
  const canvas = document.createElement('canvas');
  canvas.width = w;
  canvas.height = h;
  const ctx = canvas.getContext('2d');
  if (!ctx) {
    return { dataUrl: '', pngBase64: '' };
  }

  ctx.drawImage(sourceImg, 0, 0, w, h);

  if (page.mosaics && page.mosaics.length > 0) {
    drawMosaicsOnContext(ctx, sourceImg, page.mosaics, w, h, w, h);
  }
  if (page.brushStrokes && page.brushStrokes.length > 0) {
    drawBrushStrokesOnContext(ctx, page.brushStrokes, w, h, w, h);
  }

  // Render Explode lifted slices if any block moved/resized
  const transformedBlocks = page.blocks.filter((b) => {
    const sw = Math.max(1, b.source.x2 - b.source.x1);
    const sh = Math.max(1, b.source.y2 - b.source.y1);
    const dw = b.dest.x2 - b.dest.x1;
    const dh = b.dest.y2 - b.dest.y1;
    return (
      Math.abs(b.dest.x1 - b.source.x1) > 1 ||
      Math.abs(b.dest.y1 - b.source.y1) > 1 ||
      Math.abs(dw - sw) > 1 ||
      Math.abs(dh - sh) > 1
    );
  });

  if (transformedBlocks.length > 0) {
    const imgW = sourceImg.naturalWidth || w;
    const imgH = sourceImg.naturalHeight || h;
    const sxFactor = imgW / w;
    const syFactor = imgH / h;

    // 1. Checkerboard cavity
    for (const b of transformedBlocks) {
      const x = b.source.x1;
      const y = b.source.y1;
      const cw = b.source.x2 - b.source.x1;
      const ch = b.source.y2 - b.source.y1;
      ctx.save();
      ctx.beginPath();
      ctx.rect(x, y, cw, ch);
      ctx.clip();
      ctx.fillStyle = '#f3f4f7';
      ctx.fillRect(x, y, cw, ch);
      ctx.fillStyle = '#dcdfe6';
      const step = 6;
      for (let py = Math.floor(y / step) * step; py < y + ch; py += step) {
        for (let px = Math.floor(x / step) * step; px < x + cw; px += step) {
          if (((Math.floor(px / step) + Math.floor(py / step)) & 1) === 0) {
            ctx.fillRect(px, py, step, step);
          }
        }
      }
      ctx.strokeStyle = 'rgba(15, 23, 42, 0.16)';
      ctx.lineWidth = 1;
      ctx.strokeRect(x + 0.5, y + 0.5, cw - 1, ch - 1);
      ctx.restore();
    }

    // 2. Lifted visual slices
    for (const b of transformedBlocks) {
      const dx = b.dest.x1;
      const dy = b.dest.y1;
      const dw = b.dest.x2 - b.dest.x1;
      const dh = b.dest.y2 - b.dest.y1;
      ctx.save();
      ctx.shadowColor = 'rgba(0,0,0,0.22)';
      ctx.shadowBlur = 16;
      ctx.shadowOffsetY = 6;
      ctx.drawImage(
        sourceImg,
        b.source.x1 * sxFactor,
        b.source.y1 * syFactor,
        (b.source.x2 - b.source.x1) * sxFactor,
        (b.source.y2 - b.source.y1) * syFactor,
        dx,
        dy,
        dw,
        dh
      );
      ctx.shadowColor = 'transparent';
      ctx.strokeStyle = '#007aff';
      ctx.lineWidth = 1.5;
      ctx.strokeRect(dx + 0.75, dy + 0.75, dw - 1.5, dh - 1.5);
      ctx.restore();
    }

    // 3. Trajectory arrows
    for (const b of transformedBlocks) {
      const x1 = (b.source.x1 + b.source.x2) / 2;
      const y1 = (b.source.y1 + b.source.y2) / 2;
      const x2 = (b.dest.x1 + b.dest.x2) / 2;
      const y2 = (b.dest.y1 + b.dest.y2) / 2;
      const len = Math.hypot(x2 - x1, y2 - y1);
      if (len < 6) continue;
      const ux = (x2 - x1) / len;
      const uy = (y2 - y1) / len;
      const nx = -uy;
      const ny = ux;
      const head = Math.min(11, Math.max(6, len * 0.25));
      ctx.save();
      ctx.lineCap = 'round';
      ctx.lineJoin = 'round';
      ctx.strokeStyle = 'rgba(255,255,255,0.88)';
      ctx.lineWidth = 4;
      ctx.beginPath();
      ctx.moveTo(x1, y1);
      ctx.lineTo(x2, y2);
      ctx.stroke();

      ctx.strokeStyle = '#007aff';
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.moveTo(x1, y1);
      ctx.lineTo(x2, y2);
      ctx.stroke();

      ctx.beginPath();
      ctx.moveTo(x2 - ux * head + nx * head * 0.45, y2 - uy * head + ny * head * 0.45);
      ctx.lineTo(x2, y2);
      ctx.lineTo(x2 - ux * head - nx * head * 0.45, y2 - uy * head - ny * head * 0.45);
      ctx.stroke();
      ctx.restore();
    }
  }

  if (includeAnnotations) {
    for (const note of page.annotations) {
      if (note.kind === 'region' && note.rect) {
        const x = note.rect.x1;
        const y = note.rect.y1;
        const rw = Math.max(2, note.rect.x2 - note.rect.x1);
        const rh = Math.max(2, note.rect.y2 - note.rect.y1);
        ctx.save();
        ctx.fillStyle = note.movement ? 'rgba(88, 86, 214, 0.08)' : 'rgba(0, 122, 255, 0.08)';
        ctx.fillRect(x, y, rw, rh);
        ctx.strokeStyle = note.movement ? '#5856d6' : '#007aff';
        ctx.lineWidth = 2;
        ctx.strokeRect(x + 1, y + 1, Math.max(1, rw - 2), Math.max(1, rh - 2));
        ctx.restore();
        drawBadgeCircle(ctx, Math.max(12, x), Math.max(12, y), note.number, note.movement ? '#5856d6' : '#007aff');
      } else if (note.kind === 'point' && note.point) {
        drawBadgeCircle(
          ctx,
          Math.max(12, Math.min(w - 12, note.point.x)),
          Math.max(12, Math.min(h - 12, note.point.y)),
          note.number,
          '#007aff'
        );
      }
    }
  }

  const dataUrl = canvas.toDataURL('image/png');
  const pngBase64 = dataUrl.split(',')[1] ?? '';
  return { dataUrl, pngBase64 };
}

export function strokeToSvgPath(points: PixelPoint[]): string {
  if (points.length === 0) return '';
  if (points.length === 1) {
    return `M ${points[0].x} ${points[0].y} L ${points[0].x + 0.1} ${points[0].y + 0.1}`;
  }
  let d = `M ${points[0].x} ${points[0].y}`;
  for (let i = 1; i < points.length - 1; i++) {
    const midX = (points[i].x + points[i + 1].x) / 2;
    const midY = (points[i].y + points[i + 1].y) / 2;
    d += ` Q ${points[i].x} ${points[i].y} ${midX} ${midY}`;
  }
  const last = points[points.length - 1];
  d += ` L ${last.x} ${last.y}`;
  return d;
}

export function drawBrushStrokesOnContext(
  ctx: CanvasRenderingContext2D,
  strokes: BrushStroke[],
  pageWidth: number,
  pageHeight: number,
  destWidth = pageWidth,
  destHeight = pageHeight
): void {
  if (!strokes.length) return;
  const sx = destWidth / Math.max(1, pageWidth);
  const sy = destHeight / Math.max(1, pageHeight);
  const swScale = (sx + sy) / 2;

  for (const stroke of strokes) {
    if (!stroke.points.length) continue;
    ctx.save();
    ctx.globalAlpha = stroke.opacity ?? 1;
    ctx.strokeStyle = stroke.color || '#FF453A';
    ctx.lineWidth = Math.max(1, stroke.width * swScale);
    ctx.lineCap = 'round';
    ctx.lineJoin = 'round';
    ctx.beginPath();
    const pts = stroke.points;
    ctx.moveTo(pts[0].x * sx, pts[0].y * sy);
    if (pts.length === 1) {
      ctx.lineTo(pts[0].x * sx + 0.1, pts[0].y * sy + 0.1);
    } else {
      for (let i = 1; i < pts.length - 1; i++) {
        const midX = ((pts[i].x + pts[i + 1].x) / 2) * sx;
        const midY = ((pts[i].y + pts[i + 1].y) / 2) * sy;
        ctx.quadraticCurveTo(pts[i].x * sx, pts[i].y * sy, midX, midY);
      }
      const last = pts[pts.length - 1];
      ctx.lineTo(last.x * sx, last.y * sy);
    }
    ctx.stroke();
    ctx.restore();
  }
}

export function composeBaseWithEditsToPng(
  page: GalleryPage,
  sourceImg: HTMLImageElement
): { dataUrl: string; pngBase64: string } {
  const w = Math.max(1, page.width);
  const h = Math.max(1, page.height);
  const canvas = document.createElement('canvas');
  canvas.width = w;
  canvas.height = h;
  const ctx = canvas.getContext('2d');
  if (!ctx) return { dataUrl: '', pngBase64: '' };

  ctx.drawImage(sourceImg, 0, 0, w, h);
  if (page.mosaics && page.mosaics.length > 0) {
    drawMosaicsOnContext(ctx, sourceImg, page.mosaics, w, h, w, h);
  }
  if (page.brushStrokes && page.brushStrokes.length > 0) {
    drawBrushStrokesOnContext(ctx, page.brushStrokes, w, h, w, h);
  }
  const dataUrl = canvas.toDataURL('image/png');
  return { dataUrl, pngBase64: dataUrl.split(',')[1] ?? '' };
}

export async function attachRedactedPagesToPayload(
  pages: GalleryPage[],
  groupNotes: GroupNote[],
  mediaContainer?: HTMLElement | null
): Promise<{
  pages: Array<GalleryPage & { redactedPngBase64?: string }>;
  groupNotes: GroupNote[];
}> {
  const serializedPages = await Promise.all(
    pages.map(async (page) => {
      const hasEdits =
        (page.mosaics?.length ?? 0) > 0 || (page.brushStrokes?.length ?? 0) > 0;
      if (!hasEdits) return page;
      const bmp = await loadBitmapForPage(page, mediaContainer);
      if (!bmp) return page;
      const { pngBase64 } = composeBaseWithEditsToPng(page, bmp);
      return pngBase64 ? { ...page, redactedPngBase64: pngBase64 } : page;
    })
  );
  return { pages: serializedPages, groupNotes };
}

let loupeCanvas: HTMLCanvasElement | null = null;
let loupeSampleCanvas: HTMLCanvasElement | null = null;

export function sampleLoupeGrid(
  sourceImg: HTMLImageElement,
  pageWidth: number,
  pageHeight: number,
  px: number,
  py: number,
  radius = 6
): { dataUrl: string; hex: string; rgb: string } | null {
  const imgW = sourceImg.naturalWidth || pageWidth;
  const imgH = sourceImg.naturalHeight || pageHeight;
  const ix = Math.max(0, Math.min(imgW - 1, Math.round((px / Math.max(1, pageWidth)) * imgW)));
  const iy = Math.max(0, Math.min(imgH - 1, Math.round((py / Math.max(1, pageHeight)) * imgH)));
  const grid = radius * 2 + 1;

  if (!loupeSampleCanvas) loupeSampleCanvas = document.createElement('canvas');
  loupeSampleCanvas.width = grid;
  loupeSampleCanvas.height = grid;
  const sctx = loupeSampleCanvas.getContext('2d', { willReadFrequently: true });
  if (!sctx) return null;

  sctx.fillStyle = '#000000';
  sctx.fillRect(0, 0, grid, grid);
  const srcX0 = Math.max(0, ix - radius);
  const srcY0 = Math.max(0, iy - radius);
  const srcX1 = Math.min(imgW, ix + radius + 1);
  const srcY1 = Math.min(imgH, iy + radius + 1);
  const sw = Math.max(0, srcX1 - srcX0);
  const sh = Math.max(0, srcY1 - srcY0);
  if (sw > 0 && sh > 0) {
    sctx.drawImage(sourceImg, srcX0, srcY0, sw, sh, srcX0 - (ix - radius), srcY0 - (iy - radius), sw, sh);
  }

  const centerPixel = sctx.getImageData(radius, radius, 1, 1).data;
  const r = centerPixel[0];
  const g = centerPixel[1];
  const b = centerPixel[2];
  const hex =
    '#' +
    [r, g, b]
      .map((n) => n.toString(16).padStart(2, '0'))
      .join('')
      .toUpperCase();
  const rgb = `${r}, ${g}, ${b}`;

  const scale = 8;
  const outSize = grid * scale;
  if (!loupeCanvas) loupeCanvas = document.createElement('canvas');
  loupeCanvas.width = outSize;
  loupeCanvas.height = outSize;
  const octx = loupeCanvas.getContext('2d');
  if (!octx) return null;

  octx.imageSmoothingEnabled = false;
  octx.drawImage(loupeSampleCanvas, 0, 0, grid, grid, 0, 0, outSize, outSize);

  // Subtle pixel grid + center reticle
  octx.strokeStyle = 'rgba(255, 255, 255, 0.14)';
  octx.lineWidth = 1;
  for (let i = 1; i < grid; i++) {
    const p = i * scale + 0.5;
    octx.beginPath();
    octx.moveTo(p, 0);
    octx.lineTo(p, outSize);
    octx.moveTo(0, p);
    octx.lineTo(outSize, p);
    octx.stroke();
  }
  const c = radius * scale;
  octx.strokeStyle = '#ffffff';
  octx.lineWidth = 1.5;
  octx.strokeRect(c + 0.5, c + 0.5, scale - 1, scale - 1);
  octx.strokeStyle = '#007aff';
  octx.lineWidth = 1;
  octx.strokeRect(c - 0.5, c - 0.5, scale + 1, scale + 1);

  return { dataUrl: loupeCanvas.toDataURL('image/png'), hex, rgb };
}
