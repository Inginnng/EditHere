using System;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using Forms = System.Windows.Forms;

namespace Help2Design
{
    public sealed partial class MainWindow
    {
        private ColumnDefinition notesColumn;
        private Border notesPanel;
        private Button notesButton, pinButton;
        private bool notesUserHidden, initialHandled;
        public bool TestMode;
        private const double NotesWidth = 272;

        public MainWindow()
        {
            SetResourceReference(StyleProperty, typeof(Window));
            Title = "Help2Design · 截图批注"; Width = 840; Height = 580; MinWidth = 570; MinHeight = 260;
            WindowStartupLocation = WindowStartupLocation.Manual;
            Topmost = true; ShowInTaskbar = false;
            Ui.FloatingWindow(this, true);
            var frame = new Border { Background = Ui.Brush("#FAFAFC"), BorderBrush = Ui.Brush("#DDDDDF"), BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(16), ClipToBounds = true };
            Content = frame;
            var layout = new Grid(); layout.ColumnDefinitions.Add(new ColumnDefinition()); notesColumn = new ColumnDefinition { Width = new GridLength(0) }; layout.ColumnDefinitions.Add(notesColumn); frame.Child = layout;
            var main = new Grid(); main.RowDefinitions.Add(new RowDefinition { Height = new GridLength(36) }); main.RowDefinitions.Add(new RowDefinition()); main.RowDefinitions.Add(new RowDefinition { Height = new GridLength(68) }); layout.Children.Add(main);

            var header = new DockPanel { Margin = new Thickness(12, 2, 8, 0), Background = Brushes.Transparent, LastChildFill = true };
            header.MouseLeftButtonDown += delegate(object s, MouseButtonEventArgs e) { if (!Ui.FromButton(e.OriginalSource as DependencyObject)) { MovePin(); e.Handled = true; } };
            var controls = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center };
            pinButton = Ui.IconButton("pin", "置顶贴图", delegate { Topmost = !Topmost; UpdatePin(); }); pinButton.Width = 28; pinButton.Height = 28; controls.Children.Add(pinButton);
            var close = Ui.IconButton("close", "收起贴图 · Esc（批注保留在内存，托盘可恢复）", Hide); close.Width = 28; close.Height = 28; controls.Children.Add(close);
            DockPanel.SetDock(controls, Dock.Right); header.Children.Add(controls);
            imageInfo = Ui.Text("截图", 11, "#8E8E93"); imageInfo.VerticalAlignment = VerticalAlignment.Center; imageInfo.TextWrapping = TextWrapping.NoWrap; imageInfo.TextTrimming = TextTrimming.CharacterEllipsis; header.Children.Add(imageInfo); main.Children.Add(header);

            canvas = new AnnotationCanvas { HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center };
            viewport = new ScrollViewer { Content = canvas, HorizontalScrollBarVisibility = ScrollBarVisibility.Auto, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, CanContentScroll = false, Focusable = false, Background = Ui.Brush("#F2F2F5") };
            Grid.SetRow(viewport, 1); main.Children.Add(viewport);

            var bottom = new Grid(); bottom.RowDefinitions.Add(new RowDefinition { Height = new GridLength(20) }); bottom.RowDefinitions.Add(new RowDefinition()); Grid.SetRow(bottom, 2); main.Children.Add(bottom);
            status = Ui.Text("滚轮切换区域 · 单击批注 · 右键更多", 10, "#8E8E93"); status.HorizontalAlignment = HorizontalAlignment.Center; status.VerticalAlignment = VerticalAlignment.Center; status.TextWrapping = TextWrapping.NoWrap; status.TextTrimming = TextTrimming.CharacterEllipsis; status.Margin = new Thickness(10, 0, 10, 0); bottom.Children.Add(status);
            var toolbar = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(8, 0, 8, 5) };
            AddIconTool(toolbar, "point", CanvasTool.Point, "点标注 · P"); AddIconTool(toolbar, "box", CanvasTool.Rectangle, "画框 · R"); AddIconTool(toolbar, "smart", CanvasTool.Smart, "智能选块 · B · 滚轮切换大小"); AddIconTool(toolbar, "move", CanvasTool.Select, "调整 / 移动 · V");
            toolbar.Children.Add(Ui.Separator());
            undoButton = Ui.IconButton("undo", "撤销 · Ctrl+Z", Undo); redoButton = Ui.IconButton("redo", "重做 · Ctrl+Y", Redo); toolbar.Children.Add(undoButton); toolbar.Children.Add(redoButton);
            toolbar.Children.Add(Ui.Separator());
            toolbar.Children.Add(Ui.IconButton("copy", "复制原图 · Ctrl+C", CopyImage));
            notesButton = Ui.IconButton("sidebar", "显示 / 隐藏批注", delegate { notesUserHidden = notesPanel.Visibility == Visibility.Visible; SetNotesVisible(!notesUserHidden, true); }); toolbar.Children.Add(notesButton);
            var more = Ui.IconButton("more", "更多操作", delegate { }); more.ContextMenu = BuildMenu(); more.Click += delegate { more.ContextMenu.PlacementTarget = more; more.ContextMenu.IsOpen = true; }; toolbar.Children.Add(more);
            toolbar.Children.Add(Ui.Separator());
            exportButton = Ui.Button("导出 JSON", Export, true); exportButton.Padding = new Thickness(14, 8, 14, 8); exportButton.Margin = new Thickness(2, 0, 0, 0); toolbar.Children.Add(exportButton);
            Grid.SetRow(toolbar, 1); bottom.Children.Add(toolbar);
            zoomLabel = Ui.Text("100%", 11, "#8E8E93"); detectionInfo = Ui.Text("", 11, "#8E8E93");

