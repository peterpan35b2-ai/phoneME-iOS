export const PWA_UPDATE_READY_EVENT = "phoneme:pwa-update-ready";

export interface PwaUpdateReadyDetail {
  version: string;
}

interface BuildMetadata {
  version?: string;
}

interface WorkerReply {
  ok?: boolean;
  error?: string;
  version?: string;
}

const UPDATE_CHECK_INTERVAL_MS = 60 * 1000;
const WORKER_MESSAGE_TIMEOUT_MS = 90 * 1000;

let registration: ServiceWorkerRegistration | null = null;
let initialized = false;
let checkTimer: number | undefined;
let checkInFlight: Promise<void> | null = null;
let announcedVersion: string | null = null;

function dispatchUpdateReady(version: string) {
  window.dispatchEvent(new CustomEvent<PwaUpdateReadyDetail>(PWA_UPDATE_READY_EVENT, {
    detail: { version }
  }));
}

async function requestWorker(type: string, version?: string) {
  const ready = registration ?? await navigator.serviceWorker.ready;
  const worker = ready.active ?? navigator.serviceWorker.controller;
  if (!worker) throw new Error("Service worker chưa sẵn sàng");

  return await new Promise<WorkerReply>((resolve, reject) => {
    const channel = new MessageChannel();
    const timeout = window.setTimeout(() => {
      channel.port1.close();
      reject(new Error(`Service worker không phản hồi ${type}`));
    }, WORKER_MESSAGE_TIMEOUT_MS);

    channel.port1.onmessage = (event: MessageEvent<WorkerReply>) => {
      window.clearTimeout(timeout);
      channel.port1.close();
      if (event.data?.ok) resolve(event.data);
      else reject(new Error(event.data?.error || `Service worker xử lý ${type} thất bại`));
    };

    worker.postMessage({ type, version }, [channel.port2]);
  });
}

async function fetchLatestBuild() {
  const url = new URL("/version.json", window.location.href);
  url.searchParams.set("t", Date.now().toString(36));
  const response = await fetch(url, {
    cache: "no-store",
    headers: { "Cache-Control": "no-cache" }
  });
  if (!response.ok) throw new Error(`Không đọc được version.json: HTTP ${response.status}`);
  const metadata = await response.json() as BuildMetadata;
  if (!metadata.version) throw new Error("version.json không hợp lệ");
  return metadata.version;
}

async function runUpdateCheck() {
  if (!navigator.onLine || announcedVersion) return;
  const latestVersion = await fetchLatestBuild();
  if (latestVersion === __PHONEME_BUILD_ID__) return;

  // Revalidate the worker implementation as well. Most releases only need the
  // build metadata path, but this also picks up service-worker fixes immediately.
  await registration?.update().catch(() => undefined);
  announcedVersion = latestVersion;
  dispatchUpdateReady(latestVersion);
}

export function getPendingPwaUpdateVersion() {
  return announcedVersion;
}

export function checkForPwaUpdate() {
  if (!import.meta.env.PROD || !("serviceWorker" in navigator)) return Promise.resolve();
  if (checkInFlight) return checkInFlight;
  checkInFlight = runUpdateCheck()
    .catch((error) => console.warn("phoneME PWA update check failed", error))
    .finally(() => { checkInFlight = null; });
  return checkInFlight;
}

export async function applyPwaUpdate(version: string) {
  if (!import.meta.env.PROD || !("serviceWorker" in navigator)) {
    window.location.reload();
    return;
  }

  const prepared = await requestWorker("PREPARE_UPDATE", version);
  await requestWorker("ACTIVATE_UPDATE", prepared.version || version);
  window.location.reload();
}

export async function initPwa() {
  if (initialized || !import.meta.env.PROD || !("serviceWorker" in navigator)) return;
  initialized = true;

  registration = await navigator.serviceWorker.register("/sw.js", { scope: "/", updateViaCache: "none" });
  await navigator.serviceWorker.ready;

  // Update monitoring must start before offline warm-up. If a newer deployment
  // already exists, ENSURE_OFFLINE for this (older) build is expected to reject;
  // that must never disable update detection for the lifetime of the page.
  const scheduleCheck = () => { void checkForPwaUpdate(); };
  window.addEventListener("online", scheduleCheck);
  window.addEventListener("focus", scheduleCheck);
  window.addEventListener("pageshow", scheduleCheck);
  document.addEventListener("visibilitychange", () => {
    if (document.visibilityState === "visible") scheduleCheck();
  });
  checkTimer = window.setInterval(scheduleCheck, UPDATE_CHECK_INTERVAL_MS);
  void checkTimer;
  scheduleCheck();

  // Safari/iOS may finish page loading before a large service-worker install has
  // fully warmed Cache Storage. Warm the currently running build opportunistically,
  // but only while it is still the server's latest build. This task is deliberately
  // detached from update monitoring so network/cache failures cannot disable it.
  if (navigator.onLine) {
    void (async () => {
      await checkForPwaUpdate();
      if (announcedVersion) return;
      await requestWorker("ENSURE_OFFLINE", __PHONEME_BUILD_ID__);
      if (navigator.storage?.persist) {
        await navigator.storage.persist().catch(() => false);
      }
    })().catch((error) => console.warn("phoneME PWA offline warm-up failed", error));
  }
}
