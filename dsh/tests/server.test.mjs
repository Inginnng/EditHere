import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import http from 'node:http';
import crypto from 'node:crypto';
import {startServer} from '../server.mjs';
import {createRequire} from 'node:module';

const require = createRequire(import.meta.url);
const feedbackFormat = require('../../web/src/feedback.js');
// A 1 × 1 opaque PNG; server tests only need a valid signature and decodable page.
const PNG = 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==';
const exportedAt = '2026-11-07T12:00:00+08:00';

function request(port, endpoint, {method = 'GET', body, token, key} = {}) {
  return new Promise((resolve, reject) => {
    const content = body === undefined ? undefined : JSON.stringify(body);
    const headers = {};
    if (content) { headers['Content-Type'] = 'application/json'; headers['Content-Length'] = Buffer.byteLength(content); }
    if (key) headers.Authorization = 'Bearer ' + key;
    if (token) headers.Origin = 'http://127.0.0.1:' + port;
    const req = http.request({host:'127.0.0.1', port, path:endpoint, method, headers}, res => {
      let chunks = [];
      res.on('data', chunk => chunks.push(chunk));
      res.on('end', () => {
        let data;
        try { data = JSON.parse(Buffer.concat(chunks).toString('utf8')); } catch { data = {}; }
        if (res.statusCode < 200 || res.statusCode >= 300) reject(Object.assign(new Error(data.error || 'HTTP ' + res.statusCode), {status:res.statusCode}));
        else resolve(data);
      });
    });
    req.on('error', reject);
    req.end(content);
  });
}
function setFeedback() {
  const page = feedbackFormat.exportImage({image:PNG, movements:[],
    notes:[{source:{x1:0,y1:0,x2:0,y2:0}, text:'把标题放大'}]
  }, feedbackFormat.isTypedVersion ? 'embedded' : true, exportedAt);
  return feedbackFormat.exportSet({
    name:'测试组图',
    pageIndex:[
      {id:'p1', name:'home', width:1, height:1},
      {id:'p2', name:'settings', width:1, height:1}
    ],
    pages:[{id:'p2', name:'settings', width:1, height:1, feedback:page}],
    groupNotes:[{text:'整体保持深色'}]
  }, exportedAt);
}

