import Ajv2020 from 'ajv/dist/2020.js';
import addFormats from 'ajv-formats';
import schema from '../schema/feedback-v1.schema.json' with { type: 'json' };

const ajv = new Ajv2020({ allErrors: true, strict: false });
addFormats(ajv);
const validateSchema = ajv.compile(schema);
export const now = () => new Date().toISOString();
export const uid = () => crypto.randomUUID().replaceAll('-', '');
export const clone = value => structuredClone(value);
export const clamp = (value, min, max) => Math.max(min, Math.min(max, value));
export const area = r => (r.x2 - r.x1) * (r.y2 - r.y1);
export const contains = (r, x, y) => x >= r.x1 && x < r.x2 && y >= r.y1 && y < r.y2;
export const rectFromPoints = (a, b, width, height) => ({
  x1: clamp(Math.round(Math.min(a.x, b.x)), 0, width),
  y1: clamp(Math.round(Math.min(a.y, b.y)), 0, height),
  x2: clamp(Math.round(Math.max(a.x, b.x)), 0, width),
  y2: clamp(Math.round(Math.max(a.y, b.y)), 0, height),
});
export const manualTarget = () => ({ source: 'manual', label: '手动标注', controlType: null, automationId: null, method: 'user-selection', originalScreenBounds: null, clipped: false });
export function annotation(kind, geometry, target = manualTarget()) {
  return { id: uid(), number: 0, kind, point: kind === 'point' ? geometry : null, rectangle: kind === 'rectangle' ? geometry : null, comment: '', target: clone(target), createdAt: now(), updatedAt: now() };
}
export function renumber(notes) { notes.forEach((n, i) => { n.number = i + 1; }); return notes; }
export function validateDocument(doc) {
  if (!validateSchema(doc)) {
    const error = validateSchema.errors[0];
    throw new Error('项目格式不正确：' + (error.instancePath || '/') + ' ' + error.message);
  }
  const c = doc.capture;
  if (c.width * c.height > 32000000 || c.width > 32767 || c.height > 32767) throw new Error('图片过大，请使用不超过 3200 万像素的图片。');
  if (!/^[^\\/:*?"<>|\u0000-\u001f]+\.png$/i.test(c.imageFile) || c.imageFile === '.' || c.imageFile === '..') throw new Error('原图文件名不正确。');
  const ids = new Set();
  for (const [index, n] of doc.annotations.entries()) {
    if (ids.has(n.id) || n.number !== index + 1) throw new Error('批注 ID 或编号不正确。');
    ids.add(n.id);
    if (n.kind === 'point' && (n.point.x >= c.width || n.point.y >= c.height)) throw new Error('点标注超出原图范围。');
    if (n.kind === 'rectangle') {
      const r = n.rectangle;
      if (r.x2 <= r.x1 || r.y2 <= r.y1 || r.x2 > c.width || r.y2 > c.height) throw new Error('框选范围超出原图或面积为空。');
    }
  }
  return doc;
}
export function makeExport(doc, embed = false) {
  const result = {
    schemaVersion: '1.0.0', tool: 'Help2Design Capture', exportedAt: now(),
    capture: { ...clone(doc.info), pngBase64: embed ? bytesToBase64(doc.png) : null },
    annotations: renumber(clone(doc.notes)),
  };
  return validateDocument(result);
}
export function bytesToBase64(bytes) {
  let binary = '';
  for (let i = 0; i < bytes.length; i += 8192) binary += String.fromCharCode(...bytes.subarray(i, i + 8192));
  return btoa(binary);
}
export function base64ToBytes(text) {
  if (!/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(text)) throw new Error('原图 Base64 数据无效。');
  return Uint8Array.from(atob(text), c => c.charCodeAt(0));
}
export async function hash(bytes) { return Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), b => b.toString(16).padStart(2, '0')).join(''); }
export function checkPng(bytes) { if (![137,80,78,71,13,10,26,10].every((n, i) => bytes[i] === n)) throw new Error('项目原图必须是 PNG。'); }
export class CandidatePicker {
  levels = []; index = 0; anchor = null;
  get current() { return this.levels[this.index] ?? null; }
  reset() { this.levels = []; this.index = 0; this.anchor = null; }
  update(candidates, x, y) {
    const previous = this.current;
    const samePlace = this.anchor && Math.abs(this.anchor.x - x) <= 5 && Math.abs(this.anchor.y - y) <= 5;
    const available = candidates.filter(c => contains(c.bounds, x, y)).sort((a, b) => area(a.bounds) - area(b.bounds) || (a.target.source === 'uia' ? -1 : 1));
    const chain = [];
    for (const c of available) {
      const inner = chain.at(-1)?.bounds, r = c.bounds;
      if (!inner || (r.x1 <= inner.x1 && r.y1 <= inner.y1 && r.x2 >= inner.x2 && r.y2 >= inner.y2 && area(r) > area(inner))) chain.push(c);
    }
    this.levels = chain; this.index = 0;
    if (samePlace && previous) { const index = chain.findIndex(c => sameRect(c.bounds, previous.bounds)); if (index >= 0) this.index = index; }
    if (!samePlace) this.anchor = { x, y };
    return this.current;
  }
  step(direction) { this.index = clamp(this.index + Math.sign(direction), 0, Math.max(0, this.levels.length - 1)); return this.current; }
}
export const sameRect = (a, b) => a.x1 === b.x1 && a.y1 === b.y1 && a.x2 === b.x2 && a.y2 === b.y2;
export function moveGeometry(note, dx, dy, width, height, handle = 'move') {
  const n = clone(note); dx = Math.round(dx); dy = Math.round(dy);
  if (n.point) { n.point.x = clamp(n.point.x + dx, 0, width - 1); n.point.y = clamp(n.point.y + dy, 0, height - 1); }
  else {
    const r = n.rectangle;
    if (handle === 'move') { dx = clamp(dx, -r.x1, width - r.x2); dy = clamp(dy, -r.y1, height - r.y2); r.x1 += dx; r.x2 += dx; r.y1 += dy; r.y2 += dy; }
    else {
      if (handle.includes('w')) r.x1 = clamp(r.x1 + dx, 0, r.x2 - 1);
      if (handle.includes('e')) r.x2 = clamp(r.x2 + dx, r.x1 + 1, width);
      if (handle.includes('n')) r.y1 = clamp(r.y1 + dy, 0, r.y2 - 1);
      if (handle.includes('s')) r.y2 = clamp(r.y2 + dy, r.y1 + 1, height);
    }
  }
  n.updatedAt = now(); return n;
}
export class History {
  undoStack = []; redoStack = [];
  push(notes) { this.undoStack.push(clone(notes)); if (this.undoStack.length > 100) this.undoStack.shift(); this.redoStack = []; }
  undo(notes) { if (!this.undoStack.length) return notes; this.redoStack.push(clone(notes)); return this.undoStack.pop(); }
  redo(notes) { if (!this.redoStack.length) return notes; this.undoStack.push(clone(notes)); return this.redoStack.pop(); }
  clear() { this.undoStack = []; this.redoStack = []; }
}
