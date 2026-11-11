const { app, BrowserWindow, dialog, shell, ipcMain } = require('electron');
const { existsSync, mkdirSync } = require('node:fs');
const path = require('node:path');

// Give the preview its own taskbar identity and cache instead of inheriting
// the launcher identity or sharing files with the existing native application.
app.setName('EditHere Desktop Preview');
if (process.platform === 'win32') app.setAppUserModelId('EditHere.DesktopPreview');
const isUiTest = process.env.EDITHERE_PREVIEW_TEST === '1';
const previewData = isUiTest ? app.getPath('temp') + '/edithere-ui-test-' + process.pid : path.join(app.getPath('appData'), 'EditHere-Desktop-Preview');
mkdirSync(previewData, { recursive: true });
app.setPath('userData', previewData);

let mainWindow = null;
const isDevelopment = process.env.EDITHERE_PREVIEW_DEV === '1' && !app.isPackaged;
const developmentUrl = 'http://127.0.0.1:4313/';

function openExternalPage(url) {
  try {
    if (new URL(url).protocol !== 'https:') return;
    void shell.openExternal(url).catch(() => {});
  } catch {
    // An invalid or unsupported link must never replace the local application.
  }
}

function showMainWindow() {
  if (!mainWindow || mainWindow.isDestroyed()) return;
  if (mainWindow.isMinimized()) mainWindow.restore();
  mainWindow.show();
  mainWindow.focus();
}

function createWindow() {
  const icon = path.join(__dirname, 'icon.ico');
  mainWindow = new BrowserWindow({
    title: 'EditHere 0.13.0 · UI Preview',
    width: 1360,
    height: 940,
    minWidth: 900,
    minHeight: 680,
    show: false,
    autoHideMenuBar: true,
    backgroundColor: '#fafafa',
    titleBarStyle: 'hidden',
    titleBarOverlay: {
      color: '#f5f5f7',
      symbolColor: '#52525b',
      height: 36,
    },
    ...(existsSync(icon) ? { icon } : {}),
    webPreferences: {
      preload: path.join(__dirname, 'preload.cjs'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  });
  const window = mainWindow;
  window.removeMenu();
  window.webContents.on('page-title-updated', event => event.preventDefault());
  window.once('ready-to-show', () => window.show());
  window.on('closed', () => {
    if (mainWindow === window) mainWindow = null;
  });

  window.webContents.setWindowOpenHandler(({ url }) => {
    openExternalPage(url);
    return { action: 'deny' };
  });
  window.webContents.on('will-navigate', (event, url) => {
    const currentUrl = window.webContents.getURL();
    if (url !== currentUrl) event.preventDefault();
  });
  window.webContents.on('will-attach-webview', event => event.preventDefault());

  // Relative renderer assets resolve against dist/ both in a source checkout
  // and inside the packaged resources/app(.asar) directory.
  const loaded = isDevelopment
    ? window.loadURL(developmentUrl)
    : window.loadFile(path.join(__dirname, '..', 'dist', 'index.html'));
  void loaded.catch(() => {
    if (window.isDestroyed()) return;
    dialog.showErrorBox('EditHere 启动失败', '无法加载界面文件。请保留程序文件夹的完整内容后重新启动。');
    window.close();
  });
}

if (!app.requestSingleInstanceLock()) {
  app.quit();
} else {
  app.on('second-instance', showMainWindow);
  app.whenReady().then(() => {
    ipcMain.on('preview:theme', (event, theme) => {
      if (!mainWindow || event.sender !== mainWindow.webContents || event.senderFrame !== mainWindow.webContents.mainFrame || !['dark', 'light'].includes(theme)) return;
      mainWindow.setTitleBarOverlay({color: theme === 'dark' ? '#1b1b1e' : '#f5f5f7', symbolColor: theme === 'dark' ? '#d4d4d8' : '#52525b', height: 36});
    });
    createWindow();
    app.on('activate', () => {
      if (BrowserWindow.getAllWindows().length === 0) createWindow();
      else showMainWindow();
    });
  });
  app.on('window-all-closed', () => {
    if (process.platform !== 'darwin') app.quit();
  });
}
