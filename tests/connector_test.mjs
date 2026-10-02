import test from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

// Node itself is an executable CLI fixture. No GUI, shell scripts, installed
// EditHere binary, or third-party test package is required.
process.env.EDITHERE_CLI = process.execPath;
const connectorUrl = new URL('../connector/mcp-server.mjs', import.meta.url);
const { runCli, cliResultToContent, statusContent, annotateResultToContent } = await import(connectorUrl);

function videoFeedback(frameCount = 2) {
  const frames = Array.from({ length: frameCount }, (_, index) => ({
    id: `frame-${index}`,
    timestampMs: 12540 + index * 1000,
    timestampUs: (12540 + index * 1000) * 1000 + 123,
    feedback: {
      annotationSpace: 'result',
      image: `data:image/png;base64,${Buffer.from(`image-for-frame-${index}`).toString('base64')}`,
      objects: [{
        source: { x1: 120, y1: 80, x2: 360, y2: 160 },
        movements: [],
        annotations: [index === 0 ? '标题太早出现，延后到下一秒' : `这一帧的标题改为蓝色 ${index}`],
      }],
    },
  }));
  return {
    schemaVersion: 'video-feedback-1',
    video: { source: 'E:/宣传/动画/demo.mp4', durationMs: 12540 + frameCount * 1000, width: 1920, height: 1080 },
    frames,
    objects: frames.flatMap((frame) => frame.feedback.objects.map((object) => ({ ...object, frameId: frame.id, timestampMs: frame.timestampMs }))),
  };
}

function feedbackResult(feedback, options = {}, files = {}) {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'edithere-feedback-test-'));
  const filename = path.join(directory, 'feedback.json');
  try {
    fs.writeFileSync(filename, JSON.stringify(feedback));
    for (const [name, bytes] of Object.entries(files)) fs.writeFileSync(path.join(directory, name), bytes);
    return annotateResultToContent({ exitCode: 0, json: { ok: true } }, filename, options);
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
}

function resultText(result) {
  return result.content.filter((item) => item.type === 'text').map((item) => item.text).join('\n');
}

test('video summaries keep different edits at identical coordinates in their own frames', () => {
  const result = feedbackResult(videoFeedback());
  assert.equal(result.isError, false);
  const text = resultText(result);
  assert.match(text, /frameId=frame-0，时间 00:00:12\.540/);
  assert.match(text, /frameId=frame-1，时间 00:00:13\.540/);
  assert.match(text, /timestampUs=12540123/);
  assert.equal(text.split('标题太早出现，延后到下一秒').length - 1, 1, 'root expanded objects must not duplicate frame annotations');
  assert.equal(text.split('这一帧的标题改为蓝色 1').length - 1, 1);
  assert.equal(text.split('原图区域(120, 80) 240x80').length - 1, 2);
  assert.ok(!text.includes(Buffer.from('image-for-frame-0').toString('base64')), 'summary must not repeat image payload');
  assert.equal(result.content.filter((item) => item.type === 'image').length, 0);
});

test('video image blocks each have the corresponding frame identifier and timestamp', () => {
  const feedback = videoFeedback();
  const result = feedbackResult(feedback, { includeImage: true });
  assert.equal(result.isError, false);
  for (let index = 0; index < 2; index += 1) {
    const imageIndex = result.content.findIndex((item) => item.type === 'image' && item.data === feedback.frames[index].feedback.image.split(',')[1]);
    assert.ok(imageIndex > 0);
    assert.equal(result.content[imageIndex].mimeType, 'image/png');
    assert.match(result.content[imageIndex - 1].text, new RegExp(`frameId=frame-${index}`));
    assert.match(result.content[imageIndex - 1].text, new RegExp(`timestampMs=${feedback.frames[index].timestampMs}`));
    assert.match(result.content[imageIndex - 1].text, new RegExp(`frames\\[${index}\\]\\.feedback\\.image`));
  }
});

test('video point feedback is described as a located point rather than an empty rectangle', () => {
  const feedback = videoFeedback(1);
  feedback.frames[0].feedback.objects[0].source = { x1: 1187, y1: 528, x2: 1187, y2: 528 };
  feedback.objects[0].source = feedback.frames[0].feedback.objects[0].source;
  const result = feedbackResult(feedback);
  assert.equal(result.isError, false);
  assert.match(resultText(result), /原图点\(1187, 528\)/);
  assert.equal(resultText(result).includes('0x0'), false);
});

test('video result image limit explicitly identifies omitted frames and extraction paths', () => {
  const feedback = videoFeedback(11);
  const result = feedbackResult(feedback, { includeImage: true });
  assert.equal(result.isError, false);
  assert.equal(result.content.filter((item) => item.type === 'image').length, 8);
  const text = resultText(result);
  assert.match(text, /3 张截图未在此次工具结果附上/);
  assert.match(text, /n = 8, 9, 10/);
  assert.match(text, /frames\[n\]\.feedback\.image/);
  assert.match(text, /这一帧的标题改为蓝色 10/);
});

