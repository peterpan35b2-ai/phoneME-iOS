type MediaEntry = {
  handle: number;
  contentType: string;
  bytes?: Uint8Array;
  locator?: string;
  buffer?: AudioBuffer;
  loading?: Promise<void>;
  abortController?: AbortController;
  source?: AudioBufferSourceNode;
  gain?: GainNode;
  loopCount: number;
  volume: number;
  muted: boolean;
  playing: boolean;
  desiredStart: boolean;
  ended: boolean;
  error: boolean;
  offsetSeconds: number;
  startedAt: number;
  decodedBytes: number;
  lastTouchedAt: number;
};

type MidiNote = {
  start: number;
  end: number;
  note: number;
  velocity: number;
};

const MAX_MEDIA_BYTES = 64 * 1024 * 1024;
const MAX_TOTAL_MEDIA_BYTES = 128 * 1024 * 1024;
const MAX_DECODED_MEDIA_BYTES = 96 * 1024 * 1024;
const MAX_MIDI_SECONDS = 10 * 60;
const MIDI_SAMPLE_RATE = 22_050;
const textEncoder = new TextEncoder();
const textDecoder = new TextDecoder();

function audioBufferBytes(buffer?: AudioBuffer) {
  if (!buffer) return 0;
  return buffer.length * Math.max(1, buffer.numberOfChannels) * 4;
}

function normalizeContentType(value: string) {
  return value.split(";", 1)[0].trim().toLowerCase();
}

