using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using Microsoft.Win32;
using Forms = System.Windows.Forms;

namespace Help2Design
{
    public sealed partial class MainWindow : Window
    {
        public string InitialFile;
        private CaptureDocument document;
        private AnnotationCanvas canvas;
        private ScrollViewer viewport;
        private StackPanel cards;
        private TextBlock status, noteCount, imageInfo, detectionInfo, zoomLabel;
        private int captureDelay;
        private Button undoButton, redoButton, exportButton;
        private List<Button> toolButtons = new List<Button>();
        private Stack<List<Annotation>> undo = new Stack<List<Annotation>>(), redo = new Stack<List<Annotation>>();
        private bool capturing, closing, autoFit = true;
        private string savePath;
        private Forms.NotifyIcon tray;
        private IntPtr handle;
        private HwndSource hwndSource;
        private HashSet<int> registeredHotkeys = new HashSet<int>();
        private void SetTool(CanvasTool tool)
        {
            canvas.SetTool(tool);
            for (int i = 0; i < toolButtons.Count; i++) { toolButtons[i].Background = Ui.Brush(i == (int)tool ? "#E7F0FF" : "Transparent"); toolButtons[i].Foreground = Ui.Brush(i == (int)tool ? "#007AFF" : "#636366"); }
            status.Text = ToolHint(); canvas.Focus();
        }
        private string ToolHint()
        {
            switch (canvas.Tool) { case CanvasTool.Rectangle: return "拖动框选范围，松开后填写批注"; case CanvasTool.Smart: return "滚轮 ↑ 更大  ↓ 更小 · 单击批注 · 拖动框选"; case CanvasTool.Select: return "拖动标签或框边控制点调整范围 · 双击编辑 · Delete 删除"; default: return "单击批注 · 滚轮缩放 · 空格拖动贴图"; }
        }
        private void InitializeHotkeys(object sender, EventArgs args)
        {
            handle = new WindowInteropHelper(this).Handle; hwndSource = HwndSource.FromHwnd(handle); hwndSource.AddHook(WndProc);
            for (int id = 2; id <= 4; id++) if (Native.RegisterHotKey(handle, id, 0x4003, (uint)(0x30 + id))) registeredHotkeys.Add(id);
            tray = new Forms.NotifyIcon { Icon = System.Drawing.SystemIcons.Application, Text = "Help2Design · 截图批注", Visible = true };
            var menu = new Forms.ContextMenuStrip();
            menu.Items.Add("截图   Ctrl+Alt+2", null, delegate { StartCapture("region"); });
            menu.Items.Add("窗口截图   Ctrl+Alt+3", null, delegate { StartCapture("window"); });
            menu.Items.Add("全屏截图   Ctrl+Alt+4", null, delegate { StartCapture("fullscreen"); });
            menu.Items.Add(new Forms.ToolStripSeparator());
            menu.Items.Add("恢复当前贴图", null, delegate { ResumePin(); });
            menu.Items.Add("打开图片或项目…", null, delegate { OpenFile(); });
            menu.Items.Add("粘贴图片", null, delegate { PasteImage(); });
            menu.Items.Add(new Forms.ToolStripSeparator());
            menu.Items.Add("退出 Help2Design", null, delegate { ExitApplication(); });
            tray.ContextMenuStrip = menu;
            if (registeredHotkeys.Count < 3) { var warning = new Forms.ToolStripMenuItem("部分快捷键被占用，可点击托盘截图") { Enabled = false }; menu.Items.Insert(0, warning); }
            tray.MouseClick += delegate(object s, Forms.MouseEventArgs e) { if (e.Button == Forms.MouseButtons.Left) StartCapture("region"); };
        }

        private IntPtr WndProc(IntPtr hwnd, int message, IntPtr wParam, IntPtr lParam, ref bool handled)
        {
            if (message == 0x0312) { int id = wParam.ToInt32(); handled = true; Dispatcher.BeginInvoke(new Action(delegate { StartCapture(id == 3 ? "window" : id == 4 ? "fullscreen" : "region"); })); }
            return IntPtr.Zero;
        }
        internal Func<string, CaptureDocument> CaptureProvider = CaptureService.Capture;
        internal async void StartCapture(string requested)
        {
            if (capturing || OwnedWindows.Cast<Window>().Any(w => w.IsVisible)) return;
            if (!ConfirmReplace()) return;
            bool wasVisible = IsVisible; capturing = true;
            try {
                Hide(); await Task.Delay(140 + captureDelay * 1000);
                var result = CaptureProvider(requested ?? "region");
                if (result != null) {
                    if (result.CopyOnly) {
                        // Keep the previous pin available when the user only wants a normal screenshot.
                        Clipboard.SetImage(result.Image);
                        if (wasVisible && document != null) ResumePin();
                    } else SetDocument(result);
                } else if (wasVisible && document != null) ResumePin();
            } catch (Exception ex) { if (document != null) ResumePin(); Ui.Error(IsVisible ? this : null, ex); }
            finally { capturing = false; }
        }

