/* Workspace chrome shared by the offline editor and embedded panels. */
(() => {
  'use strict';
  const $ = id => document.getElementById(id);
  let manuallyChosen = false;
  const compact = matchMedia('(max-width: 899px), (max-aspect-ratio: 6/5)');
  function setInspector(open) {
    document.body.dataset.inspectorOpen = String(open);
    $('inspect-toggle').setAttribute('aria-expanded', String(open));
    $('inspect-toggle').setAttribute('aria-label', open ? '收起修改意见' : '显示修改意见');
    window.dispatchEvent(new Event('edithere:layout'));
  }
  setInspector(!compact.matches);
  $('inspect-toggle').onclick = () => { manuallyChosen = true; setInspector(document.body.dataset.inspectorOpen !== 'true'); };
  $('panel-close').onclick = () => { manuallyChosen = true; setInspector(false); };
  compact.addEventListener('change', () => { if (!manuallyChosen) setInspector(!compact.matches); });
  globalThis.EditHereChrome = {
    refreshCount(count) {
      $('inspect-count').textContent = count;
      $('inspect-count').hidden = !count;
      $('inspect-toggle').title = count ? '修改意见 · ' + count : '修改意见';
    },
    revealNote(id) {
      setInspector(true);
      requestAnimationFrame(() => {
        if (id) $('notes').querySelector('[data-note="' + CSS.escape(id) + '"]')?.scrollIntoView({ block: 'nearest' });
      });
    }
  };
  $('theme-toggle').onclick = () => {
    const dark = document.documentElement.dataset.theme !== 'dark';
    document.documentElement.dataset.theme = dark ? 'dark' : 'light';
    $('theme-toggle').setAttribute('aria-label', dark ? '切换浅色主题' : '切换深色主题');
    $('theme-toggle').setAttribute('aria-pressed', String(dark));
  };
  $('help').onclick = () => $('help-dialog').showModal();
  $('fit-icon').onclick = () => $('fit').click();
  function fitHeader() {
    const header = document.querySelector('.header');
    const needed = [...header.children].reduce((width, element) => width + element.getBoundingClientRect().width, 0) + 20;
    document.body.dataset.headerWrap = String(!$('workspace').hidden && needed > header.clientWidth);
    window.dispatchEvent(new Event('edithere:layout'));
  }
  const headerObserver = new ResizeObserver(fitHeader);
  for (const element of document.querySelectorAll('.header,.header>*')) headerObserver.observe(element);
  new MutationObserver(fitHeader).observe($('workspace'), { attributes:true, attributeFilter:['hidden'] });
  const utilityToggle = $('utility-toggle');
  utilityToggle.onclick = () => utilityToggle.setAttribute('aria-expanded', String(utilityToggle.getAttribute('aria-expanded') !== 'true'));
  document.addEventListener('click', event => {
    if (!event.target.closest('.utility-dock') || event.target.closest('.utility-actions button')) utilityToggle.setAttribute('aria-expanded', 'false');
  });
  document.addEventListener('keydown', event => {
    if (event.key === 'Escape') utilityToggle.setAttribute('aria-expanded', 'false');
  });
  new ResizeObserver(() => {
    document.documentElement.style.setProperty('--tools-height', document.querySelector('.tools').getBoundingClientRect().height + 'px');
    window.dispatchEvent(new Event('edithere:layout'));
  }).observe(document.querySelector('.tools'));
  new ResizeObserver(() => {
    document.documentElement.style.setProperty('--video-panel-height', $('video-controls').getBoundingClientRect().height + 'px');
    window.dispatchEvent(new Event('edithere:layout'));
  }).observe($('video-controls'));
})();