function isMidiType(type: string, locator?: string) {
  const normalized = normalizeContentType(type);
  if (normalized === "audio/midi" || normalized === "audio/x-midi" || normalized === "audio/sp-midi") return true;
  return /\.(?:mid|midi)(?:$|[?#])/i.test(locator ?? "");
}

function writeU32(view: DataView, offset: number, value: number) {
  view.setUint32(offset, value >>> 0, true);
}

function readU32(view: DataView, offset: number) {
  return view.getUint32(offset, true);
}

function encodeProxyRequest(url: string) {
  const method = textEncoder.encode("GET");
  const target = textEncoder.encode(url);
  const headers = new Uint8Array();
  const body = new Uint8Array();
  const prefix = 20;
  const output = new Uint8Array(prefix + method.length + target.length);
  output.set([0x50, 0x4d, 0x48, 0x31], 0); // PMH1
  const view = new DataView(output.buffer);
  writeU32(view, 4, method.length);
  writeU32(view, 8, target.length);
  writeU32(view, 12, headers.length);
  writeU32(view, 16, body.length);
  output.set(method, prefix);
  output.set(target, prefix + method.length);
  return output;
}

function decodeProxyResponse(bytes: Uint8Array) {
  if (bytes.length < 24 || bytes[0] !== 0x50 || bytes[1] !== 0x4d || bytes[2] !== 0x52 || bytes[3] !== 0x31) {
    throw new Error("HTTP proxy returned an invalid response envelope");
  }
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const status = view.getInt32(4, true);
  const urlLength = readU32(view, 8);
  const reasonLength = readU32(view, 12);
  const headerLength = readU32(view, 16);
  const bodyLength = readU32(view, 20);
  const bodyOffset = 24 + urlLength + reasonLength + headerLength;
  if (bodyOffset > bytes.length || bodyLength > bytes.length - bodyOffset) {
    throw new Error("HTTP proxy response envelope is truncated");
  }
  if (status < 200 || status >= 400) {
    const reasonOffset = 24 + urlLength;
    const reason = textDecoder.decode(bytes.subarray(reasonOffset, reasonOffset + reasonLength));
    throw new Error(`HTTP ${status}${reason ? ` ${reason}` : ""}`);
  }
  return bytes.slice(bodyOffset, bodyOffset + bodyLength);
}

async function fetchMediaBytes(locator: string, signal?: AbortSignal) {
  try {
    const direct = await fetch(locator, { cache: "no-store", signal });
    if (direct.ok) {
      const bytes = new Uint8Array(await direct.arrayBuffer());
      if (bytes.length > MAX_MEDIA_BYTES) throw new Error("Media response is too large");
      return bytes;
    }
  } catch {
    // Cross-origin and mixed-content requests are retried through the phoneME HTTP bridge.
  }

  const response = await fetch("/api/http", {
    method: "POST",
    headers: { "Content-Type": "application/octet-stream" },
    body: encodeProxyRequest(locator),
    cache: "no-store",
    signal
  });
  if (!response.ok) throw new Error(`HTTP bridge ${response.status}`);
  const bytes = decodeProxyResponse(new Uint8Array(await response.arrayBuffer()));
  if (bytes.length > MAX_MEDIA_BYTES) throw new Error("Media response is too large");
  return bytes;
}

function readMidiVlq(bytes: Uint8Array, cursor: { value: number }, end: number) {
  let result = 0;
  for (let count = 0; count < 4; count += 1) {
    if (cursor.value >= end) throw new Error("Truncated MIDI variable length value");
    const byte = bytes[cursor.value++];
    result = (result << 7) | (byte & 0x7f);
    if ((byte & 0x80) === 0) return result;
  }
  return result;
}

function parseMidi(bytes: Uint8Array) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const tag = (offset: number) => String.fromCharCode(bytes[offset], bytes[offset + 1], bytes[offset + 2], bytes[offset + 3]);
  if (bytes.length < 14 || tag(0) !== "MThd") throw new Error("Unsupported MIDI data");
  const headerLength = view.getUint32(4, false);
  const trackCount = view.getUint16(10, false);
  const division = view.getUint16(12, false);
  if ((division & 0x8000) !== 0 || division === 0) throw new Error("SMPTE MIDI timing is not supported");

  const tempos: Array<{ tick: number; microseconds: number }> = [{ tick: 0, microseconds: 500_000 }];
  const notes: Array<{ startTick: number; endTick: number; note: number; velocity: number }> = [];
  let offset = 8 + headerLength;

  for (let track = 0; track < trackCount && offset + 8 <= bytes.length; track += 1) {
    if (tag(offset) !== "MTrk") break;
    const trackLength = view.getUint32(offset + 4, false);
    const cursor = { value: offset + 8 };
    const end = Math.min(bytes.length, cursor.value + trackLength);
    let tick = 0;
    let runningStatus = 0;
    const active = new Map<string, Array<{ tick: number; velocity: number }>>();

    while (cursor.value < end) {
      tick += readMidiVlq(bytes, cursor, end);
      if (cursor.value >= end) break;
      let status = bytes[cursor.value];
      if ((status & 0x80) !== 0) {
        cursor.value += 1;
        runningStatus = status;
      } else {
        if (!runningStatus) throw new Error("Invalid MIDI running status");
        status = runningStatus;
      }

      if (status === 0xff) {
        runningStatus = 0;
        if (cursor.value >= end) break;
        const metaType = bytes[cursor.value++];
        const length = readMidiVlq(bytes, cursor, end);
        if (cursor.value + length > end) break;
        if (metaType === 0x51 && length === 3) {
          const microseconds = (bytes[cursor.value] << 16) | (bytes[cursor.value + 1] << 8) | bytes[cursor.value + 2];
          if (microseconds > 0) tempos.push({ tick, microseconds });
        }
        cursor.value += length;
        continue;
      }

      if (status === 0xf0 || status === 0xf7) {
        runningStatus = 0;
        const length = readMidiVlq(bytes, cursor, end);
        cursor.value = Math.min(end, cursor.value + length);
        continue;
      }

      const kind = status & 0xf0;
      const channel = status & 0x0f;
      const dataLength = kind === 0xc0 || kind === 0xd0 ? 1 : 2;
      if (cursor.value + dataLength > end) break;
      const a = bytes[cursor.value++];
      const b = dataLength === 2 ? bytes[cursor.value++] : 0;
      if (kind !== 0x80 && kind !== 0x90) continue;
      const key = `${channel}:${a}`;
      const noteOn = kind === 0x90 && b > 0;
      if (noteOn) {
        const stack = active.get(key) ?? [];
        stack.push({ tick, velocity: b });
        active.set(key, stack);
      } else {
        const stack = active.get(key);
        const started = stack?.shift();
        if (started) notes.push({ startTick: started.tick, endTick: Math.max(started.tick + 1, tick), note: a, velocity: started.velocity });
        if (stack && stack.length === 0) active.delete(key);
      }
    }
    offset = end;
  }

  tempos.sort((a, b) => a.tick - b.tick);
  const timeline: Array<{ tick: number; seconds: number; microseconds: number }> = [];
  let lastTick = 0;
  let seconds = 0;
  let currentTempo = 500_000;
  for (const tempo of tempos) {
    if (tempo.tick < lastTick) continue;
    seconds += ((tempo.tick - lastTick) * currentTempo) / division / 1_000_000;
    timeline.push({ tick: tempo.tick, seconds, microseconds: tempo.microseconds });
    lastTick = tempo.tick;
    currentTempo = tempo.microseconds;
  }
  if (timeline.length === 0) timeline.push({ tick: 0, seconds: 0, microseconds: 500_000 });

  const tickToSeconds = (tick: number) => {
    let segment = timeline[0];
    for (let index = 1; index < timeline.length && timeline[index].tick <= tick; index += 1) segment = timeline[index];
    return segment.seconds + ((tick - segment.tick) * segment.microseconds) / division / 1_000_000;
  };

  const rendered: MidiNote[] = notes.map((note) => ({
    start: tickToSeconds(note.startTick),
    end: tickToSeconds(note.endTick),
    note: note.note,
    velocity: note.velocity
  }));
  const duration = rendered.reduce((maximum, note) => Math.max(maximum, note.end), 0);
  return { notes: rendered, duration };
}

function renderMidi(context: AudioContext, bytes: Uint8Array) {
  const parsed = parseMidi(bytes);
  if (!(parsed.duration > 0) || parsed.duration > MAX_MIDI_SECONDS) throw new Error("MIDI duration is unsupported");
  const frames = Math.ceil((parsed.duration + 0.1) * MIDI_SAMPLE_RATE);
  const buffer = context.createBuffer(1, frames, MIDI_SAMPLE_RATE);
  const output = buffer.getChannelData(0);

  for (const event of parsed.notes) {
    const start = Math.max(0, Math.floor(event.start * MIDI_SAMPLE_RATE));
    const end = Math.min(output.length, Math.ceil(event.end * MIDI_SAMPLE_RATE));
    const frequency = 440 * Math.pow(2, (event.note - 69) / 12);
    const amplitude = 0.12 * (event.velocity / 127);
    const attack = Math.max(1, Math.floor(0.008 * MIDI_SAMPLE_RATE));
    const release = Math.max(1, Math.floor(0.03 * MIDI_SAMPLE_RATE));
    for (let frame = start; frame < end; frame += 1) {
      const local = frame - start;
      const remaining = end - frame;
      const envelope = Math.min(1, local / attack, remaining / release);
      output[frame] += Math.sin((2 * Math.PI * frequency * local) / MIDI_SAMPLE_RATE) * amplitude * envelope;
    }
  }
  for (let index = 0; index < output.length; index += 1) output[index] = Math.max(-1, Math.min(1, output[index]));
  return buffer;
}

class PhoneMEWebMediaBridge {
  private context: AudioContext | null = null;
  private entries = new Map<number, MediaEntry>();
  private tones = new Set<OscillatorNode>();
  private nextHandle = 1;
  private decodeQueue: Promise<void> = Promise.resolve();
  private contextPrimed = false;

  constructor() {
    const unlock = () => { void this.unlock(); };
    window.addEventListener("pointerdown", unlock, { passive: true });
    window.addEventListener("keydown", unlock, { passive: true });
    window.addEventListener("touchend", unlock, { passive: true });
    document.addEventListener("visibilitychange", () => {
      if (!document.hidden) void this.context?.resume().catch(() => undefined);
    });
  }

  createData(data: Uint8Array, contentType: string) {
    if (!data?.byteLength || data.byteLength > MAX_MEDIA_BYTES) return 0;
    if (!this.ensureBudget(data.byteLength, 0)) return 0;
    const handle = this.allocateHandle();
    const entry: MediaEntry = {
      handle,
      contentType: normalizeContentType(contentType),
      bytes: data,
      loopCount: 1,
      volume: 100,
      muted: false,
      playing: false,
      desiredStart: false,
      ended: false,
      error: false,
      offsetSeconds: 0,
      startedAt: 0,
      decodedBytes: 0,
      lastTouchedAt: performance.now()
    };
    this.entries.set(handle, entry);
    this.prepare(entry);
    return handle;
  }

  createLocator(locator: string, contentType: string) {
    if (!locator) return 0;
    const handle = this.allocateHandle();
    const entry: MediaEntry = {
      handle,
      contentType: normalizeContentType(contentType),
      locator,
      loopCount: 1,
      volume: 100,
      muted: false,
      playing: false,
      desiredStart: false,
      ended: false,
      error: false,
      offsetSeconds: 0,
      startedAt: 0,
      decodedBytes: 0,
      lastTouchedAt: performance.now()
    };
    this.entries.set(handle, entry);
    this.prepare(entry);
    return handle;
  }

  start(handle: number) {
    const entry = this.entries.get(handle);
    if (!entry) return 0;
    entry.lastTouchedAt = performance.now();
    entry.desiredStart = true;
    entry.ended = false;
    entry.error = false;
    void this.unlock();
    if (!entry.buffer) {
      this.prepare(entry);
      return 1;
    }
    return this.startPrepared(entry);
  }

  stop(handle: number) {
    const entry = this.entries.get(handle);
    if (!entry) return 0;
    entry.lastTouchedAt = performance.now();
    entry.desiredStart = false;
    this.captureOffset(entry);
    this.stopSource(entry, false);
    return 1;
  }

  close(handle: number) {
    const entry = this.entries.get(handle);
    if (!entry) return;
    entry.desiredStart = false;
    entry.abortController?.abort();
    entry.abortController = undefined;
    this.stopSource(entry, false);
    this.entries.delete(handle);
  }

  reset() {
    for (const entry of this.entries.values()) {
      entry.desiredStart = false;
      entry.abortController?.abort();
      entry.abortController = undefined;
      this.stopSource(entry, false);
    }
    this.entries.clear();
    this.decodeQueue = Promise.resolve();
    this.contextPrimed = false;
    for (const oscillator of this.tones) {
      oscillator.onended = null;
      try { oscillator.stop(); } catch { /* Already stopped. */ }
      try { oscillator.disconnect(); } catch { /* Already disconnected. */ }
    }
    this.tones.clear();
    const context = this.context;
    this.context = null;
    if (context && context.state !== "closed") void context.close().catch(() => undefined);
  }

  setLoopCount(handle: number, count: number) {
    const entry = this.entries.get(handle);
    if (!entry || count === 0 || count < -1) return;
    entry.loopCount = count;
  }

  setVolume(handle: number, level: number) {
    const entry = this.entries.get(handle);
    if (!entry) return;
    entry.volume = Math.max(0, Math.min(100, level));
    this.applyGain(entry);
  }

  setMute(handle: number, muted: number | boolean) {
    const entry = this.entries.get(handle);
    if (!entry) return;
    entry.muted = Boolean(muted);
    this.applyGain(entry);
  }

  setTime(handle: number, microseconds: number) {
    const entry = this.entries.get(handle);
    if (!entry) return -1;
    const wasPlaying = entry.playing || entry.desiredStart;
    entry.desiredStart = wasPlaying;
    this.stopSource(entry, false);
    const duration = entry.buffer?.duration ?? Number.POSITIVE_INFINITY;
    entry.offsetSeconds = Math.max(0, Math.min(duration, microseconds / 1_000_000));
    entry.ended = false;
    if (wasPlaying && entry.buffer) this.startPrepared(entry);
    return Math.round(entry.offsetSeconds * 1_000_000);
  }

  getTime(handle: number) {
    const entry = this.entries.get(handle);
    if (!entry) return -1;
    if (!entry.playing || !entry.buffer || !this.context) return Math.round(entry.offsetSeconds * 1_000_000);
    const duration = entry.buffer.duration;
    const elapsed = Math.max(0, this.context.currentTime - entry.startedAt);
    const absolute = entry.offsetSeconds + elapsed;
    const position = duration > 0 && entry.loopCount !== 1 ? absolute % duration : Math.min(duration, absolute);
    return Math.round(position * 1_000_000);
  }

  getDuration(handle: number) {
    const entry = this.entries.get(handle);
    return entry?.buffer ? Math.round(entry.buffer.duration * 1_000_000) : -1;
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

  memoryUsage() {
    let compressedBytes = 0;
    let decodedBytes = 0;
    for (const entry of this.entries.values()) {
      compressedBytes += entry.bytes?.byteLength ?? 0;
      decodedBytes += entry.decodedBytes;
    }
    return {
      entries: this.entries.size,
      compressedBytes,
      decodedBytes,
      totalBytes: compressedBytes + decodedBytes
    };
  }

  playTone(note: number, durationMilliseconds: number, volume: number) {
    if (note < 0 || note > 127 || durationMilliseconds <= 0) return 0;
    const context = this.ensureContext();
    void this.unlock();
    const oscillator = context.createOscillator();
    const gain = context.createGain();
    oscillator.frequency.value = 440 * Math.pow(2, (note - 69) / 12);
    oscillator.type = "sine";
    gain.gain.value = Math.max(0, Math.min(100, volume)) / 100 * 0.22;
    oscillator.connect(gain).connect(context.destination);
    this.tones.add(oscillator);
    oscillator.onended = () => {
      this.tones.delete(oscillator);
      try { oscillator.disconnect(); } catch { /* Already disconnected. */ }
      try { gain.disconnect(); } catch { /* Already disconnected. */ }
    };
    const now = context.currentTime;
    oscillator.start(now);
    oscillator.stop(now + durationMilliseconds / 1000);
    return 1;
  }

  async unlock() {
    const context = this.ensureContext();
    if (context.state !== "running") await context.resume();
    if (context.state !== "running" || this.contextPrimed) return;
    this.contextPrimed = true;
    const buffer = context.createBuffer(1, 1, context.sampleRate);
    const source = context.createBufferSource();
    source.buffer = buffer;
    source.connect(context.destination);
    source.onended = () => {
      source.onended = null;
      try { source.disconnect(); } catch { /* Already disconnected. */ }
    };
    source.start();
  }

  private allocateHandle() {
    let handle = this.nextHandle++;
    if (this.nextHandle > 0x7fffffff) this.nextHandle = 1;
    while (this.entries.has(handle)) handle = this.nextHandle++;
    return handle;
  }

  private ensureContext() {
    if (!this.context || this.context.state === "closed") {
      const Constructor = window.AudioContext ?? (window as typeof window & { webkitAudioContext?: typeof AudioContext }).webkitAudioContext;
      if (!Constructor) throw new Error("Web Audio API is unavailable");
      this.context = new Constructor({ latencyHint: "interactive" });
      this.contextPrimed = false;
    }
    return this.context;
  }

  private prepare(entry: MediaEntry) {
    if (entry.loading || entry.buffer || entry.error) return;
    const abortController = entry.bytes ? undefined : new AbortController();
    entry.abortController = abortController;
    const decode = async () => {
      if (this.entries.get(entry.handle) !== entry) return;
      try {
        const context = this.ensureContext();
        const bytes = entry.bytes ?? await fetchMediaBytes(entry.locator!, abortController?.signal);
        if (this.entries.get(entry.handle) !== entry) return;
        let decoded: AudioBuffer;
        if (isMidiType(entry.contentType, entry.locator)) {
          decoded = renderMidi(context, bytes);
        } else {
          const copy = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength) as ArrayBuffer;
          decoded = await context.decodeAudioData(copy);
        }
        if (this.entries.get(entry.handle) !== entry) return;
        const decodedBytes = audioBufferBytes(decoded);
        if (!this.ensureBudget(0, decodedBytes, entry.handle)) {
          throw new Error("Media decode vượt ngân sách bộ nhớ an toàn");
        }
        entry.buffer = decoded;
        entry.decodedBytes = decodedBytes;
        entry.lastTouchedAt = performance.now();
        if (entry.desiredStart) this.startPrepared(entry);
      } catch (error) {
        if (this.entries.get(entry.handle) === entry) {
          entry.error = true;
          entry.playing = false;
          entry.desiredStart = false;
        }
        console.warn("phoneME media decode failed", error);
      } finally {
        if (entry.abortController === abortController) entry.abortController = undefined;
      }
    };
    const queued = this.decodeQueue.catch(() => undefined).then(decode);
    entry.loading = queued.finally(() => {
      if (entry.loading) entry.loading = undefined;
    });
    this.decodeQueue = entry.loading.catch(() => undefined);
  }

  private startPrepared(entry: MediaEntry) {
    entry.lastTouchedAt = performance.now();
    const context = this.ensureContext();
    const buffer = entry.buffer;
    if (!buffer) return 1;
    this.stopSource(entry, false);

    const source = context.createBufferSource();
    const gain = context.createGain();
    source.buffer = buffer;
    source.loop = entry.loopCount !== 1;
    source.connect(gain).connect(context.destination);
    entry.source = source;
    entry.gain = gain;
    this.applyGain(entry);

    const duration = Math.max(0.000_001, buffer.duration);
    const offset = Math.max(0, Math.min(duration, entry.offsetSeconds));
    entry.offsetSeconds = offset;
    entry.startedAt = context.currentTime;
    entry.playing = true;
    entry.desiredStart = true;
    entry.ended = false;

    source.onended = () => {
      if (entry.source !== source) return;
      entry.source = undefined;
      entry.gain = undefined;
      entry.playing = false;
      entry.lastTouchedAt = performance.now();
      if (entry.desiredStart) {
        entry.offsetSeconds = duration;
        entry.ended = true;
        entry.desiredStart = false;
      }
    };

    try {
      source.start(0, offset >= duration ? 0 : offset);
      if (entry.loopCount > 1) {
        const totalSeconds = (duration - (offset >= duration ? 0 : offset)) + duration * (entry.loopCount - 1);
        source.stop(context.currentTime + Math.max(0.001, totalSeconds));
      }
      return 1;
    } catch (error) {
      entry.error = true;
      entry.playing = false;
      entry.desiredStart = false;
      console.warn("phoneME media playback failed", error);
      return 0;
    }
  }

  private captureOffset(entry: MediaEntry) {
    if (!entry.playing || !entry.buffer || !this.context) return;
    const duration = entry.buffer.duration;
    const elapsed = Math.max(0, this.context.currentTime - entry.startedAt);
    const absolute = entry.offsetSeconds + elapsed;
    entry.offsetSeconds = duration > 0 && entry.loopCount !== 1 ? absolute % duration : Math.min(duration, absolute);
  }

  private stopSource(entry: MediaEntry, ended: boolean) {
    entry.lastTouchedAt = performance.now();
    const source = entry.source;
    entry.source = undefined;
    entry.gain = undefined;
    entry.playing = false;
    if (source) {
      source.onended = null;
      try { source.stop(); } catch { /* Already stopped. */ }
      try { source.disconnect(); } catch { /* Already disconnected. */ }
    }
    if (ended) entry.ended = true;
  }

  private ensureBudget(extraCompressedBytes: number, extraDecodedBytes: number, excludeHandle = 0) {
    const fits = () => {
      const usage = this.memoryUsage();
      return usage.decodedBytes + extraDecodedBytes <= MAX_DECODED_MEDIA_BYTES &&
        usage.totalBytes + extraCompressedBytes + extraDecodedBytes <= MAX_TOTAL_MEDIA_BYTES;
    };
    if (fits()) return true;

    const candidates = [...this.entries.values()]
      .filter((candidate) =>
        candidate.handle !== excludeHandle &&
        Boolean(candidate.buffer) &&
        !candidate.playing &&
        !candidate.desiredStart &&
        !candidate.source &&
        !candidate.loading)
      .sort((left, right) => left.lastTouchedAt - right.lastTouchedAt);
    for (const candidate of candidates) {
      candidate.buffer = undefined;
      candidate.decodedBytes = 0;
      if (fits()) return true;
    }
    return fits();
  }

  private applyGain(entry: MediaEntry) {
    if (!entry.gain) return;
    entry.gain.gain.value = entry.muted ? 0 : entry.volume / 100;
  }
}

export type PhoneMEGlobalMediaBridge = PhoneMEWebMediaBridge;

export function installWebMediaBridge() {
  const target = window as typeof window & { __phoneMEMediaBridge?: PhoneMEWebMediaBridge };
  if (!target.__phoneMEMediaBridge) target.__phoneMEMediaBridge = new PhoneMEWebMediaBridge();
  return target.__phoneMEMediaBridge;
}

declare global {
  interface Window {
    __phoneMEMediaBridge?: PhoneMEWebMediaBridge;
  }
}
