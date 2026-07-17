import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import path from 'path';

export default defineConfig({
  plugins: [react()],
  resolve: {
    alias: {
      '@': path.resolve(__dirname, './src'),
    },
  },
  server: {
    // 5173 (vite's default), NOT 3000 — ThothOS's dev server owns :3000 on
    // this machine (operator directive 2026-07-16), and :3100 is occupied
    // by another local service. strictPort so a clash fails loudly instead
    // of silently hopping back toward a ThothOS port.
    port: 5173,
    strictPort: true,
    // NEVER auto-open a browser window: this dev server is routinely started
    // by headless test tooling on a machine the operator is actively using,
    // and a surprise tab on their desktop violates the no-visible-testing
    // rule (operator directive 2026-07-16). Open localhost:5173 manually.
    open: false,
  },
  build: {
    outDir: 'dist',
    sourcemap: true,
    rollupOptions: {
      output: {
        manualChunks: {
          vendor: ['react', 'react-dom'],
          three: ['three', '@react-three/fiber', '@react-three/drei'],
          game: ['zustand'],
        },
      },
    },
  },
  optimizeDeps: {
    include: ['three', '@react-three/fiber', '@react-three/drei', 'zustand'],
  },
}); 