test('DSH server creates, extends and completes a set with only annotated images', async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'edithere-dsh-test-'));
  const input = path.join(root, 'home.png');
  fs.writeFileSync(input, Buffer.from(PNG.split(',')[1], 'base64'));
  const output = path.join(root, 'feedback.json');
  const {server, store, port} = await startServer({port:0, directory:path.join(root, 'data')});
  try {
    const session = await request(port, '/__edithere_dsh/sessions', {method:'POST', key:store.key,
      body:{name:'测试组图', output, timeoutSeconds:60, images:[{path:input}]}});
    assert.match(session.sessionId, /^[a-f0-9]{32}$/);
    assert.match(session.url, /^http:\/\/127\.0\.0\.1:\d+\/\?dsh=[a-f0-9]{64}$/);

    const browser = await request(port, '/__edithere_dsh/session?token=' + session.token);
    assert.equal(browser.state, 'pending');
    assert.equal(browser.pages.length, 1);
    assert.equal(browser.pages[0].name, 'home');
    assert.match(browser.pages[0].dataUrl, /^data:image\/png;base64,/);

    const second = path.join(root, 'settings.png');
    fs.writeFileSync(second, Buffer.from(PNG.split(',')[1], 'base64'));
    const added = await request(port, '/__edithere_dsh/sessions/' + session.sessionId + '/pages', {method:'POST', key:store.key, body:{images:[{path:second}]}});
    assert.equal(added.pageCount, 2);
    assert.equal(added.added.length, 1);

    const draft = {schemaVersion:'edithere-web-project-1', kind:'set', name:'测试组图', pages:[{id:'p1', name:'home', image:PNG, notes:[{id:'n1', source:{x1:0,y1:0,x2:0,y2:0}, text:'草稿意见'}], layout:null}], groupNotes:[]};
    const saved = await request(port, '/__edithere_dsh/draft?token=' + session.token, {method:'POST', token:true, body:{project:draft}});
    assert.equal(saved.state, 'pending');
    assert.match(saved.draftSavedAt, /^\d{4}-\d{2}-\d{2}T/);
    const restored = await request(port, '/__edithere_dsh/session?token=' + session.token);
    assert.equal(restored.draft.pages[0].notes[0].text, '草稿意见');
    assert.equal(restored.draft.pages[0].image, PNG);

    const receipt = await request(port, '/__edithere_dsh/feedback?token=' + session.token, {method:'POST', token:true,
      body:{feedback:setFeedback(), annotated:[{id:'p2', dataUrl:PNG}]}});
    assert.equal(receipt.state, 'completed');
    assert.equal(receipt.summary.annotations, 1);
    assert.equal(receipt.summary.groupNotes, 1);
    assert.equal(receipt.summary.annotatedPageCount, 1);
    assert.equal(receipt.pages.length, 1);
    assert.match(receipt.sha256, /^[a-f0-9]{64}$/);
    assert.ok(fs.existsSync(receipt.pages[0].sourcePath));
    assert.ok(fs.existsSync(receipt.pages[0].annotatedPath));

    const feedback = JSON.parse(fs.readFileSync(output, 'utf8'));
    assert.equal(feedback.schemaVersion, 'image-set-feedback-1');
    assert.equal(feedback.set.pageCount, 2);
    assert.equal(feedback.set.annotatedPageCount, 1);
    assert.equal(feedback.pageIndex.length, 2);
    assert.equal(feedback.pageIndex[0].annotated, false);
    assert.equal(feedback.pageIndex[1].annotated, true);
    assert.equal(feedback.pages.length, 1);
    assert.equal(feedback.pages[0].name, 'settings');
    assert.ok(feedback.pages[0].feedback.image === undefined || feedback.pages[0].feedback.image.mode === 'none');
    assert.equal(feedback.pages[0].feedback.objects[0].annotations[0].text, '把标题放大');
    assert.equal(feedback.groupNotes[0].text, '整体保持深色');
    assert.equal(feedback.dsh.sessionId, session.sessionId);
    const body = fs.readFileSync(output, 'utf8');
    assert.equal(body.includes('data:image'), false, 'feedback JSON never embeds page base64');
    assert.ok(fs.existsSync(path.join(root, 'images', 'page-1-settings.png')));
    assert.ok(fs.existsSync(path.join(root, 'images', 'page-1-settings-annotated.png')));
    assert.equal(fs.existsSync(path.join(root, 'images', 'page-2-settings.png')), false, 'unannotated pages never get images');
    assert.equal(receipt.pages[0].name, 'settings');

    const status = await request(port, '/__edithere_dsh/sessions/' + session.sessionId, {key:store.key});
    assert.equal(status.state, 'completed');
    assert.equal(status.pageCount, 2);
    assert.equal(store.readDraft(store.load(session.sessionId)), null);
    await assert.rejects(request(port, '/__edithere_dsh/feedback?token=' + session.token, {method:'POST', token:true,
      body:{feedback:setFeedback(), annotated:[{id:'p2', dataUrl:PNG}]}}), /already exists|completed/i);
  } finally {
    await new Promise(resolve => server.close(resolve));
    fs.rmSync(root, {recursive:true, force:true});
  }
});

test('DSH server rejects malformed and unauthenticated requests', async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'edithere-dsh-reject-'));
  const {server, store, port} = await startServer({port:0, directory:path.join(root, 'data')});
  try {
    await assert.rejects(request(port, '/__edithere_dsh/sessions', {method:'POST', body:{name:'x', output:path.join(root, 'x.json')}}), /authentication/i);
    const session = await request(port, '/__edithere_dsh/sessions', {method:'POST', key:store.key,
      body:{name:'拒绝测试', output:path.join(root, 'out.json'), timeoutSeconds:60}});
    assert.equal(session.pageCount, 0);
    await assert.rejects(request(port, '/__edithere_dsh/feedback?token=' + session.token, {method:'POST', token:true,
      body:{feedback:{schemaVersion:'unknown'}, annotated:[]}}), /Invalid image set feedback/i);
    await assert.rejects(request(port, '/__edithere_dsh/sessions/not-a-session', {key:store.key}), /not found/i);
    const cancelled = await request(port, '/__edithere_dsh/sessions/' + session.sessionId + '/cancel', {method:'POST', key:store.key, body:{}});
    assert.equal(cancelled.state, 'cancelled');
  } finally {
    await new Promise(resolve => server.close(resolve));
    fs.rmSync(root, {recursive:true, force:true});
  }
});
