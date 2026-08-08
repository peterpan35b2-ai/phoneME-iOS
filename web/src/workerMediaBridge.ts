/// <reference lib="webworker" />

type MediaEntry = {
  handle: number;
  duration: number;
  time: number;
  playing: boolean;
  ended: boolean;
  error: boolean;
  startedAt: number;
};

type MediaStatusMessage = {
  event: "mediaStatus";
  handle: number;
  duration?: number;
  time?: number;
  playing?: boolean;
  ended?: boolean;
  error?: boolean;
};

type MediaCommand = {
  event: "media";
  action: string;
  handle?: number;
  data?: Uint8Array;
  locator?: string;
  contentType?: string;
  count?: number;
  level?: number;
  muted?: number | boolean;
  microseconds?: number;
  note?: number;
  durationMilliseconds?: number;
  volume?: number;
};

class PhoneMEWorkerMediaBridge {
  private readonly scope: DedicatedWorkerGlobalScope;
  private readonly entries = new Map<number, MediaEntry>();
  private nextHandle = 1;

  constructor(scope: DedicatedWorkerGlobalScope) {
    this.scope = scope;
  }

  createData(data: Uint8Array, contentType: string) {
    if (!data?.byteLength) return 0;
    const handle = this.allocateHandle();
    this.entries.set(handle, this.newEntry(handle));
    try {
      this.post({ event: "media", action: "createData", handle, data, contentType }, [data.buffer]);
      return handle;
    } catch {
      this.entries.delete(handle);
      return 0;
    }
  }

  createLocator(locator: string, contentType: string) {
    if (!locator) return 0;
    const handle = this.allocateHandle();
    this.entries.set(handle, this.newEntry(handle));
    try {
      this.post({ event: "media", action: "createLocator", handle, locator, contentType });
      return handle;
    } catch {
      this.entries.delete(handle);
      return 0;
    }
  }

  start(handle: number) {
    const entry = this.entries.get(handle);
    if (!entry) return 0;
    entry.playing = true;
    entry.ended = false;
    entry.error = false;
    entry.startedAt = performance.now() - entry.time / 1000;
    this.post({ event: "media", action: "start", handle });
    return 1;
  }

  stop(handle: number) {
    const entry = this.entries.get(handle);
    if (!entry) return 0;
    this.captureTime(entry);
    entry.playing = false;
    this.post({ event: "media", action: "stop", handle });
    return 1;
  }

  close(handle: number) {
    if (!this.entries.has(handle)) return;
    this.entries.delete(handle);
    this.post({ event: "media", action: "close", handle });
  }

  setLoopCount(handle: number, count: number) {
    if (!this.entries.has(handle)) return;
    this.post({ event: "media", action: "setLoopCount", handle, count });
  }

  setVolume(handle: number, level: number) {
    if (!this.entries.has(handle)) return;
    this.post({ event: "media", action: "setVolume", handle, level });
  }

  setMute(handle: number, muted: number | boolean) {
    if (!this.entries.has(handle)) return;
    this.post({ event: "media", action: "setMute", handle, muted });
  }

  setTime(handle: number, microseconds: number) {
    const entry = this.entries.get(handle);
    if (!entry) return -1;
    entry.time = Math.max(0, Math.trunc(microseconds));
    if (entry.duration >= 0) entry.time = Math.min(entry.duration, entry.time);
    if (entry.playing) entry.startedAt = performance.now() - entry.time / 1000;
    this.post({ event: "media", action: "setTime", handle, microseconds: entry.time });
    return entry.time;
  }

  getTime(handle: number) {
    const entry = this.entries.get(handle);
    if (!entry) return -1;
    if (!entry.playing) return entry.time;
    const elapsedMicroseconds = Math.max(0, performance.now() - entry.startedAt) * 1000;
    const time = Math.round(elapsedMicroseconds);
    return entry.duration >= 0 ? Math.min(entry.duration, time) : time;
  }

  getDuration(handle: number) {
    return this.entries.get(handle)?.duration ?? -1;
  }

  isPlaying(handle: number) {
    return this.entries.get(handle)?.playing ? 1 : 0;
  }

  hasEnded(handle: number) {
    return this.entries.get(handle)?.ended ? 1 : 0;
  }

  hasError(handle: number) {
    return this.entries.get(handle)?.error ? 1 : 0;
  }

  playTone(note: number, durationMilliseconds: number, volume: number) {
    this.post({ event: "media", action: "playTone", note, durationMilliseconds, volume });
    return 1;
  }

  applyStatus(message: MediaStatusMessage) {
    const entry = this.entries.get(message.handle);
    if (!entry) return;
    if (typeof message.duration === "number") entry.duration = message.duration;
    if (typeof message.time === "number" && message.time >= 0) entry.time = message.time;
    if (typeof message.playing === "boolean") {
      entry.playing = message.playing;
      if (entry.playing) entry.startedAt = performance.now() - entry.time / 1000;
    }
    if (typeof message.ended === "boolean") entry.ended = message.ended;
    if (typeof message.error === "boolean") entry.error = message.error;
  }

  private newEntry(handle: number): MediaEntry {
    return {
      handle,
      duration: -1,
      time: 0,
      playing: false,
      ended: false,
      error: false,
      startedAt: performance.now()
    };
  }

  private captureTime(entry: MediaEntry) {
    if (!entry.playing) return;
    entry.time = this.getTime(entry.handle);
    entry.startedAt = performance.now() - entry.time / 1000;
  }

  private allocateHandle() {
    let handle = this.nextHandle++;
    if (this.nextHandle > 0x7fffffff) this.nextHandle = 1;
    while (this.entries.has(handle)) handle = this.nextHandle++;
    return handle;
  }

  private post(message: MediaCommand, transfer: Transferable[] = []) {
    this.scope.postMessage(message, transfer);
  }
}

export function installWorkerMediaBridge(scope: DedicatedWorkerGlobalScope) {
  const bridge = new PhoneMEWorkerMediaBridge(scope);
  (globalThis as typeof globalThis & { __phoneMEMediaBridge?: PhoneMEWorkerMediaBridge }).__phoneMEMediaBridge = bridge;
  return bridge;
}

export type { MediaStatusMessage };
