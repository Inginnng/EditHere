using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Web.Script.Serialization;
using System.Windows.Media.Imaging;

namespace Help2Design
{
    public sealed class PixelPoint
    {
        public int x;
        public int y;
        public PixelPoint() { }
        public PixelPoint(int px, int py) { x = px; y = py; }
    }

    // Rectangles use inclusive top-left and exclusive bottom-right image pixels.
    public sealed class PixelRect
    {
        public int x1, y1, x2, y2;
        public PixelRect() { }
        public PixelRect(int a, int b, int c, int d) { x1 = a; y1 = b; x2 = c; y2 = d; }
        public int Width() { return x2 - x1; }
        public int Height() { return y2 - y1; }
        public long Area() { return (long)Width() * Height(); }
        public bool Contains(int x, int y) { return x >= x1 && x < x2 && y >= y1 && y < y2; }
        public Rectangle DrawingRect() { return Rectangle.FromLTRB(x1, y1, x2, y2); }
        public PixelRect Clip(int w, int h) { return new PixelRect(Math.Max(0, Math.Min(w, x1)), Math.Max(0, Math.Min(h, y1)), Math.Max(0, Math.Min(w, x2)), Math.Max(0, Math.Min(h, y2))); }
        public static PixelRect FromPoints(int x, int y, int xx, int yy) { return new PixelRect(Math.Min(x, xx), Math.Min(y, yy), Math.Max(x, xx), Math.Max(y, yy)); }
    }

    public sealed class TargetInfo
    {
        public string source; // manual | uia | vision
        public string label;
        public string controlType;
        public string automationId;
        public string method;
        public PixelRect originalScreenBounds;
        public bool clipped;
    }

    public sealed class Annotation
    {
        public string id;
        public int number;
        public string kind;
        public PixelPoint point;
        public PixelRect rectangle;
        public string comment;
        public TargetInfo target;
        public string createdAt;
        public string updatedAt;
        public static Annotation Create(string kind, PixelPoint point, PixelRect rect, TargetInfo target)
        {
            return new Annotation { id = Guid.NewGuid().ToString("N"), kind = kind, point = point, rectangle = rect, comment = "", target = target ?? new TargetInfo { source = "manual", method = "user-selection", label = "手动标注" }, createdAt = Json.Now(), updatedAt = Json.Now() };
        }
    }

    public sealed class CaptureInfo
    {
        public string id;
        public string createdAt;
        public string source;
        public string title;
        public string imageFile;
        public int width;
        public int height;
        public string coordinateSpace = "image-pixels";
        public string origin = "top-left";
        public string rectangleConvention = "top-left-inclusive-bottom-right-exclusive";
        public PixelRect screenBounds;
        public string sha256;
        public string pngBase64;
    }

    public sealed class ExportDocument
    {
        public string schemaVersion = "1.0.0";
        public string tool = "Help2Design Capture";
        public string exportedAt;
        public CaptureInfo capture;
        public List<Annotation> annotations;
    }

    public sealed class Candidate
    {
        public PixelRect bounds;
        public TargetInfo target;
        public long windowId;
    }

    public sealed class CaptureDocument
    {
        public CaptureInfo Info;
        public byte[] Png;
        public BitmapSource Image;
        public List<Annotation> Notes = new List<Annotation>();
        public List<Candidate> Candidates = new List<Candidate>();
        public bool Dirty;
        public bool CopyOnly;

        public static CaptureDocument FromBitmap(Bitmap bitmap, string source, string title, PixelRect screenBounds)
        {
            byte[] bytes;
            using (var stream = new MemoryStream()) { bitmap.Save(stream, ImageFormat.Png); bytes = stream.ToArray(); }
            return FromPng(bytes, source, title, screenBounds);
        }

