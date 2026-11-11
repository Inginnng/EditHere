interface Window {
  readonly edithereDesktop?: Readonly<{ isDesktop: boolean; platform: string; version: string; setTheme?: (theme: 'light' | 'dark') => void }>;
}
