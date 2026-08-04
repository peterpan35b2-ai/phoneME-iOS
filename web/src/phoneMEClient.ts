import type { FrameData, PhoneMEOptions } from "./phoneME";
import type { GameEntry, JarMetadata, LcduiEvent } from "./types";

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
  event?: "log";
  line?: string;
  isError?: boolean;
};

type PendingRequest = {
  resolve: (value: unknown) => void;
  reject: (reason?: unknown) => void;
};

export type RuntimeTick = {
  events: LcduiEvent[];
  frame: FrameData | null;
  state: number;
  usedMemory: number;
};

export class PhoneMEWebRuntime {
  private worker: Worker | null = null;
  private nextRequestId = 0;
  private pending = new Map<number, PendingRequest>();
  private initialized = false;
  private currentGameValue: GameEntry | null = null;
  private onLog?: PhoneMEOptions["onLog"];

  get ready() {
    return this.initialized && this.worker !== null;
  }

  get activeGame() {
    return this.currentGameValue;
  }

  async initialize(options: PhoneMEOptions = {}) {
    if (this.ready) return;
    if (!globalThis.crossOriginIsolated || typeof SharedArrayBuffer === "undefined") {
      throw new Error("WebAssembly đa luồng cần COOP/COEP. Hãy chạy bằng Vite hoặc máy chủ có header cách ly chéo nguồn.");
    }

    this.onLog = options.onLog;
    const worker = new Worker(new URL("./phoneme.worker.ts", import.meta.url), {
      type: "module",
      name: "phoneME Web runtime"
    });
    this.worker = worker;
    worker.addEventListener("message", this.handleMessage);
    worker.addEventListener("error", this.handleWorkerError);
    worker.addEventListener("messageerror", this.handleWorkerMessageError);

    try {
      await this.request("initialize", {
        websocketProxyUrl: options.websocketProxyUrl
      });
      this.initialized = true;
    } catch (error) {
      this.dispose();
      throw error;
    }
  }

  async installJar(file: File, metadata: JarMetadata): Promise<GameEntry> {
    return await this.request<GameEntry>("installJar", { file, metadata });
  }

  async uninstall(game: GameEntry, removeData: boolean) {
    await this.request("uninstall", { game, removeData });
    if (this.currentGameValue?.id === game.id) this.currentGameValue = null;
  }

  async launch(game: GameEntry, width: number, height: number) {
    await this.request("launch", { game, width, height });
    this.currentGameValue = game;
  }

  async resize(width: number, height: number) {
    if (!this.currentGameValue) return;
    await this.request("resize", { width, height });
  }

  async pause() {
    if (!this.currentGameValue) return;
    await this.request("pause");
  }

  async resume() {
    if (!this.currentGameValue) return;
    await this.request("resume");
  }

  async stopMidlet() {
    if (!this.currentGameValue) return;
    await this.request("stopMidlet");
    this.currentGameValue = null;
  }

  async tick(previousGeneration: bigint, includeFrame: boolean): Promise<RuntimeTick> {
    return await this.request<RuntimeTick>("tick", {
      previousGeneration,
      includeFrame
    });
  }

  async copyLcduiImage(componentId: number): Promise<FrameData | null> {
    return await this.request<FrameData | null>("copyLcduiImage", { componentId });
  }

  sendKey(keyCode: number, pressed: boolean) {
    this.notify("sendKey", { keyCode, pressed });
  }

  sendPointer(x: number, y: number, action: number) {
    this.notify("sendPointer", { x, y, action });
  }

  selectCommand(commandId: number) {
    this.notify("selectCommand", { commandId });
  }

  selectListItemCommand(componentId: number, elementIndex: number, commandId: number) {
    this.notify("selectListItemCommand", { componentId, elementIndex, commandId });
  }

  focusItem(componentId: number) {
    this.notify("focusItem", { componentId });
  }

  activateItem(componentId: number) {
    this.notify("activateItem", { componentId });
  }

  setText(componentId: number, value: string, caret: number) {
    this.notify("setText", { componentId, value, caret });
  }

  setChoice(componentId: number, index: number, selected: boolean) {
    this.notify("setChoice", { componentId, index, selected });
  }

  setGauge(componentId: number, value: number) {
    this.notify("setGauge", { componentId, value });
  }

  setDate(componentId: number, unixSeconds: number) {
    this.notify("setDate", { componentId, unixSeconds });
  }

  setScrollPosition(position: number) {
    this.notify("setScrollPosition", { position });
  }

  async flushStorage() {
    if (!this.worker) return;
    await this.request("flushStorage");
  }

  dispose() {
    const worker = this.worker;
    if (!worker) return;
    worker.removeEventListener("message", this.handleMessage);
    worker.removeEventListener("error", this.handleWorkerError);
    worker.removeEventListener("messageerror", this.handleWorkerMessageError);
    worker.postMessage({ id: 0, type: "dispose" } satisfies WorkerRequest);
    worker.terminate();
    this.worker = null;
    this.initialized = false;
    this.currentGameValue = null;
    for (const { reject } of this.pending.values()) {
      reject(new Error("phoneME Web runtime đã đóng"));
    }
    this.pending.clear();
  }

  private request<T = void>(type: string, payload?: unknown): Promise<T> {
    const worker = this.worker;
    if (!worker) return Promise.reject(new Error("phoneME Web chưa sẵn sàng"));
    const id = ++this.nextRequestId;
    worker.postMessage({ id, type, payload } satisfies WorkerRequest);
    return new Promise<T>((resolve, reject) => {
      this.pending.set(id, {
        resolve: (value) => resolve(value as T),
        reject
      });
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
    if (!message.id) return;
    const pending = this.pending.get(message.id);
    if (!pending) return;
    this.pending.delete(message.id);
    if (message.ok) pending.resolve(message.result);
    else pending.reject(new Error(message.error || "Lỗi phoneME Web Worker"));
  };

  private handleWorkerError = (event: ErrorEvent) => {
    const error = new Error(event.message || "phoneME Web Worker bị lỗi");
    for (const { reject } of this.pending.values()) reject(error);
    this.pending.clear();
  };

  private handleWorkerMessageError = () => {
    const error = new Error("Không đọc được phản hồi từ phoneME Web Worker");
    for (const { reject } of this.pending.values()) reject(error);
    this.pending.clear();
  };
}
