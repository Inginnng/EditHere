#!/usr/bin/env node
// EditHere MCP 连接器（零依赖，Node.js >= 18，stdio transport）
//
// 把本地 edithere-cli 的 Agent 接口封装为 MCP 工具，供 WorkBuddy 等
// MCP 客户端调用：
//   edithere_status           查询 EditHere 运行与文档状态
//   edithere_open             打开图片、视频或 .edithere 项目（仅请求受理）
//   edithere_capture          唤起截图（仅请求受理）
//   edithere_annotate_start   发起标注会话后立即返回会话 ID（推荐）
//   edithere_annotate_poll    查询会话是否完成，完成时返回反馈摘要
//   edithere_annotate         发起标注会话并阻塞等待用户点击"完成并返回 AI"
//   edithere_export           离线导出已保存项目/图片为反馈 JSON
//
// annotate_start / annotate_poll 是推荐路径：两者都在毫秒级返回，满足
// WorkBuddy 连接器规范"单次请求建议 30 秒内响应"；annotate 保留给支持
// 长任务的客户端，会阻塞直到用户提交、取消或超时。
//
// CLI 定位顺序：EDITHERE_CLI 环境变量 → PATH → 默认安装目录；
// EDITHERE_CLI 指向的路径不可用时直接报错，不静默改用其他来源的 CLI。
// 反馈 JSON 遵循 schema/feedback-minimal.schema.json：当前导出为 objects 对象结构，
// 兼容早期版本的 annotations + changes 并行数组。

import { spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import readline from 'node:readline';
import { fileURLToPath } from 'node:url';

const SERVER_NAME = 'edithere';
const SERVER_VERSION = '0.2.0';
const PROTOCOL_VERSION = '2025-06-18';

// ---------------------------------------------------------------- CLI 定位

function lookupOnPath(name) {
  const dirs = String(process.env.PATH || '').split(path.delimiter).filter(Boolean);
  const extensions = process.platform === 'win32'
    ? String(process.env.PATHEXT || '.EXE;.CMD;.BAT').split(';').filter(Boolean)
    : [''];
  for (const dir of dirs) {
    for (const extension of extensions) {
      const candidate = path.join(dir, name + (name.toLowerCase().endsWith(extension.toLowerCase()) ? '' : extension));
      try {
        fs.accessSync(candidate, fs.constants.X_OK);
        return candidate;
      } catch {
        /* try next */
      }
    }
  }
  return null;
}

// 返回 { path, source, error }。error 非空表示 CLI 定位失败，调用方应直接报错，
// 不静默改用其他来源的 CLI（与 skill 中"路径失效时先检查原目录，不悄悄改用另一份程序"一致）。
function resolveCli() {
  const explicit = process.env.EDITHERE_CLI;
  if (explicit) {
    try {
      fs.accessSync(explicit, fs.constants.X_OK);
      return { path: explicit, source: 'EDITHERE_CLI' };
    } catch {
      return {
        path: explicit,
        source: 'EDITHERE_CLI',
        error: `EDITHERE_CLI 指定的路径不存在或不可执行：${explicit}\n请修正该环境变量，或将其清空后改用 PATH / 默认安装目录。为避免悄然使用另一份程序，本次不再回退到其他位置。`,
      };
    }
  }
  const suffix = process.platform === 'win32' ? 'edithere-cli.exe' : 'edithere-cli';
  const onPath = lookupOnPath(suffix);
  if (onPath) return { path: onPath, source: 'PATH' };
  const candidates = [];
  if (process.platform === 'win32' && process.env.LOCALAPPDATA) {
    candidates.push([path.join(process.env.LOCALAPPDATA, 'Programs', 'EditHere', 'edithere-cli.exe'), '默认安装目录']);
  } else if (process.platform === 'darwin') {
    candidates.push([path.join(os.homedir(), 'Applications', 'EditHere.app', 'Contents', 'MacOS', 'edithere-cli'), '默认安装目录']);
    candidates.push(['/Applications/EditHere.app/Contents/MacOS/edithere-cli', '默认安装目录']);
  }
  for (const [candidate, source] of candidates) {
    try {
      fs.accessSync(candidate, fs.constants.X_OK);
      return { path: candidate, source };
    } catch {
      /* try next */
    }
  }
  return {
    path: suffix,
    source: '未解析',
    error: `未找到 edithere-cli：未设置 EDITHERE_CLI，${suffix} 也不在 PATH 与默认安装目录中。请安装 EditHere，或通过 EDITHERE_CLI 指定 edithere-cli 的绝对路径。`,
  };
}

const CLI = resolveCli();
const CLI_CHILDREN = new Set();
const MAX_CLI_OUTPUT_CHARS = 1024 * 1024;
let closing = false;

function trackCliChild(child) {
  CLI_CHILDREN.add(child);
  child.once('close', () => CLI_CHILDREN.delete(child));
  return child;
}

function terminateCliChildren() {
  closing = true;
  for (const child of CLI_CHILDREN) child.kill('SIGKILL');
}

// A Buffer may end halfway through a UTF-8 character. Let each stream's decoder
// carry the remaining bytes, and cap retained text if a broken CLI floods output.
function captureCliOutput(child, result) {
  for (const [stream, key] of [[child.stdout, 'stdout'], [child.stderr, 'stderr']]) {
    stream.setEncoding('utf8');
    stream.on('data', (chunk) => {
      if (result.error) return;
      const remaining = MAX_CLI_OUTPUT_CHARS - result.stdout.length - result.stderr.length;
      result[key] += chunk.slice(0, remaining);
      if (chunk.length > remaining) {
        result.error = { code: 'output_limit', message: 'CLI output exceeded the supported size; the process was terminated.' };
        child.kill('SIGKILL');
      }
    });
  }
}

// ---------------------------------------------------------------- CLI 调用

function runCli(args, { timeoutMs = 60_000, onWaiting } = {}) {
  return new Promise((resolve) => {
    let child;
    try {
      child = trackCliChild(spawn(CLI.path, args, { stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true }));
    } catch (err) {
      resolve({ ok: false, exitCode: -1, error: { code: 'spawn_failed', message: String(err?.message || err) } });
      return;
    }
    const output = { stdout: '', stderr: '', timedOut: false };
    const timer = setTimeout(() => {
      output.timedOut = true;
      child.kill('SIGKILL');
    }, timeoutMs);

    let waitingTimer = null;
    if (typeof onWaiting === 'function') {
      waitingTimer = setInterval(() => onWaiting(), 60_000);
      waitingTimer.unref?.();
    }

    captureCliOutput(child, output);
    child.on('error', (err) => {
      clearTimeout(timer);
      if (waitingTimer) clearInterval(waitingTimer);
      resolve({ ok: false, exitCode: -1, error: { code: 'spawn_failed', message: String(err?.message || err) } });
    });
    child.on('close', (code) => {
      clearTimeout(timer);
      if (waitingTimer) clearInterval(waitingTimer);
      const parsed = parseCliJson(output.stdout);
      resolve({
        ...output,
        exitCode: code ?? -1,
        json: parsed,
      });
    });
  });
}

function parseCliJson(stdout) {
  if (!stdout) return null;
  for (const line of stdout.trim().split(/\r?\n/).reverse()) {
    const t = line.trim();
    if (!t.startsWith('{')) continue;
    try {
      const obj = JSON.parse(t);
      if (obj && typeof obj === 'object') return obj;
    } catch {
      /* next line */
    }
  }
  return null;
}

const EXIT_CODE_HINTS = {
  2: '参数或通信协议错误',
  3: 'EditHere 应用或连接不可用（可能未运行，或 Agent 接口未就绪）',
  4: '当前状态忙（已有未保存文档、截图或进行中的标注会话）',
  5: '文件或 I/O 错误（输入不存在、输出已存在或写入失败）',
  6: '用户取消了本次操作',
  7: '等待用户提交反馈超时',
  8: '当前执行环境无权访问桌面接口（desktop_access_required）',
};

// ---------------------------------------------------------------- 反馈处理

function defaultOutputDir() {
  const dir = path.join(os.tmpdir(), 'edithere-feedback');
  fs.mkdirSync(dir, { recursive: true });
  return dir;
}

function makeOutputPath(outputDir, tag) {
  const dir = outputDir ? path.resolve(outputDir) : defaultOutputDir();
  fs.mkdirSync(dir, { recursive: true });
  const stamp = new Date().toISOString().replace(/[-:T]/g, '').slice(0, 14);
  const rand = Math.random().toString(36).slice(2, 8);
  return path.join(dir, `${tag}-${stamp}-${rand}.json`);
}

function dataUrlToImageContent(dataUrl) {
  if (typeof dataUrl !== 'string') return null;
  const match = /^data:(image\/(?:png|jpeg));base64,([A-Za-z0-9+/]+={0,2})$/.exec(dataUrl);
  if (!match || match[2].length % 4 !== 0) return null;
  return { type: 'image', data: match[2], mimeType: match[1] };
}

// A video document can hold hundreds of screenshots. Keep its JSON intact on
// disk, and make both text and image transport limits visible to the caller.
const MAX_FEEDBACK_FILE_BYTES = 512 * 1024 * 1024;
const MAX_VIDEO_FRAMES = 10000;
const MAX_VIDEO_SUMMARY_FRAMES = 128;
const MAX_VIDEO_SUMMARY_OBJECTS = 256;
const MAX_VIDEO_SUMMARY_CHARS = 100000;
const MAX_VIDEO_RESULT_IMAGES = 8;
const MAX_VIDEO_RESULT_IMAGE_CHARS = 24 * 1024 * 1024;

function isRecord(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}

function isVideoFeedback(feedback) {
  return feedback.schemaVersion === 'video-feedback-1';
}

function formatTimestamp(timestampMs) {
  const hours = Math.floor(timestampMs / 3600000);
  const minutes = Math.floor(timestampMs / 60000) % 60;
  const seconds = Math.floor(timestampMs / 1000) % 60;
  const milliseconds = timestampMs % 1000;
  return `${String(hours).padStart(2, '0')}:${String(minutes).padStart(2, '0')}:${String(seconds).padStart(2, '0')}.${String(milliseconds).padStart(3, '0')}`;
}

function validateRectangle(rectangle, location) {
  if (!isRecord(rectangle)
      || !['x1', 'y1', 'x2', 'y2'].every((key) => Number.isFinite(rectangle[key]))
      || rectangle.x1 < 0 || rectangle.y1 < 0
      || rectangle.x2 < rectangle.x1 || rectangle.y2 < rectangle.y1) {
    throw new Error(`${location} 不是有效的像素矩形。`);
  }
}

function validateVideoObject(object, location) {
  if (!isRecord(object) || !Array.isArray(object.movements) || !Array.isArray(object.annotations)) {
    throw new Error(`${location} 必须包含 source、movements 和 annotations。`);
  }
  if (object.source !== null) validateRectangle(object.source, `${location}.source`);
  object.movements.forEach((movement, index) => {
    if (!isRecord(movement)) throw new Error(`${location}.movements[${index}] 不是对象。`);
    validateRectangle(movement.to, `${location}.movements[${index}].to`);
  });
  if (object.annotations.some((text) => typeof text !== 'string' || !text.trim())) {
    throw new Error(`${location}.annotations 必须是非空文字数组。`);
  }
}

// Recovery has no successful CLI receipt to vouch for the file. Require a
// complete feedback shape rather than treating arbitrary JSON (even {}) as an
// empty submission. Keep the legacy image format usable too.
function validateImageRecoveryFeedback(feedback) {
  const keys = (value, allowed, location) => {
    if (!isRecord(value) || Object.keys(value).some((key) => !allowed.includes(key))) {
      throw new Error(`${location} 包含不支持的字段或不是对象。`);
    }
  };
  const rectangle = (value, location, integer = false, allowPoint = false) => {
    keys(value, ['x1', 'y1', 'x2', 'y2'], location);
    validateRectangle(value, location);
    if (Object.values(value).some((coordinate) => coordinate > 32767 || (integer && !Number.isInteger(coordinate)))) {
      throw new Error(`${location} 超过像素坐标范围。`);
    }
    const width = value.x2 - value.x1;
    const height = value.y2 - value.y1;
    const point = width === 0 && height === 0;
    if (!(allowPoint && point) && (width <= 1e-7 || height <= 1e-7)) {
      throw new Error(`${location} 的区域尺寸不正确。`);
    }
    return point;
  };
  const text = (value, location) => {
    if (typeof value !== 'string' || !value.trim() || value.length > 10000) {
      throw new Error(`${location} 必须是有效的非空批注文字。`);
    }
  };
  if (feedback.image !== undefined && !dataUrlToImageContent(feedback.image)) {
    throw new Error('image 不是有效的 PNG/JPEG Base64 data URL。');
  }
  if (feedback.objects !== undefined) {
    keys(feedback, ['annotationSpace', 'objects', 'image'], '反馈');
    if (feedback.annotationSpace !== 'result' || !Array.isArray(feedback.objects) || feedback.objects.length > 9704) {
      throw new Error('反馈必须包含 annotationSpace=result 和有效的 objects 数组。');
    }
    let noteCount = 0;
    let movementCount = 0;
    feedback.objects.forEach((object, index) => {
      const location = `objects[${index}]`;
      keys(object, ['source', 'movements', 'annotations'], location);
      validateVideoObject(object, location);
      const locatedPoint = object.source !== null && rectangle(object.source, `${location}.source`, false, true);
      if ((object.source === null || locatedPoint) && object.movements.length) {
        throw new Error(`${location} 的全局或点批注不能关联移动。`);
      }
      noteCount += object.annotations.length;
      movementCount += object.movements.length;
      if (noteCount > 1000 || movementCount > 8704) throw new Error('反馈的批注或移动总数量超过支持范围。');
      object.movements.forEach((movement, movementIndex) => {
        keys(movement, ['to'], `${location}.movements[${movementIndex}]`);
        rectangle(movement.to, `${location}.movements[${movementIndex}].to`);
      });
      object.annotations.forEach((annotation, noteIndex) => text(annotation, `${location}.annotations[${noteIndex}]`));
    });
    return;
  }
  keys(feedback, ['annotationSpace', 'annotations', 'changes', 'image'], '反馈');
  if ((feedback.annotationSpace !== undefined && feedback.annotationSpace !== 'result')
      || !Array.isArray(feedback.annotations) || feedback.annotations.length > 1000
      || !Array.isArray(feedback.changes) || feedback.changes.length > 8192) {
    throw new Error('旧版反馈必须包含有效的 annotations 和 changes 数组。');
  }
  feedback.changes.forEach((change, index) => {
    keys(change, ['from', 'to'], `changes[${index}]`);
    rectangle(change.from, `changes[${index}].from`);
    rectangle(change.to, `changes[${index}].to`);
  });
  feedback.annotations.forEach((annotation, index) => {
    const location = `annotations[${index}]`;
    keys(annotation, ['text', 'point', 'rectangle', 'change'], location);
    text(annotation.text, `${location}.text`);
    const locations = ['point', 'rectangle', 'change'].filter((key) => annotation[key] !== undefined);
    if (locations.length > 1) throw new Error(`${location} 包含多个位置。`);
    if (annotation.point !== undefined) {
      keys(annotation.point, ['x', 'y'], `${location}.point`);
      if (!['x', 'y'].every((key) => Number.isInteger(annotation.point[key]) && annotation.point[key] >= 0 && annotation.point[key] <= 32766)) {
        throw new Error(`${location}.point 不是有效的像素点。`);
      }
    }
    if (annotation.rectangle !== undefined) rectangle(annotation.rectangle, `${location}.rectangle`, true);
    if (annotation.change !== undefined && (!Number.isInteger(annotation.change) || annotation.change < 0 || annotation.change >= feedback.changes.length)) {
      throw new Error(`${location}.change 没有对应的布局变化。`);
    }
  });
}

function objectIndexKey(object) {
  return JSON.stringify([object.source, object.movements, object.annotations]);
}

function validateVideoFeedback(feedback) {
  if (!isRecord(feedback.video) || typeof feedback.video.source !== 'string' || !feedback.video.source.trim()
      || feedback.video.source.length > 32768) {
    throw new Error('video.source 必须是源视频的路径或地址。');
  }
  for (const field of ['durationMs', 'width', 'height']) {
    const value = feedback.video[field];
    if (!Number.isSafeInteger(value) || value < (field === 'durationMs' ? 0 : 1)) {
      throw new Error(`video.${field} 不是有效的非负时长或正整数尺寸。`);
    }
  }
  if (!Array.isArray(feedback.frames) || feedback.frames.length > MAX_VIDEO_FRAMES) {
    throw new Error(`frames 必须是数组，最多 ${MAX_VIDEO_FRAMES} 帧。`);
  }
  const byId = new Map();
  feedback.frames.forEach((frame, index) => {
    const location = `frames[${index}]`;
    if (!isRecord(frame) || typeof frame.id !== 'string' || !frame.id.trim() || frame.id.length > 256 || byId.has(frame.id)) {
      throw new Error(`${location}.id 必须是唯一的非空字符串。`);
    }
    if (!Number.isSafeInteger(frame.timestampMs) || frame.timestampMs < 0
        || frame.timestampMs > feedback.video.durationMs) {
      throw new Error(`${location}.timestampMs 必须位于视频时长范围内。`);
    }
    if (frame.timestampUs !== undefined && (!Number.isSafeInteger(frame.timestampUs) || frame.timestampUs < 0
        || Math.floor(frame.timestampUs / 1000) !== frame.timestampMs)) {
      throw new Error(`${location}.timestampUs 与 timestampMs 不一致。`);
    }
    if (!isRecord(frame.feedback) || frame.feedback.annotationSpace !== 'result'
        || !Array.isArray(frame.feedback.objects)) {
      throw new Error(`${location}.feedback 必须是 annotationSpace=result 的对象反馈。`);
    }
    if (frame.feedback.image !== undefined && typeof frame.feedback.image !== 'string') {
      throw new Error(`${location}.feedback.image 必须是图像 data URL 字符串。`);
    }
    if (frame.feedback.image && !dataUrlToImageContent(frame.feedback.image)) {
      throw new Error(`${location}.feedback.image 不是有效的 PNG/JPEG Base64 data URL。`);
    }
    if (frame.imageFile !== undefined && (typeof frame.imageFile !== 'string'
        || /[\\/:\0]/.test(frame.imageFile) || !/\.(png|jpe?g)$/i.test(frame.imageFile))) {
      throw new Error(`${location}.imageFile 必须是反馈 JSON 同目录的 PNG/JPEG 文件名，不能包含路径。`);
    }
    frame.feedback.objects.forEach((object, objectIndex) => {
      validateVideoObject(object, `${location}.feedback.objects[${objectIndex}]`);
    });
    const remaining = new Map();
    frame.feedback.objects.forEach((object) => {
      const key = objectIndexKey(object);
      remaining.set(key, (remaining.get(key) || 0) + 1);
    });
    byId.set(frame.id, { frame, remaining });
  });
  // The expanded root index is optional for older preview exports, but if it
  // exists it must reference exactly the frame-local objects. Never merge by
  // rectangle: the same location at two timestamps can mean different edits.
  if (feedback.objects !== undefined) {
    if (!Array.isArray(feedback.objects)) throw new Error('根 objects 必须是展开索引数组。');
    feedback.objects.forEach((object, index) => {
      if (!isRecord(object)) throw new Error(`objects[${index}] 不是对象。`);
      const entry = byId.get(object.frameId);
      if (!entry || object.timestampMs !== entry.frame.timestampMs) {
        throw new Error(`objects[${index}] 的 frameId 或 timestampMs 没有匹配的帧。`);
      }
      validateVideoObject(object, `objects[${index}]`);
      const key = objectIndexKey(object);
      const remaining = entry.remaining.get(key) || 0;
      if (!remaining) throw new Error(`objects[${index}] 与对应帧的批注内容不一致。`);
      if (remaining === 1) entry.remaining.delete(key);
      else entry.remaining.set(key, remaining - 1);
    });
    if ([...byId.values()].some((entry) => entry.remaining.size)) {
      throw new Error('根 objects 展开索引遗漏了帧中的对象。');
    }
  }
}

function summarizeVideoFeedback(feedback, feedbackPath) {
  const frames = feedback.frames;
  const objectCount = frames.reduce((total, frame) => total + frame.feedback.objects.length, 0);
  const imageCount = frames.reduce((total, frame) => total + (frame.feedback.image ? 1 : 0), 0);
  const externalImageCount = frames.reduce((total, frame) => total + (frame.imageFile ? 1 : 0), 0);
  const noteCount = frames.reduce((total, frame) => total + frame.feedback.objects.reduce((n, object) => n + object.annotations.length, 0), 0);
  const lines = [
    `反馈文件: ${feedbackPath}`,
    '反馈类型: video-feedback-1（视频按帧反馈）',
    `源视频: ${feedback.video.source}`,
    `视频: ${feedback.video.width}x${feedback.video.height}，时长 ${formatTimestamp(feedback.video.durationMs)}（${feedback.video.durationMs} ms）`,
    `标注帧数量: ${frames.length}，对象数量: ${objectCount}，批注数量: ${noteCount}，内嵌截图数量: ${imageCount}，外部截图文件索引数量: ${externalImageCount}`,
    '时间戳: 相对源视频起点，timestampMs 为毫秒；timestampUs 如存在则是更精确的实际截帧时间（微秒）。请用 frameId + timestampMs + 坐标定位修改点，不能把不同时刻的相同坐标合并。',
    '坐标系: 对应帧截图的图像像素，原点在左上角；annotationSpace=result。source 是该帧原图区域，movements.to 是调整后的区域；右下边界不包含自身。坐标不能直接当作屏幕坐标或 CSS 像素。',
    'frames[n].feedback 保留原图片反馈结构；根 objects 是同一批对象的展开索引，请勿重复执行两遍批注。每帧截图提取位置: frames[n].feedback.image（data URL，不是视频地址）；如有 frames[n].imageFile，则指向反馈 JSON 同目录的帧截图文件。源视频只以地址引用，文件不存在时仍可根据已保存截图分析。',
  ];
  let characterCount = lines.join('\n').length;
  let trimmed = false;
  let displayedObjects = 0;
  const add = (line) => {
    if (characterCount + line.length + 1 > MAX_VIDEO_SUMMARY_CHARS) {
      trimmed = true;
      return false;
    }
    characterCount += line.length + 1;
    lines.push(line);
    return true;
  };
  frames.slice(0, MAX_VIDEO_SUMMARY_FRAMES).forEach((frame, index) => {
    if (!add(`帧 frames[${index}]: frameId=${frame.id}，时间 ${formatTimestamp(frame.timestampMs)}（timestampMs=${frame.timestampMs}${frame.timestampUs === undefined ? '' : `，timestampUs=${frame.timestampUs}`}），${frame.feedback.objects.length} 个对象，截图=${frame.feedback.image ? `frames[${index}].feedback.image` : '未内嵌'}${frame.imageFile ? `，外部截图=${frame.imageFile}（JSON 同目录）` : ''}`)) return;
    frame.feedback.objects.forEach((object, objectIndex) => {
      if (displayedObjects >= MAX_VIDEO_SUMMARY_OBJECTS) { trimmed = true; return; }
      if (!add(`  frames[${index}].feedback.objects[${objectIndex}]: ${formatVideoSource(object.source)}`)) return;
      displayedObjects += 1;
      object.movements.slice(0, 32).forEach((movement, movementIndex) => add(`      移动#${movementIndex}: -> ${formatRectangle(movement.to)}`));
      object.annotations.slice(0, 64).forEach((text, noteIndex) => {
        const shortened = text.length > 1000;
        if (shortened) trimmed = true;
        add(`      批注#${noteIndex}: ${text.slice(0, 1000)}${shortened ? '…（全文见 JSON）' : ''}`);
      });
      if (object.movements.length > 32 || object.annotations.length > 64) trimmed = true;
    });
  });
  if (!objectCount) lines.push('用户本轮没有提交任何对象（这是有效结果，不要编造修改意见）。');
  if (trimmed || frames.length > MAX_VIDEO_SUMMARY_FRAMES) {
    lines.push(`摘要限量: 最多 ${MAX_VIDEO_SUMMARY_FRAMES} 帧、${MAX_VIDEO_SUMMARY_OBJECTS} 个对象、${MAX_VIDEO_SUMMARY_CHARS} 字符，部分内容未在摘要展开；完整批注和截图仍保存在反馈 JSON。请按 frames[n] 逐帧读取，帧索引范围 0–${frames.length - 1}；不能把摘要当作全部修改要求。`);
  }
  return lines.join('\n');
}

function readExternalFrameImage(filename, feedbackPath, maximumCharacters) {
  const directory = fs.realpathSync(path.dirname(feedbackPath));
  const candidate = path.join(directory, filename);
  // Filenames are checked during parsing. A symlink must not turn a same-dir
  // frame reference into a request to read an unrelated file elsewhere.
  const resolved = fs.realpathSync(candidate);
  if (path.dirname(resolved) !== directory) throw new Error('外部截图不能通过符号链接读取其他目录。');
  const stat = fs.statSync(resolved);
  if (!stat.isFile() || !stat.size) throw new Error('外部截图不是有效的非空文件。');
  const size = stat.size;
  if (Math.ceil(size / 3) * 4 > maximumCharacters) return null;
  const mimeType = /\.png$/i.test(filename) ? 'image/png' : 'image/jpeg';
  return { type: 'image', data: fs.readFileSync(resolved).toString('base64'), mimeType };
}

function videoImageContents(feedback, feedbackPath) {
  const content = [];
  const omitted = [];
  const missing = [];
  let imageCount = 0;
  let imageCharacters = 0;
  feedback.frames.forEach((frame, index) => {
    const location = frame.feedback.image ? `frames[${index}].feedback.image`
      : `frames[${index}].imageFile = ${frame.imageFile}（反馈 JSON 同目录）`;
    if (!frame.feedback.image && !frame.imageFile) {
      missing.push(index);
      return;
    }
    if (imageCount >= MAX_VIDEO_RESULT_IMAGES) {
      omitted.push(index);
      return;
    }
    let image = dataUrlToImageContent(frame.feedback.image);
    if (!image) {
      try {
        image = readExternalFrameImage(frame.imageFile, feedbackPath, MAX_VIDEO_RESULT_IMAGE_CHARS - imageCharacters);
      } catch (error) {
        content.push({ type: 'text', text: `帧 frames[${index}] 外部截图不可读取: ${frame.imageFile}；${String(error?.message || error)}` });
        missing.push(index);
        return;
      }
    }
    if (!image || imageCharacters + image.data.length > MAX_VIDEO_RESULT_IMAGE_CHARS) {
      omitted.push(index);
      return;
    }
    content.push({ type: 'text', text: `下面的截图对应 frameId=${frame.id}，${formatTimestamp(frame.timestampMs)}（timestampMs=${frame.timestampMs}）；提取位置 ${location}。` });
    content.push(image);
    imageCount += 1;
    imageCharacters += image.data.length;
  });
  const indexDescription = (indices) => `${indices.slice(0, 128).join(', ')}${indices.length > 128 ? `, …（共 ${indices.length} 帧；其余索引请遍历 frames）` : ''}`;
  if (omitted.length) {
    content.push({ type: 'text', text: `工具图片内容限量: 最多 ${MAX_VIDEO_RESULT_IMAGES} 张，Base64 总量 ${MAX_VIDEO_RESULT_IMAGE_CHARS} 字符。${omitted.length} 张截图未在此次工具结果附上，其内嵌图像数据或外部文件索引仍完整保存在反馈 JSON；提取 frames[n].feedback.image，或读取 frames[n].imageFile 指向的同目录文件，n = ${indexDescription(omitted)}。外部截图需随 JSON 一起保留。请按这些索引继续读取，不要把已附截图当作全部标注帧。` });
  }
  if (missing.length) {
    content.push({ type: 'text', text: `${missing.length} 帧未内嵌可用的 PNG/JPEG 截图；索引 n = ${indexDescription(missing)}。这些帧仍有时间戳和文字坐标反馈，视觉核对需读取源视频或重新导出包含截图的 JSON。` });
  }
  return content;
}

function formatRectangle(r) {
  if (!r) return '?';
  return `(${r.x1}, ${r.y1}) ${r.x2 - r.x1}x${r.y2 - r.y1}`;
}

function formatVideoSource(rectangle) {
  if (!rectangle) return '全局对象（无 source）';
  if (rectangle.x1 === rectangle.x2 && rectangle.y1 === rectangle.y2) {
    return `原图点(${rectangle.x1}, ${rectangle.y1})`;
  }
  return `原图区域${formatRectangle(rectangle)}`;
}

// 把反馈 JSON 摘要成紧凑文本，坐标与 schema 语义保持一致。
// 支持两种格式：0.9.0 面向对象格式（objects）与旧版动作格式（annotations+changes）。
function summarizeFeedback(feedback, feedbackPath) {
  if (isVideoFeedback(feedback)) return summarizeVideoFeedback(feedback, feedbackPath);
  const lines = [];
  const objects = Array.isArray(feedback.objects) ? feedback.objects : null;
  const annotations = Array.isArray(feedback.annotations) ? feedback.annotations : [];
  const changes = Array.isArray(feedback.changes) ? feedback.changes : [];
  lines.push(`反馈文件: ${feedbackPath}`);
  if (objects) {
    const noteCount = objects.reduce(
      (n, o) => n + (Array.isArray(o.annotations) ? o.annotations.length : 0), 0);
    const moveCount = objects.reduce(
      (n, o) => n + (Array.isArray(o.movements) ? o.movements.length : 0), 0);
    lines.push(`对象数量: ${objects.length}，批注数量: ${noteCount}，移动数量: ${moveCount}，包含原图: ${feedback.image ? '是' : '否'}`);
    lines.push('坐标系: 图像像素，原点在左上角；批注位于调整后的画面（annotationSpace=result）。矩形右下边界不包含自身，宽高为 x2-x1、y2-y1。坐标不能直接当作屏幕坐标或 CSS 像素。');
    if (objects.length) {
      lines.push('对象列表 (source=原图区域，movements=移动到的位置，annotations=修改意见):');
      objects.forEach((o, i) => {
        if (o.source) {
          lines.push(`  [${i}] 原图区域${formatRectangle(o.source)}`);
        } else {
          lines.push(`  [${i}] 全局对象（无 source）`);
        }
        (Array.isArray(o.movements) ? o.movements : []).forEach((m, j) => {
          lines.push(`      移动#${j}: -> ${formatRectangle(m.to)}`);
        });
        (Array.isArray(o.annotations) ? o.annotations : []).forEach((t, j) => {
          lines.push(`      批注#${j}: ${t}`);
        });
      });
    } else {
      lines.push('用户本轮没有提交任何对象（这是有效结果，不要编造修改意见）。');
    }
    return lines.join('\n');
  }
  lines.push(`批注数量: ${annotations.length}，布局变化数量: ${changes.length}，包含原图: ${feedback.image ? '是' : '否'}`);
  lines.push('坐标系: 图像像素，原点在左上角；批注位于调整后的画面（annotationSpace=result）。矩形右下边界不包含自身，宽高为 x2-x1、y2-y1。坐标不能直接当作屏幕坐标或 CSS 像素。');
  if (annotations.length) {
    lines.push('批注列表:');
    annotations.forEach((a, i) => {
      let loc = '全局意见';
      if (a.point) loc = `点(${a.point.x}, ${a.point.y})`;
      else if (a.rectangle) loc = `区域${formatRectangle(a.rectangle)}`;
      else if (typeof a.change === 'number') loc = `对应变化 #${a.change}`;
      lines.push(`  [${i}] ${loc}: ${a.text}`);
    });
  }
  if (changes.length) {
    lines.push('布局变化列表 (from=原图区域 -> to=最终区域):');
    changes.forEach((c, i) => {
      lines.push(`  [${i}] ${formatRectangle(c.from)} -> ${formatRectangle(c.to)}`);
    });
  }
  if (!annotations.length && !changes.length) {
    lines.push('用户本轮没有提交任何批注或布局变化（这是有效结果，不要编造修改意见）。');
  }
  return lines.join('\n');
}

// ---------------------------------------------------------------- 非阻塞标注会话

// annotate_start 在后台保留 CLI 子进程，annotate_poll 按会话 ID 查询结果。
// 会话只存在于连接器进程内；连接器重启后旧会话 ID 失效（会明确报错，不静默重来）。
const SESSIONS = new Map();
const SESSION_TTL_MS = 2 * 60 * 60 * 1000;
let sessionSeq = 0;
let activeSessionId = null;

function newSessionId() {
  sessionSeq += 1;
  return `s${sessionSeq}-${Math.random().toString(36).slice(2, 8)}`;
}

// 清理已结束且超过 TTL 的会话，避免长期运行累积。
function reapSessions() {
  const now = Date.now();
  for (const [id, s] of SESSIONS) {
    if (s.state === 'finished' && now - s.finishedAt > SESSION_TTL_MS) SESSIONS.delete(id);
  }
}

function runningSession() {
  for (const s of SESSIONS.values()) {
    if (s.state === 'running') return s;
  }
  return null;
}

function startAnnotateSession(p) {
  const timeoutSeconds = clampTimeout(p.timeoutSeconds);
  const outputPath = makeOutputPath(p.outputDir, 'annotate');
  const cliArgs = ['annotate', p.imagePath, '--output', outputPath, '--timeout', String(timeoutSeconds)];
  if (p.noImage) cliArgs.push('--no-image');
  let child;
  try {
    child = trackCliChild(spawn(CLI.path, cliArgs, { stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true }));
  } catch (err) {
    return { error: `无法启动 edithere-cli：${String(err?.message || err)}` };
  }
  const id = newSessionId();
  const session = {
    id,
    child,
    outputPath,
    includeImage: Boolean(p.includeImage),
    timeoutSeconds,
    state: 'running',
    exitCode: null,
    stdout: '',
    stderr: '',
    timedOut: false,
    startedAt: Date.now(),
    finishedAt: 0,
    resultContent: null,
  };
  captureCliOutput(child, session);
  child.on('error', (err) => {
    session.state = 'finished';
    session.exitCode = -1;
    session.stderr += String(err?.message || err);
    session.finishedAt = Date.now();
  });
  child.on('close', (code) => {
    session.state = 'finished';
    session.exitCode = code ?? -1;
    session.finishedAt = Date.now();
    clearTimeout(session.timer);
  });
  session.timer = setTimeout(() => {
    session.timedOut = true;
    child.kill('SIGKILL');
  }, (timeoutSeconds + 60) * 1000);
  session.timer.unref?.();
  SESSIONS.set(id, session);
  activeSessionId = id;
  return { session };
}

function pendingSessionContent(session) {
  const waited = Math.round((Date.now() - session.startedAt) / 1000);
  return {
    content: [{
      type: 'text',
      text: `标注会话仍在等待用户提交。\n会话 ID: ${session.id}\n已等待: ${waited} 秒（上限 ${session.timeoutSeconds} 秒）\n反馈文件: ${session.outputPath}\n请确认用户已在 EditHere 中点击顶部的"完成并返回 AI"，然后带上同一 sessionId 再次调用 edithere_annotate_poll。等待期间可以处理其他独立任务，不要重复发起 annotate。`,
    }],
    isError: false,
  };
}

function finishSessionContent(session) {
  if (!session.resultContent) {
    const r = {
      exitCode: session.exitCode,
      json: parseCliJson(session.stdout),
      stdout: session.stdout,
      stderr: session.stderr,
      timedOut: session.timedOut,
      error: session.error,
    };
    const body = annotateResultToContent(r, session.outputPath, { includeImage: session.includeImage, recoverCompletedFeedback: true });
    const prefix = { type: 'text', text: `会话 ${session.id} 已结束。` };
    session.resultContent = { content: [prefix, ...body.content], isError: body.isError };
  }
  return session.resultContent;
}

// ---------------------------------------------------------------- MCP 工具定义

const FEEDBACK_NOTE = '返回结构化反馈摘要；视频反馈按帧提供时间戳、坐标及修改意见，同坐标的不同时间点不会合并。反馈 JSON 原文保存在输出的反馈文件路径，需要原图或帧截图 data URL 时可用文件工具读取该文件。includeImage 最多附带 8 张视频帧截图，未附图的帧会明确给出提取索引。';

const TOOLS = [
  {
    name: 'edithere_status',
    description: '查询本机 EditHere（改这里）桌面程序的运行与文档状态，不启动 GUI。用于发起标注前确认程序可用。',
    inputSchema: { type: 'object', properties: {}, additionalProperties: false },
  },
  {
    name: 'edithere_open',
    description: '让 EditHere 打开图片、视频或 .edithere 项目（含视频项目）。仅表示请求受理，不等待用户反馈；需要等待用户提交批注请用 edithere_annotate_start。',
    inputSchema: {
      type: 'object',
      required: ['imagePath'],
      properties: {
        imagePath: { type: 'string', description: '图片（PNG/JPEG/WebP/BMP）、视频（如 MP4/MOV/WebM）或 .edithere 项目（含视频项目）的绝对路径；为兼容已有客户端，参数名仍为 imagePath' },
      },
      additionalProperties: false,
    },
  },
  {
    name: 'edithere_capture',
    description: '唤起 EditHere 的截图功能（仅请求受理，不等待反馈）。用户随后可用全局快捷键或界面完成截图。',
    inputSchema: { type: 'object', properties: {}, additionalProperties: false },
  },
  {
    name: 'edithere_annotate',
    description: `发起一次 EditHere 标注会话：打开指定图片、视频或项目，等待用户写批注、调整布局并点击"完成并返回 AI"，然后返回结构化反馈（批注文字、坐标、布局变化；视频还包括每帧时间戳）。视频可播放、拖动时间轴并暂停后逐帧标注。此调用会阻塞直到用户提交、取消或超时（默认 1800 秒）。同一时刻只支持一个标注会话：已有会话进行中时再次调用会返回 busy（退出码 4），请先让用户处理未保存文档、截图或进行中的会话。取消或超时不算收到反馈，不要重试覆盖。${FEEDBACK_NOTE}`,
    inputSchema: {
      type: 'object',
      required: ['imagePath'],
      properties: {
        imagePath: { type: 'string', description: '图片、视频或 .edithere 项目（含视频项目）的绝对路径' },
        timeoutSeconds: { type: 'number', description: '等待用户提交的最长秒数，1–86400，默认 1800' },
        noImage: { type: 'boolean', description: '为 true 时反馈 JSON 不内嵌原图或视频帧截图（默认内嵌）' },
        includeImage: { type: 'boolean', description: '为 true 时在工具结果附原图或视频帧截图（最多 8 张并标明对应时间；其余截图仍保留于 JSON）' },
        outputDir: { type: 'string', description: '反馈 JSON 输出目录（默认系统临时目录下的 edithere-feedback）' },
      },
      additionalProperties: false,
    },
  },
  {
    name: 'edithere_annotate_start',
    description: `打开图片、视频或 .edithere 项目，发起 EditHere 标注会话并立即返回会话 ID，不阻塞等待。推荐用它替代 edithere_annotate：调用立刻返回，你随后告知用户在 EditHere 中批注或调整布局（视频先暂停到目标画面），再点击顶部"完成并返回 AI"，然后用 edithere_annotate_poll 带同一 sessionId 查询结果（等待期间可以处理其他独立任务）。同一时刻只支持一个标注会话，已有会话进行中时不会重复发起，会提示先处理该会话。取消或超时不算收到反馈，不要重试覆盖。${FEEDBACK_NOTE}`,
    inputSchema: {
      type: 'object',
      required: ['imagePath'],
      properties: {
        imagePath: { type: 'string', description: '图片、视频或 .edithere 项目（含视频项目）的绝对路径' },
        timeoutSeconds: { type: 'number', description: '等待用户提交的最长秒数，1–86400，默认 1800' },
        noImage: { type: 'boolean', description: '为 true 时反馈 JSON 不内嵌原图或视频帧截图（默认内嵌）' },
        includeImage: { type: 'boolean', description: '为 true 时完成后的工具结果附原图或视频帧截图（最多 8 张并标明对应时间；其余截图仍保留于 JSON）' },
        outputDir: { type: 'string', description: '反馈 JSON 输出目录（默认系统临时目录下的 edithere-feedback）' },
      },
      additionalProperties: false,
    },
  },
  {
    name: 'edithere_annotate_poll',
    description: `查询 edithere_annotate_start 发起的标注会话。会话仍在等待用户提交时返回已等待秒数；用户已点击"完成并返回 AI"时返回结构化反馈摘要（批注文字、坐标、布局变化），与 edithere_annotate 的返回格式一致。省略 sessionId 时查询当前进行中的会话。连接器进程重启后旧会话 ID 失效，此时先读取发起时返回的反馈文件路径，确认没有完整反馈后再考虑重新发起。${FEEDBACK_NOTE}`,
    inputSchema: {
      type: 'object',
      properties: {
        sessionId: { type: 'string', description: 'edithere_annotate_start 返回的会话 ID；省略时查询当前进行中的会话' },
      },
      additionalProperties: false,
    },
  },
  {
    name: 'edithere_export',
    description: `离线把已保存的 .edithere 项目（含视频项目）、图片或有效反馈 JSON 转为反馈 JSON，不等待用户输入。视频项目导出已保存的标注帧，不需要源视频文件仍然存在；新视频的标注请先用 edithere_annotate_start。${FEEDBACK_NOTE}`,
    inputSchema: {
      type: 'object',
      required: ['imagePath'],
      properties: {
        imagePath: { type: 'string', description: '.edithere 项目（含视频项目）、图片或有效图片/视频反馈 JSON 的绝对路径' },
        noImage: { type: 'boolean', description: '为 true 时反馈 JSON 不内嵌原图或视频帧截图（默认内嵌）' },
        includeImage: { type: 'boolean', description: '为 true 时工具结果附原图或视频帧截图（最多 8 张并标明对应时间；其余截图仍保留于 JSON）' },
        outputDir: { type: 'string', description: '反馈 JSON 输出目录（默认系统临时目录下的 edithere-feedback）' },
      },
      additionalProperties: false,
    },
  },
];

// ---------------------------------------------------------------- 工具实现

// 参数校验：必填的图片/项目路径缺失时给出明确原因，不把 undefined 透给 CLI。
function requirePathArg(p, name = 'imagePath') {
  const value = p[name];
  if (typeof value !== 'string' || !value.trim()) {
    return {
      content: [{
        type: 'text',
        text: `缺少必填参数 ${name}：需要图片、视频或 .edithere 项目文件的路径。建议传入绝对路径。`,
      }],
      isError: true,
    };
  }
  return null;
}

// EditHere 程序缺失是市场用户最常遇到的失败，给出可执行的安装/指定路径指引。
const INSTALL_HINT = [
  'EditHere 是需要单独安装的本机桌面程序，仅安装本连接器不会带上程序本体。',
  '安装：从 https://github.com/Inginnng/EditHere/releases/latest 下载安装包（Windows 用 setup.exe，macOS 用 DMG）。',
  '已安装但不在默认位置时，把环境变量 EDITHERE_CLI 设为 edithere-cli 的绝对路径：',
  '  Windows 默认 %LOCALAPPDATA%\\Programs\\EditHere\\edithere-cli.exe',
  '  macOS 默认 /Applications/EditHere.app/Contents/MacOS/edithere-cli',
].join('\n');

// CLI 定位失败时统一报错，避免 spawn 失败被误读成文件或桌面问题。
function cliUnavailableContent() {
  return {
    content: [{
      type: 'text',
      text: `edithere-cli 不可用，无法执行本次调用。\nCLI 定位来源: ${CLI.source}\n${CLI.error}\n\n${INSTALL_HINT}`,
    }],
    isError: true,
  };
}

async function toolCall(name, args, notifyProgress) {
  const p = args || {};
  if (CLI.error) return cliUnavailableContent();
  switch (name) {
    case 'edithere_status': {
      const r = await runCli(['status']);
      return statusContent(r);
    }
    case 'edithere_open': {
      const invalid = requirePathArg(p);
      if (invalid) return invalid;
      const r = await runCli(['open', p.imagePath]);
      return cliResultToContent(r);
    }
    case 'edithere_capture': {
      const r = await runCli(['capture']);
      return cliResultToContent(r);
    }
    case 'edithere_annotate': {
      const invalid = requirePathArg(p);
      if (invalid) return invalid;
      const timeoutSeconds = clampTimeout(p.timeoutSeconds);
      const output = makeOutputPath(p.outputDir, 'annotate');
      const cliArgs = ['annotate', p.imagePath, '--output', output, '--timeout', String(timeoutSeconds)];
      if (p.noImage) cliArgs.push('--no-image');
      if (typeof notifyProgress === 'function') {
        notifyProgress(`已打开 ${path.basename(String(p.imagePath))}，等待用户在 EditHere 中完成批注…`);
      }
      const r = await runCli(cliArgs, {
        timeoutMs: (timeoutSeconds + 60) * 1000,
        onWaiting: () => notifyProgress?.('仍在等待用户在 EditHere 中点击"完成并返回 AI"…'),
      });
      return annotateResultToContent(r, output, { ...p, recoverCompletedFeedback: true });
    }
    case 'edithere_annotate_start': {
      const invalid = requirePathArg(p);
      if (invalid) return invalid;
      const existing = runningSession();
      if (existing) {
        return {
          content: [{
            type: 'text',
            text: `已有一个标注会话在进行中，未重复发起。\n会话 ID: ${existing.id}\n请先用 edithere_annotate_poll（sessionId 传 ${existing.id}）查询结果；若用户已放弃这一轮，请让其先处理完 EditHere 中的窗口再重新发起。`,
          }],
          isError: false,
        };
      }
      reapSessions();
      const started = startAnnotateSession(p);
      if (started.error) {
        return { content: [{ type: 'text', text: started.error }], isError: true };
      }
      const s = started.session;
      return {
        content: [{
          type: 'text',
          text: `标注会话已发起。\n会话 ID: ${s.id}\n请让用户在 EditHere 中批注或调整组件，完成后点击顶部的"完成并返回 AI"，然后用 edithere_annotate_poll 带上 sessionId="${s.id}" 查询结果。等待期间可以处理其他独立任务，不要重复发起标注。\n反馈文件: ${s.outputPath}\n等待上限: ${s.timeoutSeconds} 秒`,
        }],
        isError: false,
      };
    }
    case 'edithere_annotate_poll': {
      let id = typeof p.sessionId === 'string' ? p.sessionId.trim() : '';
      if (!id) id = activeSessionId || '';
      if (!id) {
        return {
          content: [{ type: 'text', text: '当前没有进行中的标注会话。请先用 edithere_annotate_start 发起一次标注。' }],
          isError: false,
        };
      }
      const session = SESSIONS.get(id);
      if (!session) {
        return {
          content: [{
            type: 'text',
            text: `找不到会话 ${id}：连接器进程可能已重启，会话状态不再保留。反馈文件可能已完整写出；请先用文件工具读取 edithere_annotate_start 或之前 poll 返回的“反馈文件”路径，核对完整 JSON 并恢复本轮结果。不要立即重新发起标注。若文件不存在或不完整，再用 edithere_status 检查程序和用户保留的编辑内容后决定下一步。`,
          }],
          isError: true,
        };
      }
      if (session.state === 'running') return pendingSessionContent(session);
      return finishSessionContent(session);
    }
    case 'edithere_export': {
      const invalid = requirePathArg(p);
      if (invalid) return invalid;
      const output = makeOutputPath(p.outputDir, 'export');
      const cliArgs = ['export', p.imagePath, '--output', output];
      if (p.noImage) cliArgs.push('--no-image');
      const r = await runCli(cliArgs, { timeoutMs: 120_000 });
      return annotateResultToContent(r, output, { includeImage: p.includeImage });
    }
    default:
      throw new McpError(-32602, `未知工具: ${name}`);
  }
}

// status 结果附带连接器信息与实际使用的 CLI，便于排查"用了哪一份 CLI"。
function statusContent(r) {
  const ok = cliSucceeded(r);
  if (!ok) return cliResultToContent(r);
  const body = {
    ...r.json,
    connector: { name: SERVER_NAME, version: SERVER_VERSION, cli: CLI.path, cliSource: CLI.source },
  };
  return { content: [{ type: 'text', text: JSON.stringify(body, null, 2) }], isError: false };
}

function clampTimeout(t) {
  const n = Number(t);
  if (!Number.isFinite(n)) return 1800;
  return Math.min(86400, Math.max(1, Math.round(n)));
}

function cliErrorText(r) {
  const code = r.error?.code || r.json?.error?.code;
  const message = r.error?.message || r.json?.error?.message;
  const hint = EXIT_CODE_HINTS[r.exitCode];
  const parts = [`edithere-cli 退出码 ${r.exitCode}`];
  if (r.timedOut) parts.push('连接器等待 CLI 超时，已终止本次进程。');
  if (code) parts.push(`error.code=${code}`);
  if (message) parts.push(message);
  if (hint) parts.push(`提示: ${hint}`);
  parts.push(`CLI: ${CLI.path}（来源: ${CLI.source}）`);
  if (!r.json && r.stderr?.trim()) parts.push(`stderr: ${r.stderr.trim().slice(0, 500)}`);
  return parts.join('\n');
}

// CLI 一行 JSON 结果 → MCP content
function cliSucceeded(r) {
  return r.exitCode === 0 && !r.timedOut && !r.error && r.json?.ok === true;
}

function cliResultToContent(r) {
  const ok = cliSucceeded(r);
  const body = r.json
    ? JSON.stringify(r.json, null, 2)
    : (r.stdout || r.stderr || '（无输出）').trim();
  return {
    content: [{
      type: 'text',
      text: ok ? body : `命令未成功完成。\n${cliErrorText(r)}\n\n原始输出:\n${body}`,
    }],
    isError: !ok,
  };
}

// annotate/export 结果 → 反馈摘要（+可选原图）
function annotateResultToContent(r, outputPath, p) {
  const ok = cliSucceeded(r);
  // Only annotation callers opt into recovery. Rejections, cancellation and
  // timeouts must retain their original meaning even if a file happens to exist.
  const rejectedExit = [2, 4, 5, 6, 7, 8].includes(r.exitCode);
  const rejectedResult = ['cancelled', 'timeout'].includes(r.json?.error?.code);
  const responseLost = r.json?.ok === false && r.json.error?.code === 'connection_error'
    && r.json.connection?.phase === 'response';
  const sameOutput = typeof r.json?.output === 'string'
    && (process.platform === 'win32'
      ? path.resolve(r.json.output).toLowerCase() === path.resolve(outputPath).toLowerCase()
      : path.resolve(r.json.output) === path.resolve(outputPath));
  const recover = !ok && p.recoverCompletedFeedback && !r.timedOut && !r.error && !rejectedExit && !rejectedResult
    && (!r.json || responseLost || (r.json.ok === true && sameOutput));
  if (!ok && !recover) return cliResultToContent(r);

  const content = [];
  let feedback = null;
  try {
    const file = recover ? fs.lstatSync(outputPath) : fs.statSync(outputPath);
    if (!file.isFile()) throw new Error('反馈路径不是普通文件。');
    if (file.size > MAX_FEEDBACK_FILE_BYTES) {
      throw new Error('反馈文件超过 512 MiB 的连接器读取上限。请拆分标注项目或压缩帧截图。');
    }
    feedback = JSON.parse(fs.readFileSync(outputPath, 'utf8'));
    if (!isRecord(feedback)) throw new Error('反馈 JSON 顶层必须是对象。');
    if (isVideoFeedback(feedback)) {
      validateVideoFeedback(feedback);
      if (recover) feedback.frames.forEach((frame) => validateImageRecoveryFeedback(frame.feedback));
    } else if (feedback.video !== undefined || feedback.frames !== undefined) {
      throw new Error(`不支持的视频反馈版本: ${String(feedback.schemaVersion || '未指定')}；需要 video-feedback-1。`);
    } else if (recover) validateImageRecoveryFeedback(feedback);
  } catch (error) {
    if (recover) {
      const failure = cliResultToContent(r);
      failure.content.push({ type: 'text', text: `未能从反馈文件恢复本轮结果: ${outputPath}\n${String(error?.message || error)}\n请保留 EditHere 中的编辑内容，确认文件状态后再决定下一步。` });
      return failure;
    }
    return {
      content: [{ type: 'text', text: `命令回执成功但反馈文件无法读取或解析: ${outputPath}\n${String(error?.message || error)}` }],
      isError: true,
    };
  }
  if (recover) content.push({ type: 'text', text: 'CLI 完成回执中断；已从本轮输出路径的完整有效反馈文件恢复结果。该结果仅确认反馈文件已保存，不代表桌面程序仍在运行。' });
  content.push({ type: 'text', text: summarizeFeedback(feedback, outputPath) });
  if (p.includeImage) {
    if (isVideoFeedback(feedback)) content.push(...videoImageContents(feedback, outputPath));
    else {
      const imageContent = dataUrlToImageContent(feedback.image);
      if (imageContent) content.push(imageContent);
      else content.push({ type: 'text', text: '反馈中未包含原图 data URL。' });
    }
  }
  return { content, isError: false };
}

// ---------------------------------------------------------------- MCP 框架

class McpError extends Error {
  constructor(code, message) {
    super(message);
    this.code = code;
  }
}

function writeMessage(obj) {
  if (closing) return;
  process.stdout.write(JSON.stringify(obj) + '\n');
}

function reply(id, result) {
  writeMessage({ jsonrpc: '2.0', id, result });
}

function replyError(id, code, message) {
  writeMessage({ jsonrpc: '2.0', id, error: { code, message } });
}

function toolDef(t) {
  return {
    name: t.name,
    description: t.description,
    inputSchema: t.inputSchema,
  };
}

async function handleRequest(msg) {
  const { id, method, params } = msg;
  switch (method) {
    case 'initialize':
      reply(id, {
        protocolVersion: PROTOCOL_VERSION,
        capabilities: { tools: {} },
        serverInfo: { name: SERVER_NAME, version: SERVER_VERSION },
      });
      return;
    case 'ping':
      reply(id, {});
      return;
    case 'tools/list':
      reply(id, { tools: TOOLS.map(toolDef) });
      return;
    case 'tools/call': {
      const name = params?.name;
      const args = params?.arguments || {};
      const progressToken = params?._meta?.progressToken;
      const notifyProgress = (message) => {
        if (progressToken === undefined) return;
        writeMessage({
          jsonrpc: '2.0',
          method: 'notifications/progress',
          params: { progressToken, message },
        });
      };
      try {
        if (!TOOLS.some((t) => t.name === name)) {
          throw new McpError(-32602, `未知工具: ${name}`);
        }
        const result = await toolCall(name, args, notifyProgress);
        reply(id, result);
      } catch (err) {
        if (err instanceof McpError) {
          replyError(id, err.code, err.message);
        } else {
          reply(id, {
            content: [{ type: 'text', text: `工具执行异常: ${String(err?.stack || err)}` }],
            isError: true,
          });
        }
      }
      return;
    }
    default:
      if (id !== undefined) replyError(id, -32601, `方法不存在: ${method}`);
  }
}

async function main() {
  if (process.argv.includes('--help') || process.argv.includes('-h')) {
    process.stdout.write(`EditHere MCP 连接器 v${SERVER_VERSION}\nCLI: ${CLI.path}（来源: ${CLI.source}）\nstdio MCP server；支持图片、视频及 .edithere 视频项目。视频反馈按帧返回时间戳、坐标和批注，可附带帧截图。\n工具: ${TOOLS.map((t) => t.name).join(', ')}\n`);
    process.exit(0);
  }
  process.stderr.write(`[edithere-mcp] CLI: ${CLI.path}（来源: ${CLI.source}）${CLI.error ? ' — 不可用' : ''}\n`);
  process.once('exit', terminateCliChildren);
  process.once('SIGINT', () => process.exit(0));
  process.once('SIGTERM', () => process.exit(0));
  process.stdout.on('error', (err) => {
    if (err.code === 'EPIPE') process.exit(0);
    else throw err;
  });
  const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
  try {
    for await (const line of rl) {
      const t = line.trim();
      if (!t) continue;
      let msg;
      try {
        msg = JSON.parse(t);
      } catch {
        replyError(null, -32700, '消息不是有效的 JSON。');
        continue;
      }
      if (!msg || Array.isArray(msg) || typeof msg !== 'object'
          || msg.jsonrpc !== '2.0' || typeof msg.method !== 'string') {
        replyError(null, -32600, '消息不是有效的 JSON-RPC 请求。');
        continue;
      }
      // Keep long annotation requests from blocking status and polling calls.
      handleRequest(msg).catch((err) => {
        if (msg.id !== undefined) replyError(msg.id, -32603, String(err?.message || err));
      });
    }
  } finally {
    // Host disconnects must cancel waiting CLI sessions as well; otherwise the
    // orphan CLI keeps the GUI busy until its annotation timeout expires.
    terminateCliChildren();
  }
}

// Keep the transport entry point separate from the helpers so their real child
// processes can be covered by Node's built-in test runner.
export { runCli, cliResultToContent, statusContent, annotateResultToContent, summarizeFeedback, main };

if (process.argv[1]
    && fs.realpathSync(process.argv[1]) === fs.realpathSync(fileURLToPath(import.meta.url))) {
  main().catch((err) => {
    process.stderr.write(`[edithere-mcp] fatal: ${String(err?.stack || err)}\n`);
    process.exit(1);
  });
}
