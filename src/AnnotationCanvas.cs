using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;

namespace Help2Design
{
    public enum CanvasTool { Point, Rectangle, Smart, Select }
    public sealed class AnnotationCanvas : FrameworkElement
    {
        public CaptureDocument Document;
        public CanvasTool Tool = CanvasTool.Point;
        public Annotation Selected;
        public bool ShowCandidates;
        public double Zoom = 1;
        public const double Padding = 18;
        public Action<Annotation, bool> EditRequested;
        public Action<Annotation> SelectionChanged;
        public Action<Annotation, PixelRect, PixelPoint> GeometryChanged;
        public Action<int, int, string> PointerChanged;
        public Action<double> ZoomRequested;
        public Action MoveWindowRequested;
        public Action ResetZoomRequested;
        private Point down;
        private Point current;
        private bool drawing;
        private bool manipulating;
        private int handle = -1;
        private PixelRect previewRect;
        private PixelPoint previewPoint;
        private Candidate hovered;
        private CandidatePicker picker = new CandidatePicker();
        private readonly Brush green = Ui.Brush("#007AFF");
        private readonly Brush cyan = Ui.Brush("#007AFF");
        public AnnotationCanvas() { Focusable = true; Cursor = Cursors.Cross; SnapsToDevicePixels = true; }
        public void RefreshSize()
        {
            if (Document == null) { Width = 600; Height = 420; }
            else { Width = Document.Info.width * Zoom + Padding * 2; Height = Document.Info.height * Zoom + Padding * 2; }
            InvalidateVisual();
        }
        public void SetTool(CanvasTool tool) { CancelGesture(); Tool = tool; hovered = null; picker.Reset(); Cursor = tool == CanvasTool.Select ? Cursors.Arrow : Cursors.Cross; InvalidateVisual(); }
        public Point ToDisplay(int x, int y) { return new Point(Padding + x * Zoom, Padding + y * Zoom); }
        public Point ToImage(Point p) { return new Point((p.X - Padding) / Zoom, (p.Y - Padding) / Zoom); }
        private bool InImage(Point p) { return Document != null && p.X >= 0 && p.Y >= 0 && p.X < Document.Info.width && p.Y < Document.Info.height; }
        private PixelPoint ClampPoint(Point p) { return new PixelPoint(Math.Max(0, Math.Min(Document.Info.width - 1, (int)Math.Round(p.X))), Math.Max(0, Math.Min(Document.Info.height - 1, (int)Math.Round(p.Y)))); }
        private PixelRect DrawRect(Point a, Point b) { return PixelRect.FromPoints((int)Math.Round(a.X), (int)Math.Round(a.Y), (int)Math.Round(b.X), (int)Math.Round(b.Y)).Clip(Document.Info.width, Document.Info.height); }
        public void Select(Annotation n) { Selected = n; InvalidateVisual(); if (SelectionChanged != null) SelectionChanged(n); }
        private Point Marker(Annotation n) { return n.kind == "point" ? ToDisplay(n.point.x, n.point.y) : ToDisplay(n.rectangle.x1, n.rectangle.y1); }
        private Annotation HitMarker(Point p) { return Document.Notes.AsEnumerable().Reverse().FirstOrDefault(n => (Marker(n) - p).Length <= 16); }
        private Rect DisplayRect(PixelRect r) { return new Rect(Padding + r.x1 * Zoom, Padding + r.y1 * Zoom, r.Width() * Zoom, r.Height() * Zoom); }
        private Point[] Handles(PixelRect r)
        {
            Rect d = DisplayRect(r); double x = d.X + d.Width / 2, y = d.Y + d.Height / 2;
            return new[] { d.TopLeft, new Point(x, d.Top), d.TopRight, new Point(d.Right, y), d.BottomRight, new Point(x, d.Bottom), d.BottomLeft, new Point(d.Left, y) };
        }
        protected override void OnRender(DrawingContext dc)
        {
            dc.DrawRectangle(Brushes.Transparent, null, new Rect(0, 0, ActualWidth, ActualHeight));
            if (Document == null) return;
            Rect imageRect = new Rect(Padding, Padding, Document.Info.width * Zoom, Document.Info.height * Zoom);
            dc.DrawRectangle(Brushes.White, null, imageRect);
            dc.DrawImage(Document.Image, imageRect);
            if (ShowCandidates) foreach (var c in Document.Candidates) dc.DrawRectangle(null, new Pen(Ui.Brush(c.target.source == "uia" ? "#607DBC99" : "#405599AC"), 1), DisplayRect(c.bounds));
            if (Tool == CanvasTool.Smart && hovered != null && !drawing) {
                var pen = new Pen(cyan, 1.5) { DashStyle = DashStyles.Dash };
                dc.DrawRectangle(Ui.Brush("#16007AFF"), pen, DisplayRect(hovered.bounds));
            }
            foreach (var n in Document.Notes) {
                var rect = manipulating && n == Selected ? previewRect : n.rectangle;
                var point = manipulating && n == Selected ? previewPoint : n.point;
                bool active = n == Selected;
                if (rect != null) {
                    var d = DisplayRect(rect);
                    dc.DrawRectangle(active ? Ui.Brush("#14007AFF") : null, new Pen(green, active ? 2 : 1.4), d);
                    if (active && Tool == CanvasTool.Select) foreach (var h in Handles(rect)) dc.DrawRectangle(Brushes.White, new Pen(green, 1), new Rect(h.X - 4, h.Y - 4, 8, 8));
                }
                Point center = rect == null ? ToDisplay(point.x, point.y) : ToDisplay(rect.x1, rect.y1);
                DrawMarker(dc, center, n.number, active);
            }
            if (drawing && (Tool == CanvasTool.Rectangle || (Tool == CanvasTool.Smart && (current - down).Length * Zoom > 5))) dc.DrawRectangle(Ui.Brush("#15007AFF"), new Pen(green, 1.5) { DashStyle = DashStyles.Dash }, DisplayRect(DrawRect(down, current)));
        }
        private void DrawMarker(DrawingContext dc, Point p, int number, bool selected)
        {
            if (selected) dc.DrawEllipse(Ui.Brush("#30007AFF"), null, p, 18, 18);
            dc.DrawEllipse(green, new Pen(Brushes.White, 2), p, 13, 13);
            var text = new FormattedText(number.ToString(), CultureInfo.InvariantCulture, FlowDirection.LeftToRight, new Typeface("Segoe UI Semibold"), number < 100 ? 12 : 9, Brushes.White, VisualTreeHelper.GetDpi(this).PixelsPerDip);
            dc.DrawText(text, new Point(p.X - text.Width / 2, p.Y - text.Height / 2));
        }
        protected override void OnMouseDown(MouseButtonEventArgs e)
        {
            base.OnMouseDown(e); if (Document == null) return;
            if (e.ChangedButton == MouseButton.Middle) { if (ResetZoomRequested != null) ResetZoomRequested(); e.Handled = true; return; }
            if (e.ChangedButton == MouseButton.Right && CancelActiveGesture()) { e.Handled = true; return; }
            if (e.ChangedButton != MouseButton.Left) return;
            if (Keyboard.IsKeyDown(Key.Space) || (Keyboard.Modifiers & ModifierKeys.Alt) != 0) { CancelGesture(); if (MoveWindowRequested != null) MoveWindowRequested(); e.Handled = true; return; }
            Focus(); Point display = e.GetPosition(this); Point p = ToImage(display);
            if (Tool == CanvasTool.Select && Selected != null && Selected.rectangle != null) {
                var hs = Handles(Selected.rectangle); handle = Array.FindIndex(hs, h => (h - display).Length < 10);
                if (handle >= 0) { BeginMove(p); e.Handled = true; return; }
            }
            var hit = HitMarker(display);
            if (hit != null) {
                Select(hit);
                if (Tool == CanvasTool.Select && e.ClickCount == 1) { handle = -1; BeginMove(p); }
                else if (EditRequested != null) EditRequested(hit, false);
                e.Handled = true; return;
            }
            if (!InImage(p)) { if (MoveWindowRequested != null) MoveWindowRequested(); e.Handled = true; return; }
            if (Tool == CanvasTool.Select) {
                hit = Document.Notes.AsEnumerable().Reverse().FirstOrDefault(n => n.rectangle != null && n.rectangle.Contains((int)p.X, (int)p.Y));
                Select(hit);
                if (hit != null) { if (e.ClickCount >= 2) { if (EditRequested != null) EditRequested(hit, false); } else { handle = -1; BeginMove(p); } }
                else if (MoveWindowRequested != null) MoveWindowRequested();
                return;
            }
            if (Tool == CanvasTool.Smart) UpdateCandidate(p);
            down = p; current = p; drawing = true; CaptureMouse(); e.Handled = true;
        }
        private void BeginMove(Point p)
        {
            down = p; current = p; manipulating = true;
            previewRect = Selected.rectangle == null ? null : Json.Clone(Selected.rectangle);
            previewPoint = Selected.point == null ? null : Json.Clone(Selected.point);
            CaptureMouse();
        }
        protected override void OnMouseMove(MouseEventArgs e)
        {
            base.OnMouseMove(e); if (Document == null) return;
            Point p = ToImage(e.GetPosition(this)); current = p;
            if (manipulating && Selected != null) {
                int dx = (int)Math.Round(p.X - down.X), dy = (int)Math.Round(p.Y - down.Y);
                if (Selected.point != null) previewPoint = ClampPoint(new Point(Selected.point.x + dx, Selected.point.y + dy));
                else {
                    var r = Json.Clone(Selected.rectangle);
                    if (handle < 0) {
                        dx = Math.Max(-r.x1, Math.Min(Document.Info.width - r.x2, dx)); dy = Math.Max(-r.y1, Math.Min(Document.Info.height - r.y2, dy));
                        r.x1 += dx; r.x2 += dx; r.y1 += dy; r.y2 += dy;
                    } else {
                        if (handle == 0 || handle == 6 || handle == 7) r.x1 = Math.Max(0, Math.Min(r.x2 - 2, r.x1 + dx));
                        if (handle == 2 || handle == 3 || handle == 4) r.x2 = Math.Min(Document.Info.width, Math.Max(r.x1 + 2, r.x2 + dx));
                        if (handle == 0 || handle == 1 || handle == 2) r.y1 = Math.Max(0, Math.Min(r.y2 - 2, r.y1 + dy));
                        if (handle == 4 || handle == 5 || handle == 6) r.y2 = Math.Min(Document.Info.height, Math.Max(r.y1 + 2, r.y2 + dy));
                    }
                    previewRect = r;
                }
            } else if (!drawing && Tool == CanvasTool.Smart) {
                UpdateCandidate(p);
            }
            NotifyPointer(p);
            InvalidateVisual();
        }
        protected override void OnMouseUp(MouseButtonEventArgs e)
        {
            base.OnMouseUp(e); if (Document == null || e.ChangedButton != MouseButton.Left) return;
            if (manipulating) {
                manipulating = false; ReleaseMouseCapture();
                if ((current - down).Length * Zoom > 2 && GeometryChanged != null) GeometryChanged(Selected, previewRect, previewPoint);
                InvalidateVisual(); return;
            }
            if (!drawing) return;
            drawing = false; ReleaseMouseCapture();
            Annotation note = null;
            if (Tool == CanvasTool.Point) note = Annotation.Create("point", ClampPoint(down), null, null);
            if (Tool == CanvasTool.Rectangle) { var box = DrawRect(down, current); if (box.Width() >= 2 && box.Height() >= 2) note = Annotation.Create("rectangle", null, box, null); }
            if (Tool == CanvasTool.Smart) {
                if ((current - down).Length * Zoom > 5) { var box = DrawRect(down, current); if (box.Width() >= 2 && box.Height() >= 2) note = Annotation.Create("rectangle", null, box, null); }
                else if (hovered != null && hovered.bounds.Contains((int)down.X, (int)down.Y)) note = Annotation.Create("rectangle", null, Json.Clone(hovered.bounds), Json.Clone(hovered.target));
                else note = Annotation.Create("point", ClampPoint(down), null, null);
            }
            InvalidateVisual(); if (note != null && EditRequested != null) EditRequested(note, true);
        }
        protected override void OnMouseLeave(MouseEventArgs e) { base.OnMouseLeave(e); if (!drawing && !manipulating) { hovered = null; InvalidateVisual(); } }
        private void UpdateCandidate(Point p) { picker.Update(Document.Candidates, (int)p.X, (int)p.Y); hovered = picker.Current; }
        private void NotifyPointer(Point p) { if (PointerChanged != null && InImage(p)) PointerChanged((int)p.X, (int)p.Y, hovered == null || Tool != CanvasTool.Smart ? "" : hovered.target.label + "  ·  " + (picker.Index + 1) + "/" + picker.Levels.Count + "  ·  滚轮 ↑ 更大  ↓ 更小"); }
        internal void StepCandidate(Point p, int direction) { UpdateCandidate(p); hovered = picker.Step(direction); NotifyPointer(p); InvalidateVisual(); }
        protected override void OnMouseWheel(MouseWheelEventArgs e)
        {
            if (drawing || manipulating) { e.Handled = true; return; }
            if ((Keyboard.Modifiers & ModifierKeys.Control) != 0 && ZoomRequested != null) { ZoomRequested(e.Delta > 0 ? 1.15 : 1 / 1.15); e.Handled = true; }
            else if (Document != null && Tool == CanvasTool.Smart) { StepCandidate(ToImage(e.GetPosition(this)), e.Delta); e.Handled = true; }
            else if (ZoomRequested != null) { ZoomRequested(e.Delta > 0 ? 1.1 : 1 / 1.1); e.Handled = true; }
            else base.OnMouseWheel(e);
        }
        protected override void OnKeyDown(KeyEventArgs e)
        {
            if (e.Key == Key.Escape) { CancelGesture(); e.Handled = true; }
            if (e.Key == Key.Tab && Tool == CanvasTool.Smart && Document != null) { StepCandidate(ToImage(Mouse.GetPosition(this)), (Keyboard.Modifiers & ModifierKeys.Shift) != 0 ? -1 : 1); e.Handled = true; }
            base.OnKeyDown(e);
        }
        private void CancelGesture() { drawing = false; manipulating = false; ReleaseMouseCapture(); InvalidateVisual(); }
        public bool CancelActiveGesture() { bool active = drawing || manipulating; if (active) CancelGesture(); return active; }
    }
}
