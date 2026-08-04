import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

const isolationHeaders = {
  "Cross-Origin-Opener-Policy": "same-origin",
  "Cross-Origin-Embedder-Policy": "require-corp",
  "Cross-Origin-Resource-Policy": "same-origin"
};

export default defineConfig({
  plugins: [react()],
  server: {
    host: true,
    headers: isolationHeaders
  },
  preview: {
    host: true,
    headers: isolationHeaders
  },
  build: {
    target: "es2022",
    sourcemap: true
  }
});
