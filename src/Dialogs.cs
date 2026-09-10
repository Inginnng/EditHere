using System;
using System.IO;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using Microsoft.Win32;

namespace Help2Design
{
    public sealed class NoteDialog : Window
    {
        public string Comment;
        public NoteDialog(Annotation annotation, int number)
        {
            SetResourceReference(StyleProperty, typeof(Window));
            Title = "批注 " + number + " · Help2Design"; Width = 360; Height = 284; ShowInTaskbar = false; WindowStartupLocation = WindowStartupLocation.CenterOwner;
            Ui.FloatingWindow(this, false);
            var root = new Grid { Margin = new Thickness(20) };
            root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto }); root.RowDefinitions.Add(new RowDefinition()); root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            var head = new StackPanel(); head.Children.Add(new TextBlock { Text = "批注 " + number, FontSize = 17, FontWeight = FontWeights.SemiBold });
            head.Children.Add(new TextBlock { Text = Ui.Coordinates(annotation), Foreground = Ui.Brush("#8E8E93"), FontSize = 11, Margin = new Thickness(0, 6, 0, 12) }); root.Children.Add(head);
            var box = new TextBox { Text = annotation.comment, AcceptsReturn = true, TextWrapping = TextWrapping.Wrap, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, MaxLength = 10000, FontSize = 14, Margin = new Thickness(0, 0, 0, 16) };
            System.Windows.Automation.AutomationProperties.SetName(box, "批注描述");
            Grid.SetRow(box, 1); root.Children.Add(box);
            var foot = new DockPanel();
            var hint = Ui.Text("Ctrl+Enter 保存", 11, "#879089"); hint.VerticalAlignment = VerticalAlignment.Center; DockPanel.SetDock(hint, Dock.Left); foot.Children.Add(hint);
            var actions = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right };
            var cancel = Ui.Button("取消", delegate { DialogResult = false; }, false); cancel.IsCancel = true; actions.Children.Add(cancel);
            Action save = delegate { if (String.IsNullOrWhiteSpace(box.Text)) { hint.Text = "请先写下修改意见"; box.Focus(); return; } Comment = box.Text.Trim(); DialogResult = true; };
            actions.Children.Add(Ui.Button("保存批注", save, true)); foot.Children.Add(actions); Grid.SetRow(foot, 2); root.Children.Add(foot); Content = new Border { Child = root, Background = Background, CornerRadius = new CornerRadius(16), BorderBrush = Ui.Brush("#DDDDDF"), BorderThickness = new Thickness(1) };
            Loaded += delegate { box.Focus(); box.CaretIndex = box.Text.Length; };
            PreviewKeyDown += delegate(object s, KeyEventArgs e) { if (e.Key == Key.Enter && Keyboard.Modifiers == ModifierKeys.Control) { save(); e.Handled = true; } };
        }
    }
    public sealed class ExportDialog : Window
    {
        private CaptureDocument document;
        private TextBox json;
        private CheckBox embedded;
        private TextBlock message;
        public ExportDialog(CaptureDocument doc)
        {
            SetResourceReference(StyleProperty, typeof(Window));
            document = doc;
            Title = "导出批注 JSON · Help2Design"; Width = 720; Height = 610; MinWidth = 620; MinHeight = 460; ShowInTaskbar = false; WindowStartupLocation = WindowStartupLocation.CenterOwner;
            Ui.FloatingWindow(this, true);
            var root = new Grid { Margin = new Thickness(24) }; root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto }); root.RowDefinitions.Add(new RowDefinition()); root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            var head = new StackPanel(); head.Children.Add(new TextBlock { Text = "导出批注", FontSize = 21, FontWeight = FontWeights.SemiBold });
            head.Children.Add(new TextBlock { Text = "JSON v1.0.0  ·  " + doc.Notes.Count + " 条批注  ·  " + doc.Info.width + " × " + doc.Info.height + " 原图像素", FontSize = 12, Foreground = Ui.Brush("#77837E"), Margin = new Thickness(0, 8, 0, 12) });
            embedded = new CheckBox { Content = "包含原图数据（文件更大，可独立导入还原）", Margin = new Thickness(0, 0, 0, 14) }; head.Children.Add(embedded); root.Children.Add(head);
            json = new TextBox { IsReadOnly = true, AcceptsReturn = true, AcceptsTab = true, TextWrapping = TextWrapping.NoWrap, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, HorizontalScrollBarVisibility = ScrollBarVisibility.Auto, FontFamily = new FontFamily("Consolas, Microsoft YaHei UI"), FontSize = 12, Background = Ui.Brush("#F0F0F4"), Foreground = Ui.Brush("#45454A"), Padding = new Thickness(14), BorderThickness = new Thickness(0) };
            System.Windows.Automation.AutomationProperties.SetName(json, "标准化 JSON"); Grid.SetRow(json, 1); root.Children.Add(json);
            var foot = new StackPanel { Margin = new Thickness(0, 14, 0, 0) };
            message = Ui.Text("将 JSON 和原图一起交给 AI，即可准确定位每条修改意见。", 11, "#8E8E93"); foot.Children.Add(message);
            var buttons = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 15, 0, 0) };
            buttons.Children.Add(Ui.Button("保存 JSON 与图片", SaveBundle, false)); buttons.Children.Add(Ui.Button("复制 JSON", Copy, true));
            var close = Ui.Button("关闭", delegate { Close(); }, false); close.IsCancel = true; buttons.Children.Add(close); foot.Children.Add(buttons); Grid.SetRow(foot, 2); root.Children.Add(foot); Content = new Border { Child = root, Background = Background, CornerRadius = new CornerRadius(16), BorderBrush = Ui.Brush("#DDDDDF"), BorderThickness = new Thickness(1) };
            embedded.Checked += delegate { Refresh(); }; embedded.Unchecked += delegate { Refresh(); }; Refresh();
        }
        private void Refresh() { json.Text = Json.Serialize(document.Export(embedded.IsChecked == true)); }
        private void Copy()
        {
            try { System.Windows.Forms.Clipboard.SetDataObject(json.Text, true, 5, 80); message.Text = "已复制 " + document.Notes.Count + " 条批注的标准 JSON。"; } catch (Exception ex) { Ui.Error(this, ex); }
        }
        private void SaveBundle()
        {
            var dialog = new SaveFileDialog { FileName = "feedback-" + DateTime.Now.ToString("yyyyMMdd-HHmmss") + ".json", Filter = "标准批注 JSON|*.json", DefaultExt = ".json" };
            if (dialog.ShowDialog(this) != true) return;
            try {
                string directory = Path.GetDirectoryName(dialog.FileName);
                string source = Path.Combine(directory, document.Info.imageFile);
                if (File.Exists(source) && CaptureDocument.Hash(File.ReadAllBytes(source)) != document.Info.sha256) throw new IOException("同名原图内容不同，请选择另一个文件夹。");
                File.WriteAllBytes(source, document.Png);
                string preview = Path.Combine(directory, Path.GetFileNameWithoutExtension(dialog.FileName) + "-preview-" + Guid.NewGuid().ToString("N").Substring(0, 6) + ".png");
                PreviewRenderer.Save(document, preview);
                File.WriteAllText(dialog.FileName, json.Text, new UTF8Encoding(false));
                message.Text = "已保存 JSON、原图和带批注的预览图。";
            } catch (Exception ex) { Ui.Error(this, ex); }
        }
    }
}