        public static CaptureDocument FromPng(byte[] png, string source, string title, PixelRect screenBounds)
        {
            if (png == null || png.Length < 8 || !png.Take(8).SequenceEqual(new byte[] { 137, 80, 78, 71, 13, 10, 26, 10 })) throw new InvalidDataException("原图数据必须是 PNG 格式。");
            var image = new BitmapImage();
            using (var stream = new MemoryStream(png)) { image.BeginInit(); image.CacheOption = BitmapCacheOption.OnLoad; image.StreamSource = stream; image.EndInit(); }
            image.Freeze();
            if (image.PixelWidth < 1 || image.PixelHeight < 1 || (long)image.PixelWidth * image.PixelHeight > 100000000) throw new InvalidDataException("图片太大，请使用不超过一亿像素的图片。");
            var id = Guid.NewGuid().ToString("N");
            return new CaptureDocument { Png = png, Image = image, Info = new CaptureInfo { id = id, createdAt = Json.Now(), source = source, title = title ?? "截图", imageFile = "capture-" + id.Substring(0, 8) + ".png", width = image.PixelWidth, height = image.PixelHeight, screenBounds = screenBounds, sha256 = Hash(png) } };
        }

        public static CaptureDocument LoadImage(string path)
        {
            using (var bitmap = new Bitmap(path)) return FromBitmap(bitmap, "file", Path.GetFileName(path), null);
        }
        public static string Hash(byte[] bytes) { using (var sha = SHA256.Create()) return BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-", "").ToLowerInvariant(); }
        public void Renumber() { for (int i = 0; i < Notes.Count; i++) Notes[i].number = i + 1; }
        public ExportDocument Export(bool embed)
        {
            Renumber();
            var copy = Json.Clone(Info);
            copy.pngBase64 = embed ? Convert.ToBase64String(Png) : null;
            var result = new ExportDocument { exportedAt = Json.Now(), capture = copy, annotations = Json.Clone(Notes) };
            Json.Validate(result);
            return result;
        }
        public void Save(string path)
        {
            string data = Json.Serialize(Export(true));
            // Atomic replacement: a failed save must not destroy an earlier review.
            string temp = path + ".tmp-" + Guid.NewGuid().ToString("N");
            try {
                File.WriteAllText(temp, data, new UTF8Encoding(false));
                if (File.Exists(path)) File.Replace(temp, path, null); else File.Move(temp, path);
            } finally { if (File.Exists(temp)) File.Delete(temp); }
            Dirty = false;
        }
        public static CaptureDocument Load(string path)
        {
            var raw = File.ReadAllText(path, Encoding.UTF8);
            var export = Json.ReadDocument(raw);
            byte[] bytes;
            if (!String.IsNullOrEmpty(export.capture.pngBase64)) bytes = Convert.FromBase64String(export.capture.pngBase64);
            else {
                // Only sibling image filenames are accepted from imported review JSON.
                var imageName = export.capture.imageFile;
                if (String.IsNullOrWhiteSpace(imageName) || Path.GetFileName(imageName) != imageName || imageName.Contains(":")) throw new InvalidDataException("JSON 中的图片文件名无效。");
                var imagePath = Path.Combine(Path.GetDirectoryName(Path.GetFullPath(path)), imageName);
                if (!File.Exists(imagePath)) throw new InvalidDataException("缺少原图 " + imageName + "。请把原图与 JSON 放在同一文件夹，或使用包含原图的项目文件。");
                bytes = File.ReadAllBytes(imagePath);
            }
            if (Hash(bytes) != export.capture.sha256) throw new InvalidDataException("原图校验不一致，无法保证批注坐标正确。");
            var doc = FromPng(bytes, export.capture.source, export.capture.title, export.capture.screenBounds);
            if (doc.Info.width != export.capture.width || doc.Info.height != export.capture.height) throw new InvalidDataException("图片尺寸与批注不一致。");
            doc.Info = export.capture;
            doc.Info.pngBase64 = null;
            doc.Notes = export.annotations;
            return doc;
        }
    }

