const { contextBridge, ipcRenderer } = require('electron');

// Only a validated appearance hint crosses the bridge. No general-purpose IPC,
// filesystem, shell or credentials are exposed to the renderer.
contextBridge.exposeInMainWorld('edithereDesktop', Object.freeze({
  isDesktop: true,
  platform: process.platform,
  version: '0.13.0',
  setTheme: (theme) => {
    if (theme === 'light' || theme === 'dark') ipcRenderer.send('preview:theme', theme);
  },
}));