            var side = new Grid(); side.RowDefinitions.Add(new RowDefinition { Height = new GridLength(54) }); side.RowDefinitions.Add(new RowDefinition());
            var sideHead = new DockPanel { Margin = new Thickness(18, 10, 10, 6) };
            var fold = Ui.IconButton("sidebar", "收起批注", delegate { notesUserHidden = true; SetNotesVisible(false, true); }); DockPanel.SetDock(fold, Dock.Right); sideHead.Children.Add(fold);
            noteCount = new TextBlock { Text = "批注", FontSize = 14, FontWeight = FontWeights.SemiBold, VerticalAlignment = VerticalAlignment.Center }; sideHead.Children.Add(noteCount); side.Children.Add(sideHead);
            cards = new StackPanel { Margin = new Thickness(10, 0, 10, 12) };
            var scroll = new ScrollViewer { Content = cards, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled }; Grid.SetRow(scroll, 1); side.Children.Add(scroll);
            notesPanel = new Border { Child = side, Background = Brushes.White, BorderBrush = Ui.Brush("#E5E5EA"), BorderThickness = new Thickness(1, 0, 0, 0), Visibility = Visibility.Collapsed }; Grid.SetColumn(notesPanel, 1); layout.Children.Add(notesPanel);
            ContextMenu = BuildMenu();

