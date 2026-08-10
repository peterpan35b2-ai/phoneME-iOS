import { execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { defineConfig, type Plugin } from "vite";
import react from "@vitejs/plugin-react";
import { onRequestPost as handleHttpBridge } from "./functions/api/http.js";

const iosAppIconUrl = new URL("../phoneME/Resources/Assets.xcassets/AppIcon.appiconset/AppIcon.svg", import.meta.url);
const webBuildId = `${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 10)}`;
const webBuildCreatedAt = new Date().toISOString();

function phoneMEBuildMetadata(): Plugin {
  return {
    name: "phoneme-build-metadata",
    generateBundle() {
      this.emitFile({
        type: "asset",
        fileName: "version.json",
        source: JSON.stringify({ version: webBuildId, createdAt: webBuildCreatedAt })
      });
    }
  };
}

function renderIosAppIconPngs() {
  const svg = new TextDecoder().decode(readFileSync(iosAppIconUrl));
  const payload = svg.match(/base64,([^\"]+)\"\/>/)?.[1];
  if (!payload) throw new Error("AppIcon.svg không chứa PNG payload");

  const binary = atob(payload);
  const sourceBytes = new Uint8Array(binary.length);
  for (let index = 0; index < binary.length; index += 1) sourceBytes[index] = binary.charCodeAt(index);

  const root = mkdtempSync(join(tmpdir(), "phoneme-pwa-icon-"));
  try {
    const sourcePng = join(root, "source.png");
    const sourceJpg = join(root, "source.jpg");
    const scaledJpg = join(root, "scaled.jpg");
    const paddedJpg = join(root, "padded.jpg");
    const app1024 = join(root, "app-icon-1024.png");
    writeFileSync(sourcePng, sourceBytes);

    const sips = (...args: string[]) => execFileSync("/usr/bin/sips", args, { stdio: "ignore" });
    sips("-s", "format", "jpeg", sourcePng, "--out", sourceJpg);
    sips("--resampleHeight", "820", sourceJpg, "--out", scaledJpg);
    sips("--padToHeightWidth", "1024", "1024", "--padColor", "FFFFFF", scaledJpg, "--out", paddedJpg);
    sips("-s", "format", "png", paddedJpg, "--out", app1024);

    const result: Record<number, Uint8Array> = {};
    for (const size of [180, 192, 512]) {
      const output = join(root, `app-icon-${size}.png`);
      sips("--resampleHeightWidth", String(size), String(size), app1024, "--out", output);
      result[size] = readFileSync(output);
    }
    return result;
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
}

function phoneMEAppIcon(): Plugin {
  const svgIcon = readFileSync(iosAppIconUrl);
  const pngIcons = renderIosAppIconPngs();
  const install = (middlewares: { use: (handler: (req: any, res: any, next: () => void) => void) => void }) => {
    middlewares.use((req, res, next) => {
      const pathname = String(req.url ?? "").split("?", 1)[0];
      if (pathname === "/app-icon.svg") {
        res.statusCode = 200;
        res.setHeader("Content-Type", "image/svg+xml; charset=utf-8");
        res.setHeader("Cache-Control", "public, max-age=3600");
        res.end(svgIcon);
        return;
      }
      const match = pathname.match(/^\/app-icon-(180|192|512)\.png$/);
      if (!match) {
        next();
        return;
      }
      res.statusCode = 200;
      res.setHeader("Content-Type", "image/png");
      res.setHeader("Cache-Control", "public, max-age=3600");
      res.end(pngIcons[Number(match[1])]);
    });
  };

  return {
    name: "phoneme-app-icon",
    configureServer(server) { install(server.middlewares); },
    configurePreviewServer(server) { install(server.middlewares); },
    generateBundle() {
      this.emitFile({ type: "asset", fileName: "app-icon.svg", source: svgIcon });
      for (const [size, source] of Object.entries(pngIcons)) {
        this.emitFile({ type: "asset", fileName: `app-icon-${size}.png`, source });
      }
    }
  };
}

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
  define: {
    __PHONEME_BUILD_ID__: JSON.stringify(webBuildId)
  },
  plugins: [react(), phoneMEBuildMetadata(), phoneMEAppIcon(), phoneMEHttpBridge()],
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
    sourcemap: true,
    manifest: "asset-manifest.json"
  }
});