    public static class Json
    {
        public static string Now() { return DateTime.UtcNow.ToString("yyyy-MM-dd'T'HH:mm:ss.fff'Z'"); }
        public static JavaScriptSerializer Serializer() { return new JavaScriptSerializer { MaxJsonLength = 268435456, RecursionLimit = 100 }; }
        public static T Clone<T>(T value) { return Serializer().Deserialize<T>(Serializer().Serialize(value)); }
        public static string Serialize(object value) { return Pretty(Serializer().Serialize(value)); }
        public static string Pretty(string raw)
        {
            var s = new StringBuilder(); bool quote = false, escape = false; int indent = 0;
            foreach (char c in raw) {
                if (quote) { s.Append(c); if (escape) escape = false; else if (c == '\\') escape = true; else if (c == '"') quote = false; continue; }
                if (c == '"') { quote = true; s.Append(c); }
                else if (c == '{' || c == '[') { s.Append(c); s.Append('\n'); indent++; s.Append(' ', indent * 2); }
                else if (c == '}' || c == ']') { s.Append('\n'); indent--; s.Append(' ', indent * 2); s.Append(c); }
                else if (c == ',') { s.Append(",\n"); s.Append(' ', indent * 2); }
                else if (c == ':') s.Append(": ");
                else if (!Char.IsWhiteSpace(c)) s.Append(c);
            }
            return s.ToString();
        }
        public static ExportDocument ReadDocument(string raw)
        {
            // Reject unknown keys, fractional coordinates and implicit serializer coercions.
            var root = Serializer().DeserializeObject(raw) as Dictionary<string, object>;
            Keys(root, "schemaVersion", "tool", "exportedAt", "capture", "annotations");
            Strings(root, false, "schemaVersion", "tool", "exportedAt");
            var cap = root["capture"] as Dictionary<string, object>;
            Keys(cap, "id", "createdAt", "source", "title", "imageFile", "width", "height", "coordinateSpace", "origin", "rectangleConvention", "screenBounds", "sha256", "pngBase64");
            Integer(cap, "width"); Integer(cap, "height");
            Strings(cap, false, "id", "createdAt", "source", "title", "imageFile", "coordinateSpace", "origin", "rectangleConvention", "sha256");
            Strings(cap, true, "pngBase64");
            CheckRect(cap["screenBounds"], true);
            var items = root["annotations"] as object[];
            if (items == null) throw new InvalidDataException("annotations 必须为数组。");
            foreach (var item in items) {
                var ann = item as Dictionary<string, object>;
                Keys(ann, "id", "number", "kind", "point", "rectangle", "comment", "target", "createdAt", "updatedAt"); Integer(ann, "number");
                Strings(ann, false, "id", "kind", "comment", "createdAt", "updatedAt");
                if (ann["point"] != null) { var p = ann["point"] as Dictionary<string, object>; Keys(p, "x", "y"); Integer(p, "x"); Integer(p, "y"); }
                CheckRect(ann["rectangle"], true);
                var target = ann["target"] as Dictionary<string, object>;
                Keys(target, "source", "label", "controlType", "automationId", "method", "originalScreenBounds", "clipped");
                Strings(target, false, "source", "label", "method"); Strings(target, true, "controlType", "automationId");
                if (!(target["clipped"] is bool)) throw new InvalidDataException("clipped 必须为布尔值。");
                CheckRect(target["originalScreenBounds"], true);
            }
            var result = Serializer().Deserialize<ExportDocument>(raw); Validate(result); return result;
        }
        private static void Keys(Dictionary<string, object> o, params string[] keys)
        {
            if (o == null || o.Count != keys.Length || keys.Any(k => !o.ContainsKey(k))) throw new InvalidDataException("JSON 字段不符合 Help2Design 1.0.0 标准。");
        }
        private static void Integer(Dictionary<string, object> o, string key) { if (!(o[key] is int)) throw new InvalidDataException(key + " 必须是整数像素。"); }
        private static void Strings(Dictionary<string, object> o, bool nullable, params string[] keys) { foreach (string key in keys) if (!(o[key] is string) && !(nullable && o[key] == null)) throw new InvalidDataException(key + " 必须是文本。"); }
        private static void CheckRect(object r, bool nullable) { if (r == null && nullable) return; var o = r as Dictionary<string, object>; Keys(o, "x1", "y1", "x2", "y2"); foreach (var k in o.Keys) Integer(o, k); }
        public static void Validate(ExportDocument doc)
        {
            if (doc == null || doc.schemaVersion != "1.0.0" || doc.tool != "Help2Design Capture" || doc.capture == null || doc.annotations == null) throw new InvalidDataException("不支持的批注文件格式。");
            var c = doc.capture;
            if (c.width < 1 || c.height < 1 || (long)c.width * c.height > 100000000 || c.coordinateSpace != "image-pixels" || c.origin != "top-left" || c.rectangleConvention != "top-left-inclusive-bottom-right-exclusive") throw new InvalidDataException("无效的图片尺寸或坐标约定。");
            if (!new[] { "screen", "file", "clipboard", "demo" }.Contains(c.source) || String.IsNullOrWhiteSpace(c.id) || String.IsNullOrWhiteSpace(c.imageFile) || c.title == null || c.sha256 == null || !System.Text.RegularExpressions.Regex.IsMatch(c.sha256, "^[a-f0-9]{64}$")) throw new InvalidDataException("无效的截图元数据。");
            if (Path.GetFileName(c.imageFile) != c.imageFile || c.imageFile.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || !Path.GetExtension(c.imageFile).Equals(".png", StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("原图必须是同文件夹中的 PNG 文件名。");
            Date(c.createdAt); Date(doc.exportedAt);
            if (doc.annotations.Count > 1000) throw new InvalidDataException("最多支持 1000 条批注。");
            var ids = new HashSet<string>();
            for (int i = 0; i < doc.annotations.Count; i++) {
                var n = doc.annotations[i];
                if (n == null || String.IsNullOrWhiteSpace(n.id) || !ids.Add(n.id) || n.number != i + 1 || String.IsNullOrWhiteSpace(n.comment) || n.comment.Length > 10000) throw new InvalidDataException("批注编号、ID 或文本无效。");
                Date(n.createdAt); Date(n.updatedAt);
                if (n.kind == "point") { if (n.point == null || n.rectangle != null || n.point.x < 0 || n.point.y < 0 || n.point.x >= c.width || n.point.y >= c.height) throw new InvalidDataException("点标注坐标超出图片范围。"); }
                else if (n.kind == "rectangle") { if (n.rectangle == null || n.point != null) throw new InvalidDataException("矩形批注格式错误。"); ValidateRect(n.rectangle, c.width, c.height); }
                else throw new InvalidDataException("未知批注类型。");
                if (n.target == null || !new[] { "manual", "uia", "vision" }.Contains(n.target.source) || n.target.label == null || n.target.method == null) throw new InvalidDataException("未知元素来源。");
            }
        }
        private static void Date(string value) { DateTimeOffset date; if (value == null || !System.Text.RegularExpressions.Regex.IsMatch(value, @"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d+)?(Z|[+-]\d{2}:\d{2})$") || !DateTimeOffset.TryParse(value, System.Globalization.CultureInfo.InvariantCulture, System.Globalization.DateTimeStyles.None, out date)) throw new InvalidDataException("时间格式无效。"); }
        public static void ValidateRect(PixelRect r, int w, int h) { if (r.x1 < 0 || r.y1 < 0 || r.x2 > w || r.y2 > h || r.x2 <= r.x1 || r.y2 <= r.y1) throw new InvalidDataException("矩形边界超出图片或面积为空。"); }
    }
}
