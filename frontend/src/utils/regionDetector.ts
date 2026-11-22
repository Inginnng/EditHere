import type { ExplodeBlock, PixelRect } from '../bridge/types';

function clampRect(r: PixelRect, width: number, height: number): PixelRect {
  const x1 = Math.max(0, Math.min(width - 1, Math.round(r.x1)));
  const y1 = Math.max(0, Math.min(height - 1, Math.round(r.y1)));
  const x2 = Math.max(x1 + 1, Math.min(width, Math.round(r.x2)));
  const y2 = Math.max(y1 + 1, Math.min(height, Math.round(r.y2)));
  return { x1, y1, x2, y2 };
}

function areaOf(r: PixelRect): number {
  return Math.max(0, r.x2 - r.x1) * Math.max(0, r.y2 - r.y1);
}

function intersectArea(a: PixelRect, b: PixelRect): number {
  const x1 = Math.max(a.x1, b.x1);
  const y1 = Math.max(a.y1, b.y1);
  const x2 = Math.min(a.x2, b.x2);
  const y2 = Math.min(a.y2, b.y2);
  if (x2 <= x1 || y2 <= y1) return 0;
  return (x2 - x1) * (y2 - y1);
}

function fallbackQuadrants(width: number, height: number): ExplodeBlock[] {
  const mx = Math.round(width * 0.5);
  const my = Math.round(height * 0.5);
  const padX = Math.max(4, Math.round(width * 0.03));
  const padY = Math.max(4, Math.round(height * 0.03));
  const raw: PixelRect[] = [
    { x1: padX, y1: padY, x2: Math.max(padX + 16, mx - padX), y2: Math.max(padY + 16, my - padY) },
    { x1: mx + padX, y1: padY, x2: width - padX, y2: Math.max(padY + 16, my - padY) },
    { x1: padX, y1: my + padY, x2: Math.max(padX + 16, mx - padX), y2: height - padY },
    { x1: mx + padX, y1: my + padY, x2: width - padX, y2: height - padY },
  ];
  return raw.map((rect, idx) => {
    const clamped = clampRect(rect, width, height);
    return { id: `region-q${idx + 1}`, source: clamped, dest: { ...clamped } };
  });
}

/**
 * Zero-dependency browser-side pixel segmentation for user-imported screenshots.
 * Computes Sobel edge gradients + adaptive thresholding + morphological dilation +
 * 4-connected BFS bounding boxes, without assigning synthetic semantic names.
 */
