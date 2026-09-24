#!/usr/bin/env node
// EditHere MCP 连接器（零依赖，Node.js >= 18，stdio transport）
//
// 把本地 edithere-cli 的 Agent 接口封装为 MCP 工具，供 WorkBuddy 等
// MCP 客户端调用：
//   edithere_status           查询 EditHere 运行与文档状态
//   edithere_open             打开图片或 .edithere 项目（仅请求受理）
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
// 反馈 JSON 遵循 schema/feedback-v0.7.schema.json（并兼容新版 objects 结构）。

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

// ---------------------------------------------------------------- CLI 调用

function runCli(args, { timeoutMs = 60_000, onWaiting } = {}) {
  return new Promise((resolve) => {
    let child;
    try {
      child = spawn(CLI.path, args, { stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true });
    } catch (err) {
      resolve({ ok: false, exitCode: -1, error: { code: 'spawn_failed', message: String(err?.message || err) } });
      return;
    }
    let stdout = '';
    let stderr = '';
    let timedOut = false;
    const timer = setTimeout(() => {
      timedOut = true;
      child.kill('SIGKILL');
    }, timeoutMs);

    let waitingTimer = null;
    if (typeof onWaiting === 'function') {
      waitingTimer = setInterval(() => onWaiting(), 60_000);
      waitingTimer.unref?.();
    }

    child.stdout.on('data', (d) => { stdout += d; });
    child.stderr.on('data', (d) => { stderr += d; });
    child.on('error', (err) => {
      clearTimeout(timer);
      if (waitingTimer) clearInterval(waitingTimer);
      resolve({ ok: false, exitCode: -1, error: { code: 'spawn_failed', message: String(err?.message || err) } });
    });
    child.on('close', (code) => {
      clearTimeout(timer);
      if (waitingTimer) clearInterval(waitingTimer);
      const parsed = parseCliJson(stdout);
      resolve({
        exitCode: code ?? -1,
        json: parsed,
        stdout,
        stderr,
        timedOut,
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
  const match = /^data:(image\/(?:png|jpeg));base64,(.+)$/s.exec(dataUrl || '');
  if (!match) return null;
  return { type: 'image', data: match[2], mimeType: match[1] };
}

function formatRectangle(r) {
  if (!r) return '?';
  return `(${r.x1}, ${r.y1}) ${r.x2 - r.x1}x${r.y2 - r.y1}`;
}

// 把反馈 JSON 摘要成紧凑文本，坐标与 schema 语义保持一致。
// 支持两种格式：0.9.0 面向对象格式（objects）与旧版动作格式（annotations+changes）。
function summarizeFeedback(feedback, feedbackPath) {
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
    child = spawn(CLI.path, cliArgs, { stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true });
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
  child.stdout.on('data', (d) => { session.stdout += d; });
  child.stderr.on('data', (d) => { session.stderr += d; });
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
    };
    const body = annotateResultToContent(r, session.outputPath, { includeImage: session.includeImage });
    const prefix = { type: 'text', text: `会话 ${session.id} 已结束。` };
    session.resultContent = { content: [prefix, ...body.content], isError: body.isError };
  }
  return session.resultContent;
}

// ---------------------------------------------------------------- MCP 工具定义

const FEEDBACK_NOTE = '返回结构化反馈摘要；反馈 JSON 原文保存在输出的反馈文件路径，需要原图 data URL 时可用文件工具读取该文件。';

const TOOLS = [
  {
    name: 'edithere_status',
    description: '查询本机 EditHere（改这里）桌面程序的运行与文档状态，不启动 GUI。用于发起标注前确认程序可用。',
    inputSchema: { type: 'object', properties: {}, additionalProperties: false },
  },
  {
    name: 'edithere_open',
    description: '让 EditHere 打开一张图片或 .edithere 项目。仅表示请求受理，不等待用户反馈；需要等待用户提交批注请用 edithere_annotate。',
    inputSchema: {
      type: 'object',
      required: ['imagePath'],
      properties: {
        imagePath: { type: 'string', description: '图片（PNG/JPEG/WebP/BMP）或 .edithere 项目的绝对路径' },
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
    description: `发起一次 EditHere 标注会话：打开指定图片/项目，等待用户写批注、调整布局并点击"完成并返回 AI"，然后返回结构化反馈（批注文字、坐标、布局变化）。此调用会阻塞直到用户提交、取消或超时（默认 1800 秒）。同一时刻只支持一个标注会话：已有会话进行中时再次调用会返回 busy（退出码 4），请先让用户处理未保存文档、截图或进行中的会话。取消或超时不算收到反馈，不要重试覆盖。${FEEDBACK_NOTE}`,
    inputSchema: {
      type: 'object',
      required: ['imagePath'],
      properties: {
        imagePath: { type: 'string', description: '图片（PNG/JPEG/WebP/BMP）或 .edithere 项目的绝对路径' },
        timeoutSeconds: { type: 'number', description: '等待用户提交的最长秒数，1–86400，默认 1800' },
        noImage: { type: 'boolean', description: '为 true 时反馈 JSON 不内嵌原图（默认内嵌）' },
        includeImage: { type: 'boolean', description: '为 true 时在工具结果中附上原图（图片内容块），便于模型直接查看' },
        outputDir: { type: 'string', description: '反馈 JSON 输出目录（默认系统临时目录下的 edithere-feedback）' },
      },
      additionalProperties: false,
    },
  },
  {
    name: 'edithere_annotate_start',
    description: `发起一次 EditHere 标注会话并立即返回会话 ID，不阻塞等待。推荐用它替代 edithere_annotate：调用立刻返回，你随后告知用户在 EditHere 中批注或调整布局，再点击顶部"完成并返回 AI"，然后用 edithere_annotate_poll 带同一 sessionId 查询结果（等待期间可以处理其他独立任务）。同一时刻只支持一个标注会话，已有会话进行中时不会重复发起，会提示先处理该会话。取消或超时不算收到反馈，不要重试覆盖。${FEEDBACK_NOTE}`,
    inputSchema: {
      type: 'object',
      required: ['imagePath'],
      properties: {
        imagePath: { type: 'string', description: '图片（PNG/JPEG/WebP/BMP）或 .edithere 项目的绝对路径' },
        timeoutSeconds: { type: 'number', description: '等待用户提交的最长秒数，1–86400，默认 1800' },
        noImage: { type: 'boolean', description: '为 true 时反馈 JSON 不内嵌原图（默认内嵌）' },
        includeImage: { type: 'boolean', description: '为 true 时在完成后的工具结果中附上原图（图片内容块）' },
        outputDir: { type: 'string', description: '反馈 JSON 输出目录（默认系统临时目录下的 edithere-feedback）' },
      },
      additionalProperties: false,
    },
  },
  {
    name: 'edithere_annotate_poll',
    description: `查询 edithere_annotate_start 发起的标注会话。会话仍在等待用户提交时返回已等待秒数；用户已点击"完成并返回 AI"时返回结构化反馈摘要（批注文字、坐标、布局变化），与 edithere_annotate 的返回格式一致。省略 sessionId 时查询当前进行中的会话。连接器进程重启后旧会话 ID 失效，此时需重新发起。${FEEDBACK_NOTE}`,
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
    description: `离线把已保存的 .edithere 项目、图片或有效反馈 JSON 转为反馈 JSON，不等待用户输入。${FEEDBACK_NOTE}`,
    inputSchema: {
      type: 'object',
      required: ['imagePath'],
      properties: {
        imagePath: { type: 'string', description: '.edithere 项目、图片或有效反馈 JSON 的绝对路径' },
        noImage: { type: 'boolean', description: '为 true 时反馈 JSON 不内嵌原图（默认内嵌）' },
        includeImage: { type: 'boolean', description: '为 true 时在工具结果中附上原图（图片内容块）' },
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
        text: `缺少必填参数 ${name}：需要图片（PNG/JPEG/WebP/BMP）或 .edithere 项目文件的路径。建议传入绝对路径。`,
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
      return annotateResultToContent(r, output, p);
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
            text: `找不到会话 ${id}：连接器进程可能已重启，会话状态不再保留。请用 edithere_status 确认程序可用后重新发起 edithere_annotate_start。`,
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
      return annotateResultToContent(r, output, p);
    }
    default:
      throw new McpError(-32602, `未知工具: ${name}`);
  }
}

// status 结果附带连接器信息与实际使用的 CLI，便于排查"用了哪一份 CLI"。
function statusContent(r) {
  const ok = r.json?.ok === true;
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
  const code = r.json?.error?.code;
  const message = r.json?.error?.message;
  const hint = EXIT_CODE_HINTS[r.exitCode];
  const parts = [`edithere-cli 退出码 ${r.exitCode}`];
  if (code) parts.push(`error.code=${code}`);
  if (message) parts.push(message);
  if (hint) parts.push(`提示: ${hint}`);
  parts.push(`CLI: ${CLI.path}（来源: ${CLI.source}）`);
  if (!r.json && r.stderr?.trim()) parts.push(`stderr: ${r.stderr.trim().slice(0, 500)}`);
  return parts.join('\n');
}

// CLI 一行 JSON 结果 → MCP content
function cliResultToContent(r) {
  const ok = r.json?.ok === true;
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
  const ok = r.json?.ok === true;
  if (!ok) return cliResultToContent(r);

  const content = [];
  let feedback = null;
  try {
    feedback = JSON.parse(fs.readFileSync(outputPath, 'utf8'));
  } catch {
    return {
      content: [{ type: 'text', text: `命令回执成功但反馈文件无法读取: ${outputPath}\n${cliErrorText(r)}` }],
      isError: true,
    };
  }
  content.push({ type: 'text', text: summarizeFeedback(feedback, outputPath) });
  if (p.includeImage) {
    const imageContent = dataUrlToImageContent(feedback.image);
    if (imageContent) content.push(imageContent);
    else content.push({ type: 'text', text: '反馈中未包含原图 data URL。' });
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
    process.stdout.write(`EditHere MCP 连接器 v${SERVER_VERSION}\nCLI: ${CLI.path}（来源: ${CLI.source}）\nstdio MCP server；工具: ${TOOLS.map((t) => t.name).join(', ')}\n`);
    process.exit(0);
  }
  process.stderr.write(`[edithere-mcp] CLI: ${CLI.path}（来源: ${CLI.source}）${CLI.error ? ' — 不可用' : ''}\n`);
  const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
  for await (const line of rl) {
    const t = line.trim();
    if (!t) continue;
    let msg;
    try {
      msg = JSON.parse(t);
    } catch {
      continue; // 忽略非 JSON 行
    }
    if (msg && msg.jsonrpc === '2.0') {
      // 不 await：顺序处理即可，annotate 长任务阻塞后续请求是可接受的
      handleRequest(msg).catch((err) => {
        if (msg.id !== undefined) replyError(msg.id, -32700, String(err?.message || err));
      });
    }
  }
}

main().catch((err) => {
  process.stderr.write(`[edithere-mcp] fatal: ${String(err?.stack || err)}\n`);
  process.exit(1);
});
