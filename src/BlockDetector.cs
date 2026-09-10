using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Linq;
using System.Runtime.InteropServices;

namespace Help2Design
{
    // Local, deterministic region proposals. These are visual candidates, not semantic objects.
    public static class BlockDetector
    {
        public static List<Candidate> Detect(Bitmap original)
        {
            double scale = Math.Min(1.0, 1000.0 / Math.Max(original.Width, original.Height));
            int w = Math.Max(1, (int)Math.Round(original.Width * scale)), h = Math.Max(1, (int)Math.Round(original.Height * scale));
            var result = new List<Candidate>();
            if (w < 16 || h < 16) return result;
            int[] pixels = new int[w * h];
            using (var bitmap = new Bitmap(w, h, PixelFormat.Format32bppArgb)) {
                using (var g = Graphics.FromImage(bitmap)) { g.Clear(Color.White); g.DrawImage(original, 0, 0, w, h); }
                var data = bitmap.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
                try { for (int y = 0; y < h; y++) Marshal.Copy(IntPtr.Add(data.Scan0, y * data.Stride), pixels, y * w, w); }
                finally { bitmap.UnlockBits(data); }
            }
            // Quantized colour components recover filled panels, cards and buttons.
            var colors = new int[pixels.Length];
            for (int i = 0; i < pixels.Length; i++) colors[i] = (((pixels[i] >> 16) & 255) / 16 << 8) | (((pixels[i] >> 8) & 255) / 16 << 4) | ((pixels[i] & 255) / 16);
            Components(colors, w, h, false, delegate(PixelRect box, int count) {
                double fill = (double)count / box.Area();
                // Cards can have large image/text holes. A continuous rectangular frame still identifies the outer card.
                if (box.Width() >= 24 && box.Height() >= 18 && count > 250 && (fill >= .70 || (fill >= .22 && FrameSupport(colors, box, w) > .86))) Add(result, box, scale, original.Width, original.Height, "color-region", "色块区域");
            });
            // Join neighbouring edges. Closing horizontal gaps groups text lines without OCR.
            var edges = new int[pixels.Length];
            for (int y = 1; y < h - 1; y++) for (int x = 1; x < w - 1; x++) {
                int i = y * w + x;
                if (Difference(pixels[i], pixels[i + 1]) > 36 || Difference(pixels[i], pixels[i + w]) > 36) edges[i] = 1;
            }
            var dilated = Dilate(edges, w, h, 3, 1);
            Components(dilated, w, h, true, delegate(PixelRect box, int count) {
                if (box.Width() >= 24 && box.Height() >= 14 && count >= 80) {
                    box.x1 = Math.Max(0, box.x1 + 2); box.x2 = Math.Min(w, box.x2 - 2);
                    Add(result, box, scale, original.Width, original.Height, "edge-region", "内容区域");
                }
            });
            // Preserve nested candidates, suppress only near-identical boxes.
            var final = new List<Candidate>();
            foreach (var candidate in result.OrderBy(c => c.bounds.Area())) {
                if (!final.Any(other => IoU(candidate.bounds, other.bounds) > .87)) final.Add(candidate);
                if (final.Count >= 180) break;
            }
            return final;
        }
        private static int Difference(int a, int b) { return Math.Max(Math.Abs(((a >> 16) & 255) - ((b >> 16) & 255)), Math.Max(Math.Abs(((a >> 8) & 255) - ((b >> 8) & 255)), Math.Abs((a & 255) - (b & 255)))); }
        private static double FrameSupport(int[] colors, PixelRect r, int w)
        {
            int value = colors[r.y1 * w + r.x1], matches = 0, count = 0;
            for (int x = r.x1; x < r.x2; x++) { if (colors[r.y1 * w + x] == value) matches++; if (colors[(r.y2 - 1) * w + x] == value) matches++; count += 2; }
            for (int y = r.y1; y < r.y2; y++) { if (colors[y * w + r.x1] == value) matches++; if (colors[y * w + r.x2 - 1] == value) matches++; count += 2; }
            return count == 0 ? 0 : (double)matches / count;
        }
        private static void Add(List<Candidate> list, PixelRect r, double scale, int w, int h, string method, string label)
        {
            var box = new PixelRect((int)Math.Floor(r.x1 / scale), (int)Math.Floor(r.y1 / scale), (int)Math.Ceiling(r.x2 / scale), (int)Math.Ceiling(r.y2 / scale)).Clip(w, h);
            if (box.Width() < 12 || box.Height() < 10 || box.Area() > (long)w * h * .90) return;
            list.Add(new Candidate { bounds = box, target = new TargetInfo { source = "vision", method = method, label = label } });
        }
        public static double IoU(PixelRect a, PixelRect b)
        {
            long inter = (long)Math.Max(0, Math.Min(a.x2, b.x2) - Math.Max(a.x1, b.x1)) * Math.Max(0, Math.Min(a.y2, b.y2) - Math.Max(a.y1, b.y1));
            long total = a.Area() + b.Area() - inter;
            return total <= 0 ? 0 : (double)inter / total;
        }
        private static int[] Dilate(int[] input, int w, int h, int rx, int ry)
        {
            var integral = new int[(w + 1) * (h + 1)];
            for (int y = 0; y < h; y++) { int row = 0; for (int x = 0; x < w; x++) { row += input[y * w + x]; integral[(y + 1) * (w + 1) + x + 1] = integral[y * (w + 1) + x + 1] + row; } }
            var output = new int[input.Length];
            for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
                int l = Math.Max(0, x - rx), t = Math.Max(0, y - ry), r = Math.Min(w, x + rx + 1), b = Math.Min(h, y + ry + 1);
                output[y * w + x] = integral[b * (w + 1) + r] - integral[t * (w + 1) + r] - integral[b * (w + 1) + l] + integral[t * (w + 1) + l] > 0 ? 1 : 0;
            }
            return output;
        }
        private static void Components(int[] map, int w, int h, bool skipZero, Action<PixelRect, int> found)
        {
            var visited = new bool[map.Length]; var queue = new int[map.Length];
            for (int start = 0; start < map.Length; start++) {
                if (visited[start] || (skipZero && map[start] == 0)) continue;
                int head = 0, tail = 0, value = map[start]; queue[tail++] = start; visited[start] = true;
                int l = start % w, r = l, t = start / w, b = t;
                while (head < tail) {
                    int i = queue[head++], x = i % w, y = i / w;
                    l = Math.Min(l, x); r = Math.Max(r, x); t = Math.Min(t, y); b = Math.Max(b, y);
                    if (x > 0) Enqueue(i - 1, value, map, visited, queue, ref tail);
                    if (x + 1 < w) Enqueue(i + 1, value, map, visited, queue, ref tail);
                    if (y > 0) Enqueue(i - w, value, map, visited, queue, ref tail);
                    if (y + 1 < h) Enqueue(i + w, value, map, visited, queue, ref tail);
                }
                found(new PixelRect(l, t, r + 1, b + 1), tail);
            }
        }
        private static void Enqueue(int i, int value, int[] map, bool[] visited, int[] queue, ref int tail) { if (!visited[i] && map[i] == value) { visited[i] = true; queue[tail++] = i; } }
    }
}
