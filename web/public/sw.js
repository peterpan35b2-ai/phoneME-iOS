const CACHE_SCHEMA = "v5";
const SHELL_PREFIX = `phoneme-shell-${CACHE_SCHEMA}-`;
const RUNTIME_PREFIX = `phoneme-runtime-${CACHE_SCHEMA}-`;
const META_CACHE = `phoneme-meta-${CACHE_SCHEMA}`;
const ACTIVE_BUILD_KEY = new URL("/__phoneme_active_build__", self.location.origin).href;

function safeVersion(version) {
  return String(version || "unknown").replace(/[^a-zA-Z0-9._-]/g, "_");
}

function shellCacheName(version) {
  return `${SHELL_PREFIX}${safeVersion(version)}`;
}

function runtimeCacheName(version) {
  return `${RUNTIME_PREFIX}${safeVersion(version)}`;
}

async function readActiveBuild() {
  const cache = await caches.open(META_CACHE);
  const response = await cache.match(ACTIVE_BUILD_KEY);
  return response ? response.text() : "";
}

async function setActiveBuild(version) {
  const cache = await caches.open(META_CACHE);
  await cache.put(ACTIVE_BUILD_KEY, new Response(version, {
    headers: { "Content-Type": "text/plain; charset=utf-8", "Cache-Control": "no-store" }
  }));
}

async function fetchFresh(url) {
  const response = await fetch(url, { cache: "reload", credentials: "same-origin" });
  if (!response.ok) throw new Error(`HTTP ${response.status}: ${url}`);
  return response;
}

async function putFresh(cache, url, cacheKey) {
  const response = await fetchFresh(url);
  await cache.put(cacheKey || url, response.clone());
  return response;
}

function collectFrontendAssets(manifest) {
  const urls = new Set();
  for (const entry of Object.values(manifest || {})) {
    if (!entry || typeof entry !== "object") continue;
    if (typeof entry.file === "string") urls.add(`/${entry.file.replace(/^\//, "")}`);
    for (const css of Array.isArray(entry.css) ? entry.css : []) {
      if (typeof css === "string") urls.add(`/${css.replace(/^\//, "")}`);
    }
    for (const asset of Array.isArray(entry.assets) ? entry.assets : []) {
      if (typeof asset === "string") urls.add(`/${asset.replace(/^\//, "")}`);
    }
  }
  return [...urls];
}

function discoverBundledAssetUrls(text) {
  const urls = new Set();
  const pattern = /\/assets\/[a-zA-Z0-9._~-]+/g;
  for (const match of text.matchAll(pattern)) urls.add(match[0]);
  return [...urls];
}

async function precacheBuild(expectedVersion) {
  const versionUrl = `/version.json?sw=${Date.now().toString(36)}`;
  const versionResponse = await fetchFresh(versionUrl);
  const versionMetadata = await versionResponse.clone().json();
  const version = versionMetadata && versionMetadata.version;
  if (!version || typeof version !== "string") throw new Error("version.json không hợp lệ");
  if (expectedVersion && version !== expectedVersion) {
    throw new Error(`Build trên server đã đổi từ ${expectedVersion} sang ${version}`);
  }

  const shell = await caches.open(shellCacheName(version));
  const runtime = await caches.open(runtimeCacheName(version));
  await shell.put("/version.json", versionResponse.clone());

  const assetManifestResponse = await putFresh(shell, "/asset-manifest.json");
  const assetManifest = await assetManifestResponse.clone().json();
  const frontendAssets = collectFrontendAssets(assetManifest);

  const wasmManifestResponse = await putFresh(shell, "/wasm/manifest.json");
  const wasmManifest = await wasmManifestResponse.clone().json();
  if (!wasmManifest || typeof wasmManifest.module !== "string" || typeof wasmManifest.wasm !== "string") {
    throw new Error("WASM manifest không hợp lệ");
  }
  const wasmManifestUrl = new URL("/wasm/manifest.json", self.location.origin);
  const wasmUrls = [
    new URL(wasmManifest.module, wasmManifestUrl).href,
    new URL(wasmManifest.wasm, wasmManifestUrl).href
  ];
  if (wasmUrls.some((url) => new URL(url).origin !== self.location.origin)) {
    throw new Error("WASM manifest trỏ ra ngoài origin");
  }

  for (const url of ["/", "/manifest.webmanifest", "/app-icon.svg"]) {
    await putFresh(shell, url);
  }
  const frontendQueue = [...frontendAssets];
  const queuedFrontend = new Set(frontendQueue);
  for (let index = 0; index < frontendQueue.length; index += 1) {
    const url = frontendQueue[index];
    const response = await putFresh(shell, url);
    if (!url.endsWith(".js") && !url.endsWith(".css")) continue;
    const text = await response.clone().text();
    for (const discovered of discoverBundledAssetUrls(text)) {
      if (queuedFrontend.has(discovered)) continue;
      queuedFrontend.add(discovered);
      frontendQueue.push(discovered);
    }
  }
  for (const url of wasmUrls) {
    await putFresh(runtime, url);
  }

  return version;
}

