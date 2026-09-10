const paths = {
  capture:'M7 3H3v4m14-4h4v4M3 17v4h4m14-4v4h-4M8 8h8v8H8z',
  point:'M12 3a9 9 0 1 0 .01 0M12 9a3 3 0 1 0 .01 0',
  rect:'M5 4h14a1 1 0 0 1 1 1v14a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1V5a1 1 0 0 1 1-1Z',
  smart:'M8 3H3v5m13-5h5v5M3 16v5h5m13-5v5h-5M12 7v10M7 12h10',
  select:'M5 3v18l5-6 8-2L5 3Z',
  undo:'m8 4-5 5 5 5M3 9h11a6 6 0 0 1 0 12',
  redo:'m16 4 5 5-5 5m5-5H10a6 6 0 0 0 0 12',
  copy:'M8 8h13v13H8zM16 4V3H3v13h1',
  sidebar:'M4 3h16a1 1 0 0 1 1 1v16a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1Zm11 0v18',
  more:'M5 11v2m7-2v2m7-2v2',
  close:'m6 6 12 12M18 6 6 18',
  upload:'M12 16V3m-5 5 5-5 5 5M4 14v6h16v-6',
  image:'M4 3h16a1 1 0 0 1 1 1v16a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1Zm-1 14 6-7 6 7 3-3 3 3M15 7h.01',
  export:'M5 13v8h14v-8M12 16V2m-4 4 4-4 4 4',
  trash:'M4 6h16M9 3h6M6 6l1 15h10l1-15M10 10v7m4-7v7',
  edit:'m14 4 6 6M4 20l5-1L21 7l-4-4L5 15l-1 5Z',
  check:'m5 12 4 4L19 6',
  minus:'M5 12h14',plus:'M5 12h14M12 5v14',
  fit:'M8 3H3v5m13-5h5v5M3 16v5h5m13-5v5h-5',
  crop:'M7 3v14h14M3 7h14v14',
  keyboard:'M3 5h18v14H3zm3 4h.01M10 9h.01M14 9h.01M18 9h.01M7 14h10',
};
export const icon = (name,cls='') => '<svg class="icon '+cls+'" viewBox="0 0 24 24" aria-hidden="true" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="'+(paths[name]||paths.more)+'"/></svg>';
export const escapeHtml = text => String(text).replace(/[&<>"']/g,ch=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[ch]));
export const toolButton = (name,title,action,extra='') => '<button class="icon-button '+extra+'" type="button" data-action="'+action+'" title="'+title+'" aria-label="'+title+'">'+icon(name)+'</button>';
