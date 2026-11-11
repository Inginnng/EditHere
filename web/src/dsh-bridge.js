/* DSH bridge: activated only by a capability URL from the local EditHere DSH
 * server. The editor stays a plain local page; this script only moves page
 * images in over the loopback server and posts the explicit submission. */
(() => {
  'use strict';
  const token = new URL(location.href).searchParams.get('dsh');
  if (!token || location.protocol !== 'http:' || location.hostname !== '127.0.0.1' || !/^[a-f0-9]{64}$/i.test(token)) return;
  const $ = id => document.getElementById(id), app = globalThis.EditHereWeb, API = '/__edithere_dsh';
  const submit = $('export'), cancel = $('open');
  const originalExport = submit.onclick, originalOpen = cancel.onclick;
  let session = null, sending = false, events = null, receipt = null;
  let draftTimer = null, draftTail = Promise.resolve(), draftRevision = -1;
  app.bindAgent();
  async function request(route, value) {
    const controller = new AbortController(), timer = setTimeout(() => controller.abort(), 30000);
    try {
      const response = await fetch(API + '/' + route + '?token=' + encodeURIComponent(token), {
        method:value === undefined ? 'GET' : 'POST',
        headers:value === undefined ? {} : {'Content-Type':'application/json'},
        body:value === undefined ? undefined : JSON.stringify(value),
        signal:controller.signal, cache:'no-store', credentials:'omit'
      });
      let result;
      try { result = await response.json(); } catch { throw new Error('本地组图服务返回了无法读取的响应'); }
      if (!response.ok) { const error = new Error(result.error || '本地组图服务不可用'); error.status = response.status; throw error; }
      return result;
    } catch (error) {
      if (error.name === 'AbortError' || error instanceof TypeError) throw new Error('连接暂时不可用，编辑已保留；恢复服务后可再次点击“完成并返回 AI”');
      throw error;
    } finally { clearTimeout(timer); }
  }
  function label(button, text) { button.querySelector('span').textContent = text; }
  function restore() {
    app.releaseAgent();
    cancel.onclick = originalOpen; submit.onclick = originalExport;
    cancel.querySelector('use').setAttribute('href', '#i-open'); label(cancel, '打开');
    cancel.title = '打开图片、视频或项目 (Ctrl / ⌘ + O)'; cancel.setAttribute('aria-label', '打开图片、视频或项目');
    label(submit, '导出反馈'); submit.title = '导出结构化反馈 (Ctrl / ⌘ + E)'; submit.setAttribute('aria-label', '导出反馈');
    submit.disabled = false; cancel.disabled = false;
    $('workspace').inert = false;
    delete document.body.dataset.agentLocked;
    if (events) { events.close(); events = null; }
  }
  function endSession(state, message) {
    document.body.dataset.dshState = state;
    clearTimeout(draftTimer);
    app.releaseAgent(); $('workspace').inert = false; delete document.body.dataset.agentLocked;
    if (events) { events.close(); events = null; }
    if (state === 'completed') {
      app.completedAgent(); document.body.dataset.agentState = 'completed';
      label(submit, '已保存'); submit.disabled = true; submit.onclick = null;
      submit.title = '反馈已保存'; submit.setAttribute('aria-label', submit.title);
      cancel.disabled = false; label(cancel, '复制路径'); cancel.querySelector('use').setAttribute('href', '#i-copy');
      cancel.title = '复制反馈 JSON 路径'; cancel.setAttribute('aria-label', cancel.title);
      cancel.onclick = async () => {
        try { await navigator.clipboard.writeText(receipt.output); app.notify('路径已复制：' + receipt.output); }
        catch { app.notify('反馈路径：' + receipt.output); }
      };
    } else {
      restore();
      submit.disabled = true;
      app.notify(message || (state === 'expired' ? 'AI 批注会话已超时，当前编辑仍可保存或导出' : 'AI 批注会话已取消，当前编辑仍可保存或导出'));
      document.body.dataset.agentState = state;
    }
  }
  function markDshMode(value) {
    document.body.dataset.dshMode = 'true';
    const bar = $('set-bar');
    if (bar) bar.title = 'DSH 收件箱：' + value.inbox + '\n反馈将写入：' + value.output;
    const head = document.querySelector('.set-head');
    if (head && !head.querySelector('.dsh-badge')) {
      const badge = document.createElement('span');
      badge.className = 'dsh-badge'; badge.textContent = 'DSH'; badge.title = 'Agent 会话：截图保存到收件箱会自动加入';
      head.append(badge);
    }
  }
  async function saveDraft() {
    if (sending || !session || document.body.dataset.dshState !== 'pending') return;
    const revision = app.projectRevision();
    if (revision === draftRevision) return;
    const project = app.project();
    const result = await request('draft', {project});
    if (result.state !== 'pending') return;
    draftRevision = revision;
    document.body.dataset.draftState = 'saved';
  }
  function scheduleDraft() {
    if (sending || !session || document.body.dataset.dshState !== 'pending') return;
    document.body.dataset.draftState = 'unsaved';
    clearTimeout(draftTimer);
    draftTimer = setTimeout(() => {
      draftTail = draftTail.catch(() => {}).then(saveDraft).catch(() => {
        document.body.dataset.draftState = 'error';
      });
    }, 900);
  }
  app.onProjectChange(scheduleDraft);
  function connectEvents() {
    if (events || typeof EventSource === 'undefined') return;
    events = new EventSource(API + '/events?token=' + encodeURIComponent(token));
    events.onopen = () => { document.body.dataset.dshConnection = 'live'; };
    events.onerror = () => { document.body.dataset.dshConnection = 'reconnecting'; };
    events.addEventListener('page', event => {
      try { app.addAgentPages([JSON.parse(event.data)]); }
      catch (error) { app.notify('新截图加入失败：' + error.message); }
    });
    events.addEventListener('state', event => {
      let value;
      try { value = JSON.parse(event.data); } catch { return; }
      if (value.state && value.state !== 'pending') endSession(value.state);
    });
  }
  function notifyParent(value) {
    if (window.parent === window) return;
    try {
      window.parent.postMessage({
        type: 'edithere-dsh-complete',
        sessionId: value.sessionId,
        output: value.output,
        summary: value.summary,
        pages: value.pages,
      }, '*');
    } catch { /* a parent that cannot receive messages is not a failure */ }
  }
  async function complete() {
    if (sending || document.body.dataset.dshState !== 'pending') return;
    if ($('note-dialog').open || $('group-note-dialog').open) { app.notify('请先保存或关闭正在编辑的批注'); return; }
    sending = true; submit.disabled = true; cancel.disabled = true; label(submit, '返回中');
    clearTimeout(draftTimer);
    $('workspace').inert = true; document.body.dataset.agentLocked = 'true';
    try {
      const payload = app.feedbackForDsh();
      const result = await request('feedback', payload);
      if (result.state !== 'completed') throw new Error('反馈尚未完成，请重试');
      receipt = result;
      notifyParent(result);
      document.body.dataset.dshState = 'completed';
      endSession('completed');
      app.notify('反馈已保存：' + receipt.summary.annotations + ' 条意见 · ' + receipt.summary.annotatedPageCount + ' 个已批注页面。回到对话让 AI 继续。');
    } catch (error) {
      label(submit, '重试完成'); submit.disabled = false; cancel.disabled = false;
      $('workspace').inert = false; delete document.body.dataset.agentLocked;
      app.notify(error.message);
    } finally { sending = false; }
  }
  async function cancelSession() {
    if (sending || !app.canCancel()) return;
    sending = true; submit.disabled = true; cancel.disabled = true;
    try {
      const result = await request('cancel', {});
      endSession(result.state === 'completed' ? 'completed' : 'cancelled', 'AI 批注会话已取消，当前编辑仍可保存或导出');
    } catch (error) {
      submit.disabled = false; cancel.disabled = false; app.notify(error.message);
    } finally { sending = false; }
  }
  async function load() {
    document.body.dataset.dshState = 'loading'; submit.disabled = true; cancel.disabled = true;
    $('drop-card').disabled = true; $('demo').disabled = true;
    try {
      const value = await request('session');
      if (value.state !== 'pending') {
        document.body.dataset.dshState = value.state;
        restore(); submit.disabled = true;
        app.notify(value.state === 'completed' ? '这次反馈已保存，回到对话查看' : '这次 AI 批注会话已结束，可以重新发起');
        return;
      }
      session = value;
      if (value.draft) {
        if (value.draft.kind === 'set') {
          await app.loadAgentSetProject(value.draft,{editablePages:true});
          const known = new Set(value.draft.pages.map(page => page.id));
          const missing = value.pages.filter(page => !known.has(page.id));
          if (missing.length) await app.addAgentPages(missing);
        } else if (value.draft.kind === 'image') {
          const image = value.draft.document?.image;
          if (typeof image !== 'string') throw new Error('批注草稿缺少原图');
          await app.loadAgentProject({project:value.draft, image, name:value.draft.name || value.name});
        } else {
          throw new Error('批注草稿格式不支持');
        }
        document.body.dataset.draftState = 'restored';
      } else if (value.pages.length) {
        await app.loadAgentSet({name:value.name, pages:value.pages, groupNotes:value.groupNotes,editablePages:true});
        document.body.dataset.draftState = 'none';
      } else {
        app.loadAgentEmptySet({name:value.name});
        document.body.dataset.draftState = 'none';
      }
      draftRevision = app.projectRevision();
      document.title = value.name + ' · EditHere DSH';
      document.body.dataset.dshSession = value.sessionId;
      document.body.dataset.dshState = value.state;
      markDshMode(value);
      label(submit, '完成并返回 AI'); submit.title = '保存批注并返回当前 DSH 对话 (Ctrl / ⌘ + E)'; submit.setAttribute('aria-label', submit.title);
      label(cancel, '取消'); cancel.querySelector('use').setAttribute('href', '#i-close');
      cancel.title = '取消 AI 批注会话'; cancel.setAttribute('aria-label', cancel.title);
      submit.onclick = complete; cancel.onclick = cancelSession;
      submit.disabled = false; cancel.disabled = false;
      connectEvents();
    } catch (error) {
      document.body.dataset.dshState = 'error'; restore(); submit.disabled = true;
      app.notify(error.message);
    } finally { $('drop-card').disabled = false; $('demo').disabled = false; }
  }
  load();
})();
