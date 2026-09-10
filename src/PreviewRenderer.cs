using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Collections.Generic;

namespace Help2Design
{
    public static class PreviewRenderer
    {
        public static void Save(CaptureDocument doc, string path)
        {
            const int pad = 24, side = 340;
            int width = doc.Info.width + pad * 3 + side;
            var heights = new List<int>(); int total = 80;
            using (var font = new Font("Microsoft YaHei UI", 12, FontStyle.Regular, GraphicsUnit.Pixel))
            using (var small = new Font("Microsoft YaHei UI", 10, FontStyle.Regular, GraphicsUnit.Pixel))
            using (var title = new Font("Microsoft YaHei UI", 18, FontStyle.Bold, GraphicsUnit.Pixel))
            using (var number = new Font("Segoe UI", 12, FontStyle.Bold, GraphicsUnit.Pixel)) {
                using (var measure = new Bitmap(1, 1)) using (var g = Graphics.FromImage(measure)) foreach (var note in doc.Notes) { int h = Math.Max(80, (int)Math.Ceiling(g.MeasureString(note.comment, font, side - 48).Height) + 60); heights.Add(h); total += h + 12; }
                int height = Math.Max(doc.Info.height + pad * 2, total + pad);
                if ((long)width * height > 100000000) throw new InvalidOperationException("批注预览图过大，请导出 JSON 与原图。");
                using (var bitmap = new Bitmap(width, height, PixelFormat.Format32bppArgb))
                using (var g = Graphics.FromImage(bitmap))
                using (var stream = new MemoryStream(doc.Png))
                using (var image = new Bitmap(stream))
                using (var green = new SolidBrush(Color.FromArgb(0, 122, 255)))
                using (var ink = new SolidBrush(Color.FromArgb(29, 29, 31)))
                using (var muted = new SolidBrush(Color.FromArgb(142, 142, 147)))
                using (var pen = new Pen(Color.FromArgb(0, 122, 255), 2)) {
                    g.Clear(Color.FromArgb(250, 250, 252)); g.DrawImageUnscaled(image, pad, pad); g.SmoothingMode = SmoothingMode.AntiAlias;
                    var center = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center };
                    foreach (var note in doc.Notes) {
                        int x = note.kind == "point" ? note.point.x : note.rectangle.x1, y = note.kind == "point" ? note.point.y : note.rectangle.y1;
                        if (note.rectangle != null) g.DrawRectangle(pen, pad + note.rectangle.x1, pad + note.rectangle.y1, Math.Max(1, note.rectangle.Width() - 1), Math.Max(1, note.rectangle.Height() - 1));
                        var badge = new Rectangle(pad + x - 14, pad + y - 14, 28, 28); g.FillEllipse(green, badge); g.DrawEllipse(Pens.White, badge); g.DrawString(note.number.ToString(), number, Brushes.White, badge, center);
                    }
                    int left = doc.Info.width + pad * 2, top = 28;
                    g.DrawString("Help2Design / 批注 " + doc.Notes.Count, title, ink, left, top); top += 50;
                    for (int i = 0; i < doc.Notes.Count; i++) {
                        var note = doc.Notes[i]; int h = heights[i];
                        g.FillRectangle(Brushes.White, left, top, side, h); g.FillEllipse(green, left + 14, top + 12, 24, 24);
                        g.DrawString(note.number.ToString(), number, Brushes.White, new Rectangle(left + 14, top + 12, 24, 24), center);
                        g.DrawString(note.comment, font, ink, new RectangleF(left + 18, top + 46, side - 36, h - 48));
                        g.DrawString(Ui.Coordinates(note), small, muted, left + 45, top + 17); top += h + 12;
                    }
                    bitmap.Save(path, ImageFormat.Png); center.Dispose();
                }
            }
        }
    }
}
