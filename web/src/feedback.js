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

  // Matches app/model.cpp exportFeedback. Movements are the actual group
  // trajectories (layoutMovements), not merged or split rendered pixel pieces.
  // The caller supplies current result coordinates for ordinary notes and the
  // original movementSource for a note attached to a trajectory.
  function exportImage(document, embed = true) {
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
    for (const note of document.notes || []) {
      if (note.movementSource) {
        const entry = ensureEntry(note.movementSource);
        if (note.text.trim()) entry.annotations.push(note.text);
      } else {
        // Independent notes stay independent even when their rectangles match.
        objects.push({source:rectangle(note.source), movements:[], annotations:[note.text]});
      }
    }
    return {annotationSpace:'result', objects, ...(embed ? {image:document.image} : {})};
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
      feedback:exportImage(document, embed)
    }));
    const objects = frames.flatMap(frame => frame.feedback.objects.map(object => ({
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
    return {objects:objects.length,annotations:objects.reduce((sum,object)=>sum+object.annotations.length,0),
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
    if (Object.prototype.hasOwnProperty.call(feedback, 'image')) {
      lines.push('  "image": ' + JSON.stringify(foldedImage(feedback.image)) + ',');
    }
    lines.push('  "annotationSpace": "result",', '  "objects": [');
    feedback.objects.forEach((object, index) => {
      lines.push('    ' + JSON.stringify(ordered(object)) + (index + 1 < feedback.objects.length ? ',' : ''));
    });
    lines.push('  ]', '}');
    return lines.join('\n');
  }

  return {exportImage, exportVideo, serialize, preview, summary};
});
