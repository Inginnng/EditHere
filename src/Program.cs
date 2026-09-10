using System;
using System.IO;
using System.Reflection;
using System.Threading;
using System.Windows;
using System.Windows.Markup;

[assembly: AssemblyTitle("Help2Design Capture")]
[assembly: AssemblyDescription("截图、元素标注与标准化设计反馈")]
[assembly: AssemblyVersion("0.2.0.0")]

namespace Help2Design
{
    internal static class Program
    {
        [STAThread]
        public static int Main(string[] args)
        {
            try { Native.SetProcessDpiAwarenessContext(new IntPtr(-4)); } catch { }
            if (args.Length >= 2 && args[0] == "--probe") {
                try { var thread = new Thread(delegate() { try { UiaProbe.Run(args[1]); } catch { } }); thread.SetApartmentState(ApartmentState.MTA); thread.Start(); thread.Join(2900); return 0; } catch { return 1; }
            }
            if (args.Length >= 2 && args[0] == "--self-test") return SelfTest.Run(args[1]);
            try {
                var app = new Application { ShutdownMode = ShutdownMode.OnExplicitShutdown };
                using (var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("Theme.xaml")) app.Resources.MergedDictionaries.Add((ResourceDictionary)XamlReader.Load(stream));
                app.DispatcherUnhandledException += delegate(object sender, System.Windows.Threading.DispatcherUnhandledExceptionEventArgs e) {
                    Log(e.Exception); MessageBox.Show("操作未完成：" + e.Exception.Message, "Help2Design", MessageBoxButton.OK, MessageBoxImage.Warning); e.Handled = true;
                };
                var main = new MainWindow();
                if (args.Length >= 2 && args[0] == "--ui-test") { UiTest.Start(app, main, args[1]); app.Run(main); return UiTest.ExitCode; }
                if (args.Length >= 2 && args[0] == "--startup-test") { UiTest.Startup(app, main, args[1]); app.Run(); return UiTest.ExitCode; }
                if (args.Length > 0) main.InitialFile = args[0];
                using (var instance = new AppInstance()) {
                    if (!instance.IsPrimary) { instance.Send(args.Length > 0 ? (args[0] == "--demo" ? "--demo" : Path.GetFullPath(args[0])) : ""); return 0; }
                    instance.Listen(argument => app.Dispatcher.BeginInvoke(new Action(delegate { main.ReceiveRequest(argument); })));
                    app.MainWindow = main;
                    main.BeginStartup();
                    app.Run(); return 0;
                }
            } catch (Exception e) { Log(e); MessageBox.Show(e.ToString(), "Help2Design 启动失败"); return 1; }
        }
        public static void Log(Exception e)
        {
            try { var path = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Help2Design"); Directory.CreateDirectory(path); File.AppendAllText(Path.Combine(path, "errors.log"), DateTime.Now + " " + e + Environment.NewLine); } catch { }
        }
    }
}
