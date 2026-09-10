import { defineConfig } from 'vite';
export default defineConfig({ base: './', build: { outDir: 'dist/web', emptyOutDir: true, target: 'es2022' } });
