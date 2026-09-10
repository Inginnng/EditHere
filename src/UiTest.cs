using System;
using System.IO;
using System.Reflection;
using System.Linq;
using System.Collections.Generic;
using System.Text;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace Help2Design
{
    // In-process smoke test for this app's own UI; does not automate other desktop apps.
    public static class UiTest
    {
        public static int ExitCode = 1;
        public static void Startup(Application app, MainWindow window, string directory)
        {
            Directory.CreateDirectory(directory); window.TestMode = true; app.MainWindow = window;
            bool captured = false, visibleDuringCapture = false, shownBeforeCapture = false;
            window.IsVisibleChanged += delegate { if (window.IsVisible && !captured) shownBeforeCapture = true; };
            window.CaptureProvider = delegate(string mode) { visibleDuringCapture = window.IsVisible; captured = true; return null; };
            window.BeginStartup();
            var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(700) }; int stage = 0;
            timer.Tick += delegate {
                try {
                    if (stage == 0) {
                        if (!captured || visibleDuringCapture || shownBeforeCapture || window.IsVisible) throw new Exception("Startup showed an editor before capture, or cancellation revealed a home page.");
                        window.CaptureProvider = delegate(string mode) { using (var bitmap = SelfTest.DemoImage()) return CaptureDocument.FromBitmap(bitmap, "demo", "Startup fixture", null); };
                        window.StartCapture("region"); stage++;
                    } else if (stage == 1) {
                        if (!window.IsVisible) throw new Exception("Successful capture did not open a pin.");
                        var doc = (CaptureDocument)typeof(MainWindow).GetField("document", BindingFlags.NonPublic | BindingFlags.Instance).GetValue(window);
                        var note = Annotation.Create("point", new PixelPoint(50, 50), null, null); note.comment = "Preserve this note"; doc.Notes.Add(note); doc.Renumber();
                        window.Hide(); typeof(MainWindow).GetMethod("ResumePin", BindingFlags.NonPublic | BindingFlags.Instance).Invoke(window, null);
                        if (!window.IsVisible || doc.Notes.Count != 1) throw new Exception("Hide/resume lost notes.");
                        // Mouse-only close must hide, not destroy the resident app or its current document.
                        window.TestMode = false; window.Close(); window.TestMode = true;
                        if (window.IsVisible || doc.Notes.Count != 1) throw new Exception("Close did not preserve the resident pin.");
                        timer.Stop(); ExitCode = 0; File.WriteAllText(Path.Combine(directory, "startup-results.txt"), "PASS Direct capture with no visible home page.\nPASS Cancel stays hidden.\nPASS Capture opens a pin; hiding/closing and resuming preserve annotations."); app.Shutdown();
                    }
                } catch (Exception e) { timer.Stop(); File.WriteAllText(Path.Combine(directory, "startup-results.txt"), "FAIL " + e); app.Shutdown(); }
            }; timer.Start();
        }
        public static void Start(Application app, MainWindow window, string directory)
        {
            Directory.CreateDirectory(directory);
            File.WriteAllText(Path.Combine(directory, "ui-progress.txt"), "MainWindow constructed", Encoding.UTF8);
            window.InitialFile = "--demo";
            window.TestMode = true;
            var timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
            UiaProbe probe = null;
            timer.Tick += delegate {
                timer.Stop();
                try {
                    window.UpdateLayout(); Render(window, Path.Combine(directory, "workbench.png"));
                    var f = typeof(MainWindow).GetField("document", BindingFlags.NonPublic | BindingFlags.Instance);
                    var doc = (CaptureDocument)f.GetValue(window);
                    if (doc == null) throw new Exception("Demo did not load");
                    var log = new List<string>();
                    var elements = probe.Finish(); probe.Dispose(); probe = null;
                    if (!elements.Any(c => c.target.controlType == "ControlType.Button" && c.bounds.Width() > 0)) throw new Exception("UIA worker did not return fixture button boundaries");
                    File.WriteAllText(Path.Combine(directory, "own-window-elements.json"), Json.Serialize(elements), Encoding.UTF8);
                    log.Add("PASS UIA worker retrieves this app's own live control bounds (not a browser compatibility test).");
                    var note = Annotation.Create("rectangle", null, new PixelRect(42, 240, 322, 560), null);
                    var inputTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(150) };
                    inputTimer.Tick += delegate {
                        inputTimer.Stop();
                        var editor = window.OwnedWindows.OfType<NoteDialog>().Single();
                        Children(editor).OfType<System.Windows.Controls.TextBox>().Single().Text = "卡片内边距加大到 24px，保持三列布局。";
                        Children(editor).OfType<System.Windows.Controls.Button>().Single(b => b.Content as string == "保存批注").RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Button.ClickEvent));
                    };
                    inputTimer.Start();
                    typeof(MainWindow).GetMethod("EditNote", BindingFlags.NonPublic | BindingFlags.Instance).Invoke(window, new object[] { note, true });
                    if (doc.Notes.Count != 1 || !doc.Notes[0].comment.Contains("24px")) throw new Exception("Annotation dialog save failed");
                    log.Add("PASS Annotation dialog saves description into the document and sidebar.");
                    var canvas = (AnnotationCanvas)typeof(MainWindow).GetField("canvas", BindingFlags.NonPublic | BindingFlags.Instance).GetValue(window);
                    var savedCandidates = doc.Candidates;
                    doc.Candidates = new List<Candidate> {
                        new Candidate { bounds = new PixelRect(100,280,180,350), target = new TargetInfo { source = "vision", label = "inner", method = "fixture" } },
                        new Candidate { bounds = new PixelRect(42,240,322,560), target = new TargetInfo { source = "vision", label = "card", method = "fixture" } }
                    };
                    double savedZoom = canvas.Zoom; canvas.Zoom = .5; canvas.SetTool(CanvasTool.Smart);
                    canvas.StepCandidate(new Point(130,300), 1);
                    var chosen = (Candidate)typeof(AnnotationCanvas).GetField("hovered", BindingFlags.NonPublic | BindingFlags.Instance).GetValue(canvas);
                    if (chosen.bounds.x1 != 42 || chosen.bounds.x2 != 322) throw new Exception("Pin wheel did not grow the original-pixel region.");
                    canvas.StepCandidate(new Point(130,300), -1);
                    chosen = (Candidate)typeof(AnnotationCanvas).GetField("hovered", BindingFlags.NonPublic | BindingFlags.Instance).GetValue(canvas);
                    if (chosen.bounds.x1 != 100 || chosen.bounds.x2 != 180) throw new Exception("Pin wheel did not shrink the original-pixel region.");
                    canvas.StepCandidate(new Point(130,300), 1);
                    Annotation created = null; var editHandler = canvas.EditRequested; canvas.EditRequested = delegate(Annotation n, bool isNew) { created = n; };
                    var canvasFlags = BindingFlags.NonPublic | BindingFlags.Instance;
                    typeof(AnnotationCanvas).GetField("drawing", canvasFlags).SetValue(canvas, true);
                    typeof(AnnotationCanvas).GetField("down", canvasFlags).SetValue(canvas, new Point(130,300));
                    typeof(AnnotationCanvas).GetField("current", canvasFlags).SetValue(canvas, new Point(130,300));
                    typeof(AnnotationCanvas).GetMethod("OnMouseUp", canvasFlags).Invoke(canvas, new object[] { new System.Windows.Input.MouseButtonEventArgs(System.Windows.Input.Mouse.PrimaryDevice,0,System.Windows.Input.MouseButton.Left) });
                    if (created == null || created.rectangle.x1 != 42 || created.rectangle.x2 != 322 || created.target.label != "card") throw new Exception("Annotation did not retain the wheel-selected candidate.");
                    canvas.EditRequested = editHandler; doc.Candidates = savedCandidates; canvas.Zoom = savedZoom; canvas.SetTool(CanvasTool.Smart);
                    log.Add("PASS Pin wheel chooses larger/smaller blocks and annotation retains original pixels at 50% zoom.");
                    canvas.Selected = note; canvas.InvalidateVisual();
                    window.UpdateLayout(); Render(window, Path.Combine(directory, "workbench-annotated.png"));
                    var export = new ExportDialog(doc) { Owner = window }; export.Show(); export.UpdateLayout(); Render(export, Path.Combine(directory, "export-dialog.png")); export.Close();
                    var dialog = new NoteDialog(note, 1) { Owner = window }; dialog.Show(); dialog.UpdateLayout(); Render(dialog, Path.Combine(directory, "note-dialog.png")); dialog.Close();
                    Children(window).OfType<System.Windows.Controls.Button>().Single(b => b.Content as string == "×").RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Button.ClickEvent));
                    if (doc.Notes.Count != 0) throw new Exception("Delete action failed");
                    typeof(MainWindow).GetMethod("Undo", BindingFlags.NonPublic | BindingFlags.Instance).Invoke(window, null);
                    if (doc.Notes.Count != 1 || doc.Notes[0].comment != note.comment) throw new Exception("Undo failed");
                    typeof(MainWindow).GetMethod("Redo", BindingFlags.NonPublic | BindingFlags.Instance).Invoke(window, null);
                    if (doc.Notes.Count != 0) throw new Exception("Redo failed");
                    log.Add("PASS Sidebar delete, undo and redo preserve annotation state.");
                    doc.Dirty = false;
                    log.Add("PASS Workbench, export dialog and note dialog rendered.");
                    ExitCode = 0; File.WriteAllLines(Path.Combine(directory, "ui-results.txt"), log, Encoding.UTF8);
                } catch (Exception e) { File.WriteAllText(Path.Combine(directory, "ui-results.txt"), "FAIL " + e, Encoding.UTF8); }
                finally { if (probe != null) probe.Dispose(); app.Shutdown(); }
            };
            window.Loaded += delegate {
                File.AppendAllText(Path.Combine(directory, "ui-progress.txt"), "\nLoaded", Encoding.UTF8);
                probe = new UiaProbe(new List<NativeWindow> { new NativeWindow { id = new System.Windows.Interop.WindowInteropHelper(window).Handle.ToInt64(), title = window.Title, bounds = new PixelRect(0, 0, 2000, 2000) } });
                timer.Start();
            };
        }
        private static IEnumerable<DependencyObject> Children(DependencyObject parent) { for (int i = 0; i < VisualTreeHelper.GetChildrenCount(parent); i++) { var child = VisualTreeHelper.GetChild(parent, i); yield return child; foreach (var nested in Children(child)) yield return nested; } }
        private static void Render(Window window, string path)
        {
            window.UpdateLayout();
            var image = new RenderTargetBitmap((int)Math.Ceiling(window.ActualWidth), (int)Math.Ceiling(window.ActualHeight), 96, 96, PixelFormats.Pbgra32);
            image.Render(window); var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(image));
            using (var file = File.Create(path)) encoder.Save(file);
        }
    }
}
