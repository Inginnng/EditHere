type WebViewMessage =
  | { type: 'rpcResult'; id: number; ok: boolean; data?: unknown; error?: string }
  | { type: 'event'; event: string; data?: Record<string, unknown> };

interface WebViewApi {
  postMessage: (message: unknown) => void;
  addEventListener: (type: 'message', listener: (ev: { data: unknown }) => void) => void;
  removeEventListener: (type: 'message', listener: (ev: { data: unknown }) => void) => void;
}

declare global {
  interface Window {
    chrome?: {
      webview?: WebViewApi;
    };
  }
}

type EventHandler = (event: string, data: Record<string, unknown>) => void;

let nextRpcId = 1;
const pending = new Map<
  number,
  {
    resolve: (value: any) => void;
    reject: (reason: Error) => void;
    timer?: ReturnType<typeof setTimeout>;
  }
>();
const subscribers = new Set<EventHandler>();
let listenerAttached = false;

function ensureListener(): WebViewApi | null {
  if (typeof window === 'undefined') return null;
  const webview = window.chrome?.webview;
  if (!webview || typeof webview.postMessage !== 'function') return null;

  if (!listenerAttached && typeof webview.addEventListener === 'function') {
    webview.addEventListener('message', (ev) => {
      const msg = ev?.data as WebViewMessage | undefined;
      if (!msg || typeof msg !== 'object') return;

      if (msg.type === 'rpcResult' && typeof msg.id === 'number') {
        const entry = pending.get(msg.id);
        if (!entry) return;
        pending.delete(msg.id);
        if (entry.timer) clearTimeout(entry.timer);
        if (msg.ok) entry.resolve(msg.data);
        else entry.reject(new Error(msg.error || 'Desktop RPC error'));
      } else if (msg.type === 'event' && typeof msg.event === 'string') {
        const payload = msg.data ?? {};
        for (const fn of subscribers) {
          try {
            fn(msg.event, payload);
          } catch (err) {
            console.error('[EditHere Bridge] Event handler error:', err);
          }
        }
      }
    });
    listenerAttached = true;
  }
  return webview;
}

export function isDesktopWebView(): boolean {
  return typeof window !== 'undefined' && Boolean(window.chrome?.webview?.postMessage);
}

export function desktopRpc<T = Record<string, unknown>>(
  method: string,
  params: Record<string, unknown> = {},
  timeoutMs = 15000
): Promise<T> {
  const webview = ensureListener();
  if (!webview) {
    return Promise.reject(new Error('Desktop WebView2 bridge is unavailable'));
  }

  const id = nextRpcId++;
  return new Promise<T>((resolve, reject) => {
    const timer =
      timeoutMs > 0
        ? setTimeout(() => {
            pending.delete(id);
            reject(new Error(`Desktop RPC "${method}" timed out`));
          }, timeoutMs)
        : undefined;

    pending.set(id, { resolve, reject, timer });
    webview.postMessage({ type: 'rpc', id, method, params });
  });
}

export function subscribeDesktopEvents(handler: EventHandler): () => void {
  ensureListener();
  subscribers.add(handler);
  return () => {
    subscribers.delete(handler);
  };
}
