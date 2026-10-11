(function (root, factory) {
  'use strict';
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  if (root) root.EditHereLayout = api;
})(typeof window !== 'undefined' ? window : typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  // Pieces partition the original image. Groups share IDs, never duplicate pixels.
  const EPSILON = 1e-7;
  const MAX_PIECES = 4096;
  const MAX_GROUPS = 512;
  let serial = 0;
  const copy = r => ({ x1: r.x1, y1: r.y1, x2: r.x2, y2: r.y2 });
  const width = r => r.x2 - r.x1;
  const height = r => r.y2 - r.y1;
  const area = r => width(r) * height(r);
  const positive = r => r && width(r) > EPSILON && height(r) > EPSILON;
  const close = (a, b) => Math.abs(a - b) <= EPSILON;
  const same = (a, b) => close(a.x1, b.x1) && close(a.y1, b.y1) && close(a.x2, b.x2) && close(a.y2, b.y2);
  const contains = (r, x, y) => x >= r.x1 && x < r.x2 && y >= r.y1 && y < r.y2;
  const full = state => ({ x1: 0, y1: 0, x2: state.width, y2: state.height });

  function require(condition, message) {
    if (!condition) throw new Error(message);
  }
  function readRect(value, allowEmpty) {
    require(value && typeof value === 'object', '布局坐标格式不正确');
    const r = copy(value);
    require(Object.values(r).every(v => typeof v === 'number' && Number.isFinite(v) && Math.abs(v) <= 1000000), '布局坐标必须是有限数值');
    if (!allowEmpty) require(positive(r), '布局区域尺寸必须为正');
    return r;
  }
  function fits(r, w, h) {
    return positive(r) && r.x1 >= -EPSILON && r.y1 >= -EPSILON && r.x2 <= w + EPSILON && r.y2 <= h + EPSILON;
  }
  function intersect(a, b) {
    const r = { x1: Math.max(a.x1, b.x1), y1: Math.max(a.y1, b.y1), x2: Math.min(a.x2, b.x2), y2: Math.min(a.y2, b.y2) };
    return positive(r) ? r : null;
  }
  function unite(a, b) {
    if (!a) return copy(b);
    return { x1: Math.min(a.x1, b.x1), y1: Math.min(a.y1, b.y1), x2: Math.max(a.x2, b.x2), y2: Math.max(a.y2, b.y2) };
  }
  // Four rectangles outside the intersection plus the intersection partition a.
  function outside(a, intersection) {
    return [
      { x1: a.x1, y1: a.y1, x2: a.x2, y2: intersection.y1 },
      { x1: a.x1, y1: intersection.y2, x2: a.x2, y2: a.y2 },
      { x1: a.x1, y1: intersection.y1, x2: intersection.x1, y2: intersection.y2 },
      { x1: intersection.x2, y1: intersection.y1, x2: a.x2, y2: intersection.y2 }
    ].filter(positive);
  }
  function mapRect(r, before, after) {
    const sx = width(after) / width(before), sy = height(after) / height(before);
    return {
      x1: after.x1 + (r.x1 - before.x1) * sx,
      y1: after.y1 + (r.y1 - before.y1) * sy,
      x2: after.x1 + (r.x2 - before.x1) * sx,
      y2: after.y1 + (r.y2 - before.y1) * sy
    };
  }
  function identifier(state, prefix) {
    let id;
    do { id = prefix + '-' + (++serial); }
    while (state.pieces.some(p => p.id === id) || state.groups.some(g => g.id === id));
    return id;
  }
  function orderedPieces(state) {
    // Match feedback reconstruction: original components are always background.
    return state.pieces.filter(p => same(p.source, p.to)).concat(state.pieces.filter(p => !same(p.source, p.to)));
  }
  function visiblePart(piece, region, covers) {
    const intersection = intersect(piece.to, region);
    if (!intersection) return [];
    let parts = [intersection];
    for (const cover of covers) {
      const next = [];
      for (const part of parts) {
        const overlap = intersect(part, cover.to);
        if (overlap) next.push(...outside(part, overlap));
        else next.push(part);
      }
      parts = next;
      require(parts.length <= MAX_PIECES, '区域过于复杂，请减少手动切分');
      if (!parts.length) break;
    }
    return parts;
  }
  function cut(state, region) {
    const drawOrder = orderedPieces(state);
    const visible = new Map();
    for (let i = 0; i < drawOrder.length; ++i) {
      visible.set(drawOrder[i].id, visiblePart(drawOrder[i], region, drawOrder.slice(i + 1)));
    }
    const next = [], replacements = new Map(), selected = [];
    for (const piece of state.pieces) {
      const regions = visible.get(piece.id);
      if (!regions.length) {
        next.push(piece);
        continue;
      }
      let fragments = [{ to: piece.to, selected: false }];
      for (const selection of regions) {
        const split = [];
        for (const fragment of fragments) {
          if (fragment.selected) { split.push(fragment); continue; }
          const overlap = intersect(fragment.to, selection);
          if (!overlap) split.push(fragment);
          else {
            split.push(...outside(fragment.to, overlap).map(to => ({ to, selected: false })));
            split.push({ to: overlap, selected: true });
          }
        }
        fragments = split;
        require(next.length + fragments.length <= MAX_PIECES, '区域过于复杂，请减少手动切分');
      }
      if (fragments.length === 1 && same(fragments[0].to, piece.to)) {
        next.push(piece);
        selected.push(piece.id);
      } else {
        const ids = [];
        for (const fragment of fragments) {
          const id = identifier(state, 'piece');
          next.push({ id, source: mapRect(fragment.to, piece.to, piece.source), to: copy(fragment.to) });
          ids.push(id);
          if (fragment.selected) selected.push(id);
        }
        replacements.set(piece.id, ids);
      }
      require(next.length <= MAX_PIECES, '区域过于复杂，请减少手动切分');
    }
    const groups = state.groups.map(group => ({
      id: group.id,
      label: group.label,
      pieces: group.pieces.flatMap(id => replacements.get(id) || [id])
    }));
    return { pieces: next, groups, selected };
  }
  function create(w, h, candidates) {
    require(Number.isInteger(w) && Number.isInteger(h) && w > 0 && h > 0 && w <= 1000000 && h <= 1000000 && w * h <= 100000000, '图片尺寸不正确');
    const state = { width: w, height: h, pieces: [], groups: [] };
    const canvas = full(state);
    state.pieces.push({ id: identifier(state, 'piece'), source: copy(canvas), to: copy(canvas) });
    const sorted = (Array.isArray(candidates) ? candidates : []).slice().sort((a, b) => {
      const aa = a && a.bounds, bb = b && b.bounds;
      return aa && bb ? area(aa) - area(bb) : 0;
    });
    const seen = [];
    for (const candidate of sorted) {
      if (state.groups.length >= MAX_GROUPS) break;
      let r;
      try { r = intersect(readRect(candidate.bounds, true), canvas); }
      catch (_) { continue; }
      // Detector background boxes must not become a selectable whole-image group.
      if (!r || area(r) >= w * h * 0.85 || seen.some(other => same(other, r))) continue;
      let id;
      try { id = addRegion(state, r); }
      catch (_) { continue; }
      if (id) {
        const label = candidate.label || (candidate.target && candidate.target.label) || '检测区域 ' + state.groups.length;
        state.groups[state.groups.length - 1].label = String(label).slice(0, 1000);
        seen.push(r);
      }
    }
    return state;
  }
  function addRegion(state, rectangle) {
    require(state.groups.length < MAX_GROUPS, '最多支持 512 个可选区域');
    const requested = readRect(rectangle, true);
    const normalized = {
      x1: Math.min(requested.x1, requested.x2), y1: Math.min(requested.y1, requested.y2),
      x2: Math.max(requested.x1, requested.x2), y2: Math.max(requested.y1, requested.y2)
    };
    const region = intersect(normalized, full(state));
    if (!region) return null;
    const result = cut(state, region);
    if (!result.selected.length) return null;
    const id = identifier(state, 'group');
    result.groups.push({ id, label: '手动区域 ' + (state.groups.length + 1), pieces: result.selected });
    state.pieces = result.pieces;
    state.groups = result.groups;
    return id;
  }
  function bounds(state, groupId) {
    const group = state.groups.find(g => g.id === groupId);
    if (!group) return null;
    const ids = new Set(group.pieces);
    let result = null;
    for (const piece of state.pieces) if (ids.has(piece.id)) result = unite(result, piece.to);
    return result;
  }
  function constrain(r, state) {
    const w = Math.min(width(r), state.width), h = Math.min(height(r), state.height);
    const x = Math.max(0, Math.min(r.x1, state.width - w));
    const y = Math.max(0, Math.min(r.y1, state.height - h));
    return { x1: x, y1: y, x2: x + w, y2: y + h };
  }
  function transform(state, groupId, destination) {
    const group = state.groups.find(g => g.id === groupId);
    const old = bounds(state, groupId);
    if (!group || !old) return null;
    const actual = constrain(readRect(destination, false), state);
    if (same(old, actual)) return actual;
    const selected = new Set(group.pieces), background = [], foreground = [];
    for (const piece of state.pieces) {
      if (!selected.has(piece.id)) background.push(piece);
      else {
        const to = mapRect(piece.to, old, actual);
        require(positive(to) && fits(to, state.width, state.height), '缩放后的区域过小');
        foreground.push({ id: piece.id, source: copy(piece.source), to });
      }
    }
    state.pieces = background.concat(foreground);
    // A moved child becomes independent of strict parents. Nested children of
    // the selected group and partially overlapping groups retain their refs.
    for (const parent of state.groups) {
      if (parent.id === groupId || parent.pieces.length <= selected.size) continue;
      const members = new Set(parent.pieces);
      if (Array.from(selected).every(id => members.has(id))) {
        parent.pieces = parent.pieces.filter(id => !selected.has(id));
      }
    }
    return bounds(state, groupId);
  }
  function paint(ctx, image, state) {
    ctx.save();
    try {
      if (typeof ctx.setTransform === 'function') ctx.setTransform(1, 0, 0, 1, 0, 0);
      ctx.clearRect(0, 0, state.width, state.height);
      ctx.globalAlpha = 1;
      ctx.globalCompositeOperation = 'source-over';
      ctx.beginPath();
      ctx.rect(0, 0, state.width, state.height);
      ctx.clip();
      for (const piece of orderedPieces(state)) {
        const s = piece.source, d = piece.to;
        ctx.drawImage(image, s.x1, s.y1, width(s), height(s), d.x1, d.y1, width(d), height(d));
      }
    } finally { ctx.restore(); }
  }
  function sameTransform(a, b) {
    return same(mapRect(b.source, a.source, a.to), b.to);
  }
  function adjacent(a, b, horizontal) {
    return horizontal
      ? close(a.y1, b.y1) && close(a.y2, b.y2) && close(a.x2, b.x1)
      : close(a.x1, b.x1) && close(a.x2, b.x2) && close(a.y2, b.y1);
  }
  function mergeKeepsOrder(regions, a, b) {
    const earlier = a.layer < b.layer ? a : b, later = a.layer < b.layer ? b : a;
    return !regions.some(other => other !== a && other !== b && other.active && other.layer > earlier.layer && other.layer < later.layer && intersect(earlier.to, other.to));
  }
  function movements(state, annotationsForPiece) {
    require(annotationsForPiece === undefined || typeof annotationsForPiece === 'function', '切片批注回调格式不正确');
    const regions = orderedPieces(state).map((piece, layer) => {
      if (same(piece.source, piece.to)) return null;
      const annotations = annotationsForPiece ? annotationsForPiece(piece) : [];
      require(Array.isArray(annotations) && annotations.every(text => typeof text === 'string'), '切片批注格式不正确');
      return { source: copy(piece.source), to: copy(piece.to), layer, active: true, annotations: annotations.slice() };
    }).filter(Boolean);
    let merged;
    do {
      merged = false;
      for (const horizontal of [true, false]) {
        const order = regions.filter(r => r.active).sort((a, b) => horizontal
          ? a.source.y1 - b.source.y1 || height(a.source) - height(b.source) || a.source.x1 - b.source.x1
          : a.source.x1 - b.source.x1 || width(a.source) - width(b.source) || a.source.y1 - b.source.y1);
        let previous = null;
        for (const current of order) {
          if (previous && adjacent(previous.source, current.source, horizontal) && sameTransform(previous, current) && previous.annotations.length === current.annotations.length && previous.annotations.every((text, i) => text === current.annotations[i]) && mergeKeepsOrder(regions, previous, current)) {
            previous.source = unite(previous.source, current.source);
            previous.to = unite(previous.to, current.to);
            previous.layer = Math.max(previous.layer, current.layer);
            current.active = false;
            merged = true;
          } else previous = current;
        }
      }
    } while (merged);
    return regions.filter(r => r.active).sort((a, b) => a.layer - b.layer).map(r => ({ source: r.source, movements: [{ to: r.to }], annotations: r.annotations.slice() }));
  }
  function hit(state, x, y) {
    if (!Number.isFinite(x) || !Number.isFinite(y)) return [];
    const top = topmost(orderedPieces(state), x, y);
    if (!top) return [];
    return state.groups.filter(group => group.pieces.includes(top.id)).map(group => ({ id: group.id, label: group.label, bounds: bounds(state, group.id) })).filter(choice => choice.bounds).sort((a, b) => area(a.bounds) - area(b.bounds));
  }
  function topmost(order, x, y) {
    for (let i = order.length - 1; i >= 0; --i) if (contains(order[i].to, x, y)) return order[i];
    return null;
  }
  function delta(before, after) {
    const sx = width(after.to) / width(before.to), sy = height(after.to) / height(before.to);
    return { sx, sy, tx: after.to.x1 - before.to.x1 * sx, ty: after.to.y1 - before.to.y1 * sy };
  }
  function applyDelta(r, change) {
    return {
      x1: r.x1 * change.sx + change.tx, y1: r.y1 * change.sy + change.ty,
      x2: r.x2 * change.sx + change.tx, y2: r.y2 * change.sy + change.ty
    };
  }
  function remapNotes(notes, before, after) {
    require(Array.isArray(notes), '批注格式不正确');
    const result = JSON.parse(JSON.stringify(notes));
    const order = orderedPieces(before), destinations = new Map(after.pieces.map(piece => [piece.id, piece]));
    for (const note of result) {
      // Component notes retain their membership and the UI resolves live bounds.
      if (note.source === null || note.groupId) continue;
      const r = readRect(note.source, true);
      if (r.x1 === r.x2 && r.y1 === r.y2) {
        const piece = topmost(order, r.x1, r.y1), next = piece && destinations.get(piece.id);
        if (!next) continue;
        const mapped = applyDelta(r, delta(piece, next));
        const x = Math.max(0, Math.min(mapped.x1, after.width - EPSILON));
        const y = Math.max(0, Math.min(mapped.y1, after.height - EPSILON));
        note.source = { x1: x, y1: y, x2: x, y2: y };
        continue;
      }
      if (!(r.x2 > r.x1 && r.y2 > r.y1)) continue;
      let uncovered = [r], common = null, compatible = true;
      // Account for each visible part once, using the same layering as paint().
      for (let i = order.length - 1; i >= 0 && uncovered.length; --i) {
        const piece = order[i], next = destinations.get(piece.id), remaining = [];
        for (const part of uncovered) {
          const overlap = intersect(part, piece.to);
          if (!overlap) { remaining.push(part); continue; }
          if (!next) { compatible = false; break; }
          const change = delta(piece, next);
          if (common && !same(applyDelta(r, common), applyDelta(r, change))) { compatible = false; break; }
          if (!common) common = change;
          remaining.push(...outside(part, overlap));
        }
        if (!compatible) break;
        uncovered = remaining;
      }
      // A rectangle spanning a hole or differently transformed components has
      // no single meaningful rectangle to follow, so keep its existing anchor.
      if (!compatible || uncovered.length || !common) continue;
      const mapped = applyDelta(r, common);
      mapped.x1 = Math.max(0, Math.min(after.width, mapped.x1));
      mapped.y1 = Math.max(0, Math.min(after.height, mapped.y1));
      mapped.x2 = Math.max(0, Math.min(after.width, mapped.x2));
      mapped.y2 = Math.max(0, Math.min(after.height, mapped.y2));
      // Keep fractional coordinates: rounding can collapse only one dimension.
      if (mapped.x2 > mapped.x1 && mapped.y2 > mapped.y1) note.source = mapped;
    }
    return result;
  }
  function validate(state, w, h) {
    require(state && Number.isInteger(state.width) && Number.isInteger(state.height) && state.width > 0 && state.height > 0, '布局大小不正确');
    if (w === undefined) w = state.width;
    if (h === undefined) h = state.height;
    require(state.width === w && state.height === h && Array.isArray(state.pieces) && state.pieces.length > 0 && state.pieces.length <= MAX_PIECES && Array.isArray(state.groups) && state.groups.length <= MAX_GROUPS, '布局大小或数量不正确');
    const ids = new Set(), sources = [];
    let total = 0;
    for (const piece of state.pieces) {
      const source = readRect(piece.source, false), to = readRect(piece.to, false);
      require(typeof piece.id === 'string' && piece.id.length > 0 && !ids.has(piece.id) && fits(source, w, h) && fits(to, w, h), '布局切片坐标或编号不正确');
      ids.add(piece.id);
      total += area(source);
      sources.push(source);
    }
    sources.sort((a, b) => a.x1 - b.x1);
    for (let i = 0; i < sources.length; ++i) {
      for (let j = i + 1; j < sources.length && sources[j].x1 < sources[i].x2 - EPSILON; ++j) {
        require(!intersect(sources[i], sources[j]), '布局切片重复使用原图像素');
      }
    }
    require(Math.abs(total - w * h) < Math.max(0.001, w * h * 1e-8), '布局切片未完整覆盖原图');
    const groupIds = new Set();
    for (const group of state.groups) {
      require(typeof group.id === 'string' && group.id.length > 0 && !groupIds.has(group.id) && typeof group.label === 'string' && group.label.length <= 1000 && Array.isArray(group.pieces) && group.pieces.length > 0 && group.pieces.length <= MAX_PIECES, '布局区域格式不正确');
      groupIds.add(group.id);
      const members = new Set();
      for (const id of group.pieces) {
        require(ids.has(id) && !members.has(id), '布局区域引用无效');
        members.add(id);
      }
    }
    return true;
  }
  return { create, addRegion, bounds, transform, paint, movements, hit, validate, remapNotes };
});
