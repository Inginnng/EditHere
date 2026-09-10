using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
using System.Text;

namespace Help2Design
{
    public static class SelfTest
    {
        public static Bitmap DemoImage()
        {
            var image = new Bitmap(1000, 640, PixelFormat.Format32bppArgb);
            using (var g = Graphics.FromImage(image))
            using (var ink = new SolidBrush(Color.FromArgb(31, 50, 40)))
            using (var muted = new SolidBrush(Color.FromArgb(113, 127, 118)))
            using (var green = new SolidBrush(Color.FromArgb(35, 105, 70)))
            using (var light = new SolidBrush(Color.FromArgb(224, 234, 222)))
            using (var title = new Font("Microsoft YaHei UI", 30, FontStyle.Bold, GraphicsUnit.Pixel))
            using (var text = new Font("Microsoft YaHei UI", 15, FontStyle.Regular, GraphicsUnit.Pixel))
            using (var small = new Font("Microsoft YaHei UI", 12, FontStyle.Regular, GraphicsUnit.Pixel)) {
                g.Clear(Color.FromArgb(245, 244, 237));
                g.FillRectangle(Brushes.White, 0, 0, 1000, 68);
                g.DrawString("FIELDNOTES", text, ink, 40, 22); g.DrawString("探索      收藏      关于", text, muted, 700, 22);
                g.DrawString("为日常，留一点空白。", title, ink, 42, 112);
                g.DrawString("一个示例页面。试着点选标题，或框选下面的卡片并留下修改意见。", text, muted, 44, 165);
                g.FillRectangle(green, 780, 119, 170, 46); g.DrawString("发现灵感  →", text, Brushes.White, 815, 130);
                int[] xs = { 42, 360, 678 };
                string[] names = { "林间的光", "山的轮廓", "慢一点的午后" };
                for (int i = 0; i < xs.Length; i++) {
                    int x = xs[i]; g.FillRectangle(Brushes.White, x, 240, 280, 320); g.FillRectangle(light, x + 16, 256, 248, 188);
                    if (i == 0) { g.FillRectangle(green, x + 105, 270, 18, 174); g.FillEllipse(green, x + 45, 270, 130, 110); }
                    if (i == 1) g.FillPolygon(green, new[] { new Point(x + 30, 430), new Point(x + 130, 287), new Point(x + 250, 430) });
                    if (i == 2) { g.FillEllipse(Brushes.White, x + 55, 300, 160, 100); g.DrawEllipse(new Pen(Color.FromArgb(148, 174, 145), 3), x + 55, 300, 160, 100); }
                    g.DrawString(names[i], text, ink, x + 18, 466); g.DrawString("生活观察  /  VOL. 0" + (i + 1), small, muted, x + 18, 505);
                }
                g.DrawString("© FIELDNOTES  ·  示例图片，仅用于体验批注", small, muted, 42, 606);
            }
            return image;
        }
        public static int Run(string directory)
        {
            Directory.CreateDirectory(directory); var log = new List<string>();
            try {
                using (var bitmap = DemoImage()) {
                    bitmap.Save(Path.Combine(directory, "fixture.png"), ImageFormat.Png);
                    var doc = CaptureDocument.FromBitmap(bitmap, "demo", "测试画面", null);
                    var p = Annotation.Create("point", new PixelPoint(0, 0), null, null); p.comment = "标题用“中文引号”，保持换行\n路径 C:\\design\\a.png；emoji 🌿";
                    var r = Annotation.Create("rectangle", null, new PixelRect(42, 240, 322, 560), null); r.comment = "增加卡片留白，标题改为两行。";
                    doc.Notes.Add(p); doc.Notes.Add(r);
                    string raw = Json.Serialize(doc.Export(false));
                    var parsed = Json.ReadDocument(raw); Assert(parsed.annotations[0].comment == p.comment, "JSON Unicode, escaping and newline round-trip", log);
                    Assert(parsed.annotations[1].rectangle.x2 == 322 && parsed.capture.width == 1000, "Original pixel coordinates retained", log);
                    File.WriteAllText(Path.Combine(directory, "export.json"), raw, new UTF8Encoding(false));
                    string project = Path.Combine(directory, "review.json"); doc.Save(project);
                    var loaded = CaptureDocument.Load(project); Assert(loaded.Notes.Count == 2 && loaded.Info.sha256 == doc.Info.sha256, "Embedded image project round-trip and SHA256", log);
                    var outside = Json.Clone(parsed); outside.annotations[0].point.x = 1000;
                    Reject(delegate { Json.Validate(outside); }, "Reject point on exclusive right boundary", log);
                    var badRect = Json.Clone(parsed); badRect.annotations[1].rectangle.x2 = 40;
                    Reject(delegate { Json.Validate(badRect); }, "Reject inverted rectangle", log);
                    var duplicate = Json.Clone(parsed); duplicate.annotations[1].id = duplicate.annotations[0].id;
                    Reject(delegate { Json.Validate(duplicate); }, "Reject duplicate IDs", log);
                    var blank = Json.Clone(parsed); blank.annotations[1].comment = "  ";
                    Reject(delegate { Json.Validate(blank); }, "Reject blank annotations", log);
                    Reject(delegate { Json.ReadDocument(raw.Replace("\"x\": 0", "\"x\": 0.5")); }, "Reject fractional coordinate coercion", log);
                    Reject(delegate { Json.ReadDocument(raw.Replace("\"schemaVersion\":", "\"unexpected\": 1, \"schemaVersion\":")); }, "Reject unknown JSON fields", log);
                    Reject(delegate { Json.ReadDocument(raw.Replace("\"title\": \"测试画面\"", "\"title\": 12")); }, "Reject numeric text coercion", log);
                    var unsafePath = Json.Clone(parsed); unsafePath.capture.imageFile = "..\\outside.png";
                    Reject(delegate { Json.Validate(unsafePath); }, "Reject image path traversal including embedded projects", log);
                    var edge = new PixelRect(0, 0, 1000, 640); Json.ValidateRect(edge, 1000, 640); log.Add("PASS Full-image exclusive rectangle boundary");
                    var detected = BlockDetector.Detect(bitmap);
                    Assert(detected.Count > 5, "Detect visual regions in fixture", log);
                    Assert(detected.All(c => c.bounds.x1 >= 0 && c.bounds.y1 >= 0 && c.bounds.x2 <= 1000 && c.bounds.y2 <= 640), "All detected regions stay within source pixels", log);
                    var expectedCard = new PixelRect(42, 240, 322, 560);
                    Assert(detected.Any(c => BlockDetector.IoU(c.bounds, expectedCard) > .85), "Recover a known card boundary (IoU > 0.85)", log);
                    File.WriteAllText(Path.Combine(directory, "detections.json"), Json.Serialize(detected), new UTF8Encoding(false));
                    PreviewRenderer.Save(doc, Path.Combine(directory, "annotated-preview.png")); log.Add("PASS Render annotated PNG with right-side notes");
                    var clipped = new PixelRect(-30, -20, 100, 80).Clip(50, 50); Assert(clipped.x1 == 0 && clipped.y1 == 0 && clipped.x2 == 50 && clipped.y2 == 50, "Clip geometry into capture-local coordinates", log);
                    // Exercise the real crop/element mapping code using a generated image, without reading the desktop.
                    using (var overlay = new CaptureOverlay(bitmap, new Rectangle(-500, 100, 1000, 640), new List<NativeWindow>(), false)) {
                        var flags = System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance;
                        typeof(CaptureOverlay).GetField("selection", flags).SetValue(overlay, new Rectangle(50, 60, 150, 100));
                        var original = new PixelRect(-480, 170, -320, 280);
                        typeof(CaptureOverlay).GetField("elements", flags).SetValue(overlay, new List<Candidate> { new Candidate { bounds = original, target = new TargetInfo { source = "uia", label = "Partially clipped element", method = "windows-accessibility", originalScreenBounds = original } } });
                        typeof(CaptureOverlay).GetMethod("Complete", flags).Invoke(overlay, null);
                        var crop = overlay.Result;
                        Assert(crop.Info.width == 150 && crop.Info.height == 100 && crop.Info.screenBounds.x1 == -450 && crop.Info.screenBounds.y1 == 160, "Crop retains negative-monitor screen origin and exact dimensions", log);
                        var region = crop.Candidates.Single();
                        Assert(region.bounds.x1 == 0 && region.bounds.y1 == 10 && region.bounds.x2 == 130 && region.bounds.y2 == 100 && region.target.clipped && region.target.originalScreenBounds.x1 == -480, "Clipped UIA element maps to crop-local pixels and retains original bounds", log);
                        using (var stream = new MemoryStream(crop.Png)) using (var result = new Bitmap(stream)) Assert(result.GetPixel(10, 10).ToArgb() == bitmap.GetPixel(60, 70).ToArgb(), "Crop pixel content matches the source", log);
                    }
                }
                using (var blank = new Bitmap(240, 160)) { using (var g = Graphics.FromImage(blank)) g.Clear(Color.White); Assert(BlockDetector.Detect(blank).Count == 0, "Blank image does not hallucinate blocks", log); }
                using (var tiny = new Bitmap(3, 2)) Assert(BlockDetector.Detect(tiny).Count == 0, "Tiny image safe fallback", log);
                TestWheelAndGestures(log, directory);
                string suffix = ".test." + Guid.NewGuid().ToString("N");
                using (var first = new AppInstance(suffix)) using (var second = new AppInstance(suffix)) using (var received = new System.Threading.ManualResetEvent(false)) {
                    string message = null; first.Listen(delegate(string value) { message = value; received.Set(); }); second.Send("中文图片.png");
                    Assert(first.IsPrimary && !second.IsPrimary && received.WaitOne(1500) && message == "中文图片.png", "Single resident instance forwards Unicode requests without another UI", log);
                }
                log.Add("ALL TESTS PASSED"); File.WriteAllLines(Path.Combine(directory, "results.txt"), log, new UTF8Encoding(false)); return 0;
            } catch (Exception ex) { log.Add("FAIL " + ex); File.WriteAllLines(Path.Combine(directory, "results.txt"), log, new UTF8Encoding(false)); return 1; }
        }
        private static void Assert(bool yes, string name, List<string> log) { if (!yes) throw new Exception(name); log.Add("PASS " + name); }
        private static void TestWheelAndGestures(List<string> log, string directory)
        {
            Func<int, int, int, int, string, Candidate> c = delegate(int l, int t, int r, int b, string source) { return new Candidate { bounds = new PixelRect(l, t, r, b), windowId = 7, target = new TargetInfo { source = source, label = "fixture", method = "fixture" } }; };
            var regions = new List<Candidate> { c(20,20,60,60,"vision"),c(20,20,60,60,"uia"),c(30,10,90,70,"vision"),c(10,10,90,90,"uia"),c(0,0,100,100,"uia") };
            var picker = new CandidatePicker(); picker.Update(regions,40,40);
            Assert(picker.Levels.Count == 3 && picker.Current.target.source == "uia", "Wheel hierarchy deduplicates bounds and excludes intersecting non-parents", log);
            Assert(picker.Step(1).bounds.Area() == 6400 && picker.Step(1).bounds.Area() == 10000 && picker.Step(1).bounds.Area() == 10000, "Wheel-up grows region and stops at largest ancestor", log);
            picker.Update(regions,41,40); Assert(picker.Current.bounds.Area() == 10000, "Small pointer jitter preserves chosen region", log);
            Assert(picker.Step(-1).bounds.Area() == 6400 && picker.Step(-1).bounds.Area() == 1600 && picker.Step(-1).bounds.Area() == 1600, "Wheel-down shrinks region and stops at smallest child", log);
            picker.Update(regions,80,80); Assert(picker.Current.bounds.Area() == 6400, "Moving to another target resets the hierarchy", log);
            var flags = System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance;
            using (var bitmap = DemoImage()) using (var overlay = new CaptureOverlay(bitmap,new Rectangle(0,0,1000,640),new List<NativeWindow>(),false)) {
                typeof(CaptureOverlay).GetField("windows",flags).SetValue(overlay,new List<NativeWindow>{new NativeWindow{id=7,title="Fixture",bounds=new PixelRect(0,0,1000,640)}});
                typeof(CaptureOverlay).GetField("elements",flags).SetValue(overlay,regions);
                typeof(CaptureOverlay).GetMethod("UpdateHover",flags).Invoke(overlay,new object[]{new Point(40,40)});
                Action<string,System.Windows.Forms.MouseButtons,int,int,int> mouse = delegate(string name,System.Windows.Forms.MouseButtons button,int x,int y,int delta){typeof(CaptureOverlay).GetMethod(name,flags).Invoke(overlay,new object[]{new System.Windows.Forms.MouseEventArgs(button,1,x,y,delta)});};
                mouse("OnMouseWheel",System.Windows.Forms.MouseButtons.None,40,40,120);
                var hover=(Rectangle)typeof(CaptureOverlay).GetField("hover",flags).GetValue(overlay); Assert(hover.Width==80,"Capture overlay handles wheel-up as larger element",log);
                mouse("OnMouseWheel",System.Windows.Forms.MouseButtons.None,40,40,-120);
                hover=(Rectangle)typeof(CaptureOverlay).GetField("hover",flags).GetValue(overlay); Assert(hover.Width==40,"Capture overlay handles wheel-down as smaller element",log);
                typeof(CaptureOverlay).GetField("selection",flags).SetValue(overlay,new Rectangle(20,20,40,40));
                mouse("OnMouseDown",System.Windows.Forms.MouseButtons.Left,60,40,0); mouse("OnMouseMove",System.Windows.Forms.MouseButtons.Left,80,40,0); mouse("OnMouseUp",System.Windows.Forms.MouseButtons.Left,80,40,0);
                var rect=(Rectangle)typeof(CaptureOverlay).GetField("selection",flags).GetValue(overlay); Assert(rect.Left==20 && rect.Right==80,"Capture selection edge drag resizes without moving opposite edge",log);
                mouse("OnMouseDown",System.Windows.Forms.MouseButtons.Left,35,35,0); mouse("OnMouseMove",System.Windows.Forms.MouseButtons.Left,45,50,0); mouse("OnMouseUp",System.Windows.Forms.MouseButtons.Left,45,50,0);
                rect=(Rectangle)typeof(CaptureOverlay).GetField("selection",flags).GetValue(overlay); Assert(rect.Left==30 && rect.Top==35 && rect.Width==60,"Capture selection interior drag moves without resizing",log);
                typeof(CaptureOverlay).GetField("selection",flags).SetValue(overlay,new Rectangle(42,240,280,320));
                using(var preview=new Bitmap(1000,640)) using(var g=Graphics.FromImage(preview)) { typeof(CaptureOverlay).GetMethod("OnPaint",flags).Invoke(overlay,new object[]{new System.Windows.Forms.PaintEventArgs(g,new Rectangle(0,0,1000,640))}); preview.Save(Path.Combine(directory,"capture-overlay.png"),ImageFormat.Png); }
            }
        }
        private static void Reject(Action action, string name, List<string> log) { try { action(); } catch (InvalidDataException) { log.Add("PASS " + name); return; } throw new Exception("Expected rejection: " + name); }
    }
}
