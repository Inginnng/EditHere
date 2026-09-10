using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Data;
using System.Windows.Shell;

namespace Help2Design
{
    public static class Ui
    {
        public static void FloatingWindow(Window window, bool resize)
        {
            window.WindowStyle = WindowStyle.None; window.ResizeMode = resize ? ResizeMode.CanResize : ResizeMode.NoResize;
            WindowChrome.SetWindowChrome(window, new WindowChrome { CaptionHeight = 0, CornerRadius = new CornerRadius(16), GlassFrameThickness = new Thickness(0), ResizeBorderThickness = new Thickness(resize ? 5 : 0), UseAeroCaptionButtons = false });
        }
        public static bool FromButton(DependencyObject source) { while (source != null) { if (source is System.Windows.Controls.Primitives.ButtonBase) return true; source = source is Visual ? VisualTreeHelper.GetParent(source) : LogicalTreeHelper.GetParent(source); } return false; }
        public static Border Separator() { return new Border { Width = 1, Height = 18, Background = Brush("#DDDDDF"), Margin = new Thickness(7, 0, 7, 0), VerticalAlignment = VerticalAlignment.Center }; }
        public static Button IconButton(string icon, string tooltip, Action action)
        {
            string data;
            switch (icon) {
                case "point": data="M9,1 A8,8 0 1 1 8.99,1 M9,6 A3,3 0 1 1 8.99,6"; break;
                case "box": data="M3,2 L15,2 Q16,2 16,3 L16,15 Q16,16 15,16 L3,16 Q2,16 2,15 L2,3 Q2,2 3,2"; break;
                case "smart": data="M6,2 L2,2 2,6 M12,2 L16,2 16,6 M2,12 L2,16 6,16 M12,16 L16,16 16,12 M9,5 L9,13 M5,9 L13,9"; break;
                case "move": data="M3,1 L15,11 9,12 6,17 Z"; break;
                case "undo": data="M7,3 L2,7 7,11 M3,7 L11,7 C18,7 18,16 11,16"; break;
                case "redo": data="M11,3 L16,7 11,11 M15,7 L7,7 C0,7 0,16 7,16"; break;
                case "copy": data="M6,6 L16,6 16,16 6,16 Z M12,3 L12,2 2,2 2,12 3,12"; break;
                case "sidebar": data="M3,2 L15,2 Q16,2 16,3 L16,15 Q16,16 15,16 L3,16 Q2,16 2,15 L2,3 Q2,2 3,2 M11,2 L11,16"; break;
                case "pin": data="M6,2 L12,2 M7,2 L7,7 4,10 14,10 11,7 11,2 M9,10 L9,17"; break;
                case "close": data="M5,5 L13,13 M13,5 L5,13"; break;
                default: data="M3,8 L3,10 M9,8 L9,10 M15,8 L15,10"; break;
            }
            var path = new System.Windows.Shapes.Path { Data = Geometry.Parse(data), Width = 18, Height = 18, StrokeThickness = 1.5, StrokeStartLineCap = PenLineCap.Round, StrokeEndLineCap = PenLineCap.Round, StrokeLineJoin = PenLineJoin.Round, Stretch = Stretch.None };
            path.SetBinding(System.Windows.Shapes.Shape.StrokeProperty, new Binding("Foreground") { RelativeSource = new RelativeSource(RelativeSourceMode.FindAncestor, typeof(Button), 1) });
            var button = new Button { Content = path, Width = 34, Height = 34, Padding = new Thickness(7), Margin = new Thickness(1, 0, 1, 0), ToolTip = tooltip, Background = Brushes.Transparent, BorderThickness = new Thickness(0), VerticalAlignment = VerticalAlignment.Center };
            System.Windows.Automation.AutomationProperties.SetName(button, tooltip); button.Click += delegate { action(); }; return button;
        }
        public static SolidColorBrush Brush(string color) { var b = (SolidColorBrush)new BrushConverter().ConvertFromString(color); b.Freeze(); return b; }
        public static TextBlock Text(string text, double size, string color) { return new TextBlock { Text = text, FontSize = size, Foreground = Brush(color), TextWrapping = TextWrapping.Wrap }; }
        public static Button Button(string text, Action action, bool primary)
        {
            var b = new Button { Content = text, VerticalAlignment = VerticalAlignment.Center };
            if (primary) b.SetResourceReference(FrameworkElement.StyleProperty, "Primary");
            b.Click += delegate { action(); }; return b;
        }
        public static Border Card(UIElement child, Thickness padding)
        {
            return new Border { Child = child, Padding = padding, CornerRadius = new CornerRadius(12), Background = Brushes.White, BorderBrush = Brush("#E5E5EA"), BorderThickness = new Thickness(1) };
        }
        public static string Coordinates(Annotation n)
        {
            return n.kind == "point" ? "点  (" + n.point.x + ", " + n.point.y + ")" : "框  (" + n.rectangle.x1 + ", " + n.rectangle.y1 + ") → (" + n.rectangle.x2 + ", " + n.rectangle.y2 + ")";
        }
        public static void Error(Window owner, Exception e) { Program.Log(e); if (owner == null) MessageBox.Show(e.Message, "操作未完成", MessageBoxButton.OK, MessageBoxImage.Warning); else MessageBox.Show(owner, e.Message, "操作未完成", MessageBoxButton.OK, MessageBoxImage.Warning); }
    }
}
