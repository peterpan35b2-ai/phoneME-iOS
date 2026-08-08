import {
  PhoneMEWebRuntime as DirectPhoneMEWebRuntime,
  type FrameData,
  type PhoneMEOptions
} from "./phoneME";
import type { GameEntry, JarMetadata, LcduiEvent } from "./types";
import { installWebMediaBridge } from "./webMediaBridge";

type WorkerRequest = {
  id: number;
  type: string;
  payload?: unknown;
};

type WorkerResponse = {
  id?: number;
  ok?: boolean;
  result?: unknown;
  error?: string;
  fatal?: boolean;
  event?: "log" | "media";
  line?: string;
  isError?: boolean;
  action?: string;
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

type PendingRequest = {
  resolve: (value: unknown) => void;
  reject: (reason?: unknown) => void;
};

export type RuntimeTick = {
  events: LcduiEvent[];
  frame: FrameData | null;
};

function isIOS16WebKit() {
  const navigatorValue = globalThis.navigator;
  if (!navigatorValue) return false;

  const userAgent = navigatorValue.userAgent;
  const iOSDevice = /iP(?:hone|ad|od)/.test(userAgent) ||
    (navigatorValue.platform === "MacIntel" && navigatorValue.maxTouchPoints > 1);
  if (!iOSDevice) return false;

  const osVersion = userAgent.match(/(?:CPU (?:iPhone )?OS|OS) (\d+)[_.]/)?.[1];
  const safariVersion = userAgent.match(/Version\/(\d+)(?:\.|\s)/)?.[1];
  const majorVersion = Number(osVersion ?? safariVersion ?? 0);
  return majorVersion === 16;
}

export class PhoneMEWebRuntime {
  private worker: Worker | null = null;
  private directRuntime: DirectPhoneMEWebRuntime | null = null;
  private nextRequestId = 0;
  private pending = new Map<number, PendingRequest>();
  private initialized = false;
  private initializePromise: Promise<void> | null = null;
  private initializeOptions: PhoneMEOptions = {};
  private currentGameValue: GameEntry | null = null;
  private onLog?: PhoneMEOptions["onLog"];
  private mediaHandles = new Map<number, number>();
  private mediaStatusTimer: number | null = null;

  get ready() {
    return this.initialized && (this.worker !== null || this.directRuntime?.ready === true);
  }

  get activeGame() {
    return this.directRuntime?.activeGame ?? this.currentGameValue;
  }

  async initialize(options?: PhoneMEOptions) {
    if (options) this.initializeOptions = options;
    if (this.ready) return;
    if (this.initializePromise) return await this.initializePromise;

    this.initializePromise = this.initializeOnce(this.initializeOptions);
    try {
      await this.initializePromise;
    } finally {
      this.initializePromise = null;
    }
  }

  private async initializeOnce(options: PhoneMEOptions) {
    if (!globalThis.crossOriginIsolated || typeof SharedArrayBuffer === "undefined") {
      throw new Error("WebAssembly đa luồng cần COOP/COEP. Hãy chạy bằng Vite hoặc máy chủ có header cách ly chéo nguồn.");
    }

    if (this.worker || this.directRuntime) {
      this.invalidateRuntime(new Error("Đang khởi tạo lại phoneME Web runtime"));
    }
    this.onLog = options.onLog;

    // Safari iOS 16 cannot reliably host Emscripten's pthread pool inside an
    // additional module Worker. Keep the compatibility path there, but run the
    // core off the browser UI thread everywhere else so file/RMS/network or VM
    // work cannot freeze React and input handling.
    if (isIOS16WebKit()) {
      const runtime = new DirectPhoneMEWebRuntime();
      this.directRuntime = runtime;
      this.onLog?.("Safari iOS 16: bật chế độ WebAssembly tương thích.", false);
      try {
        await runtime.initialize(options);
        if (this.directRuntime !== runtime) {
          throw new Error("phoneME Web runtime đã bị thay thế khi đang khởi tạo");
        }
        this.initialized = true;
        return;
      } catch (error) {
        const reason = error instanceof Error ? error : new Error(String(error));
        if (this.directRuntime === runtime) this.invalidateRuntime(reason);
        throw reason;
      }
    }

    const worker = new Worker(new URL("./phoneme.worker.ts", import.meta.url), { type: "module" });
    this.worker = worker;
    worker.addEventListener("message", this.handleMessage);
    worker.addEventListener("error", this.handleWorkerError);
    worker.addEventListener("messageerror", this.handleWorkerMessageError);
    try {
      await this.request("initialize", { websocketProxyUrl: options.websocketProxyUrl });
      if (this.worker !== worker) {
        throw new Error("phoneME Web Worker đã bị thay thế khi đang khởi tạo");
      }
      this.initialized = true;
    } catch (error) {
      const reason = error instanceof Error ? error : new Error(String(error));
      if (this.worker === worker) this.invalidateRuntime(reason);
      throw reason;
    }
  }

  async installJar(file: File, metadata: JarMetadata): Promise<GameEntry> {
    await this.ensureReady();
    if (this.directRuntime) return await this.directRuntime.installJar(file, metadata);
    return await this.request<GameEntry>("installJar", { file, metadata });
  }

  async uninstall(game: GameEntry, removeData: boolean) {
    await this.ensureReady();
    if (this.directRuntime) {
      await this.directRuntime.uninstall(game, removeData);
      return;
    }
    await this.request("uninstall", { game, removeData });
    if (this.currentGameValue?.id === game.id) this.currentGameValue = null;
  }

  async launch(game: GameEntry, width: number, height: number) {
    await this.ensureReady();
    if (this.directRuntime) {
      await this.directRuntime.launch(game, width, height);
      return;
    }
    await this.request("launch", { game, width, height });
    this.currentGameValue = game;
  }

  async resize(width: number, height: number) {
    if (this.directRuntime) {
      this.directRuntime.resize(width, height);
      return;
    }
    if (!this.currentGameValue) return;
    await this.request("resize", { width, height });
  }

  async configureHeap(heapMegabytes: number) {
    await this.ensureReady();
    if (this.directRuntime) {
      this.directRuntime.configureHeap(heapMegabytes);
      return;
    }
    await this.request("configureHeap", { heapMegabytes });
  }

  async configureFrameRate() {
    await this.ensureReady();
    if (this.directRuntime) {
      this.directRuntime.configureFrameRate();
      return;
    }
    await this.request("configureFrameRate");
  }

  async configureFrameRateOverride(_enabled: boolean, _framesPerSecond: number) {
    await this.configureFrameRate();
  }

  async configureTranslation(enabled: boolean, provider: "google" | "bing" | "automatic", sourceLanguage: string) {
    await this.ensureReady();
    if (this.directRuntime) {
      this.directRuntime.configureTranslation(enabled, provider, sourceLanguage);
      return;
    }
    if (!this.currentGameValue) return;
    await this.request("configureTranslation", { enabled, provider, sourceLanguage });
  }

  async pause() {
    if (this.directRuntime) {
      this.directRuntime.pause();
      return;
    }
    if (!this.currentGameValue) return;
    await this.request("pause");
  }

  async resume() {
    if (this.directRuntime) {
      this.directRuntime.resume();
      return;
    }
    if (!this.currentGameValue) return;
    await this.request("resume");
  }

  async stopMidlet() {
    if (this.directRuntime) {
      this.directRuntime.stopMidlet();
      return;
    }
    if (!this.currentGameValue) return;
    await this.request("stopMidlet");
    this.currentGameValue = null;
  }

  async tick(previousGeneration: bigint, includeFrame: boolean): Promise<RuntimeTick> {
    if (this.directRuntime) {
      if (!includeFrame) this.directRuntime.pump();
      return {
        events: this.directRuntime.pollLcduiEvents(),
        frame: includeFrame ? this.directRuntime.copyFrame(previousGeneration) : null
      };
    }
    return await this.request<RuntimeTick>("tick", {
      previousGeneration,
      includeFrame
    });
  }

  async copyLcduiImage(componentId: number): Promise<FrameData | null> {
    if (this.directRuntime) return this.directRuntime.copyLcduiImage(componentId);
    return await this.request<FrameData | null>("copyLcduiImage", { componentId });
  }

  sendKey(keyCode: number, pressed: boolean) {
    if (this.directRuntime) this.directRuntime.sendKey(keyCode, pressed);
    else this.notify("sendKey", { keyCode, pressed });
  }

  sendPointer(x: number, y: number, action: number) {
    if (this.directRuntime) this.directRuntime.sendPointer(x, y, action);
    else this.notify("sendPointer", { x, y, action });
  }

  selectCommand(commandId: number) {
    if (this.directRuntime) this.directRuntime.selectCommand(commandId);
    else this.notify("selectCommand", { commandId });
  }

  selectListItemCommand(componentId: number, elementIndex: number, commandId: number) {
    if (this.directRuntime) {
      this.directRuntime.selectListItemCommand(componentId, elementIndex, commandId);
    } else {
      this.notify("selectListItemCommand", { componentId, elementIndex, commandId });
    }
  }

  focusItem(componentId: number) {
    if (this.directRuntime) this.directRuntime.focusItem(componentId);
    else this.notify("focusItem", { componentId });
  }

  activateItem(componentId: number) {
    if (this.directRuntime) this.directRuntime.activateItem(componentId);
    else this.notify("activateItem", { componentId });
  }

  setText(componentId: number, value: string, caret: number) {
    if (this.directRuntime) this.directRuntime.setText(componentId, value, caret);
    else this.notify("setText", { componentId, value, caret });
  }

  setChoice(componentId: number, index: number, selected: boolean) {
    if (this.directRuntime) this.directRuntime.setChoice(componentId, index, selected);
    else this.notify("setChoice", { componentId, index, selected });
  }

  setGauge(componentId: number, value: number) {
    if (this.directRuntime) this.directRuntime.setGauge(componentId, value);
    else this.notify("setGauge", { componentId, value });
  }

  setDate(componentId: number, unixSeconds: number) {
    if (this.directRuntime) this.directRuntime.setDate(componentId, unixSeconds);
    else this.notify("setDate", { componentId, unixSeconds });
  }

  setScrollPosition(position: number) {
    if (this.directRuntime) this.directRuntime.setScrollPosition(position);
    else this.notify("setScrollPosition", { position });
  }

  async flushStorage() {
    if (this.directRuntime) {
      await this.directRuntime.flushStorage();
      return;
    }
    if (!this.worker) return;
    await this.request("flushStorage");
  }

  dispose() {
    this.invalidateRuntime(new Error("phoneME Web runtime đã đóng"), true);
  }

  private async ensureReady() {
    if (!this.ready) await this.initialize();
  }

  private request<T = void>(type: string, payload?: unknown): Promise<T> {
    const worker = this.worker;
    if (!worker) return Promise.reject(new Error("phoneME Web chưa sẵn sàng"));
    const id = ++this.nextRequestId;
    return new Promise<T>((resolve, reject) => {
      this.pending.set(id, {
        resolve: (value) => resolve(value as T),
        reject
      });
      try {
        worker.postMessage({ id, type, payload } satisfies WorkerRequest);
      } catch (error) {
        this.pending.delete(id);
        reject(error);
      }
    });
  }

  private notify(type: string, payload?: unknown) {
    this.worker?.postMessage({ id: 0, type, payload } satisfies WorkerRequest);
  }

  private handleMessage = (event: MessageEvent<WorkerResponse>) => {
    const message = event.data;
    if (message.event === "log") {
      this.onLog?.(message.line ?? "", Boolean(message.isError));
      return;
    }
    if (message.event === "media") {
      this.handleMediaCommand(message);
      return;
    }
    if (!message.id) return;
    const pending = this.pending.get(message.id);
    if (!pending) return;
    this.pending.delete(message.id);
    if (message.ok) {
      pending.resolve(message.result);
      return;
    }
    const error = new Error(message.error || "Lỗi phoneME Web Worker");
    pending.reject(error);
    if (message.fatal) this.invalidateRuntime(error);
  };

  private handleMediaCommand(message: WorkerResponse) {
    const worker = this.worker;
    if (!worker || !message.action) return;
    const bridge = installWebMediaBridge();
    const logicalHandle = Number(message.handle ?? 0);
    const nativeHandle = () => this.mediaHandles.get(logicalHandle) ?? 0;

    try {
      switch (message.action) {
      case "createData": {
        const data = message.data instanceof Uint8Array ? message.data : new Uint8Array();
        const handle = bridge.createData(data, String(message.contentType ?? ""));
        if (logicalHandle > 0 && handle > 0) this.mediaHandles.set(logicalHandle, handle);
        break;
      }
      case "createLocator": {
        const handle = bridge.createLocator(String(message.locator ?? ""), String(message.contentType ?? ""));
        if (logicalHandle > 0 && handle > 0) this.mediaHandles.set(logicalHandle, handle);
        break;
      }
      case "start": bridge.start(nativeHandle()); break;
      case "stop": bridge.stop(nativeHandle()); break;
      case "close": {
        const handle = nativeHandle();
        if (handle) bridge.close(handle);
        this.mediaHandles.delete(logicalHandle);
        break;
      }
      case "setLoopCount": bridge.setLoopCount(nativeHandle(), Number(message.count ?? 1)); break;
      case "setVolume": bridge.setVolume(nativeHandle(), Number(message.level ?? 100)); break;
      case "setMute": bridge.setMute(nativeHandle(), message.muted ?? false); break;
      case "setTime": bridge.setTime(nativeHandle(), Number(message.microseconds ?? 0)); break;
      case "playTone":
        bridge.playTone(
          Number(message.note ?? 0),
          Number(message.durationMilliseconds ?? 0),
          Number(message.volume ?? 100)
        );
        break;
      default:
        return;
      }
      if (logicalHandle > 0 && this.mediaHandles.has(logicalHandle)) this.postMediaStatus(logicalHandle);
      this.ensureMediaStatusTimer();
    } catch (error) {
      this.onLog?.(`Web Audio bridge: ${error instanceof Error ? error.message : String(error)}`, true);
      if (logicalHandle > 0) {
        worker.postMessage({ event: "mediaStatus", handle: logicalHandle, error: true });
      }
    }
  }

  private ensureMediaStatusTimer() {
    if (this.mediaStatusTimer !== null || this.mediaHandles.size === 0) return;
    this.mediaStatusTimer = window.setInterval(() => {
      if (!this.worker || this.mediaHandles.size === 0) {
        if (this.mediaStatusTimer !== null) window.clearInterval(this.mediaStatusTimer);
        this.mediaStatusTimer = null;
        return;
      }
      for (const handle of this.mediaHandles.keys()) this.postMediaStatus(handle);
    }, 200);
  }

  private postMediaStatus(logicalHandle: number) {
    const worker = this.worker;
    const nativeHandle = this.mediaHandles.get(logicalHandle);
    if (!worker || !nativeHandle) return;
    const bridge = installWebMediaBridge();
    worker.postMessage({
      event: "mediaStatus",
      handle: logicalHandle,
      duration: bridge.getDuration(nativeHandle),
      time: bridge.getTime(nativeHandle),
      playing: Boolean(bridge.isPlaying(nativeHandle)),
      ended: Boolean(bridge.hasEnded(nativeHandle)),
      error: Boolean(bridge.hasError(nativeHandle))
    });
  }

  private clearMediaBridgeState() {
    if (this.mediaStatusTimer !== null) {
      window.clearInterval(this.mediaStatusTimer);
      this.mediaStatusTimer = null;
    }
    const bridge = installWebMediaBridge();
    for (const handle of this.mediaHandles.values()) bridge.close(handle);
    this.mediaHandles.clear();
  }

  private handleWorkerError = (event: ErrorEvent) => {
    this.invalidateRuntime(new Error(event.message || "phoneME Web Worker bị lỗi"));
  };

  private handleWorkerMessageError = () => {
    this.invalidateRuntime(new Error("Không đọc được phản hồi từ phoneME Web Worker"));
  };

  private invalidateRuntime(reason: Error, notifyWorker = false) {
    this.clearMediaBridgeState();
    const directRuntime = this.directRuntime;
    this.directRuntime = null;
    if (directRuntime) {
      try {
        directRuntime.dispose();
      } catch {
        // A direct Emscripten runtime may already be aborted and not safely disposable.
      }
    }

    const worker = this.worker;
    this.worker = null;
    if (worker) {
      worker.removeEventListener("message", this.handleMessage);
      worker.removeEventListener("error", this.handleWorkerError);
      worker.removeEventListener("messageerror", this.handleWorkerMessageError);
      if (notifyWorker) {
        try {
          worker.postMessage({ id: 0, type: "dispose" } satisfies WorkerRequest);
        } catch {
          // The worker may already be terminated after a fatal WebAssembly abort.
        }
      }
      worker.terminate();
    }

    this.initialized = false;
    this.currentGameValue = null;
    for (const { reject } of this.pending.values()) reject(reason);
    this.pending.clear();
  }
}