            canvas.EditRequested = EditNote;
            canvas.SelectionChanged = delegate { RefreshCards(); };
            canvas.GeometryChanged = delegate(Annotation n, PixelRect r, PixelPoint p) { PushUndo(); n.rectangle = r; n.point = p; n.updatedAt = Json.Now(); Changed(); };
            canvas.PointerChanged = delegate(int x, int y, string label) { status.Text = String.IsNullOrEmpty(label) ? ToolHint() : label; status.ToolTip = "X " + x + " · Y " + y + " px"; };
            canvas.ZoomRequested = ZoomBy; canvas.MoveWindowRequested = MovePin;
            canvas.ResetZoomRequested = delegate { if (Math.Abs(canvas.Zoom - 1) > .001) ZoomBy(1 / canvas.Zoom); else { autoFit = true; Fit(); } };
            viewport.SizeChanged += delegate { if (autoFit) Fit(); };
            AllowDrop = true;
            DragOver += delegate(object s, DragEventArgs e) { e.Effects = e.Data.GetDataPresent(DataFormats.FileDrop) ? DragDropEffects.Copy : DragDropEffects.None; e.Handled = true; };
            Drop += delegate(object s, DragEventArgs e) { if (e.Data.GetDataPresent(DataFormats.FileDrop)) { var paths = (string[])e.Data.GetData(DataFormats.FileDrop); if (paths.Length > 0) OpenPath(paths[0]); } e.Handled = true; };
            PreviewKeyDown += Keys; SourceInitialized += InitializeHotkeys;
            Loaded += delegate { if (!initialHandled && !String.IsNullOrEmpty(InitialFile)) OpenInitial(); UpdatePin(); };
            Closing += delegate(object s, System.ComponentModel.CancelEventArgs e) { if (!closing && !TestMode) { e.Cancel = true; Hide(); } };
            Closed += delegate { foreach (int id in registeredHotkeys) Native.UnregisterHotKey(handle, id); if (hwndSource != null) hwndSource.RemoveHook(WndProc); if (tray != null) { tray.Visible = false; tray.Dispose(); } };
            SetTool(CanvasTool.Smart); RefreshCards(); UpdatePin();
        }
        public void BeginStartup()
        {
            // Create only the invisible message target for global shortcuts/tray; never show a home page.
            new WindowInteropHelper(this).EnsureHandle();
            Dispatcher.BeginInvoke(new Action(OpenInitial));
        }
        private void OpenInitial() { if (initialHandled) return; initialHandled = true; if (InitialFile == "--demo") LoadDemo(); else if (!String.IsNullOrEmpty(InitialFile)) OpenPath(InitialFile); else StartCapture("region"); }
        internal void ReceiveRequest(string argument) { if (capturing || OwnedWindows.Cast<Window>().Any(w => w.IsVisible)) return; if (String.IsNullOrEmpty(argument)) StartCapture("region"); else if (argument == "--demo") LoadDemo(); else OpenPath(argument); }
        private void AddIconTool(Panel panel, string icon, CanvasTool tool, string hint) { var button = Ui.IconButton(icon, hint, delegate { SetTool(tool); }); toolButtons.Add(button); panel.Children.Add(button); }
        private void MovePin() { if (Mouse.LeftButton == MouseButtonState.Pressed) { try { DragMove(); } catch (InvalidOperationException) { } } }
        private void UpdatePin() { if (pinButton != null) { pinButton.Foreground = Ui.Brush(Topmost ? "#007AFF" : "#8E8E93"); pinButton.ToolTip = Topmost ? "取消置顶" : "置顶贴图"; } }
        private void ResumePin() { if (document == null) { StartCapture("region"); return; } Show(); WindowState = WindowState.Normal; Activate(); }
        private void ExitApplication() { if (document != null && document.Dirty && !IsVisible) ResumePin(); if (!ConfirmReplace()) return; closing = true; Application.Current.Shutdown(); }

        private Rect MonitorWorkArea()
        {
            var source = PresentationSource.FromVisual(this);
            var matrix = source != null && source.CompositionTarget != null ? source.CompositionTarget.TransformFromDevice : Matrix.Identity;
            var screen = document != null && document.Info.screenBounds != null ? Forms.Screen.FromPoint(new System.Drawing.Point(document.Info.screenBounds.x1 + document.Info.width / 2, document.Info.screenBounds.y1 + document.Info.height / 2)) : Forms.Screen.FromHandle(new WindowInteropHelper(this).Handle);
            var bounds = screen.WorkingArea; var a = matrix.Transform(new Point(bounds.Left, bounds.Top)); var b = matrix.Transform(new Point(bounds.Right, bounds.Bottom)); return new Rect(a, b);
        }
        private void SizeToImage()
        {
            if (document == null) return;
            var area = MonitorWorkArea(); double side = notesPanel.Visibility == Visibility.Visible ? NotesWidth : 0;
            double maxW = Math.Max(MinWidth, area.Width - 48 - side), maxH = Math.Max(170, area.Height - 160);
            double scale = Math.Min(1, Math.Min(Math.Min(900, maxW - 38) / document.Info.width, maxH / document.Info.height));
            Width = Math.Max(MinWidth + side, document.Info.width * scale + 38 + side); Height = Math.Max(MinHeight, document.Info.height * scale + 142);
            Width = Math.Min(Width, area.Width - 16); Height = Math.Min(Height, area.Height - 16);
            double x = area.Left + (area.Width - Width) / 2, y = area.Top + (area.Height - Height) / 2;
            if (document.Info.screenBounds != null) {
                var source = PresentationSource.FromVisual(this); var m = source.CompositionTarget.TransformFromDevice;
                var p = m.Transform(new Point(document.Info.screenBounds.x1, document.Info.screenBounds.y1)); x = p.X - AnnotationCanvas.Padding; y = p.Y - 36 - AnnotationCanvas.Padding;
            }
            Left = Math.Max(area.Left + 8, Math.Min(area.Right - Width - 8, x)); Top = Math.Max(area.Top + 8, Math.Min(area.Bottom - Height - 8, y)); autoFit = true;
        }
        private void SetNotesVisible(bool visible, bool resize)
        {
            if (notesPanel == null) return;
            bool wasVisible = notesPanel.Visibility == Visibility.Visible;
            notesPanel.Visibility = visible ? Visibility.Visible : Visibility.Collapsed; notesColumn.Width = new GridLength(visible ? NotesWidth : 0);
            MinWidth = 570 + (visible ? NotesWidth : 0);
            if (notesButton != null) notesButton.Foreground = Ui.Brush(visible ? "#007AFF" : "#636366");
            if (resize && wasVisible != visible && document != null) {
                var area = MonitorWorkArea(); Width = Math.Max(MinWidth, Math.Min(area.Width - 16, Width + (visible ? NotesWidth : -NotesWidth)));
                Left = Math.Max(area.Left + 8, Math.Min(area.Right - Width - 8, Left));
            }
        }
        private void PositionNote(NoteDialog dialog, Annotation note)
        {
            var p = note.kind == "point" ? canvas.ToDisplay(note.point.x, note.point.y) : canvas.ToDisplay(note.rectangle.x1, note.rectangle.y1);
            var screen = canvas.PointToScreen(p); var source = PresentationSource.FromVisual(this); var local = source.CompositionTarget.TransformFromDevice.Transform(screen);
            var area = MonitorWorkArea(); dialog.WindowStartupLocation = WindowStartupLocation.Manual;
            dialog.Left = Math.Max(area.Left + 12, Math.Min(area.Right - dialog.Width - 12, local.X + 24)); dialog.Top = Math.Max(area.Top + 12, Math.Min(area.Bottom - dialog.Height - 12, local.Y + 18));
        }
        private ContextMenu BuildMenu()
        {
            var menu = new ContextMenu();
            AddMenu(menu, "重新截图", delegate { StartCapture("region"); }, "Ctrl+N");
            AddMenu(menu, "窗口截图", delegate { StartCapture("window"); }, "Ctrl+Alt+3");
            AddMenu(menu, "全屏截图", delegate { StartCapture("fullscreen"); }, "Ctrl+Alt+4");
            var delayMenu = new MenuItem { Header = "截图延时" };
            foreach (int seconds in new[] { 0, 3, 5 }) { int value = seconds; var item = new MenuItem { Header = seconds == 0 ? "立即" : seconds + " 秒", IsCheckable = true }; item.Click += delegate { captureDelay = value; }; delayMenu.Items.Add(item); }
            delayMenu.SubmenuOpened += delegate { for (int i = 0; i < 3; i++) ((MenuItem)delayMenu.Items[i]).IsChecked = captureDelay == new[] { 0, 3, 5 }[i]; }; menu.Items.Add(delayMenu);
            menu.Items.Add(new Separator());
            AddMenu(menu, "点标注", delegate { SetTool(CanvasTool.Point); }, "P"); AddMenu(menu, "框选批注", delegate { SetTool(CanvasTool.Rectangle); }, "R"); AddMenu(menu, "智能选块", delegate { SetTool(CanvasTool.Smart); }, "B"); AddMenu(menu, "调整 / 移动", delegate { SetTool(CanvasTool.Select); }, "V");
            var candidates = new MenuItem { Header = "显示所有候选框", IsCheckable = true }; candidates.Click += delegate { canvas.ShowCandidates = candidates.IsChecked; canvas.InvalidateVisual(); }; menu.Items.Add(candidates);
            AddMenu(menu, "适应图片", delegate { autoFit = true; Fit(); }, "Ctrl+0");
            menu.Items.Add(new Separator());
            AddMenu(menu, "复制原图", CopyImage, "Ctrl+C"); AddMenu(menu, "复制 JSON", CopyJson, ""); AddMenu(menu, "保存图片…", SaveImage, ""); AddMenu(menu, "保存项目…", delegate { SaveProject(false); }, "Ctrl+S");
            AddMenu(menu, "打开图片或项目…", OpenFile, "Ctrl+O"); AddMenu(menu, "粘贴图片", PasteImage, "Ctrl+V");
            menu.Items.Add(new Separator()); AddMenu(menu, "收起贴图", Hide, "Esc"); AddMenu(menu, "退出 Help2Design", ExitApplication, "");
            menu.Opened += delegate { candidates.IsChecked = canvas.ShowCandidates; }; return menu;
        }
        private static void AddMenu(ItemsControl menu, string label, Action action, string key) { var item = new MenuItem { Header = label, InputGestureText = key }; item.Click += delegate { action(); }; menu.Items.Add(item); }
        private void CopyJson() { if (document == null) return; try { Forms.Clipboard.SetDataObject(Json.Serialize(document.Export(false)), true, 5, 80); status.Text = "JSON 已复制"; } catch (Exception e) { Ui.Error(this, e); } }
    }
}