test('video frames without embedded images remain useful and report the visual limitation', () => {
  const feedback = videoFeedback();
  feedback.frames.forEach((frame) => { delete frame.feedback.image; });
  const result = feedbackResult(feedback, { includeImage: true });
  assert.equal(result.isError, false);
  assert.match(resultText(result), /内嵌截图数量: 0/);
  assert.match(resultText(result), /2 帧未内嵌可用的 PNG\/JPEG 截图/);
  assert.match(resultText(result), /标题太早出现/);
});

test('video imageFile references read same-directory screenshots and keep frame association', () => {
  const feedback = videoFeedback();
  const files = {};
  feedback.frames.forEach((frame, index) => {
    delete frame.feedback.image;
    frame.imageFile = `frame-${index}.png`;
    files[frame.imageFile] = Buffer.from(`external-screenshot-${index}`);
  });
  const result = feedbackResult(feedback, { includeImage: true }, files);
  assert.equal(result.isError, false);
  const images = result.content.filter((item) => item.type === 'image');
  assert.equal(images.length, 2);
  assert.equal(images[1].data, files['frame-1.png'].toString('base64'));
  assert.match(resultText(result), /frames\[1\]\.imageFile = frame-1\.png/);
});

test('video external screenshot references cannot escape the feedback directory', () => {
  const feedback = videoFeedback(1);
  delete feedback.frames[0].feedback.image;
  feedback.frames[0].imageFile = '../private.png';
  const result = feedbackResult(feedback, { includeImage: true });
  assert.equal(result.isError, true);
  assert.match(resultText(result), /不能包含路径/);
});

test('video external image byte limit avoids reading large files and names the omitted frame', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'edithere-feedback-limit-'));
  try {
    const feedback = videoFeedback(1);
    delete feedback.frames[0].feedback.image;
    feedback.frames[0].imageFile = 'frame-large.png';
    const file = fs.openSync(path.join(directory, 'frame-large.png'), 'w');
    fs.ftruncateSync(file, 20 * 1024 * 1024);
    fs.closeSync(file);
    const filename = path.join(directory, 'feedback.json');
    fs.writeFileSync(filename, JSON.stringify(feedback));
    const result = annotateResultToContent({ exitCode: 0, json: { ok: true } }, filename, { includeImage: true });
    assert.equal(result.isError, false);
    assert.equal(result.content.filter((item) => item.type === 'image').length, 0);
    assert.match(resultText(result), /1 张截图未在此次工具结果附上/);
    assert.match(resultText(result), /n = 0/);
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
});

test('large video summaries state their limit and leave all frame payloads in the JSON', () => {
  const feedback = videoFeedback(140);
  const result = feedbackResult(feedback);
  assert.equal(result.isError, false);
  assert.match(resultText(result), /标注帧数量: 140/);
  assert.match(resultText(result), /摘要限量/);
  assert.match(resultText(result), /帧索引范围 0–139/);
  assert.equal(resultText(result).includes('frameId=frame-139'), false);
});

test('video validation rejects malformed frames, timestamps, images and expanded index mismatches', () => {
  const cases = [
    [() => null, /顶层必须是对象/],
    [(feedback) => { feedback.frames = null; return feedback; }, /frames 必须是数组/],
    [(feedback) => { feedback.frames[1].id = feedback.frames[0].id; return feedback; }, /id 必须是唯一/],
    [(feedback) => { feedback.frames[0].timestampMs = -1; return feedback; }, /timestampMs 必须位于/],
    [(feedback) => { feedback.frames[0].timestampUs += 1000; return feedback; }, /timestampUs 与 timestampMs 不一致/],
    [(feedback) => { feedback.frames[0].feedback.image = 'data:image/png;base64,not valid!'; return feedback; }, /不是有效的 PNG\/JPEG/],
    [(feedback) => { feedback.frames[0].feedback.objects[0].source = {}; return feedback; }, /不是有效的像素矩形/],
    [(feedback) => { feedback.objects[1].frameId = 'wrong-frame'; return feedback; }, /没有匹配的帧/],
    [(feedback) => { feedback.objects[1] = { ...feedback.objects[1], annotations: ['wrong edit'] }; return feedback; }, /与对应帧的批注内容不一致/],
    [(feedback) => { feedback.objects.pop(); return feedback; }, /遗漏了帧中的对象/],
    [(feedback) => { feedback.schemaVersion = 'video-feedback-2'; return feedback; }, /不支持的视频反馈版本/],
  ];
  for (const [mutate, error] of cases) {
    const result = feedbackResult(mutate(videoFeedback()));
    assert.equal(result.isError, true);
    assert.match(resultText(result), error);
  }
});

