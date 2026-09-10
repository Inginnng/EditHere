using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Automation;
using Forms = System.Windows.Forms;

namespace Help2Design
{
    public sealed class NativeWindow
    {
        public long id;
        public string title;
        public PixelRect bounds;
    }
    public static class Native
    {
        [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
        public delegate bool EnumProc(IntPtr hwnd, IntPtr parameter);
        [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr value);
        [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc, IntPtr param);
        [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
        [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hwnd);
        [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd, StringBuilder title, int length);
        [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
        [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr hwnd, int attr, out RECT result, int size);
        [DllImport("dwmapi.dll", EntryPoint = "DwmGetWindowAttribute")] public static extern int DwmGetInt(IntPtr hwnd, int attr, out int result, int size);
        [DllImport("user32.dll")] public static extern bool RegisterHotKey(IntPtr hwnd, int id, uint modifiers, uint key);
        [DllImport("user32.dll")] public static extern bool UnregisterHotKey(IntPtr hwnd, int id);
        [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
        public static List<NativeWindow> Windows()
        {
            var result = new List<NativeWindow>(); uint own = (uint)Process.GetCurrentProcess().Id;
            EnumWindows(delegate(IntPtr hwnd, IntPtr p) {
                uint pid; GetWindowThreadProcessId(hwnd, out pid);
                if (pid == own || !IsWindowVisible(hwnd) || IsIconic(hwnd)) return true;
                int cloaked; if (DwmGetInt(hwnd, 14, out cloaked, 4) == 0 && cloaked != 0) return true;
                var title = new StringBuilder(512); GetWindowText(hwnd, title, title.Capacity); if (title.Length == 0) return true;
                RECT r; if (DwmGetWindowAttribute(hwnd, 9, out r, Marshal.SizeOf(typeof(RECT))) != 0 && !GetWindowRect(hwnd, out r)) return true;
                if (r.Right - r.Left < 20 || r.Bottom - r.Top < 20) return true;
                result.Add(new NativeWindow { id = hwnd.ToInt64(), title = title.ToString(), bounds = new PixelRect(r.Left, r.Top, r.Right, r.Bottom) });
                return true;
            }, IntPtr.Zero);
            return result;
        }
    }

    public sealed class UiaProbe : IDisposable
    {
        private Process process;
        private string folder;
        private Stopwatch clock = Stopwatch.StartNew();
        private bool done;
        public UiaProbe(List<NativeWindow> windows)
        {
            folder = Path.Combine(Path.GetTempPath(), "Help2Design", Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(folder);
            File.WriteAllText(Path.Combine(folder, "windows.json"), Json.Serialize(windows.Take(5).ToList()), Encoding.UTF8);
            process = Process.Start(new ProcessStartInfo { FileName = System.Reflection.Assembly.GetExecutingAssembly().Location, Arguments = "--probe \"" + folder + "\"", UseShellExecute = false, CreateNoWindow = true, WindowStyle = ProcessWindowStyle.Hidden });
        }
        public bool Ready() { return done || process == null || process.HasExited || clock.ElapsedMilliseconds > 3200; }
        public List<Candidate> Finish()
        {
            done = true;
            if (process != null && !process.HasExited) { try { process.Kill(); process.WaitForExit(500); } catch { } }
            var list = new List<Candidate>();
            var path = Path.Combine(folder, "elements.jsonl");
            if (File.Exists(path)) {
                foreach (string line in File.ReadAllLines(path, Encoding.UTF8)) {
                    try { var c = Json.Serializer().Deserialize<Candidate>(line); if (c != null && c.bounds != null && c.bounds.Area() > 0) list.Add(c); } catch { }
                }
            }
            return list;
        }
        public void Dispose()
        {
            if (!done) Finish();
            if (process != null) process.Dispose();
            try { foreach (var file in new[] { "windows.json", "elements.jsonl" }) { string path = Path.Combine(folder, file); if (File.Exists(path)) File.Delete(path); } Directory.Delete(folder); } catch { }
        }
        public static void Run(string folder)
        {
            // All UIA calls live in a bounded MTA worker process. Broken providers cannot hang the editor.
            var windows = Json.Serializer().Deserialize<List<NativeWindow>>(File.ReadAllText(Path.Combine(folder, "windows.json"), Encoding.UTF8));
            using (var writer = new StreamWriter(Path.Combine(folder, "elements.jsonl"), false, new UTF8Encoding(false))) {
                writer.AutoFlush = true;
                var sw = Stopwatch.StartNew(); int total = 0;
                var cache = new CacheRequest();
                cache.Add(AutomationElement.BoundingRectangleProperty); cache.Add(AutomationElement.NameProperty); cache.Add(AutomationElement.ControlTypeProperty); cache.Add(AutomationElement.AutomationIdProperty);
                cache.TreeScope = TreeScope.Element;
                foreach (var window in windows) {
                    if (sw.ElapsedMilliseconds > 2800) break;
                    try {
                        var root = AutomationElement.FromHandle(new IntPtr(window.id));
                        if (root == null) continue;
                        var walker = TreeWalker.ControlViewWalker;
                        var stack = new Stack<AutomationElement>(); stack.Push(root); int windowNodes = 0;
                        while (stack.Count > 0 && sw.ElapsedMilliseconds < 2800 && total < 1800 && windowNodes < 900) {
                            var parent = stack.Pop();
                            var child = walker.GetFirstChild(parent, cache);
                            while (child != null && sw.ElapsedMilliseconds < 2800 && total < 1800 && windowNodes < 900) {
                                total++; windowNodes++;
                                try {
                                    var info = child.Cached; var rect = info.BoundingRectangle;
                                    if (!rect.IsEmpty && !Double.IsInfinity(rect.X) && rect.Width >= 4 && rect.Height >= 4) {
                                        var bounds = new PixelRect((int)Math.Floor(rect.Left), (int)Math.Floor(rect.Top), (int)Math.Ceiling(rect.Right), (int)Math.Ceiling(rect.Bottom));
                                        var c = new Candidate { bounds = bounds, windowId = window.id, target = new TargetInfo { source = "uia", method = "windows-accessibility", label = String.IsNullOrWhiteSpace(info.Name) ? info.ControlType.ProgrammaticName.Replace("ControlType.", "") : info.Name.Substring(0, Math.Min(300, info.Name.Length)), controlType = info.ControlType.ProgrammaticName, automationId = info.AutomationId, originalScreenBounds = bounds } };
                                        writer.WriteLine(Json.Serializer().Serialize(c));
                                    }
                                    stack.Push(child);
                                } catch (ElementNotAvailableException) { }
                                child = walker.GetNextSibling(child, cache);
                            }
                        }
                    } catch (Exception) { /* An unavailable provider is a normal fallback, never a capture failure. */ }
                }
            }
        }
    }

    public sealed class CaptureOverlay : Forms.Form
    {
        private Bitmap screen;
        private Rectangle desktop;
        private List<NativeWindow> windows;
        private List<Candidate> elements = new List<Candidate>();
        private readonly CandidatePicker picker = new CandidatePicker();
        private UiaProbe probe;
        private Forms.Timer poll;
        private Rectangle selection, hover, before;
        private Point start;
        private bool dragging, windowOnly, elementMode = true, copyOnly;
        private int dragHandle = -2; // -2 new region, -1 move, 0..7 resize
        private string hoverName = "";
        private Rectangle confirmButton, copyButton, resetButton, cancelButton;
        public CaptureDocument Result;

        public CaptureOverlay(Bitmap image, Rectangle bounds, List<NativeWindow> list, bool onlyWindow)
        {
            screen = image; desktop = bounds; windows = list; windowOnly = onlyWindow;
            Text = "Help2Design · 截图";
            AutoScaleMode = Forms.AutoScaleMode.None; FormBorderStyle = Forms.FormBorderStyle.None;
            StartPosition = Forms.FormStartPosition.Manual; Bounds = desktop; TopMost = true; ShowInTaskbar = false;
            DoubleBuffered = true; KeyPreview = true; Cursor = Forms.Cursors.Cross;
            Font = new Font("Microsoft YaHei UI", 14, FontStyle.Regular, GraphicsUnit.Pixel);
            try { if (windows.Count > 0) probe = new UiaProbe(windows); } catch { probe = null; }
            poll = new Forms.Timer { Interval = 120 };
            poll.Tick += delegate { if (probe != null && probe.Ready()) { elements = probe.Finish(); probe.Dispose(); probe = null; poll.Stop(); UpdateHover(PointToClient(Forms.Cursor.Position)); Invalidate(); } };
            poll.Start();
        }
        protected override void OnShown(EventArgs e) { base.OnShown(e); Bounds = desktop; Activate(); UpdateHover(PointToClient(Forms.Cursor.Position)); }
        private NativeWindow WindowAt(Point p) { return windows.FirstOrDefault(w => w.bounds.Contains(p.X + desktop.Left, p.Y + desktop.Top)); }
        private Rectangle Local(PixelRect r) { return Rectangle.Intersect(new Rectangle(0, 0, desktop.Width, desktop.Height), new Rectangle(r.x1 - desktop.Left, r.y1 - desktop.Top, r.Width(), r.Height())); }
        private void UpdateHover(Point p)
        {
            var window = WindowAt(p); hover = Rectangle.Empty; hoverName = "";
            var candidates = new List<Candidate>();
            if (window != null) {
                if (!windowOnly && elementMode) candidates.AddRange(elements.Where(v => v.windowId == window.id));
                candidates.Add(new Candidate { bounds = window.bounds, windowId = window.id, target = new TargetInfo { source = "uia", label = window.title, method = "window-bounds" } });
            }
            picker.Update(candidates, p.X + desktop.Left, p.Y + desktop.Top);
            SetHover();
        }
        private void SetHover() { var c = picker.Current; if (c != null) { hover = Local(c.bounds); hoverName = c.target.label; } }
        internal void StepRegion(Point p, int direction) { if (!selection.IsEmpty || dragging) return; UpdateHover(p); picker.Step(direction); SetHover(); Invalidate(); }
        private Point[] Handles(Rectangle r) { int x = r.Left + r.Width / 2, y = r.Top + r.Height / 2; return new[] { new Point(r.Left,r.Top), new Point(x,r.Top), new Point(r.Right,r.Top), new Point(r.Right,y), new Point(r.Right,r.Bottom), new Point(x,r.Bottom), new Point(r.Left,r.Bottom), new Point(r.Left,y) }; }
        private int HitHandle(Point p) { return selection.IsEmpty ? -2 : Array.FindIndex(Handles(selection), h => Math.Abs(h.X-p.X) <= 7 && Math.Abs(h.Y-p.Y) <= 7); }
        protected override void OnMouseDown(Forms.MouseEventArgs e)
        {
            base.OnMouseDown(e);
            if (e.Button == Forms.MouseButtons.Right) { if (dragging) { selection = before; dragging = false; Capture = false; } else if (!selection.IsEmpty) selection = Rectangle.Empty; else Close(); Invalidate(); return; }
            if (e.Button != Forms.MouseButtons.Left) return;
            if (!selection.IsEmpty) {
                if (confirmButton.Contains(e.Location)) { Complete(); return; }
                if (copyButton.Contains(e.Location)) { copyOnly = true; Complete(); return; }
                if (resetButton.Contains(e.Location)) { selection = Rectangle.Empty; picker.Reset(); Invalidate(); return; }
                if (cancelButton.Contains(e.Location)) { Close(); return; }
            }
            before = selection; start = e.Location; dragHandle = HitHandle(e.Location);
            if (dragHandle < 0) dragHandle = !selection.IsEmpty && selection.Contains(e.Location) ? -1 : -2;
            if (dragHandle == -2) { selection = Rectangle.Empty; UpdateHover(e.Location); }
            dragging = true; Capture = true;
        }
        protected override void OnMouseMove(Forms.MouseEventArgs e)
        {
            base.OnMouseMove(e);
            if (dragging) {
                int dx = e.X-start.X, dy = e.Y-start.Y;
                if (dragHandle == -1) selection = new Rectangle(Math.Max(0,Math.Min(desktop.Width-before.Width,before.X+dx)),Math.Max(0,Math.Min(desktop.Height-before.Height,before.Y+dy)),before.Width,before.Height);
                else if (dragHandle >= 0) {
                    int l=before.Left,t=before.Top,r=before.Right,b=before.Bottom;
                    if (dragHandle==0 || dragHandle==6 || dragHandle==7) l=Math.Max(0,Math.Min(r-2,l+dx));
                    if (dragHandle==2 || dragHandle==3 || dragHandle==4) r=Math.Min(desktop.Width,Math.Max(l+2,r+dx));
                    if (dragHandle==0 || dragHandle==1 || dragHandle==2) t=Math.Max(0,Math.Min(b-2,t+dy));
                    if (dragHandle==4 || dragHandle==5 || dragHandle==6) b=Math.Min(desktop.Height,Math.Max(t+2,b+dy));
                    selection=Rectangle.FromLTRB(l,t,r,b);
                } else if (!windowOnly) selection = Rectangle.Intersect(ClientRectangle, Rectangle.FromLTRB(Math.Min(start.X,e.X),Math.Min(start.Y,e.Y),Math.Max(start.X,e.X),Math.Max(start.Y,e.Y)));
            } else if (selection.IsEmpty) { UpdateHover(e.Location); Cursor = Forms.Cursors.Cross; }
            else {
                int h = HitHandle(e.Location);
                Cursor = h==0 || h==4 ? Forms.Cursors.SizeNWSE : h==2 || h==6 ? Forms.Cursors.SizeNESW : h==1 || h==5 ? Forms.Cursors.SizeNS : h==3 || h==7 ? Forms.Cursors.SizeWE : selection.Contains(e.Location) ? Forms.Cursors.SizeAll : Forms.Cursors.Cross;
            }
            Invalidate();
        }
        protected override void OnMouseUp(Forms.MouseEventArgs e)
        {
            if (!dragging || e.Button != Forms.MouseButtons.Left) return;
            dragging = false; Capture = false;
            if (dragHandle == -2 && (windowOnly || Math.Abs(e.X-start.X)+Math.Abs(e.Y-start.Y)<5)) selection=hover;
            Invalidate();
        }
        protected override void OnMouseDoubleClick(Forms.MouseEventArgs e) { if (e.Button == Forms.MouseButtons.Left && !selection.IsEmpty && selection.Contains(e.Location)) { dragging=false; Capture=false; Complete(); } }
        protected override void OnMouseWheel(Forms.MouseEventArgs e) { StepRegion(e.Location,e.Delta); }
        protected override void OnKeyDown(Forms.KeyEventArgs e)
        {
            base.OnKeyDown(e); e.Handled = true;
            if (e.KeyCode == Forms.Keys.Escape) Close();
            else if (e.KeyCode == Forms.Keys.Enter || (e.Control && e.KeyCode==Forms.Keys.C)) { if (selection.IsEmpty) selection=hover; copyOnly=e.Control; Complete(); }
            else if (e.KeyCode==Forms.Keys.Tab) { StepRegion(PointToClient(Forms.Cursor.Position),e.Shift ? -1 : 1); }
            else if (e.KeyCode==Forms.Keys.Space) { elementMode=!elementMode; picker.Reset(); UpdateHover(PointToClient(Forms.Cursor.Position)); Invalidate(); }
            else if (!selection.IsEmpty) {
                int step=e.Shift ? 10:1,dx=0,dy=0;
                if(e.KeyCode==Forms.Keys.Left)dx=-step; if(e.KeyCode==Forms.Keys.Right)dx=step;
                if(e.KeyCode==Forms.Keys.Up)dy=-step; if(e.KeyCode==Forms.Keys.Down)dy=step;
                selection.X=Math.Max(0,Math.Min(desktop.Width-selection.Width,selection.X+dx));
                selection.Y=Math.Max(0,Math.Min(desktop.Height-selection.Height,selection.Y+dy)); Invalidate();
            }
        }
        private void Complete()
        {
            if (selection.Width < 2 || selection.Height < 2) return;
            dragging=false; Capture=false;
            if (probe != null) { elements=probe.Finish(); probe.Dispose(); probe=null; }
            var global=new PixelRect(selection.Left+desktop.Left,selection.Top+desktop.Top,selection.Right+desktop.Left,selection.Bottom+desktop.Top);
            var window=windows.FirstOrDefault(w=>w.bounds.Contains(global.x1+global.Width()/2,global.y1+global.Height()/2));
            using(var crop=screen.Clone(selection,PixelFormat.Format32bppArgb)) Result=CaptureDocument.FromBitmap(crop,"screen",window==null ? "屏幕截图":window.title,global);
            Result.CopyOnly=copyOnly;
            foreach(var c in elements) {
                var clipped=new PixelRect(c.bounds.x1-global.x1,c.bounds.y1-global.y1,c.bounds.x2-global.x1,c.bounds.y2-global.y1).Clip(global.Width(),global.Height());
                if(clipped.Width()<4 || clipped.Height()<4)continue;
                int cx=global.x1+(clipped.x1+clipped.x2)/2,cy=global.y1+(clipped.y1+clipped.y2)/2;
                var front=windows.FirstOrDefault(w=>w.bounds.Contains(cx,cy)); if(front!=null && front.id!=c.windowId)continue;
                var target=Json.Clone(c.target); target.clipped=clipped.Width()!=c.bounds.Width() || clipped.Height()!=c.bounds.Height();
                Result.Candidates.Add(new Candidate { bounds=clipped,target=target,windowId=c.windowId });
            }
            DialogResult=Forms.DialogResult.OK; Close();
        }
        protected override void OnPaint(Forms.PaintEventArgs e)
        {
            var g=e.Graphics; g.DrawImageUnscaled(screen,0,0);
            using(var shade=new SolidBrush(Color.FromArgb(120,20,20,24))) g.FillRectangle(shade,ClientRectangle);
            var r=selection.IsEmpty ? hover:selection;
            if(!r.IsEmpty) {
                g.DrawImage(screen,r,r,GraphicsUnit.Pixel);
                using(var pen=new Pen(Color.FromArgb(0,122,255),2)) g.DrawRectangle(pen,r.X,r.Y,Math.Max(0,r.Width-1),Math.Max(0,r.Height-1));
                if(!selection.IsEmpty) foreach(var h in Handles(r)) { g.FillRectangle(Brushes.White,h.X-3,h.Y-3,6,6); using(var pen=new Pen(Color.FromArgb(0,122,255)))g.DrawRectangle(pen,h.X-3,h.Y-3,6,6); }
                string label=r.Width+" × "+r.Height+(selection.IsEmpty && picker.Levels.Count>1 ? "    "+(picker.Index+1)+"/"+picker.Levels.Count+"  ·  滚轮 ↑ 更大  ↓ 更小":"");
                int lw=selection.IsEmpty && picker.Levels.Count>1 ? 310:112;
                var lr=new Rectangle(Math.Max(8,Math.Min(ClientSize.Width-lw-8,r.Left)),r.Top>=40 ? r.Top-36:r.Top+12,lw,28);
                DrawPill(g,lr,Color.FromArgb(245,250,250,252),8); DrawText(g,label,lr,Color.FromArgb(55,55,60));
            }
            var monitor=Rectangle.Intersect(new Rectangle(0,0,desktop.Width,desktop.Height),Local(new PixelRect(Forms.Screen.FromPoint(Forms.Cursor.Position).Bounds.Left,Forms.Screen.FromPoint(Forms.Cursor.Position).Bounds.Top,Forms.Screen.FromPoint(Forms.Cursor.Position).Bounds.Right,Forms.Screen.FromPoint(Forms.Cursor.Position).Bounds.Bottom)));
            if(monitor.IsEmpty)monitor=ClientRectangle;
            if(selection.IsEmpty) {
                int hw=Math.Min(510,monitor.Width-24);
                var hint=new Rectangle(monitor.Left+(monitor.Width-hw)/2,monitor.Top+20,hw,38);
                DrawPill(g,hint,Color.FromArgb(242,250,250,252),18);
                DrawText(g,windowOnly ? "选择窗口     ·     单击选中     ·     Esc 取消":"拖动截图     ·     滚轮切换区域     ·     右键取消",hint,Color.FromArgb(70,70,75));
            } else if(!dragging) {
                int tw=320;
                int x=Math.Max(monitor.Left+8,Math.Min(monitor.Right-tw-8,selection.Right-tw));
                int y=selection.Bottom+52<monitor.Bottom ? selection.Bottom+10:Math.Max(monitor.Top+8,selection.Top-54);
                var bar=new Rectangle(x,y,tw,44); DrawPill(g,new Rectangle(x+1,y+3,tw,44),Color.FromArgb(38,0,0,0),14); DrawPill(g,bar,Color.FromArgb(252,250,250,252),14);
                confirmButton=new Rectangle(x+6,y+6,92,32); copyButton=new Rectangle(x+102,y+6,74,32); resetButton=new Rectangle(x+179,y+6,70,32); cancelButton=new Rectangle(x+251,y+6,63,32);
                DrawPill(g,confirmButton,Color.FromArgb(0,122,255),9); DrawText(g,"开始批注",confirmButton,Color.White);
                DrawText(g,"复制",copyButton,Color.FromArgb(40,40,45)); DrawText(g,"重选",resetButton,Color.FromArgb(40,40,45)); DrawText(g,"取消",cancelButton,Color.FromArgb(130,130,135));
            }
        }
        private static void DrawPill(Graphics g,Rectangle r,Color color,int radius)
        {
            using(var path=new GraphicsPath()) using(var b=new SolidBrush(color)) {
                int d=radius*2; path.AddArc(r.X,r.Y,d,d,180,90); path.AddArc(r.Right-d,r.Y,d,d,270,90); path.AddArc(r.Right-d,r.Bottom-d,d,d,0,90); path.AddArc(r.X,r.Bottom-d,d,d,90,90); path.CloseFigure();
                var old=g.SmoothingMode; g.SmoothingMode=SmoothingMode.AntiAlias; g.FillPath(b,path); g.SmoothingMode=old;
            }
        }
        private void DrawText(Graphics g,string text,Rectangle r,Color color) { using(var b=new SolidBrush(color)) using(var format=new StringFormat { Alignment=StringAlignment.Center,LineAlignment=StringAlignment.Center,Trimming=StringTrimming.EllipsisCharacter,FormatFlags=StringFormatFlags.NoWrap }) { g.TextRenderingHint=System.Drawing.Text.TextRenderingHint.AntiAliasGridFit; g.DrawString(text,Font,b,r,format); } }
        protected override void Dispose(bool disposing) { if(disposing) { if(poll!=null)poll.Dispose(); if(probe!=null)probe.Dispose(); } base.Dispose(disposing); }
    }

    public static class CaptureService
    {
        public static CaptureDocument Capture(string mode)
        {
            Rectangle bounds = Forms.SystemInformation.VirtualScreen;
            var windows = Native.Windows();
            using (var screen = new Bitmap(bounds.Width, bounds.Height, PixelFormat.Format32bppArgb)) {
                using (var g = Graphics.FromImage(screen)) g.CopyFromScreen(bounds.Left, bounds.Top, 0, 0, bounds.Size, CopyPixelOperation.SourceCopy);
                if (mode == "fullscreen") return CaptureDocument.FromBitmap(screen, "screen", "全屏截图", new PixelRect(bounds.Left, bounds.Top, bounds.Right, bounds.Bottom));
                using (var overlay = new CaptureOverlay(screen, bounds, windows, mode == "window")) { overlay.ShowDialog(); return overlay.Result; }
            }
        }
    }
}
