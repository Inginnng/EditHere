/* Activated only by a capability URL from the localhost Agent connector. */
(() => {
  'use strict';
  const token = new URL(location.href).searchParams.get('agent');
  if (!token || location.protocol !== 'http:' || location.hostname !== '127.0.0.1' || !/^[a-f0-9]{64}$/i.test(token)) return;
  const $ = id => document.getElementById(id), app = globalThis.EditHereWeb;
  const submit = $('export'), cancel = $('open');
  const originalExport = submit.onclick, originalOpen = cancel.onclick;
  let sending = false, pendingFeedback = null;
  app.bindAgent();
  async function request(route, value) {
    const controller = new AbortController(), timer = setTimeout(() => controller.abort(), 20000);
    try {
      const response = await fetch('/__edithere_agent/' + route + '?token=' + encodeURIComponent(token), {
        method: value === undefined ? 'GET' : 'POST',
        headers: value === undefined ? {} : {'Content-Type':'application/json'},
        body: value === undefined ? undefined : JSON.stringify(value),
        signal:controller.signal, cache:'no-store', credentials:'omit'
      });
      const result = await response.json();
      if (!response.ok) {const error=new Error(result.error || '本地批注会话不可用');error.status=response.status;throw error;}
      return result;
    } catch (error) {
      if (error.name === 'AbortError' || error instanceof TypeError) throw new Error('连接暂时不可用，编辑已保留；恢复服务后可再次点击反馈');
      throw error;
    } finally { clearTimeout(timer); }
  }
  function label(button, text) { button.querySelector('span').textContent = text; }
  function restore() {
    app.releaseAgent();cancel.onclick=originalOpen;submit.onclick=originalExport;
    cancel.querySelector('use').setAttribute('href','#i-open');label(cancel,'打开');
    cancel.title='打开图片、视频或项目 (Ctrl / ⌘ + O)';cancel.setAttribute('aria-label','打开图片、视频或项目');
    label(submit,'导出反馈');submit.title='导出结构化反馈 (Ctrl / ⌘ + E)';submit.setAttribute('aria-label','导出反馈');
    submit.disabled=false;cancel.disabled=false;
    $('workspace').inert=false;
    delete document.body.dataset.agentLocked;
  }
  function endSession(state) {
    document.body.dataset.agentState=state;pendingFeedback=null;restore();
    app.notify(state==='expired'?'AI 批注会话已超时，当前编辑仍可保存或导出':'AI 批注会话已取消，当前编辑仍可保存或导出');
  }
  async function recoverTerminal(error) {
    if(error.status!==409)return false;
    try {
      const value=await request('session');
      if(value.state==='expired'||value.state==='cancelled'){endSession(value.state);return true;}
    } catch { /* A disconnected server must not discard a pending snapshot. */ }
    return false;
  }
  async function complete() {
    if (sending || document.body.dataset.agentState !== 'pending') return;
    if ($('note-dialog').open) { app.notify('请先保存或关闭正在编辑的批注');return; }
    sending=true;submit.disabled=true;cancel.disabled=true;label(submit,'返回中');
    pendingFeedback ||= app.feedback();$('workspace').inert=true;document.body.dataset.agentLocked='true';
    try {
      const receipt=await request('feedback',pendingFeedback);
      if (receipt.state !== 'completed') throw new Error('反馈尚未完成，请重试');
      app.completedAgent();label(submit,'已返回');submit.title='反馈已返回 AI';
      submit.setAttribute('aria-label','反馈已返回 AI');
      app.notify('反馈已返回 AI，可以回到对话继续');
    } catch(error) {
      if(await recoverTerminal(error))return;
      label(submit,'反馈');submit.disabled=false;cancel.disabled=false;app.notify(error.message);
    } finally { sending=false; }
  }
  async function cancelSession() {
    if (sending || !app.canCancel()) return;
    sending=true;submit.disabled=true;cancel.disabled=true;
    try {
      const result=await request('cancel',{});
      if(result.state==='expired'||result.state==='cancelled'){endSession(result.state);return;}
      throw new Error('会话已经完成，请返回对话查看反馈');
    } catch(error) {if(!await recoverTerminal(error)){submit.disabled=false;cancel.disabled=false;app.notify(error.message);}}
    finally {sending=false;}
  }
  async function load() {
    document.body.dataset.agentState='loading';submit.disabled=true;cancel.disabled=true;
    $('drop-card').disabled=true;$('demo').disabled=true;
    try {
      const value=await request('session');
      if(value.state!=='pending') {
        document.body.dataset.agentState=value.state;restore();submit.disabled=true;
        app.notify(value.state==='completed'?'这次反馈已返回 AI，请回到对话查看':'这次 AI 批注会话已结束，可以重新发起');
        return;
      }
      await app.loadAgentImage(value);
      document.body.dataset.agentSession=value.sessionId;
      document.body.dataset.agentState=value.state;
      label(submit,'反馈');submit.title='完成并返回 AI (Ctrl / ⌘ + E)';submit.setAttribute('aria-label','完成并返回 AI');
      label(cancel,'取消');cancel.querySelector('use').setAttribute('href','#i-close');
      cancel.title='取消 AI 批注会话';cancel.setAttribute('aria-label','取消 AI 批注会话');
      submit.onclick=complete;cancel.onclick=cancelSession;
      document.querySelector('.local-badge').title='素材仅在本机处理，点击反馈后返回当前 AI 会话';
      submit.disabled=false;cancel.disabled=false;
    } catch(error) {
      document.body.dataset.agentState='error';restore();submit.disabled=true;
      app.notify(error.message);
    } finally {$('drop-card').disabled=false;$('demo').disabled=false;}
  }
  load();
})();
