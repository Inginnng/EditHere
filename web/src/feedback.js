(function (root, factory) {
  'use strict';

  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  if (root) root.EditHereFeedback = api;
})(typeof window !== 'undefined' ? window : typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  const imageFormat=typeof module==='object'&&module.exports?require('./image-format.js'):globalThis.EditHereImage;

  const rectangle = r => r === null ? null : ({x1:r.x1, y1:r.y1, x2:r.x2, y2:r.y2});
  const sameRectangle = (a, b) => ['x1', 'y1', 'x2', 'y2'].every(key => Math.abs(a[key] - b[key]) < 1e-7);
  const sourceKey = r => JSON.stringify([r.x1, r.y1, r.x2, r.y2]);
  const IMAGE_VERSION = 'feedback-minimal-4';
  const IMAGE_SCHEMA = 'urn:edithere:feedback-minimal:4';
  // v3 files predate child annotations; they stay readable with the same object shape.
  const PREVIOUS_VERSIONS = {'feedback-minimal-3':'urn:edithere:feedback-minimal:3', 'feedback-minimal-2':'urn:edithere:feedback-minimal:2'};
  // Typed object versions store {kind, source|point, movement}; v2 stores the legacy shape.
  const isTypedVersion = version => version === IMAGE_VERSION || version === 'feedback-minimal-3';
  // Every versioned image feedback carries an image descriptor instead of a bare data URL.
  const isImageVersion = version => version === IMAGE_VERSION || Object.hasOwn(PREVIOUS_VERSIONS, version);
  const record = value => value !== null && typeof value === 'object' && !Array.isArray(value);
  const exactKeys = (value, keys) => record(value) && Object.keys(value).sort().join(',') === keys.slice().sort().join(',');
  const closeNumber = (actual, expected) => Number.isFinite(actual) && Math.abs(actual - expected) <= Math.max(1e-7, Math.abs(expected) * 1e-9);
  const feedbackScale = value => Math.max(.01, Math.floor(value * 100 + .5 + 1e-9) / 100);
  function feedbackRectangle(rect, bounds) {
    if (rect === null) return null;
    if (rect.x1 === rect.x2 && rect.y1 === rect.y2) {
      const x=Math.round(rect.x1),y=Math.round(rect.y1);
      return {x1:x,y1:y,x2:x,y2:y};
    }
    const x1=Math.min(bounds.width-1,Math.max(0,Math.round(rect.x1)));
    const y1=Math.min(bounds.height-1,Math.max(0,Math.round(rect.y1)));
    return {x1,y1,x2:Math.min(bounds.width,Math.max(x1+1,Math.round(rect.x2))),
      y2:Math.min(bounds.height,Math.max(y1+1,Math.round(rect.y2)))};
  }
  function feedbackDestination(original, source, destination, bounds) {
    const rect=feedbackRectangle(destination,bounds);
    for (const [start,end,limit] of [['x1','x2',bounds.width],['y1','y2',bounds.height]]) {
      const size=source[end]-source[start];
      if (feedbackScale((destination[end]-destination[start])/(original[end]-original[start]))===1 ||
          feedbackScale((rect[end]-rect[start])/size)===1) {
        rect[start]=Math.min(rect[start],limit-size);
        rect[end]=rect[start]+size;
      }
    }
    return rect;
  }
  function movementResult(source, rect, compact = true) {
    const result={rect:rectangle(rect), dx:rect.x1-source.x1, dy:rect.y1-source.y1};
    for (const [key,start,end] of [['scaleX','x1','x2'],['scaleY','y1','y2']]) {
      const ratio=(rect[end]-rect[start])/(source[end]-source[start]);
      const scale=compact?feedbackScale(ratio):ratio;
      if (!compact || scale!==1) result[key]=scale;
    }
    return result;
  }
  function typedObject(object) {
    const {source, movements = [], recognizedText, result, children, ...value} = object;
    const nested = children === undefined ? {} : {children:children.map(typedObject)};
    if (recognizedText) value.ocrtext = recognizedText.text;
    // The nested child list always trails the parent object.
    if (source === null) return {...value, kind:'global', ...nested};
    if (source.x1 === source.x2 && source.y1 === source.y2) {
      if (![source.x1, source.y1].every(value => Number.isFinite(value) && value >= 0 && value <= 32767 && Number.isInteger(value))) throw new Error('Invalid feedback point.');
      return {...value, kind:'point', point:{x:source.x1,y:source.y1}, ...nested};
    }
    return {...value, kind:'region', source:rectangle(source),
      ...(movements.length ? {movement:movementResult(source,movements.at(-1).to)} : {}), ...nested};
  }
  // Preserve object identity: the importer maps each object to its layout group.
  function flattenObjects(objects) {
    const flat = [], pending = (Array.isArray(objects) ? objects : []).slice().reverse().map(object => ({object, child: false}));
    while (pending.length) {
      const {object, child} = pending.pop();
      if (!record(object)) throw new Error('Invalid feedback object.');
      const {children} = object;
      if (children !== undefined) {
        if (!Array.isArray(children) || !children.length || children.length > 1000) throw new Error('Invalid child feedback list.');
        if (object.kind !== 'region') throw new Error('Only a boxed region can own child annotations.');
        for (let i=children.length-1;i>=0;i--) pending.push({object:children[i],child:true});
      }
      if (child) {
        if (object.kind !== 'region') throw new Error('Child feedback must be a boxed region.');
      }
      flat.push(object);
      if (flat.length > 9704) throw new Error('Invalid feedback objects.');
    }
    return flat;
  }
  // Internal bridge to the established layout importer. Never write this shape
  // under a v3 version, and validate derived numbers before removing them.
  function legacyObject(object) {
    if (!record(object) || !Object.hasOwn(object,'kind')) return object;
    const fields=['kind','annotations',...['id','recognizedText','ocrtext','movements','movement','children'].filter(key=>Object.hasOwn(object,key))];
    if (Object.hasOwn(object,'ocrtext') && Object.hasOwn(object,'recognizedText')) throw new Error('Invalid recognized screenshot text fields.');
    const singleMovement=Object.hasOwn(object,'movement');
    if(singleMovement&&(!record(object.movement)||Object.hasOwn(object,'movements')||Object.hasOwn(object,'result'))) throw new Error('Invalid movement fields.');
    const movements = singleMovement ? [{to:object.movement.rect}] : Object.hasOwn(object,'movements') ? object.movements : [];
    if (!Array.isArray(movements) || !Array.isArray(object.annotations)) throw new Error('Invalid feedback object lists.');
    const coordinate=value=>Number.isFinite(value)&&value>=0&&value<=32767;
    const rect=value=>exactKeys(value,['x1','y1','x2','y2'])&&Object.values(value).every(coordinate)&&value.x2-value.x1>1e-7&&value.y2-value.y1>1e-7;
    if (object.kind==='region') {
      fields.push('source');
      if (!rect(object.source)) throw new Error('Invalid region source.');
      for (const movement of movements) if (!exactKeys(movement,['to'])||!rect(movement.to)) throw new Error('Invalid movement target.');
      if (movements.length) {
        if(!singleMovement) fields.push('result');
        const expected=movementResult(object.source,movements.at(-1).to,false),actual=singleMovement?object.movement:object.result;
        if (!record(actual) || !exactKeys(actual,['rect','dx','dy',...['scaleX','scaleY'].filter(key=>Object.hasOwn(actual,key))])||!rect(actual.rect)||
            ['x1','y1','x2','y2'].some(key=>!closeNumber(actual.rect[key],expected.rect[key]))||
            ['dx','dy'].some(key=>!closeNumber(actual[key],expected[key])) ||
            ['scaleX','scaleY'].some(key=>Object.hasOwn(actual,key)
              ? !closeNumber(actual[key],expected[key])&&!closeNumber(actual[key],feedbackScale(expected[key]))
              : feedbackScale(expected[key])!==1)) throw new Error('Movement result does not match its final target.');
      }
    } else if (object.kind==='point') {
      fields.push('point');
      if (!exactKeys(object.point,['x','y'])||!Object.values(object.point).every(value=>coordinate(value)&&Number.isInteger(value))) throw new Error('Invalid feedback point.');
    } else if (object.kind!=='global') throw new Error('Invalid feedback kind.');
    if (!exactKeys(object,fields)) throw new Error('Invalid typed feedback object fields.');
    if (object.kind!=='region'&&movements.length) throw new Error('Point and global feedback cannot move.');
    if (object.kind==='global'&&(Object.hasOwn(object,'recognizedText')||Object.hasOwn(object,'ocrtext'))) throw new Error('Global feedback cannot contain recognized screenshot text.');
    const {kind,point,result,movement,ocrtext,children,...value}=object;
    if (Object.hasOwn(object,'ocrtext')) value.recognizedText = validateRecognizedText({text:ocrtext,engine:'imported'});
    return {...value,movements,source:kind==='global'?null:kind==='point'?{x1:point.x,y1:point.y,x2:point.x,y2:point.y}:rectangle(value.source)};
  }
  function legacyImage(value) {
    const normalized=normalizeImage(value);
    if (normalized.schemaVersion!==IMAGE_VERSION && normalized.schemaVersion!=='feedback-minimal-3') return normalized;
    return {...normalized,$schema:'urn:edithere:feedback-minimal:2',schemaVersion:'feedback-minimal-2',objects:flattenObjects(normalized.objects).map(legacyObject)};
  }
  function timestampWithZone(value) {
    if (typeof value !== 'string' || !/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d{1,9})?(?:Z|[+-]\d{2}:\d{2})$/.test(value) || !Number.isFinite(Date.parse(value))) return false;
    const [year, month, day] = value.slice(0, 10).split('-').map(Number);
    const [hour, minute, second] = value.slice(11,19).split(':').map(Number);
    const monthEnd = new Date(0); monthEnd.setUTCFullYear(year, month, 0);
    return year > 0 && month > 0 && month <= 12 && day > 0 && day <= monthEnd.getUTCDate() && hour < 24 && minute < 60 && second < 60;
  }
  function validatePageContext(value) {
    if (typeof value !== 'string' || !value.trim() || value.trim().length > 500 || value.includes('\u0000')) throw new Error('Invalid page context.');
    return value.trim();
  }
  // Capture context describes the source image. Page names are caller-supplied
  // data, never instructions or a claim that EditHere recognized a page.
  function validateCapture(value) {
    const optional = ['fileName','windowTitle','appName','page','pageSource'];
    if (!record(value) || !exactKeys(value, ['source','capturedAt',...optional.filter(key => Object.hasOwn(value,key))]) ||
        !['screen','file','clipboard','demo','scroll','history','pin','drop','video'].includes(value.source) || !timestampWithZone(value.capturedAt)) throw new Error('Invalid capture context.');
    if (Object.hasOwn(value,'fileName') && (typeof value.fileName !== 'string' || !value.fileName.length || value.fileName.length > 1024 || ['.','..'].includes(value.fileName) || /[\\/\u0000]/.test(value.fileName))) throw new Error('Invalid capture filename.');
    for (const key of ['windowTitle','appName']) if (Object.hasOwn(value,key) && (typeof value[key] !== 'string' || value[key].length > 1000 || value[key].includes('\u0000'))) throw new Error('Invalid capture application context.');
    if (Object.hasOwn(value,'page') !== Object.hasOwn(value,'pageSource') || Object.hasOwn(value,'page') && (typeof value.page !== 'string' || !value.page.trim() || value.page.length > 500 || value.page.includes('\u0000') || value.pageSource !== 'agent')) throw new Error('Invalid capture page context.');
    return value;
  }
  function productVersion(document) {
    let version = document.toolVersion;
    if (!version && typeof EDITHERE_PRODUCT_VERSION !== 'undefined') version = EDITHERE_PRODUCT_VERSION;
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
    const capture = Object.hasOwn(value,'capture') ? {capture:{...validateCapture(value.capture)}} : {};
    if (!Object.hasOwn(value, 'schemaVersion')) {
      if (Object.hasOwn(value,'capture')) throw new Error('Capture context requires feedback-minimal-2.');
      return {};
    }
    if (![IMAGE_VERSION,'feedback-minimal-3','feedback-minimal-2'].includes(value.schemaVersion)) throw new Error('Unsupported image feedback version.');
    if ((value.schemaVersion==='feedback-minimal-2' || Object.hasOwn(value,'$schema')) && value.$schema !== (value.schemaVersion===IMAGE_VERSION?IMAGE_SCHEMA:PREVIOUS_VERSIONS[value.schemaVersion]) || !exactKeys(value.tool, ['name', 'version']) ||
        value.tool.name !== 'EditHere' ||
        typeof value.tool.version !== 'string' || !value.tool.version.trim() || value.tool.version.length > 100 ||
        (value.schemaVersion!==IMAGE_VERSION || Object.hasOwn(value,'exportedAt')) && !timestampWithZone(value.exportedAt)) {
      throw new Error('Invalid image feedback metadata.');
    }
    return {...(Object.hasOwn(value,'$schema') ? {$schema:value.$schema} : {}), schemaVersion:value.schemaVersion, tool:{...value.tool}, ...(Object.hasOwn(value,'exportedAt') ? {exportedAt:value.exportedAt} : {}), ...capture};
  }
  // Screenshot text is evidence/data. It never becomes an annotation or an
  // executable instruction, and editing it does not invent a confidence score.
  function validateRecognizedText(value, project = false) {
    if (project && exactKeys(value, ['text', 'edited']) && value.text === '' && value.edited === true) return value;
    const fields = ['text', 'engine', ...['language', 'confidence', 'edited'].filter(key => Object.hasOwn(value || {}, key))];
    if (!exactKeys(value, fields) || typeof value.text !== 'string' || !value.text.trim() || value.text.length > 500 ||
        typeof value.engine !== 'string' || !value.engine.trim() || value.engine.length > 64 ||
        Object.hasOwn(value, 'language') && (typeof value.language !== 'string' || value.language.length > 100) ||
        Object.hasOwn(value, 'confidence') && (typeof value.confidence !== 'number' || !Number.isFinite(value.confidence) || value.confidence < 0 || value.confidence > 1) ||
        Object.hasOwn(value, 'edited') && typeof value.edited !== 'boolean') throw new Error('Invalid recognized screenshot text.');
    return value;
  }
  function annotationEntries(objects) {
    if (!Array.isArray(objects) || objects.length > 9704) throw new Error('Invalid feedback objects.');
    const ids = new Set(), numbers = new Set(), entries = [];
    for (const object of flattenObjects(objects)) {
      const legacy=legacyObject(object);
      if (!exactKeys(legacy, ['source', 'movements', 'annotations', ...(Object.hasOwn(legacy || {}, 'id') ? ['id'] : []), ...(Object.hasOwn(legacy || {}, 'recognizedText') ? ['recognizedText'] : [])]) ||
          !Array.isArray(legacy.movements) || !Array.isArray(legacy.annotations)) throw new Error('Invalid feedback object fields.');
      if (Object.hasOwn(legacy, 'recognizedText')) {
        if (legacy.source === null) throw new Error('Global feedback cannot contain recognized screenshot text.');
        validateRecognizedText(legacy.recognizedText);
      }
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
    if(Object.hasOwn(value,'image')&&typeof value.image!=='string')imageFormat.validate(value.image);
    if (objectFormat) {
      if (!Array.isArray(value.objects)) throw new Error('Invalid feedback objects.');
      if (value.schemaVersion!==IMAGE_VERSION && value.objects.some(object=>Object.hasOwn(object||{},'children'))) throw new Error('Child annotations require feedback-minimal-4.');
      if (isTypedVersion(value.schemaVersion) ? value.objects.some(object=>!record(object)||!Object.hasOwn(object,'kind')) : value.objects.some(object=>Object.hasOwn(object||{},'kind'))) throw new Error('Feedback object kinds require feedback-minimal-3 or newer.');
      if (!modern && value.objects.some(object => Object.hasOwn(object || {}, 'recognizedText') || Object.hasOwn(object || {}, 'ocrtext'))) throw new Error('Recognized screenshot text requires feedback-minimal-2.');
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
  function upgradeImage(value) {
    const normalized=normalizeImage(value);
    if (normalized.schemaVersion===IMAGE_VERSION) return normalized;
    const {$schema, ...data}=normalized;
    const upgraded={...data,schemaVersion:IMAGE_VERSION,
      tool:normalized.tool||{name:'EditHere',version:productVersion({})},
      objects:normalized.objects.map(object=>typedObject(isTypedVersion(normalized.schemaVersion)?legacyObject(object):object))};
    return normalizeImage(upgraded);
  }

  // Older projects stored two independent parent fields. Migrate once at the
  // boundary; live documents keep one validated parent object.
  function normalizeNoteParent(note) {
    if (!record(note)) throw new Error('Invalid annotation.');
    const legacy = Object.hasOwn(note,'parentSource') || Object.hasOwn(note,'parentState');
    if (legacy) {
      if (Object.hasOwn(note,'parent') || !Object.hasOwn(note,'parentSource') || !Object.hasOwn(note,'parentState')) throw new Error('Invalid annotation parent.');
      const {parentSource, parentState, ...rest} = note;
      note = {...rest,parent:{source:parentSource,state:parentState}};
    }
    if (!Object.hasOwn(note,'parent')) return note;
    const parent=note.parent, validRect=r=>record(r)&&exactKeys(r,['x1','y1','x2','y2'])&&Object.values(r).every(v=>Number.isFinite(v)&&v>=0&&v<=32767)&&r.x2>r.x1&&r.y2>r.y1;
    if (!record(parent) || !exactKeys(parent,['source','state',...(Object.hasOwn(parent,'groupId')?['groupId']:[])]) ||
        !['child','unknown'].includes(parent.state) || !validRect(parent.source) || !validRect(note.source) || note.movementSource ||
        Object.hasOwn(parent,'groupId') && (typeof parent.groupId!=='string'||!parent.groupId)) throw new Error('Invalid annotation parent.');
    return note;
  }

  function parentMovementResolver(movements,bounds) {
    const byPixels=new Map();
    // These are current component trajectories, not a history keyed by source.
    // Equal source rectangles remain ambiguous even when their values match exactly.
    for(const movement of movements||[]){
      if(sameRectangle(movement.source,movement.destination))continue;
      const key=sourceKey(feedbackRectangle(movement.source,bounds));
      if(!byPixels.has(key))byPixels.set(key,[]);
      byPixels.get(key).push(movement);
    }
    return (source,groupId)=>{
      const candidates=byPixels.get(sourceKey(feedbackRectangle(source,bounds)));
      if(candidates?.length!==1)return null;
      const owner=candidates[0];
      if(groupId!==undefined&&owner.groupId!==groupId)return null;
      return Object.values(source).every(Number.isInteger)||sameRectangle(source,owner.source)?owner:null;
    };
  }

  function projectedNotes(document,bounds) {
    const ownerFor=parentMovementResolver(document.movements,bounds);
    const notes=(document.notes||[]).map(original=>{
      // Movement notes have their own top-level identity, even in older drafts
      // that retained their former parent's link.
      let note=original;
      if(note.movementSource){const {parent,parentSource,parentState,...rest}=note;note=rest;}
      note=normalizeNoteParent(note);
      if(!note.parent)return {...note};
      const owner=ownerFor(note.parent.source,note.parent.groupId);
      if(!owner){const {parent,...rest}=note;return rest;}
      return {...note,parent:{...note.parent,source:owner.source}};
    });
    const anchors=new Map();
    for(const note of notes)if(note.movementSource){const owner=ownerFor(note.movementSource,note.groupId);if(owner&&!anchors.has(owner))anchors.set(owner,note);}
    for(let i=0;i<notes.length;i++){
      const child=notes[i];
      if(child.parent?.state!=='child')continue;
      const owner=ownerFor(child.parent.source,child.parent.groupId);
      if(!anchors.has(owner)){
        const anchor={source:owner.destination,movementSource:owner.source,text:''};
        notes.splice(i++,0,anchor);anchors.set(owner,anchor);
      }
    }
    if(notes.length>1000)throw new Error('每个画面最多支持 1000 条批注（包括位移意见）');
    const children=new Map();
    for(const note of notes)if(note.parent?.state==='child'){
      const anchor=anchors.get(ownerFor(note.parent.source,note.parent.groupId));
      if(!children.has(anchor))children.set(anchor,[]);
      children.get(anchor).push(note);
    }
    const grouped=new Set([...children.values()].flat());
    return notes.filter(note=>!grouped.has(note)).flatMap(note=>[note,...(children.get(note)||[])]);
  }

  // Matches app/model.cpp exportFeedback. Movements are the actual group
  // trajectories (layoutMovements), not merged or split rendered pixel pieces.
  // The caller supplies current result coordinates for ordinary notes and the
  // original movementSource for a note attached to a trajectory.
  function exportImage(document, mode = 'file') {
    const image=imageFormat.descriptor(document.image,mode,document.imageFile||'feedback.'+({'image/png':'png','image/jpeg':'jpg','image/webp':'webp','image/gif':'gif','image/bmp':'bmp'}[imageFormat.dataBytes(document.image).mimeType]),{devicePixelRatio:document.devicePixelRatio??null,preview:document.previewFile});
    const bounds=image;
    const objects = [], bySource = new Map();
    const destinations=new Map();
    for(const movement of document.movements||[]) {
      const source=feedbackRectangle(movement.source,bounds);
      destinations.set(sourceKey(source),feedbackDestination(movement.source,source,movement.destination,bounds));
    }
    function ensureEntry(source) {
      const rounded=feedbackRectangle(source,bounds),key=sourceKey(rounded);
      if (!bySource.has(key)) {
        const target=destinations.get(key);
        const entry = {source:rounded, annotations:[],...(target&&!sameRectangle(rounded,target)?{movement:movementResult(rounded,target)}:{})};
        bySource.set(key, entry);
        objects.push(entry);
      }
      return bySource.get(key);
    }
    function attachRecognition(entry, note) {
      if (document.includeRecognizedText === false || !Object.hasOwn(note, 'recognizedText')) return;
      const recognized = validateRecognizedText(note.recognizedText, true);
      if (recognized.text && entry.source !== null) entry.recognizedText = {...recognized};
      else delete entry.recognizedText;
    }
    // A note that belongs to a moved component is nested under it instead of
    // becoming a second object; an undetermined one blocks the export.
    const childrenBySource = new Map();
    for (const [index, note] of projectedNotes(document,bounds).entries()) {
      const annotation = {number:index + 1, text:note.text};
      if (note.parent?.state === 'unknown') throw new Error('有批注还没确认归属，请先选择“是，作为子批注”或“否，独立对象”。');
      if (note.parent) {
        const child = {source:feedbackRectangle(note.source,bounds), annotations:[annotation]};
        attachRecognition(child, note);
        const key = sourceKey(note.parent.source);
        if (!childrenBySource.has(key)) childrenBySource.set(key, []);
        childrenBySource.get(key).push(child);
        continue;
      }
      if (note.movementSource) {
        const entry = ensureEntry(note.movementSource);
        attachRecognition(entry, note);
        if (note.text.trim()) entry.annotations.push(annotation);
      } else {
        // Independent notes stay independent even when their rectangles match.
        const entry = {source:rectangle(note.source), annotations:[annotation]};
        attachRecognition(entry, note); objects.push(entry);
      }
    }
    for(const movement of document.movements||[]) ensureEntry(movement.source);
    for (const [key, children] of childrenBySource) {
      // Match the unrounded source before considering its exported pixel key.
      // Distinct source rectangles can round to the same pixels.
      const movement=(document.movements||[]).find(m=>sourceKey(m.source)===key&&!sameRectangle(m.source,m.destination));
      const entry = movement && bySource.get(sourceKey(feedbackRectangle(movement.source,bounds)));
      // A component that is no longer moved leaves its annotations behind as
      // ordinary objects, which is also how the reader treats such a file.
      if (!entry?.movement) { objects.push(...children); continue; }
      entry.children = [...(entry.children || []), ...children];
    }
    for (const entry of objects) {
      entry.source=feedbackRectangle(entry.source,bounds);
    }
    const outputObjects=objects.filter(entry=>entry.movement||entry.annotations.length||Object.hasOwn(entry,'recognizedText'));
    // Every object is numbered, children included, so a reader can point at one.
    let objectNumber = 0;
    const numbered = object => {
      const {children, ...rest} = object;
      return {...rest, id:'o' + (++objectNumber),
        ...(children ? {children:children.map(numbered)} : {})};
    };
    return {schemaVersion:IMAGE_VERSION,
      tool:{name:'EditHere', version:productVersion(document)},
      ...(document.capture ? {capture:{...validateCapture(document.capture)}} : {}),
      annotationSpace:'result', objects:outputObjects.map(object => numbered(typedObject(object))),
      image};
  }

  // Matches app/videoproject.cpp serializeVideoFeedback: chronological frames,
  // independently readable image feedback, and a lightweight root object index.
  function exportVideo(project, mode = 'file', exportedAt = new Date().toISOString()) {
    const documents = project.frames.filter(document => document.notes.length || document.movements?.length)
      .slice().sort((a, b) => a.timestampUs - b.timestampUs);
    const frames = documents.map(document => ({
      id:document.id,
      timestampMs:Math.floor(document.timestampUs / 1000),
      timestampUs:document.timestampUs,
      ...(mode==='file'?{imageFile:document.imageFile||'frame-' + document.id + '.png'}:{}),
      feedback:exportImage({...document,imageFile:document.imageFile||'frame-'+document.id+'.png'}, mode)
    })).filter(frame=>frame.feedback.objects.length);
    const objects = frames.flatMap(frame => flattenObjects(frame.feedback.objects).map(({children,...object}) => ({
      ...JSON.parse(JSON.stringify(object)),
      timestampMs:frame.timestampMs,
      frameId:frame.id
    })));
    return {
      schemaVersion:'video-feedback-1', tool:'EditHere', exportedAt,
      ...(project.capture ? {capture:{...validateCapture(project.capture)}} : {}),
      video:{source:project.source, durationMs:project.durationMs,
        width:frames[0]?.feedback.image.width || 0, height:frames[0]?.feedback.image.height || 0},
      frames, objects
    };
  }

  // The root index repeats frame data, including v3 kinds/results. Compare all
  // fields so an index cannot silently alter the final target or annotation.
  function validateVideoRecognitionIndex(value) {
    const signature = (object, frameId, timestampMs) => serialize({...object,frameId,timestampMs});
    const expected = value.frames.flatMap(frame => flattenObjects(normalizeImage(frame.feedback).objects)
      .map(({children,...object}) => signature(object, frame.id, frame.timestampMs))).sort();
    const actual = value.objects.map(object => {
      if (!record(object)) throw new Error('Invalid video feedback index.');
      const {frameId,timestampMs,...payload}=object;
      if(Object.hasOwn(payload,'children'))throw new Error('Video feedback root index must flatten child annotations.');
      annotationEntries([payload]);
      return signature(payload,frameId,timestampMs);
    }).sort();
    if (JSON.stringify(expected) !== JSON.stringify(actual)) throw new Error('Video recognized screenshot text index does not match its frames.');
  }

  // Match native feedback ordering, keeping width and height together.
  function ordered(value) {
    if (Array.isArray(value)) return value.map(ordered);
    if (value !== null && typeof value === 'object') {
      const keys=Object.keys(value).sort();
      if(keys.includes('width')&&keys.includes('height')){keys.splice(keys.indexOf('height'),1);keys.splice(keys.indexOf('width')+1,0,'height');}
      if(keys.includes('children')){keys.splice(keys.indexOf('children'),1);keys.push('children');}
      return Object.fromEntries(keys.map(key => [key, ordered(value[key])]));
    }
    return value;
  }
  function serialize(feedback) {
    return JSON.stringify(ordered(feedback)) + '\n';
  }
  // Display-only metadata: the native feedback itself retains its exact keys,
  // array order and fractional coordinates. Both delivery modes use serialize.
  function summary(feedback,serialized=serialize(feedback)) {
    const objects=flattenObjects(feedback.objects);
    const images=feedback.schemaVersion==='video-feedback-1'?feedback.frames.map(frame=>frame.feedback.image):[feedback.image];
    const byteLength=value=>new TextEncoder().encode(value).byteLength;
    return {schemaVersion:feedback.schemaVersion || 'feedback-minimal-1', objects:objects.length,annotations:objects.reduce((sum,object)=>sum+object.annotations.length,0),
      movements:objects.reduce((sum,object)=>sum+(object.movement?1:(object.movements||[]).length),0), recognizedTexts:objects.filter(object=>object.ocrtext||object.recognizedText).length,
      bytes:byteLength(serialized),imageBytes:images.map(image=>imageFormat.dataUrlOf(image)).filter(Boolean).reduce((sum,image)=>sum+byteLength(image),0)};
  }
  const foldedImage = image => typeof image==='string'?image.slice(0, image.indexOf(',')) + ',[图片编码已折叠]':image?.mode==='embedded'?{...image,dataUrl:foldedImage(image.dataUrl)}:image;

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
      lines.push('  "image": ' + JSON.stringify(ordered(foldedImage(feedback.image))) + ',');
    }
    lines.push('  "objects": [');
    feedback.objects.forEach((object, index) => {
      lines.push('    ' + JSON.stringify(ordered(object)) + (index + 1 < feedback.objects.length ? ',' : ''));
    });
    lines.push('  ]', '}');
    return lines.join('\n');
  }

  return {exportImage, exportVideo, serialize, preview, summary, imageMetadata, normalizeImage, normalizeNoteParent, parentMovementResolver, upgradeImage, legacyImage, legacyObject, typedObject, flattenObjects, isTypedVersion, isImageVersion, annotationEntries, validateRecognizedText, validateVideoRecognitionIndex, validateCapture, validatePageContext, timestampWithZone, imageFormat};
});
