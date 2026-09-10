using System;
using System.IO;
using System.IO.Pipes;
using System.Security.Principal;
using System.Text;
using System.Threading;

namespace Help2Design
{
    // One resident instance per Windows user. Reopening the executable requests another capture.
    internal sealed class AppInstance : IDisposable
    {
        private readonly Mutex mutex;
        private readonly string name;
        private NamedPipeServerStream server;
        private volatile bool disposed;
        public readonly bool IsPrimary;
        public AppInstance(string testSuffix = null)
        {
            name = "Help2Design.Capture.v2." + WindowsIdentity.GetCurrent().User.Value + (testSuffix ?? "");
            bool created; mutex = new Mutex(true, "Local\\" + name, out created); IsPrimary = created;
        }
        public void Send(string argument)
        {
            using (var client = new NamedPipeClientStream(".", name, PipeDirection.Out)) {
                client.Connect(3000);
                using (var writer = new StreamWriter(client, new UTF8Encoding(false))) writer.WriteLine(Convert.ToBase64String(Encoding.UTF8.GetBytes(argument ?? "")));
            }
        }
        public void Listen(Action<string> receive)
        {
            var worker = new Thread(delegate() {
                while (!disposed) {
                    try {
                        using (var pipe = new NamedPipeServerStream(name, PipeDirection.In, 1, PipeTransmissionMode.Byte, PipeOptions.None)) {
                            server = pipe; pipe.WaitForConnection();
                            using (var reader = new StreamReader(pipe, Encoding.UTF8)) {
                                string line = reader.ReadLine();
                                if (line != null && line.Length < 65536 && !disposed) receive(Encoding.UTF8.GetString(Convert.FromBase64String(line)));
                            }
                        }
                    } catch (Exception e) { if (!disposed) Program.Log(e); }
                }
            });
            worker.IsBackground = true; worker.Start();
        }
        public void Dispose() { disposed = true; try { if (server != null) server.Dispose(); } catch { } if (IsPrimary) mutex.ReleaseMutex(); mutex.Dispose(); }
    }
}