        private async void SetDocument(CaptureDocument value)
        {
            document = value; savePath = null; undo.Clear(); redo.Clear(); autoFit = true;
            Title = "Help2Design Capture · 截图批注";
            canvas.Document = document; canvas.Selected = null;
            notesUserHidden = false; SetNotesVisible(document.Notes.Count > 0, false); SizeToImage();
            if (!IsVisible) Show(); WindowState = WindowState.Normal; Activate(); SetTool(CanvasTool.Smart);
            imageInfo.Text = document.Info.width + " × " + document.Info.height + "  ·  " + (document.Info.source == "screen" ? "屏幕截图" : "图片");
            imageInfo.ToolTip = document.Info.title;
            Fit(); RefreshCards(); canvas.InvalidateVisual();
            detectionInfo.Text = "正在本地识别图片块…";
            status.Text = "正在识别区域… · 可直接拖动框选";
            var current = value;
            try {
                var candidates = await Task.Run(delegate {
                    using (var stream = new MemoryStream(current.Png)) using (var bitmap = new System.Drawing.Bitmap(stream)) return BlockDetector.Detect(bitmap);
                });
                if (document != current) return;
                document.Candidates.AddRange(candidates);
                int uia = document.Candidates.Count(c => c.target.source == "uia");
                detectionInfo.Text = "界面元素 " + uia + "  ·  图像候选 " + candidates.Count;
                detectionInfo.ToolTip = uia == 0 ? "当前画面未取得界面元素。图像候选来自颜色和边缘检测，可使用框选调整。" : "界面元素来自采集时的无障碍信息；图像候选来自本地颜色和边缘算法。";
                imageInfo.ToolTip = document.Info.title + "\n" + detectionInfo.Text;
                status.Text = ToolHint();
                canvas.InvalidateVisual();
            } catch (Exception ex) { if (document == current) { status.Text = "未完成自动识别，仍可拖动框选"; Program.Log(ex); } }
        }
        private void OpenFile()
        {
            var dialog = new OpenFileDialog { Filter = "图片或批注项目|*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.json|所有文件|*.*" };
            if ((IsVisible ? dialog.ShowDialog(this) : dialog.ShowDialog()) == true) OpenPath(dialog.FileName);
        }
        private void OpenPath(string path)
        {
            if (!ConfirmReplace()) return;
            try { bool project = Path.GetExtension(path).Equals(".json", StringComparison.OrdinalIgnoreCase); var loaded = project ? CaptureDocument.Load(path) : CaptureDocument.LoadImage(path); SetDocument(loaded); if (project) savePath = path; }
            catch (Exception ex) { Ui.Error(this, ex); }
        }
        private void PasteImage()
        {
            try {
                if (!Clipboard.ContainsImage()) { status.Text = "剪贴板里没有图片。请先截图或复制图片。"; return; }
                var image = Clipboard.GetImage(); if (image == null || !ConfirmReplace()) return;
                var encoder = new System.Windows.Media.Imaging.PngBitmapEncoder(); encoder.Frames.Add(System.Windows.Media.Imaging.BitmapFrame.Create(image));
                using (var stream = new MemoryStream()) { encoder.Save(stream); SetDocument(CaptureDocument.FromPng(stream.ToArray(), "clipboard", "剪贴板图片", null)); }
            } catch (Exception ex) { Ui.Error(this, ex); }
        }
        private void LoadDemo() { if (!ConfirmReplace()) return; using (var bitmap = SelfTest.DemoImage()) SetDocument(CaptureDocument.FromBitmap(bitmap, "demo", "示例产品页面", null)); }
        private void EditNote(Annotation note, bool isNew)
        {
            if (isNew && document.Notes.Count >= 1000) { status.Text = "最多支持 1000 条批注。"; return; }
            var dialog = new NoteDialog(note, isNew ? document.Notes.Count + 1 : note.number) { Owner = this };
            PositionNote(dialog, note);
            if (dialog.ShowDialog() != true) return;
            PushUndo(); note.comment = dialog.Comment; note.updatedAt = Json.Now();
            if (isNew) document.Notes.Add(note);
            canvas.Selected = note; notesUserHidden = false; SetNotesVisible(true, true); Changed();
        }
        private void PushUndo() { undo.Push(Json.Clone(document.Notes)); redo.Clear(); }
        private void Changed() { document.Dirty = true; document.Renumber(); RefreshCards(); canvas.InvalidateVisual(); Title = "Help2Design Capture · 截图批注 *"; }
        private void Undo() { if (document == null || undo.Count == 0) return; redo.Push(Json.Clone(document.Notes)); document.Notes = undo.Pop(); canvas.Selected = null; if (document.Notes.Count > 0 && !notesUserHidden) SetNotesVisible(true, true); Changed(); }
        private void Redo() { if (document == null || redo.Count == 0) return; undo.Push(Json.Clone(document.Notes)); document.Notes = redo.Pop(); canvas.Selected = null; if (document.Notes.Count > 0 && !notesUserHidden) SetNotesVisible(true, true); Changed(); }
        private void DeleteNote(Annotation note) { if (note == null) return; PushUndo(); document.Notes.Remove(note); canvas.Selected = null; Changed(); }
        private void RefreshCards()
        {
            cards.Children.Clear(); int count = document == null ? 0 : document.Notes.Count; noteCount.Text = "批注  " + count;
            if (count == 0 && !notesUserHidden) SetNotesVisible(false, true);
            undoButton.IsEnabled = undo.Count > 0; redoButton.IsEnabled = redo.Count > 0; exportButton.IsEnabled = document != null;
            if (count == 0) {
                var hint = new StackPanel { Margin = new Thickness(12, 35, 12, 20) };
                hint.Children.Add(Ui.Text("还没有批注", 15, "#636366"));
                hint.Children.Add(new TextBlock { Text = "在左侧画面中点击一个位置，\n或者画框圈出需要修改的部分。", Foreground = Ui.Brush("#8E8E93"), FontSize = 12, LineHeight = 23, Margin = new Thickness(0, 12, 0, 0) });
                cards.Children.Add(hint); return;
            }
            foreach (var note in document.Notes) {
                var item = new StackPanel();
                var head = new DockPanel();
                var badge = new Border { Width = 25, Height = 25, Background = Ui.Brush("#007AFF"), CornerRadius = new CornerRadius(13), Margin = new Thickness(0, 0, 8, 0), Child = new TextBlock { Text = note.number.ToString(), Foreground = Brushes.White, FontSize = 12, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center } };
                DockPanel.SetDock(badge, Dock.Left); head.Children.Add(badge);
                var actions = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right };
                var edit = Ui.Button("编辑", delegate { EditNote(note, false); }, false); edit.Padding = new Thickness(7, 3, 7, 3); edit.FontSize = 11; edit.Margin = new Thickness(0, 0, 5, 0); actions.Children.Add(edit);
                var delete = Ui.Button("×", delegate { DeleteNote(note); }, false); delete.Padding = new Thickness(7, 3, 7, 3); delete.Margin = new Thickness(0); delete.ToolTip = "删除批注（可撤销）"; actions.Children.Add(delete); head.Children.Add(actions); item.Children.Add(head);
                var content = Ui.Text(note.comment, 13, "#1D1D1F"); content.Margin = new Thickness(0, 12, 0, 10); content.LineHeight = 22; item.Children.Add(content);
                item.Children.Add(Ui.Text(Ui.Coordinates(note), 10, "#8E8E93"));
                if (note.target.source != "manual") item.Children.Add(new TextBlock { Text = (note.target.source == "uia" ? "界面元素 · " : "图像候选 · ") + note.target.label, Foreground = Ui.Brush("#007AFF"), FontSize = 10, TextTrimming = TextTrimming.CharacterEllipsis, Margin = new Thickness(0, 5, 0, 0) });
                var card = Ui.Card(item, new Thickness(13)); card.Margin = new Thickness(0, 0, 0, 9);
                if (canvas.Selected == note) { card.BorderBrush = Ui.Brush("#B8D6FF"); card.Background = Ui.Brush("#F0F6FF"); }
                card.MouseLeftButtonDown += delegate(object s, MouseButtonEventArgs e) {
                    for (DependencyObject source = e.OriginalSource as DependencyObject; source != null && source != card; source = source is Visual ? VisualTreeHelper.GetParent(source) : LogicalTreeHelper.GetParent(source))
                        if (source is System.Windows.Controls.Primitives.ButtonBase) return;
                    canvas.Selected = note; canvas.InvalidateVisual();
                    var p = note.kind == "point" ? canvas.ToDisplay(note.point.x, note.point.y) : canvas.ToDisplay(note.rectangle.x1, note.rectangle.y1);
                    canvas.BringIntoView(new Rect(Math.Max(0, p.X - 40), Math.Max(0, p.Y - 40), 120, 120));
                    if (e.ClickCount == 2) EditNote(note, false); else Dispatcher.BeginInvoke(new Action(RefreshCards));
                };
                cards.Children.Add(card);
            }
        }
        private bool ConfirmReplace()
        {
            if (document == null || !document.Dirty) return true;
            if (!IsVisible) ResumePin();
            var choice = MessageBox.Show(this, "当前截图的批注还没有保存。是否先保存项目？\n项目包含原图，可在下次继续编辑。", "保留批注", MessageBoxButton.YesNoCancel, MessageBoxImage.Question);
            if (choice == MessageBoxResult.Cancel) return false;
            return choice != MessageBoxResult.Yes || SaveProject(false);
        }
        private bool SaveProject(bool saveAs)
        {
            if (document == null) return false;
            string path = savePath;
            if (path == null || saveAs) { var dialog = new SaveFileDialog { FileName = "review-" + DateTime.Now.ToString("yyyyMMdd-HHmmss") + ".json", Filter = "包含原图的批注项目|*.json", DefaultExt = ".json" }; if (dialog.ShowDialog(this) != true) return false; path = dialog.FileName; }
            try { document.Save(path); savePath = path; Title = "Help2Design Capture · 截图批注"; status.Text = "已保存项目（包含原图） · " + Path.GetFileName(path); return true; }
            catch (Exception ex) { Ui.Error(this, ex); return false; }
        }
        private void Export() { if (document != null) new ExportDialog(document) { Owner = this }.ShowDialog(); }
        private void CopyImage()
        {
            if (document == null) return;
            try { Clipboard.SetImage(document.Image); status.Text = "原图已复制到剪贴板。"; } catch (Exception ex) { Ui.Error(this, ex); }
        }
        private void SaveImage()
        {
            if (document == null) return;
            var dialog = new SaveFileDialog { FileName = document.Info.imageFile, Filter = "原始截图 PNG|*.png|带编号和批注的预览 PNG|*.png", DefaultExt = ".png" };
            if (dialog.ShowDialog(this) != true) return;
            try { if (dialog.FilterIndex == 2) PreviewRenderer.Save(document, dialog.FileName); else File.WriteAllBytes(dialog.FileName, document.Png); status.Text = "图片已保存 · " + Path.GetFileName(dialog.FileName); } catch (Exception ex) { Ui.Error(this, ex); }
        }
        private void Fit()
        {
            if (document == null) return;
            double availableWidth = Math.Max(100, viewport.ActualWidth - AnnotationCanvas.Padding * 2 - 18), availableHeight = Math.Max(100, viewport.ActualHeight - AnnotationCanvas.Padding * 2 - 18);
            canvas.Zoom = Math.Max(.02, Math.Min(1, Math.Min(availableWidth / document.Info.width, availableHeight / document.Info.height)));
            canvas.RefreshSize(); zoomLabel.Text = Math.Round(canvas.Zoom * 100) + "%";
        }
        private void ZoomBy(double factor)
        {
            if (document == null) return; autoFit = false;
            canvas.Zoom = Math.Max(.02, Math.Min(8, canvas.Zoom * factor)); canvas.RefreshSize(); zoomLabel.Text = Math.Round(canvas.Zoom * 100) + "%";
        }
        private void Keys(object sender, KeyEventArgs e)
        {
            if (Keyboard.FocusedElement is TextBox || OwnedWindows.Cast<Window>().Any(w => w.IsActive)) return;
            if (Keyboard.Modifiers == ModifierKeys.Control) {
                if (e.Key == Key.V) { PasteImage(); e.Handled = true; }
                else if (e.Key == Key.O) { OpenFile(); e.Handled = true; }
                else if (e.Key == Key.C) { CopyImage(); e.Handled = true; }
                else if (e.Key == Key.N) { StartCapture("region"); e.Handled = true; }
                else if (e.Key == Key.S) { SaveProject(false); e.Handled = true; }
                else if (e.Key == Key.E) { Export(); e.Handled = true; }
                else if (e.Key == Key.Z) { Undo(); e.Handled = true; }
                else if (e.Key == Key.Y) { Redo(); e.Handled = true; }
                else if (e.Key == Key.D0) { autoFit = true; Fit(); e.Handled = true; }
                return;
            }
            if (Keyboard.Modifiers != ModifierKeys.None) return;
            if (e.Key == Key.Escape) { if (!canvas.CancelActiveGesture()) Hide(); }
            else if (e.Key == Key.P) SetTool(CanvasTool.Point);
            else if (e.Key == Key.R) SetTool(CanvasTool.Rectangle);
            else if (e.Key == Key.B) SetTool(CanvasTool.Smart);
            else if (e.Key == Key.V) SetTool(CanvasTool.Select);
            else if (e.Key == Key.Delete && canvas.Selected != null) DeleteNote(canvas.Selected);
            else return;
            e.Handled = true;
        }
    }
}