test('original object and legacy image feedback return unchanged text and a single image', () => {
  const image = `data:image/png;base64,${Buffer.from('original-image').toString('base64')}`;
  const objectFeedback = { annotationSpace: 'result', image, objects: [{ source: null, movements: [], annotations: ['全局修改意见'] }] };
  const legacyFeedback = { annotationSpace: 'result', image, annotations: [{ point: { x: 10, y: 20 }, text: '旧版点意见' }], changes: [] };
  const objectResult = feedbackResult(objectFeedback, { includeImage: true });
  const legacyResult = feedbackResult(legacyFeedback, { includeImage: true });
  assert.equal(objectResult.isError, false);
  assert.equal(legacyResult.isError, false);
  assert.match(resultText(objectResult), /对象数量: 1，批注数量: 1，移动数量: 0，包含原图: 是/);
  assert.match(resultText(objectResult), /全局对象（无 source）/);
  assert.match(resultText(legacyResult), /点\(10, 20\): 旧版点意见/);
  assert.equal(objectResult.content.filter((item) => item.type === 'image').length, 1);
  assert.equal(legacyResult.content.filter((item) => item.type === 'image').length, 1);
  assert.equal(resultText(objectResult).includes('video-feedback-1'), false);
});

test('CLI output preserves a multibyte character split between stream chunks', async () => {
  const script = `
    const bytes = Buffer.from(JSON.stringify({ok: true, message: '中文路径'}) + '\\n');
    const split = bytes.indexOf(Buffer.from('中')) + 1;
    process.stdout.write(bytes.subarray(0, split));
    setTimeout(() => process.stdout.write(bytes.subarray(split)), 100);
  `;
  const result = await runCli(['-e', script]);
  assert.equal(result.exitCode, 0);
  assert.equal(result.json.message, '中文路径');
  assert.equal(cliResultToContent(result).isError, false);
});

test('a CLI crash after printing success is reported as failure', async () => {
  const result = await runCli(['-e', "console.log(JSON.stringify({ok:true,running:true})); process.exitCode=9"]);
  assert.equal(result.exitCode, 9);
  assert.equal(result.json.ok, true);
  assert.equal(cliResultToContent(result).isError, true);
  assert.equal(statusContent(result).isError, true);
});

test('a timed-out CLI cannot turn earlier success output into success', async () => {
  const result = await runCli(['-e', "console.log(JSON.stringify({ok:true})); setInterval(()=>{},1000)"], { timeoutMs: 1000 });
  assert.equal(result.timedOut, true);
  assert.equal(result.json.ok, true);
  assert.equal(cliResultToContent(result).isError, true);
});

test('excessive child-process output is bounded and reported as failure', async () => {
  const result = await runCli(['-e', "process.stdout.write('x'.repeat(2*1024*1024)); setInterval(()=>{},1000)"], { timeoutMs: 3000 });
  assert.equal(result.error?.code, 'output_limit');
  assert.ok(result.stdout.length <= 1024 * 1024);
  assert.equal(cliResultToContent(result).isError, true);
});

test('closing MCP stdin terminates an outstanding CLI child', { timeout: 5000 }, async () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'edithere-connector-test-'));
  const pidFile = path.join(directory, 'child.pid');
  const runner = path.join(directory, 'runner.mjs');
  fs.writeFileSync(runner, `
    import { runCli, main } from ${JSON.stringify(connectorUrl.href)};
    runCli(['-e', ${JSON.stringify(`require('fs').writeFileSync(${JSON.stringify(pidFile)}, String(process.pid)); setInterval(()=>{},1000)`)}]);
    await main();
  `);
  const server = spawn(process.execPath, [runner], { stdio: ['pipe', 'pipe', 'pipe'] });
  let childPid;
  try {
    const started = Date.now();
    while (!fs.existsSync(pidFile) && Date.now() - started < 2000) {
      await new Promise((resolve) => setTimeout(resolve, 20));
    }
    assert.ok(fs.existsSync(pidFile), 'the test CLI child must have started');
    childPid = Number(fs.readFileSync(pidFile, 'utf8'));
    const closed = once(server, 'close');
    server.stdin.end();
    const [code] = await Promise.race([
      closed,
      new Promise((_, reject) => setTimeout(() => reject(new Error('connector did not exit after stdin EOF')), 2000).unref()),
    ]);
    assert.equal(code, 0);
    assert.throws(() => process.kill(childPid, 0), { code: 'ESRCH' });
  } finally {
    server.kill('SIGKILL');
    if (childPid) { try { process.kill(childPid, 'SIGKILL'); } catch { /* already gone */ } }
    fs.rmSync(directory, { recursive: true, force: true });
  }
});

test('MCP reports malformed JSON and remains available for the next request', { timeout: 5000 }, async () => {
  const server = spawn(process.execPath, [fileURLToPath(connectorUrl)], { stdio: ['pipe', 'pipe', 'pipe'] });
  server.stdout.setEncoding('utf8');
  let output = '';
  server.stdout.on('data', (chunk) => { output += chunk; });
  try {
    server.stdin.write('{not-json}\n');
    server.stdin.write(JSON.stringify({ jsonrpc: '2.0', id: 7, method: 'ping' }) + '\n');
    const started = Date.now();
    while (!output.includes('"id":7') && Date.now() - started < 2000) {
      await new Promise((resolve) => setTimeout(resolve, 20));
    }
    const replies = output.trim().split('\n').filter(Boolean).map((line) => JSON.parse(line));
    assert.deepEqual(replies.map((reply) => reply.id), [null, 7]);
    assert.equal(replies[0].error.code, -32700);
    assert.deepEqual(replies[1].result, {});
  } finally {
    server.kill('SIGKILL');
  }
});