async function cleanupBuildCaches(activeVersion) {
  const keepShell = shellCacheName(activeVersion);
  const keepRuntime = runtimeCacheName(activeVersion);
  const keys = await caches.keys();
  await Promise.all(keys.map((key) => {
    const isPhoneMEShell = key.startsWith("phoneme-shell-");
    const isPhoneMERuntime = key.startsWith("phoneme-runtime-");
    if (!isPhoneMEShell && !isPhoneMERuntime) return Promise.resolve(false);
    if (key === keepShell || key === keepRuntime) return Promise.resolve(false);
    return caches.delete(key);
  }));
}

async function matchActive(request, preferred) {
  const version = await readActiveBuild();
  if (!version) return undefined;
  const names = preferred === "runtime"
    ? [runtimeCacheName(version), shellCacheName(version)]
    : [shellCacheName(version), runtimeCacheName(version)];
  for (const name of names) {
    const response = await (await caches.open(name)).match(request);
    if (response) return response;
  }
  return undefined;
}

async function cacheIntoActive(request, response, preferred) {
  const version = await readActiveBuild();
  if (!version || !response || !response.ok) return;
  const cache = await caches.open(preferred === "runtime" ? runtimeCacheName(version) : shellCacheName(version));
  await cache.put(request, response.clone());
}

self.addEventListener("install", (event) => {
  event.waitUntil((async () => {
    const previousActive = await readActiveBuild();
    const prepared = await precacheBuild();
    if (!previousActive) {
      await setActiveBuild(prepared);
      await cleanupBuildCaches(prepared);
    }
    await self.skipWaiting();
  })());
});

self.addEventListener("activate", (event) => {
  event.waitUntil(self.clients.claim());
});

self.addEventListener("message", (event) => {
  const data = event.data || {};
  const port = event.ports && event.ports[0];
  const reply = (payload) => { if (port) port.postMessage(payload); };

  if (data.type === "PREPARE_UPDATE") {
    event.waitUntil((async () => {
      try {
        const version = await precacheBuild(data.version);
        reply({ ok: true, version });
      } catch (error) {
        reply({ ok: false, error: error instanceof Error ? error.message : String(error) });
      }
    })());
    return;
  }

  if (data.type === "ACTIVATE_UPDATE") {
    event.waitUntil((async () => {
      try {
        const version = String(data.version || "");
        if (!version) throw new Error("Thiếu version cần kích hoạt");
        const keys = await caches.keys();
        if (!keys.includes(shellCacheName(version)) || !keys.includes(runtimeCacheName(version))) {
          await precacheBuild(version);
        }
        await setActiveBuild(version);
        await cleanupBuildCaches(version);
        reply({ ok: true, version });
      } catch (error) {
        reply({ ok: false, error: error instanceof Error ? error.message : String(error) });
      }
    })());
  }
});

self.addEventListener("fetch", (event) => {
  const request = event.request;
  if (request.method !== "GET") return;
  const url = new URL(request.url);
  if (url.origin !== self.location.origin || url.pathname === "/api/http" || url.pathname === "/sw.js") return;

  if (url.pathname === "/version.json") {
    event.respondWith((async () => {
      try {
        return await fetch(request);
      } catch {
        return (await matchActive("/version.json", "shell")) || Response.error();
      }
    })());
    return;
  }

  if (request.mode === "navigate") {
    event.respondWith((async () => {
      const cached = await matchActive("/", "shell");
      if (cached) return cached;
      try {
        const response = await fetch(request);
        await cacheIntoActive("/", response, "shell");
        return response;
      } catch {
        return Response.error();
      }
    })());
    return;
  }

  const immutableRuntime = url.pathname.startsWith("/wasm/build-") || url.pathname.startsWith("/assets/");
  const pinnedDescriptor = url.pathname === "/wasm/manifest.json" || url.pathname === "/asset-manifest.json";
  if (immutableRuntime || pinnedDescriptor) {
    event.respondWith((async () => {
      const cached = await matchActive(request, immutableRuntime ? "runtime" : "shell");
      if (cached) return cached;
      const response = await fetch(request);
      await cacheIntoActive(request, response, immutableRuntime ? "runtime" : "shell");
      return response;
    })());
    return;
  }

  event.respondWith((async () => {
    try {
      const response = await fetch(request);
      await cacheIntoActive(request, response, "shell");
      return response;
    } catch {
      return (await matchActive(request, "shell")) || Response.error();
    }
  })());
});
