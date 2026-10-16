(function (root, factory) {
  'use strict';
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  if (root) root.EditHereFeedback = api;
})(typeof window !== 'undefined' ? window : typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  const rectangle = r => r === null ? null : ({x1:r.x1, y1:r.y1, x2:r.x2, y2:r.y2});
  const sameRectangle = (a, b) => ['x1', 'y1', 'x2', 'y2'].every(key => Math.abs(a[key] - b[key]) < 1e-7);
  const sourceKey = r => JSON.stringify([r.x1, r.y1, r.x2, r.y2]);
  const IMAGE_VERSION = 'feedback-minimal-2';
  const IMAGE_SCHEMA = 'urn:edithere:feedback-minimal:2';
  const record = value => value !== null && typeof value === 'object' && !Array.isArray(value);
  const exactKeys = (value, keys) => record(value) && Object.keys(value).sort().join(',') === keys.slice().sort().join(',');
  function timestampWithZone(value) {
    if (typeof value !== 'string' || !/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2})$/.test(value) || !Number.isFinite(Date.parse(value))) return false;
    const [year, month, day] = value.slice(0, 10).split('-').map(Number);
    const monthEnd = new Date(0); monthEnd.setUTCFullYear(year, month, 0);
    return year > 0 && month > 0 && month <= 12 && day > 0 && day <= monthEnd.getUTCDate();
  }
  function productVersion(document) {
    let version = document.toolVersion;
    if (!version && typeof globalThis.document !== 'undefined') version = globalThis.document.querySelector('meta[name="edithere-version"]')?.content;
    // Node consumers run from source. Keep this lazy: the bundled MCP session
    // validator never needs the source tree or product version at runtime.
    if (!version && typeof module === 'object' && module.exports) {
      const cmake = require('node:fs').readFileSync(require('node:path').join(__dirname, '../../CMakeLists.txt'), 'utf8');
      version = /project\(\s*EditHere\s+VERSION\s+(\d+\.\d+\.\d+)\b/.exec(cmake)?.[1];
    }
    if (typeof version !== 'string' || !/^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$/.test(version)) throw new Error('EditHere build version is unavailable.');
    return version;
  }
  function imageMetadata(value) {
    if (!record(value)) throw new Error('Invalid image feedback.');
    if (!Object.hasOwn(value, 'schemaVersion')) return {};
    if (value.schemaVersion !== IMAGE_VERSION) throw new Error('Unsupported image feedback version.');
    if (value.$schema !== IMAGE_SCHEMA || !exactKeys(value.tool, ['name', 'version']) ||
        value.tool.name !== 'EditHere' ||
        typeof value.tool.version !== 'string' || !value.tool.version.trim() || value.tool.version.length > 100 ||
        !timestampWithZone(value.exportedAt)) {
      throw new Error('Invalid image feedback metadata.');
    }
    return {$schema:value.$schema, schemaVersion:value.schemaVersion, tool:{...value.tool}, exportedAt:value.exportedAt};
  }
  function annotationEntries(objects) {
    if (!Array.isArray(objects) || objects.length > 9704) throw new Error('Invalid feedback objects.');
    const ids = new Set(), numbers = new Set(), entries = [];
    for (const object of objects) {
      if (!exactKeys(object, ['source', 'movements', 'annotations', ...(Object.hasOwn(object || {}, 'id') ? ['id'] : [])]) ||
          !Array.isArray(object.movements) || !Array.isArray(object.annotations)) throw new Error('Invalid feedback object fields.');
      if (Object.hasOwn(object, 'id')) {
        if (typeof object.id !== 'string' || !object.id.trim() || object.id.length > 256 || ids.has(object.id)) throw new Error('Invalid or duplicate feedback object ID.');
        ids.add(object.id);
      }
      for (const annotation of object.annotations) {
        if (typeof annotation !== 'string' && !exactKeys(annotation, ['number', 'text'])) throw new Error('Invalid feedback annotation fields.');
        const text = typeof annotation === 'string' ? annotation : annotation.text;
        if (typeof text !== 'string' || !text.trim() || text.length > 10000) throw new Error('Invalid feedback annotation text.');
        const number = typeof annotation === 'string' ? null : annotation.number;
        if (typeof annotation !== 'string') {
          if (!Number.isInteger(number) || number < 1 || number > 1000 || numbers.has(number)) throw new Error('Invalid or duplicate feedback annotation number.');
          numbers.add(number);
        }
        entries.push({object, text, number});
        if (entries.length > 1000) throw new Error('Too many feedback annotations.');
      }
    }
    let next = 1;
    for (const entry of entries) if (entry.number === null) {
      while (numbers.has(next)) next++;
      entry.number = next++; numbers.add(entry.number);
    }
    return entries;
  }
  function normalizeImage(value) {
    const metadata = imageMetadata(value), modern = Object.hasOwn(value, 'schemaVersion');
    const objectFormat = Object.hasOwn(value, 'objects');
    const fields = [...Object.keys(metadata), ...(objectFormat ? ['objects'] : ['annotations', 'changes']),
      ...(Object.hasOwn(value, 'annotationSpace') ? ['annotationSpace'] : []), ...(Object.hasOwn(value, 'image') ? ['image'] : [])];
    if (!exactKeys(value, fields) || (modern && (!objectFormat || !Object.hasOwn(value, 'annotationSpace'))) ||
        Object.hasOwn(value, 'annotationSpace') && value.annotationSpace !== 'result') throw new Error('Invalid image feedback fields.');
    const base = {...metadata, annotationSpace:'result', ...(Object.hasOwn(value, 'image') ? {image:value.image} : {})};
    if (objectFormat) {
      if (!Array.isArray(value.objects)) throw new Error('Invalid feedback objects.');
      annotationEntries(value.objects);
      return {...base, objects:value.objects};
    }
    if (!Array.isArray(value.annotations) || value.annotations.length > 1000 || !Array.isArray(value.changes) || value.changes.length > 8704) throw new Error('Invalid legacy feedback lists.');
    const objects = value.changes.map(change => {
      if (!exactKeys(change, ['from', 'to'])) throw new Error('Invalid legacy feedback change.');
      return {source:change.from, movements:[{to:change.to}], annotations:[]};
    });
    for (const [index, annotation] of value.annotations.entries()) {
      if (!record(annotation)) throw new Error('Invalid legacy feedback annotation.');
      const kind = ['point', 'rectangle', 'change'].find(key => Object.hasOwn(annotation, key));
      if (!exactKeys(annotation, kind ? [kind, 'text'] : ['text'])) throw new Error('Invalid legacy feedback annotation fields.');
      if (kind === 'change') {
        if (!Number.isInteger(annotation.change) || annotation.change < 0 || annotation.change >= value.changes.length) throw new Error('Legacy annotation references a missing change.');
        objects[annotation.change].annotations.push({number:index + 1, text:annotation.text});
      } else {
        if (kind === 'point' && !exactKeys(annotation.point, ['x', 'y'])) throw new Error('Invalid legacy feedback point.');
        const source = kind === 'point' ? {x1:annotation.point.x, y1:annotation.point.y, x2:annotation.point.x, y2:annotation.point.y} : kind === 'rectangle' ? annotation.rectangle : null;
        objects.push({source, movements:[], annotations:[{number:index + 1, text:annotation.text}]});
      }
    }
    annotationEntries(objects);
    return {...base, objects};
  }

  // Matches app/model.cpp exportFeedback. Movements are the actual group
  // trajectories (layoutMovements), not merged or split rendered pixel pieces.
  // The caller supplies current result coordinates for ordinary notes and the
  // original movementSource for a note attached to a trajectory.
  function exportImage(document, embed = true, exportedAt = new Date().toISOString()) {
    const objects = [], bySource = new Map();
    function ensureEntry(source) {
      const key = sourceKey(source);
      if (!bySource.has(key)) {
        const entry = {source:rectangle(source), movements:[], annotations:[]};
        bySource.set(key, entry);
        objects.push(entry);
      }
      return bySource.get(key);
    }
    for (const movement of document.movements || []) {
      if (sameRectangle(movement.source, movement.destination)) continue;
      ensureEntry(movement.source).movements.push({to:rectangle(movement.destination)});
    }
    for (const [index, note] of (document.notes || []).entries()) {
      const annotation = {number:index + 1, text:note.text};
      if (note.movementSource) {
        const entry = ensureEntry(note.movementSource);
        if (note.text.trim()) entry.annotations.push(annotation);
      } else {
        // Independent notes stay independent even when their rectangles match.
        objects.push({source:rectangle(note.source), movements:[], annotations:[annotation]});
      }
    }
    return {$schema:IMAGE_SCHEMA, schemaVersion:IMAGE_VERSION,
      tool:{name:'EditHere', version:productVersion(document)}, exportedAt,
      annotationSpace:'result', objects:objects.map((object, index) => ({id:'o' + (index + 1), ...object})), ...(embed ? {image:document.image} : {})};
  }

  // Matches app/videoproject.cpp serializeVideoFeedback: chronological frames,
  // independently readable image feedback, and a lightweight root object index.
  function exportVideo(project, embed = true, exportedAt = new Date().toISOString()) {
    const documents = project.frames.filter(document => document.notes.length ||
      document.movements.some(movement => !sameRectangle(movement.source, movement.destination)))
      .slice().sort((a, b) => a.timestampUs - b.timestampUs);
    const frames = documents.map(document => ({
      id:document.id,
      timestampMs:Math.floor(document.timestampUs / 1000),
      timestampUs:document.timestampUs,
      imageFile:'frame-' + document.id + '.png',
      feedback:exportImage(document, embed, exportedAt)
    }));
    const objects = frames.flatMap(frame => frame.feedback.objects.map(object => ({
      id:object.id,
      source:rectangle(object.source),
      movements:object.movements.map(movement => ({to:rectangle(movement.to)})),
      annotations:object.annotations.slice(),
      timestampMs:frame.timestampMs,
      frameId:frame.id
    })));
    return {
      schemaVersion:'video-feedback-1', tool:'EditHere', exportedAt,
      video:{source:project.source, durationMs:project.durationMs,
        width:documents[0]?.width || 0, height:documents[0]?.height || 0},
      frames, objects
    };
  }

  // QJsonObject serializes keys in sorted order. Array order remains significant.
  function ordered(value) {
    if (Array.isArray(value)) return value.map(ordered);
    if (value !== null && typeof value === 'object') {
      return Object.fromEntries(Object.keys(value).sort().map(key => [key, ordered(value[key])]));
    }
    return value;
  }
  function serialize(feedback) {
    return JSON.stringify(ordered(feedback)) + '\n';
  }
  // Display-only metadata: the native feedback itself retains its exact keys,
  // array order and fractional coordinates. Both delivery modes use serialize.
  function summary(feedback,serialized=serialize(feedback)) {
    const objects=feedback.objects;
    const images=feedback.schemaVersion==='video-feedback-1'?feedback.frames.map(frame=>frame.feedback.image):[feedback.image];
    const byteLength=value=>new TextEncoder().encode(value).byteLength;
    return {schemaVersion:feedback.schemaVersion || 'feedback-minimal-1', objects:objects.length,annotations:objects.reduce((sum,object)=>sum+object.annotations.length,0),
      movements:objects.reduce((sum,object)=>sum+object.movements.length,0),
      bytes:byteLength(serialized),imageBytes:images.filter(image=>typeof image==='string').reduce((sum,image)=>sum+byteLength(image),0)};
  }
  const foldedImage = image => image.slice(0, image.indexOf(',')) + ',[图片编码已折叠]';

  // The desktop export dialog folds image bytes only in its readable preview.
  // Clipboard and file downloads must always use serialize(), never this text.
  function preview(feedback) {
    if (feedback.schemaVersion === 'video-feedback-1') {
      const readable = ordered(feedback);
      for (const frame of readable.frames) {
        if (Object.prototype.hasOwnProperty.call(frame.feedback, 'image')) {
          frame.feedback.image = foldedImage(frame.feedback.image);
        }
      }
      return JSON.stringify(readable, null, 4) + '\n';
    }
    const lines = ['{'];
    for (const [key, value] of Object.entries(ordered(feedback))) {
      if (key !== 'image' && key !== 'objects') lines.push('  ' + JSON.stringify(key) + ': ' + JSON.stringify(value) + ',');
    }
    if (Object.prototype.hasOwnProperty.call(feedback, 'image')) {
      lines.push('  "image": ' + JSON.stringify(foldedImage(feedback.image)) + ',');
    }
    lines.push('  "objects": [');
    feedback.objects.forEach((object, index) => {
      lines.push('    ' + JSON.stringify(ordered(object)) + (index + 1 < feedback.objects.length ? ',' : ''));
    });
    lines.push('  ]', '}');
    return lines.join('\n');
  }

  return {exportImage, exportVideo, serialize, preview, summary, imageMetadata, normalizeImage, annotationEntries};
});
