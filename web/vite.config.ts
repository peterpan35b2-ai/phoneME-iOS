import { defineConfig, type Plugin } from "vite";
import react from "@vitejs/plugin-react";
import { onRequestPost as handleHttpBridge } from "./functions/api/http.js";

const isolationHeaders = {
  "Cross-Origin-Opener-Policy": "same-origin",
  "Cross-Origin-Embedder-Policy": "require-corp",
  "Cross-Origin-Resource-Policy": "same-origin"
};

function phoneMEHttpBridge(): Plugin {
  const install = (middlewares: { use: (handler: (req: any, res: any, next: () => void) => void) => void }) => {
    middlewares.use((req, res, next) => {
      const pathname = String(req.url ?? "").split("?", 1)[0];
      if (pathname !== "/api/http") {
        next();
        return;
      }
      if (req.method !== "POST") {
        res.statusCode = 405;
        res.end("Use POST");
        return;
      }

      const chunks: Uint8Array[] = [];
      let size = 0;
      req.on("data", (chunk: Uint8Array) => {
        const bytes = new Uint8Array(chunk);
        size += bytes.byteLength;
        if (size <= 65 * 1024 * 1024) chunks.push(bytes);
      });
      req.on("end", () => {
        void (async () => {
          if (size > 65 * 1024 * 1024) {
            res.statusCode = 413;
            res.end("Request too large");
            return;
          }
          const body = new Uint8Array(size);
          let cursor = 0;
          for (const chunk of chunks) {
            body.set(chunk, cursor);
            cursor += chunk.byteLength;
          }
          const headers = new Headers();
          for (const [name, raw] of Object.entries(req.headers ?? {})) {
            if (Array.isArray(raw)) raw.forEach((value) => headers.append(name, String(value)));
            else if (raw !== undefined) headers.set(name, String(raw));
          }
          const request = new Request(`http://${req.headers?.host ?? "127.0.0.1"}${req.url ?? "/api/http"}`, {
            method: "POST",
            headers,
            body
          });
          const response = await handleHttpBridge({ request });
          res.statusCode = response.status;
          response.headers.forEach((value, name) => res.setHeader(name, value));
          res.end(new Uint8Array(await response.arrayBuffer()));
        })().catch((error) => {
          res.statusCode = 502;
          res.end(error instanceof Error ? error.message : String(error));
        });
      });
    });
  };

  return {
    name: "phoneme-http-bridge",
    configureServer(server) { install(server.middlewares); },
    configurePreviewServer(server) { install(server.middlewares); }
  };
}

export default defineConfig({
  plugins: [react(), phoneMEHttpBridge()],
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
