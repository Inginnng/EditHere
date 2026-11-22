import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  base: './',
  build: {
    rollupOptions: {
      output: {
        manualChunks(id) {
          const normalized = id.replace(/\\/g, '/');
          if (!normalized.includes('/node_modules/')) return;
          if (
            normalized.includes('/node_modules/react/') ||
            normalized.includes('/node_modules/react-dom/') ||
            normalized.includes('/node_modules/scheduler/')
          ) {
            return 'react-vendor';
          }
          if (
            normalized.includes('/node_modules/@radix-ui/') ||
            normalized.includes('/node_modules/@floating-ui/') ||
            normalized.includes('/node_modules/react-remove-scroll') ||
            normalized.includes('/node_modules/aria-hidden')
          ) {
            return 'radix-vendor';
          }
          if (normalized.includes('/node_modules/lucide-react/')) {
            return 'icons-vendor';
          }
        },
      },
    },
  },
});
