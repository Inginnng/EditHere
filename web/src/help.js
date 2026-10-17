/* Illustrated help uses the same controls and state labels as the editor. */
(() => {
  'use strict';
  const dialog = document.getElementById('help-dialog');
  if (!dialog) return;
  const icon = name => `<svg class="icon" aria-hidden="true"><use href="#i-${name}"/></svg>`;
  const picture = (label, drawing) => `<svg class="help-picture" viewBox="0 0 240 106" role="img" aria-label="${label}">${drawing}</svg>`;
  const canvas = '<rect class="help-canvas" x="8" y="8" width="224" height="90" rx="9"/><path class="help-faint" d="M22 26h70M22 35h45"/>';
  const card = (name, title, drawing, text) => `<article class="help-card"><h3>${icon(name)}${title}</h3>${drawing}<p>${text}</p></article>`;
  dialog.setAttribute('aria-labelledby', 'help-title');
  dialog.innerHTML = `
    <div class="dialog-head help-head"><div><small>使用教程</small><h2 id="help-title">把修改意见说清楚</h2></div><button data-close="help-dialog" class="icon-button" aria-label="关闭帮助">${icon('close')}</button></div>
    <div class="help-tabs" role="tablist" aria-label="教程分类">
      <button id="help-tab-edit" role="tab" aria-selected="true" aria-controls="help-panel-edit" tabindex="0">画面批注</button>
      <button id="help-tab-share" role="tab" aria-selected="false" aria-controls="help-panel-share" tabindex="-1">交给 Agent</button>
      <button id="help-tab-recover" role="tab" aria-selected="false" aria-controls="help-panel-recover" tabindex="-1">遇到问题</button>
    </div>
    <div class="help-content">
      <section id="help-panel-edit" class="help-panel" role="tabpanel" aria-labelledby="help-tab-edit" tabindex="0">
        <div class="help-card-grid">
          ${card('rect', '圈出要改的区域', picture('框选图片中的区域，添加一条文字批注', canvas + '<rect class="help-area" x="37" y="44" width="100" height="36" rx="4"/><circle class="help-pin" cx="37" cy="44" r="10"/><text class="help-pin-text" x="37" y="48">1</text><path class="help-arrow" d="m146 55 12 7-12 7"/><rect class="help-bubble" x="167" y="48" width="49" height="28" rx="5"/><path class="help-faint" d="M177 59h27M177 66h18"/>'), '选「框」，拖出区域，再写文字。范围不准时，用「选择」拖动边框或控制点。')}
          ${card('point', '精确指出一个位置', picture('在图片中点击一个位置并添加文字，编号标记指向这个位置', canvas + '<rect class="help-neutral" x="32" y="46" width="176" height="27" rx="5"/><circle class="help-pin" cx="169" cy="58" r="10"/><text class="help-pin-text" x="169" y="62">2</text><path class="help-arrow" d="m190 73-12-10m12 0v10h-10"/>'), '选「点」，点击细节写意见。整张图的要求用修改意见面板里的「全局批注」。')}
          ${card('explode', '移动后，补充原因', picture('组件从左侧移动到右侧，位移箭头连接原位置与目标位置，点击箭头可添加文字', canvas + '<rect class="help-origin" x="30" y="48" width="54" height="30" rx="4"/><rect class="help-area" x="154" y="48" width="54" height="30" rx="4"/><path class="help-arrow" d="M87 63h59m-8-6 8 6-8 6"/><rect class="help-bubble" x="95" y="25" width="52" height="23" rx="5"/><text class="help-small-text" x="121" y="40">点箭头</text><path class="help-faint" d="M120 48v11"/>'), '开启「大爆炸」，选中组件后拖动或缩放。点击位移箭头可写说明；再次点击「大爆炸」收起。')}
          ${card('fit', '看全图，也看细节', picture('使用缩小、缩放百分比、放大和适应画布按钮，控制画布显示', '<rect class="help-canvas" x="22" y="22" width="196" height="64" rx="9"/><svg class="help-fit-icon" x="33" y="41" width="24" height="24"><use href="#i-zoom-out"/></svg><text class="help-control-text" x="101" y="60">65%</text><svg class="help-fit-icon" x="141" y="41" width="24" height="24"><use href="#i-zoom-in"/></svg><svg class="help-fit-icon" x="182" y="41" width="24" height="24"><use href="#i-fit"/></svg>'), '顶栏「缩小 / 放大」缩放画布，「适应画布」让整张图可见。组件布局按钮可输入准确的位置和尺寸。')}
        </div>
        <details class="help-shortcuts"><summary>常用快捷键</summary><div class="shortcut-grid"><span>选择 / 平移</span><kbd>V / H</kbd><span>智能 / 点 / 框批注</span><kbd>B / P / R</kbd><span>多选组件</span><kbd>Ctrl / ⌘ + 点击</kbd><span>缩放选中组件</span><kbd>指向组件 + 滚轮</kbd><span>平移画布</span><kbd>空格 + 拖动</kbd><span>缩放画布</span><kbd>Ctrl / ⌘ + 滚轮</kbd><span>适应画布</span><kbd>Ctrl / ⌘ + 0</kbd><span>撤销 / 重做</span><kbd>Ctrl / ⌘ + Z / Y</kbd><span>使用当前保存方式</span><kbd>Ctrl / ⌘ + S</kbd><span>直接保存项目</span><kbd>Ctrl / ⌘ + Shift + S</kbd><span>反馈 / 导出</span><kbd>Ctrl / ⌘ + E</kbd></div></details>
        <p class="help-footnote">图片、视频可直接导入；系统截图与 OCR 使用桌面 EditHere，Agent 也可先截图再传入。</p>
      </section>
      <section id="help-panel-share" class="help-panel" role="tabpanel" aria-labelledby="help-tab-share" tabindex="0" hidden>
        <ol class="help-flow" aria-label="从图片到 Agent 修改的四个步骤">
          <li><span class="help-step">1</span>${icon('image')}<strong>放入画面</strong><span>导入，或让 Agent 传图</span></li>
          <li><span class="help-step">2</span>${icon('rect')}<strong>批注与位移</strong><span>写要求，标出目标位置</span></li>
          <li><span class="help-step">3</span>${icon('send')}<strong>反馈或保存 JSON</strong><span>发送到对话，或手动交付</span></li>
          <li><span class="help-step">4</span>${icon('note-add')}<strong>Agent 继续</strong><span>回到对话查看修改结果</span></li>
        </ol>
        <div class="help-status" role="status" aria-live="polite"><span class="help-status-dot"></span><p id="help-session-status"></p></div>
        <div class="help-file-guide">
          <article><h3>${icon('send')}反馈 JSON</h3><p>给 Agent 读取的修改意见、坐标与位移。展开顶栏保存按钮，选择「保存 JSON 文件」「复制 JSON 内容」「保存项目」或「查看 JSON 文件」；选择只记住方式，不会复制、下载或打开窗口。点击主保存按钮才执行，以后保留你上次的选择。</p></article>
          <article><h3>${icon('save')}项目文件</h3><p>保留原图或批注帧、布局与可编辑状态。顶栏保存菜单选「保存项目」后点击主按钮，或点击左下「更多工具」里的「保存项目」备份。下次通过「打开」继续编辑；视频继续播放需重新选择源视频。</p></article>
        </div>
        <div class="help-callout"><strong>图片怎么交付？</strong><p>在「查看 JSON 文件」中选择原图方式，默认「配套图片文件」：下载 ZIP 包含 JSON 与原图；复制 JSON 时还需交付引用的图片。「内嵌到 JSON」可单文件交付，并可选 JPEG 压缩；「不附带图片」适合 Agent 已持有相同原图。还可附带带编号和箭头的预览图。</p></div>
        <p>下载文件和复制内容包含相同的修改意见。重新导入可打开反馈 ZIP，或同时选择 JSON 与配套图片；校验值不一致的图片不会用于恢复批注。把文件交给 Agent，并说明「按照这些批注修改」。</p>
        <p class="help-footnote">显示「已保存」只代表反馈落盘；显示「已发送」代表宿主接收消息。Agent 是否已继续，以对话中的回复和修改结果为准。</p>
      </section>
      <section id="help-panel-recover" class="help-panel" role="tabpanel" aria-labelledby="help-tab-recover" tabindex="0" hidden>
        <p class="help-recover-intro">先看反馈按钮和提示，再按对应情况处理。</p>
        <div class="help-recovery-list">
          <article><span class="help-recovery-number">1</span><div><h3>提示保存失败，或一直没保存成功</h3><p>保持当前页面，先保存项目和 JSON 留底，再点击「重试保存」（或「反馈」）。不要把尚未保存的内容当作已反馈。</p></div></article>
          <article><span class="help-recovery-number">2</span><div><h3>已经保存，但消息发送失败</h3><p>点「重试发送」；它只重发消息，不重复提交批注。宿主不支持回传时，复制反馈路径，或保存 JSON 手动交给 Agent。</p></div></article>
          <article><span class="help-recovery-number">3</span><div><h3>点了反馈，Agent 没有继续</h3><p>回到原对话说「继续，读取 EditHere 的反馈」。仍无结果，就上传 JSON 文件或粘贴 JSON 内容；没包含图片时，带上原图。</p></div></article>
          <article><span class="help-recovery-number">4</span><div><h3>连接断开，或界面需要重开</h3><p>先保存项目并导出 JSON，保留尚未送达的修改。让 Agent 重新打开原批注会话，恢复已保存草稿；不要直接新建会话覆盖这次意见。</p></div></article>
          <article><span class="help-recovery-number">5</span><div><h3>图片空白、未加载，或只看到工具调用</h3><p>请 Agent 确认原图存在、非空，并重开同一会话。只看到工具调用时，展开工具卡片；宿主支持时，可点击「在标签页中打开」移到右侧。</p></div></article>
        </div>
        <div class="help-callout"><strong>需要让 Agent 排查时</strong><p>提供失败提示、当前按钮文字和你导出的 JSON。没有确认图片已加载、反馈已保存或消息已送达，就继续按相应步骤恢复。</p></div>
      </section>
    </div>`;
  const tabs = [...dialog.querySelectorAll('[role="tab"]')];
  function selectTab(index, focus) {
    tabs.forEach((tab, i) => {
      tab.setAttribute('aria-selected', String(i === index));
      tab.tabIndex = i === index ? 0 : -1;
      document.getElementById(tab.getAttribute('aria-controls')).hidden = i !== index;
    });
    dialog.querySelector('.help-content').scrollTop = 0;
    if (focus) tabs[index].focus();
  }
  tabs.forEach((tab, index) => {
    tab.addEventListener('click', () => selectTab(index, false));
    tab.addEventListener('keydown', event => {
      let next;
      if (event.key === 'ArrowRight') next = (index + 1) % tabs.length;
      else if (event.key === 'ArrowLeft') next = (index + tabs.length - 1) % tabs.length;
      else if (event.key === 'Home') next = 0;
      else if (event.key === 'End') next = tabs.length - 1;
      else return;
      event.preventDefault();
      selectTab(next, true);
    });
  });
  function refreshStatus() {
    const state = document.body.dataset;
    let message;
    if (state.imageState === 'error' || state.agentState === 'error') message = '当前会话或图片加载失败。先查看「遇到问题」，请 Agent 重开原会话。';
    else if (state.mcpState === 'error' || state.mcpState === 'stopped' || state.mcpState === 'unsupported') message = '当前连接不可用。先保存项目和 JSON，再回到原对话恢复会话。';
    else if (state.messageState === 'sending') message = '反馈已保存，正在发送到对话。请等待结果；发送失败时可只重试发送。';
    else if (state.messageState === 'accepted') message = '宿主已接收反馈消息。回到对话查看 Agent 的回复；没有继续时，发送「继续」。';
    else if (['failed', 'error'].includes(state.messageState)) message = '反馈已保存，但消息没有确认送达。点「重试发送」，或把 JSON 手动交给 Agent。';
    else if (['unsupported', 'stopped'].includes(state.messageState)) message = '反馈已保存，当前宿主无法发送消息。复制反馈路径或保存 JSON，回到原对话交给 Agent 并说「继续」。';
    else if (state.agentState === 'completed') message = '反馈已经保存。回到对话说「继续」，让 Agent 读取这次反馈。';
    else if (state.agentState === 'pending') message = document.getElementById('export').textContent.includes('导出')
      ? '当前宿主无法自动保存反馈。完成批注后，保存 JSON 文件或复制 JSON 内容，回到原对话交给 Agent。'
      : '当前处于 Agent 批注会话。完成后点击右上角「反馈」，再回对话查看处理结果。';
    else if (state.agentState === 'loading' || state.imageState === 'loading') message = '图片正在载入，等画面完整显示后再批注。';
    else message = '当前是本地编辑。保存 JSON 文件或复制 JSON 内容，再交给 Agent。';
    document.getElementById('help-session-status').textContent = message;
  }
  document.getElementById('help')?.addEventListener('click', refreshStatus);
  new MutationObserver(() => { if (dialog.open) refreshStatus(); }).observe(document.body, {
    attributes: true,
    attributeFilter: ['data-agent-state', 'data-mcp-state', 'data-image-state', 'data-message-state']
  });
  refreshStatus();
})();
