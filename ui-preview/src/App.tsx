import { useEffect, useRef, useState } from 'react';
import * as Dialog from '@radix-ui/react-dialog';
import * as Switch from '@radix-ui/react-switch';
import { Check, ChevronLeft, CircleHelp, Info, LayoutGrid, Menu, Moon, Palette, Settings2, SlidersHorizontal, Sparkles, Sun, X } from 'lucide-react';
import ProviderSettings from './ProviderSettings';
import Workspace from './Workspace';
import Analysis from './Analysis';

type View = 'workspace' | 'analysis' | 'providers' | 'settings';
type Theme = 'light' | 'dark' | 'system';
const labels: Record<View, string> = {workspace: '批注工作台', analysis: 'AI 解析', providers: '模型服务', settings: '设置'};

export default function App() {
  const [view, setView] = useState<View>('providers');
  const [provider, setProvider] = useState('OpenAI');
  const [toast, setToast] = useState('');
  const [collapsed, setCollapsed] = useState(false);
  const [about, setAbout] = useState(false);
  const [compact, setCompact] = useState(false);
  const [theme, setTheme] = useState<Theme>(() => {
    try { const saved = localStorage.getItem('edithere-preview-theme'); return ['light','dark','system'].includes(saved || '') ? saved as Theme : 'system'; }
    catch { return 'system'; }
  });
  const [systemDark, setSystemDark] = useState(() => matchMedia('(prefers-color-scheme: dark)').matches);
  const dark = theme === 'dark' || (theme === 'system' && systemDark);
  const toastTimer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);
  const notify = (message: string) => { clearTimeout(toastTimer.current); setToast(message); toastTimer.current = setTimeout(() => setToast(''), 3000); };
  useEffect(() => () => clearTimeout(toastTimer.current), []);
  useEffect(() => {
    const media = matchMedia('(prefers-color-scheme: dark)');
    const update = () => setSystemDark(media.matches); media.addEventListener('change', update);
    return () => media.removeEventListener('change', update);
  }, []);
  useEffect(() => {
    document.documentElement.dataset.theme = dark ? 'dark' : 'light';
    window.edithereDesktop?.setTheme?.(dark ? 'dark' : 'light');
    try { localStorage.setItem('edithere-preview-theme', theme); } catch { /* Disabled storage does not block preview. */ }
  }, [theme, dark]);
  useEffect(() => {
    const handler = (event: KeyboardEvent) => {
      if ((event.ctrlKey || event.metaKey) && ['1','2','3','4'].includes(event.key)) {
        event.preventDefault(); setView((['workspace','analysis','providers','settings'] as View[])[Number(event.key)-1]);
      }
    };
    window.addEventListener('keydown', handler); return () => window.removeEventListener('keydown', handler);
  }, []);
  const navigate = (next: View) => { setView(next); document.querySelector('.app-main')?.scrollTo({top: 0}); };

  return <div className={`app-shell ${collapsed ? 'sidebar-collapsed' : ''} ${compact ? 'density-compact' : ''}`}>
    <header className="desktop-titlebar"><div className="titlebar-identity"><svg viewBox="0 0 24 24" fill="none" aria-hidden="true"><path d="M5 4H2v18h18v-3M7 13l4 4L22 6l-4-4L7 13Z" stroke="currentColor" strokeWidth="1.7" strokeLinejoin="round"/><path d="m15 5 4 4M7 13l-1 5 5-1" stroke="currentColor" strokeWidth="1.7"/></svg><span>EditHere</span></div><span className="titlebar-description">桌面界面预览</span></header>
    <aside className="app-sidebar">
      <div className="sidebar-heading"><strong>EditHere</strong><button className="icon-button" aria-label={collapsed ? '展开导航' : '收起导航'} onClick={() => setCollapsed(!collapsed)}>{collapsed ? <Menu size={17}/> : <ChevronLeft size={17}/>}</button></div>
      <nav className="primary-navigation" aria-label="主导航">
        {([{id:'workspace', icon:LayoutGrid},{id:'analysis', icon:Sparkles},{id:'providers', icon:SlidersHorizontal}] as const).map(({id, icon:Icon}) => <button key={id} className={`nav-item ${view === id ? 'active' : ''}`} aria-label={labels[id]} aria-current={view === id ? 'page' : undefined} title={labels[id]} onClick={() => navigate(id)}><Icon size={19}/><span>{labels[id]}</span></button>)}
      </nav>
      <div className="sidebar-bottom"><div className="sidebar-separator"/><button className={`nav-item ${view === 'settings' ? 'active' : ''}`} aria-label="设置" aria-current={view === 'settings' ? 'page' : undefined} title="设置" onClick={() => navigate('settings')}><Settings2 size={19}/><span>设置</span></button><button className="nav-item" title="关于 EditHere" aria-label="关于 EditHere" onClick={() => setAbout(true)}><CircleHelp size={19}/><span>关于</span></button><div className="sidebar-footer"><span>0.13.0 预览版</span><button className="icon-button theme-shortcut" aria-label={dark ? '切换浅色主题' : '切换深色主题'} title={dark ? '切换浅色主题' : '切换深色主题'} onClick={() => setTheme(dark ? 'light' : 'dark')}>{dark ? <Sun size={17}/> : <Moon size={17}/>}</button></div></div>
    </aside>
    <div className="app-content"><main className={`app-main view-${view}`}>
      <div className="settings-page" hidden={view !== 'providers'}><ProviderSettings onToast={notify} onProviderChange={setProvider}/></div>
      <div hidden={view !== 'workspace'}><Workspace onAnalyze={() => navigate('analysis')} onToast={notify}/></div>
      <div hidden={view !== 'analysis'}><Analysis provider={provider} onSettings={() => navigate('providers')} onBack={() => navigate('workspace')} onToast={notify}/></div>
      <div className="settings-page general-settings" hidden={view !== 'settings'}>
        <div className="page-heading"><h1>设置</h1><p>调整 EditHere 的外观与显示方式。</p></div>
        <section className="settings-group"><h2>外观</h2><div className="settings-row"><span className="settings-row-icon"><Palette size={20}/></span><div className="settings-row-label"><strong>应用主题</strong><p>选择浅色、深色，或跟随系统设置。</p></div><div className="segmented-control" aria-label="应用主题">{([{id:'light',name:'浅色'},{id:'dark',name:'深色'},{id:'system',name:'跟随系统'}] as const).map(option => <button key={option.id} aria-pressed={theme === option.id} className={theme === option.id ? 'selected' : ''} onClick={() => setTheme(option.id)}>{option.name}</button>)}</div></div><div className="settings-row"><span className="settings-row-icon"><SlidersHorizontal size={20}/></span><div className="settings-row-label"><strong>紧凑布局</strong><p>减少页面间距，在同一窗口中显示更多内容。</p></div><Switch.Root className="ui-switch" aria-label="紧凑布局" checked={compact} onCheckedChange={setCompact}><Switch.Thumb className="ui-switch-thumb"/></Switch.Root></div></section>
        <section className="settings-group"><h2>关于预览</h2><div className="settings-row"><span className="settings-row-icon"><Info size={20}/></span><div className="settings-row-label"><strong>EditHere 0.13.0</strong><p>独立桌面界面原型，当前使用演示数据，尚未接入业务后端。</p></div><button className="button secondary" onClick={() => setAbout(true)}>详细信息</button></div></section>
      </div>
    </main></div>
    {toast && <div className="toast" role="status"><Check size={16}/><span>{toast}</span><button className="icon-button" aria-label="关闭通知" onClick={() => setToast('')}><X size={14}/></button></div>}
    <Dialog.Root open={about} onOpenChange={setAbout}><Dialog.Portal><Dialog.Overlay className="dialog-overlay"/><Dialog.Content className="dialog-content about-dialog"><Dialog.Title>EditHere</Dialog.Title><Dialog.Description>0.13.0 · 桌面界面预览</Dialog.Description><p>批注画面，整理修改意见，再交给你的 Agent。</p><div className="about-details"><strong>当前预览范围</strong><p>模型配置、批注工作台、AI 解析和深浅色设置可以交互体验。解析与连接检测使用演示数据，不调用真实 API；Key 仅保存在当前进程内存中。</p></div><p className="about-shortcuts">Ctrl + 1 / 2 / 3 / 4 切换页面</p><Dialog.Close className="button primary">知道了</Dialog.Close><Dialog.Close className="icon-button dialog-close" aria-label="关闭关于窗口"><X size={17}/></Dialog.Close></Dialog.Content></Dialog.Portal></Dialog.Root>
  </div>;
}