export function detectImageRegions(
  img: HTMLImageElement,
  naturalWidth: number,
  naturalHeight: number
): ExplodeBlock[] {
  const width = Math.max(32, Math.round(naturalWidth || img.naturalWidth || 800));
  const height = Math.max(32, Math.round(naturalHeight || img.naturalHeight || 480));

  try {
    const maxDim = 360;
    const scale = Math.min(1, maxDim / Math.max(width, height));
    const sw = Math.max(24, Math.round(width * scale));
    const sh = Math.max(24, Math.round(height * scale));

    const canvas = document.createElement('canvas');
    canvas.width = sw;
    canvas.height = sh;
    const ctx = canvas.getContext('2d', { willReadFrequently: true });
    if (!ctx) return fallbackQuadrants(width, height);

    ctx.drawImage(img, 0, 0, sw, sh);
    const { data } = ctx.getImageData(0, 0, sw, sh);
    const total = sw * sh;

    const luma = new Float32Array(total);
    for (let i = 0; i < total; i++) {
      const o = i * 4;
      luma[i] = 0.299 * data[o] + 0.587 * data[o + 1] + 0.114 * data[o + 2];
    }

    // Sobel gradient magnitude
    const grad = new Float32Array(total);
    let gradSum = 0;
    let gradSqSum = 0;
    for (let y = 1; y < sh - 1; y++) {
      for (let x = 1; x < sw - 1; x++) {
        const idx = y * sw + x;
        const gx =
          -luma[idx - sw - 1] +
          luma[idx - sw + 1] -
          2 * luma[idx - 1] +
          2 * luma[idx + 1] -
          luma[idx + sw - 1] +
          luma[idx + sw + 1];
        const gy =
          -luma[idx - sw - 1] -
          2 * luma[idx - sw] -
          luma[idx - sw + 1] +
          luma[idx + sw - 1] +
          2 * luma[idx + sw] +
          luma[idx + sw + 1];
        const mag = Math.hypot(gx, gy);
        grad[idx] = mag;
        gradSum += mag;
        gradSqSum += mag * mag;
      }
    }

    const innerCount = Math.max(1, (sw - 2) * (sh - 2));
    const mean = gradSum / innerCount;
    const variance = Math.max(0, gradSqSum / innerCount - mean * mean);
    const std = Math.sqrt(variance);
    const threshold = Math.max(26, mean + 0.65 * std);

    const edgeMask = new Uint8Array(total);
    for (let i = 0; i < total; i++) {
      if (grad[i] >= threshold) edgeMask[i] = 1;
    }

    // Small morphological dilation (radius ~4px) to group characters/controls into UI regions
    const radius = Math.max(2, Math.min(5, Math.round(Math.min(sw, sh) * 0.015)));
    const dilated = new Uint8Array(total);
    const horiz = new Uint8Array(total);
    for (let y = 0; y < sh; y++) {
      const rowOff = y * sw;
      for (let x = 0; x < sw; x++) {
        if (!edgeMask[rowOff + x]) continue;
        const xStart = Math.max(0, x - radius);
        const xEnd = Math.min(sw - 1, x + radius);
        for (let nx = xStart; nx <= xEnd; nx++) {
          horiz[rowOff + nx] = 1;
        }
      }
    }
    for (let y = 0; y < sh; y++) {
      const yStart = Math.max(0, y - radius);
      const yEnd = Math.min(sh - 1, y + radius);
      for (let x = 0; x < sw; x++) {
        if (!horiz[y * sw + x]) continue;
        for (let ny = yStart; ny <= yEnd; ny++) {
          dilated[ny * sw + x] = 1;
        }
      }
    }

    // 4-connected BFS connected components
    const visited = new Uint8Array(total);
    const queue = new Int32Array(total);
    const rawBoxes: PixelRect[] = [];

    for (let start = 0; start < total; start++) {
      if (!dilated[start] || visited[start]) continue;
      let head = 0;
      let tail = 0;
      queue[tail++] = start;
      visited[start] = 1;

      let minX = sw;
      let minY = sh;
      let maxX = 0;
      let maxY = 0;

      while (head < tail) {
        const cur = queue[head++];
        const cx = cur % sw;
        const cy = (cur / sw) | 0;
        if (cx < minX) minX = cx;
        if (cx > maxX) maxX = cx;
        if (cy < minY) minY = cy;
        if (cy > maxY) maxY = cy;

        const neighbors = [
          cx > 0 ? cur - 1 : -1,
          cx + 1 < sw ? cur + 1 : -1,
          cy > 0 ? cur - sw : -1,
          cy + 1 < sh ? cur + sw : -1,
        ];
        for (const nb of neighbors) {
          if (nb >= 0 && dilated[nb] && !visited[nb]) {
            visited[nb] = 1;
            queue[tail++] = nb;
          }
        }
      }

      const bw = maxX - minX + 1;
      const bh = maxY - minY + 1;
      const boxArea = bw * bh;
      const canvasArea = sw * sh;
      if (bw < sw * 0.02 || bh < sh * 0.02) continue;
      if (boxArea > canvasArea * 0.9 || (bw > sw * 0.95 && bh > sh * 0.95)) continue;

      rawBoxes.push({
        x1: (minX / sw) * width,
        y1: (minY / sh) * height,
        x2: ((maxX + 1) / sw) * width,
        y2: ((maxY + 1) / sh) * height,
      });
    }

    if (rawBoxes.length === 0) {
      return fallbackQuadrants(width, height);
    }

    // Sort larger boxes first then merge heavily overlapping boxes
    rawBoxes.sort((a, b) => areaOf(b) - areaOf(a));
    const merged: PixelRect[] = [];
    for (const candidate of rawBoxes) {
      const cArea = Math.max(1, areaOf(candidate));
      let absorbed = false;
      for (let i = 0; i < merged.length; i++) {
        const mArea = Math.max(1, areaOf(merged[i]));
        const inter = intersectArea(candidate, merged[i]);
        if (inter / Math.min(cArea, mArea) > 0.65) {
          merged[i] = {
            x1: Math.min(merged[i].x1, candidate.x1),
            y1: Math.min(merged[i].y1, candidate.y1),
            x2: Math.max(merged[i].x2, candidate.x2),
            y2: Math.max(merged[i].y2, candidate.y2),
          };
          absorbed = true;
          break;
        }
      }
      if (!absorbed) merged.push(candidate);
    }

    merged.sort((a, b) => (Math.abs(a.y1 - b.y1) > 24 ? a.y1 - b.y1 : a.x1 - b.x1));
    const finalBoxes = merged.slice(0, 14).map((box, index) => {
      const clamped = clampRect(box, width, height);
      return {
        id: `detected-${index + 1}`,
        source: clamped,
        dest: { ...clamped },
      };
    });

    return finalBoxes.length > 0 ? finalBoxes : fallbackQuadrants(width, height);
  } catch {
    return fallbackQuadrants(width, height);
  }
}
