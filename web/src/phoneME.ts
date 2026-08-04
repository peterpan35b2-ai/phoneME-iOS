import type { GameEntry, JarMetadata, LcduiEvent } from "./types";

type EmscriptenFs = {
  filesystems: { IDBFS: unknown };
  mkdir(path: string): void;
  mount(type: unknown, options: Record<string, unknown>, mountpoint: string): void;
  syncfs(populate: boolean, callback: (error?: unknown) => void): void;
  writeFile(path: string, data: Uint8Array): void;
  unlink(path: string): void;
};

type PhoneMEModule = {
  FS: EmscriptenFs;
  HEAPU8: Uint8Array;
  HEAP32: Int32Array;
  _malloc(size: number): number;
  _free(pointer: number): void;
  UTF8ToString(pointer: number): string;
  stringToUTF8(value: string, pointer: number, capacity: number): void;
  lengthBytesUTF8(value: string): number;
  _phoneme_c_api_version(): number;
  _phoneme_create(): number;
  _phoneme_destroy(runtime: number): void;
  _phoneme_configure(runtime: number, runtimeHome: number, classArchive: number): number;
  _phoneme_configure_keymap(runtime: number, up: number, down: number, left: number, right: number, fire: number, soft1: number, soft2: number): number;
  _phoneme_configure_translation(runtime: number, enabled: number, source: number, target: number): number;
  _phoneme_install_jar(runtime: number, jarPath: number, suiteIdOut: number): number;
  _phoneme_uninstall_suite(runtime: number, suiteId: number, removeData: number): number;
  _phoneme_set_suite_trust(runtime: number, suiteId: number, trust: number): number;
  _phoneme_start_system(runtime: number): number;
  _phoneme_start_midlet(runtime: number, suiteId: number, mainClass: number, appId: number, width: number, height: number): number;
  _phoneme_set_foreground(runtime: number, appId: number, width: number, height: number): number;
  _phoneme_pause_midlet(runtime: number, appId: number): number;
  _phoneme_resume_midlet(runtime: number, appId: number): number;
  _phoneme_destroy_midlet(runtime: number, appId: number): number;
  _phoneme_midlet_state(runtime: number, appId: number): number;
  _phoneme_midlet_used_memory(runtime: number, appId: number, timeout: number): bigint;
  _phoneme_send_key(runtime: number, keyCode: number, pressed: number): void;
  _phoneme_send_pointer(runtime: number, x: number, y: number, action: number): void;
  _phoneme_pump_events(runtime: number): void;
  _phoneme_copy_frame_rgba(runtime: number, destination: number, capacity: number, width: number, height: number, generation: number): number;
  _phoneme_copy_lcdui_image_rgba(runtime: number, componentId: number, destination: number, capacity: number, width: number, height: number, generation: number): number;
  _phoneme_web_poll_lcdui_event_json(runtime: number): number;
  _phoneme_web_error_name(code: number): number;
  _phoneme_lcdui_select_command(runtime: number, commandId: number): void;
  _phoneme_lcdui_select_list_item_command(runtime: number, componentId: number, elementIndex: number, commandId: number): void;
  _phoneme_lcdui_focus_item(runtime: number, componentId: number): void;
  _phoneme_lcdui_activate_item(runtime: number, componentId: number): void;
  _phoneme_lcdui_set_text(runtime: number, componentId: number, text: number, caret: number): void;
  _phoneme_lcdui_set_choice(runtime: number, componentId: number, elementIndex: number, selected: number): void;
  _phoneme_lcdui_set_gauge(runtime: number, componentId: number, value: number): void;
  _phoneme_lcdui_set_date(runtime: number, componentId: number, value: bigint): void;
  _phoneme_lcdui_set_scroll_position(runtime: number, position: number): void;
};

type ModuleFactory = (options: Record<string, unknown>) => Promise<PhoneMEModule>;

export type FrameData = {
  pixels: Uint8ClampedArray;
  width: number;
  height: number;
  generation: bigint;
};

export type PhoneMEOptions = {
  websocketProxyUrl?: string;
  onLog?: (line: string, error: boolean) => void;
};

const APP_ID = 1;
const STORAGE_ROOT = "/phoneme";
const RUNTIME_HOME = `${STORAGE_ROOT}/runtime`;
const IMPORT_ROOT = `${STORAGE_ROOT}/imports`;

function sanitizeName(value: string) {
  const normalized = value.normalize("NFKD").replace(/[^a-zA-Z0-9._-]+/g, "-");
  return normalized.replace(/^-+|-+$/g, "").slice(0, 80) || "game.jar";
}

export class PhoneMEWebRuntime {
  private module: PhoneMEModule | null = null;
  private runtime = 0;
  private metadataPointer = 0;
  private framePointer = 0;
  private frameCapacity = 0;
  private currentGame: GameEntry | null = null;
  private initialized = false;
  private flushPromise: Promise<void> | null = null;

  get ready() {
    return this.initialized && this.module !== null && this.runtime !== 0;
  }

  get activeGame() {
    return this.currentGame;
  }

  async initialize(options: PhoneMEOptions = {}) {
    if (this.ready) return;
    if (!globalThis.crossOriginIsolated || typeof SharedArrayBuffer === "undefined") {
      throw new Error("WebAssembly đa luồng cần COOP/COEP. Hãy chạy bằng Vite hoặc máy chủ có header cách ly chéo nguồn.");
    }

    const moduleUrl = new URL("/wasm/phoneme.js", globalThis.location.href).href;
    const imported = await import(/* @vite-ignore */ moduleUrl) as { default: ModuleFactory };
    this.module = await imported.default({
      locateFile: (path: string) => new URL(path, moduleUrl).href,
      print: (line: string) => options.onLog?.(String(line), false),
      printErr: (line: string) => options.onLog?.(String(line), true),
      websocket: options.websocketProxyUrl ? { url: options.websocketProxyUrl } : undefined
    });

    this.ensureDirectory(STORAGE_ROOT);
    try {
      this.module.FS.mount(this.module.FS.filesystems.IDBFS, {}, STORAGE_ROOT);
    } catch (error) {
      if (!String(error).toLowerCase().includes("busy")) throw error;
    }
    await this.syncFileSystem(true);
    this.ensureDirectory(RUNTIME_HOME);
    this.ensureDirectory(IMPORT_ROOT);

    this.runtime = this.module._phoneme_create();
    if (!this.runtime) throw new Error("Không tạo được phoneME runtime");
    this.metadataPointer = this.module._malloc(16);

    const configureCode = this.withCString(RUNTIME_HOME, (home) =>
      this.module!._phoneme_configure(this.runtime, home, 0)
    );
    this.assertOk(configureCode, "Cấu hình core");
    this.assertOk(
      this.module._phoneme_configure_keymap(this.runtime, -1, -2, -3, -4, -5, -6, -7),
      "Cấu hình phím"
    );
    this.assertOk(this.module._phoneme_start_system(this.runtime), "Khởi động hệ thống J2ME");
    this.initialized = true;
  }

  async installJar(file: File, metadata: JarMetadata): Promise<GameEntry> {
    const module = this.requireModule();
    const id = crypto.randomUUID();
    const fileName = sanitizeName(file.name.toLowerCase().endsWith(".jar") ? file.name : `${file.name}.jar`);
    const path = `${IMPORT_ROOT}/${id}-${fileName}`;
    module.FS.writeFile(path, new Uint8Array(await file.arrayBuffer()));

    const suitePointer = module._malloc(4);
    try {
      const result = this.withCString(path, (jarPath) =>
        module._phoneme_install_jar(this.runtime, jarPath, suitePointer)
      );
      this.assertOk(result, "Cài đặt JAR");
      const suiteId = module.HEAP32[suitePointer >> 2];
      this.assertOk(module._phoneme_set_suite_trust(this.runtime, suiteId, 1), "Cấp quyền suite");
      await this.flushStorage();
      return {
        id,
        suiteId,
        title: metadata.title,
        vendor: metadata.vendor,
        version: metadata.version,
        mainClass: metadata.mainClass,
        fileName,
        installedAt: Date.now(),
        iconDataUrl: metadata.iconDataUrl
      };
    } finally {
      module._free(suitePointer);
      try {
        module.FS.unlink(path);
      } catch {
        // The managed suite store already owns its private JAR copy.
      }
      void this.flushStorage();
    }
  }

  async uninstall(game: GameEntry, removeData: boolean) {
    const module = this.requireModule();
    if (this.currentGame?.id === game.id) this.stopMidlet();
    this.assertOk(
      module._phoneme_uninstall_suite(this.runtime, game.suiteId, removeData ? 1 : 0),
      "Gỡ ứng dụng"
    );
    await this.flushStorage();
  }

  launch(game: GameEntry, width: number, height: number) {
    const module = this.requireModule();
    if (this.currentGame) {
      module._phoneme_destroy_midlet(this.runtime, APP_ID);
      this.currentGame = null;
    }
    const result = this.withCString(game.mainClass, (mainClass) =>
      module._phoneme_start_midlet(this.runtime, game.suiteId, mainClass, APP_ID, width, height)
    );
    this.assertOk(result, `Mở ${game.title}`);
    this.currentGame = game;
  }

  resize(width: number, height: number) {
    if (!this.currentGame) return;
    this.assertOk(
      this.requireModule()._phoneme_set_foreground(this.runtime, APP_ID, width, height),
      "Đổi kích thước màn hình"
    );
  }

  pause() {
    if (!this.currentGame) return;
    this.assertOk(this.requireModule()._phoneme_pause_midlet(this.runtime, APP_ID), "Tạm dừng");
  }

  resume() {
    if (!this.currentGame) return;
    this.assertOk(this.requireModule()._phoneme_resume_midlet(this.runtime, APP_ID), "Tiếp tục");
  }

  stopMidlet() {
    if (!this.currentGame) return;
    this.requireModule()._phoneme_destroy_midlet(this.runtime, APP_ID);
    this.currentGame = null;
    void this.flushStorage();
  }

  state() {
    return this.ready ? this.requireModule()._phoneme_midlet_state(this.runtime, APP_ID) : 0;
  }

  usedMemory() {
    if (!this.currentGame) return 0;
    const value = this.requireModule()._phoneme_midlet_used_memory(this.runtime, APP_ID, 0);
    return value < 0n ? 0 : Number(value);
  }

  sendKey(keyCode: number, pressed: boolean) {
    this.requireModule()._phoneme_send_key(this.runtime, keyCode, pressed ? 1 : 0);
  }

  sendPointer(x: number, y: number, action: number) {
    this.requireModule()._phoneme_send_pointer(this.runtime, x, y, action);
  }

  pump() {
    this.requireModule()._phoneme_pump_events(this.runtime);
  }

  copyFrame(previousGeneration: bigint): FrameData | null {
    const module = this.requireModule();
    const widthPointer = this.metadataPointer;
    const heightPointer = this.metadataPointer + 4;
    const generationPointer = this.metadataPointer + 8;
    const required = module._phoneme_copy_frame_rgba(
      this.runtime,
      0,
      0,
      widthPointer,
      heightPointer,
      generationPointer
    );
    const view = new DataView(module.HEAPU8.buffer);
    const width = view.getInt32(widthPointer, true);
    const height = view.getInt32(heightPointer, true);
    const generation = view.getBigUint64(generationPointer, true);
    if (required <= 0 || width <= 0 || height <= 0 || generation === previousGeneration) return null;

    this.ensureFrameCapacity(required);
    const copied = module._phoneme_copy_frame_rgba(
      this.runtime,
      this.framePointer,
      this.frameCapacity,
      widthPointer,
      heightPointer,
      generationPointer
    );
    if (copied <= 0) return null;
    const pixels = new Uint8ClampedArray(module.HEAPU8.slice(this.framePointer, this.framePointer + copied).buffer);
    return { pixels, width, height, generation };
  }

  copyLcduiImage(componentId: number): FrameData | null {
    const module = this.requireModule();
    const widthPointer = this.metadataPointer;
    const heightPointer = this.metadataPointer + 4;
    const generationPointer = this.metadataPointer + 8;
    const required = module._phoneme_copy_lcdui_image_rgba(
      this.runtime,
      componentId,
      0,
      0,
      widthPointer,
      heightPointer,
      generationPointer
    );
    if (required <= 0) return null;
    this.ensureFrameCapacity(required);
    const copied = module._phoneme_copy_lcdui_image_rgba(
      this.runtime,
      componentId,
      this.framePointer,
      this.frameCapacity,
      widthPointer,
      heightPointer,
      generationPointer
    );
    if (copied <= 0) return null;
    const view = new DataView(module.HEAPU8.buffer);
    return {
      pixels: new Uint8ClampedArray(module.HEAPU8.slice(this.framePointer, this.framePointer + copied).buffer),
      width: view.getInt32(widthPointer, true),
      height: view.getInt32(heightPointer, true),
      generation: view.getBigUint64(generationPointer, true)
    };
  }

  pollLcduiEvents(limit = 256) {
    const module = this.requireModule();
    const events: LcduiEvent[] = [];
    while (events.length < limit) {
      const pointer = module._phoneme_web_poll_lcdui_event_json(this.runtime);
      if (!pointer) break;
      events.push(JSON.parse(module.UTF8ToString(pointer)) as LcduiEvent);
    }
    return events;
  }

  selectCommand(commandId: number) {
    this.requireModule()._phoneme_lcdui_select_command(this.runtime, commandId);
  }

  selectListItemCommand(componentId: number, elementIndex: number, commandId: number) {
    this.requireModule()._phoneme_lcdui_select_list_item_command(this.runtime, componentId, elementIndex, commandId);
  }

  focusItem(componentId: number) {
    this.requireModule()._phoneme_lcdui_focus_item(this.runtime, componentId);
  }

  activateItem(componentId: number) {
    this.requireModule()._phoneme_lcdui_activate_item(this.runtime, componentId);
  }

  setText(componentId: number, value: string, caret: number) {
    this.withCString(value, (text) => {
      this.requireModule()._phoneme_lcdui_set_text(this.runtime, componentId, text, caret);
      return 0;
    });
  }

  setChoice(componentId: number, index: number, selected: boolean) {
    this.requireModule()._phoneme_lcdui_set_choice(this.runtime, componentId, index, selected ? 1 : 0);
  }

  setGauge(componentId: number, value: number) {
    this.requireModule()._phoneme_lcdui_set_gauge(this.runtime, componentId, value);
  }

  setDate(componentId: number, unixSeconds: number) {
    this.requireModule()._phoneme_lcdui_set_date(this.runtime, componentId, BigInt(Math.trunc(unixSeconds)));
  }

  setScrollPosition(position: number) {
    this.requireModule()._phoneme_lcdui_set_scroll_position(this.runtime, position);
  }

  async flushStorage() {
    if (!this.module) return;
    if (!this.flushPromise) {
      this.flushPromise = this.syncFileSystem(false).finally(() => {
        this.flushPromise = null;
      });
    }
    await this.flushPromise;
  }

  dispose() {
    if (!this.module) return;
    if (this.currentGame) this.module._phoneme_destroy_midlet(this.runtime, APP_ID);
    if (this.runtime) this.module._phoneme_destroy(this.runtime);
    if (this.framePointer) this.module._free(this.framePointer);
    if (this.metadataPointer) this.module._free(this.metadataPointer);
    this.runtime = 0;
    this.framePointer = 0;
    this.metadataPointer = 0;
    this.module = null;
    this.initialized = false;
    this.currentGame = null;
  }

  private requireModule() {
    if (!this.module || !this.runtime) throw new Error("phoneME Web chưa sẵn sàng");
    return this.module;
  }

  private assertOk(code: number, action: string) {
    if (code === 0) return;
    const module = this.requireModule();
    const messagePointer = module._phoneme_web_error_name(code);
    const message = messagePointer ? module.UTF8ToString(messagePointer) : `mã ${code}`;
    throw new Error(`${action}: ${message}`);
  }

  private withCString<T>(value: string, body: (pointer: number) => T) {
    const module = this.module;
    if (!module) throw new Error("phoneME Web chưa nạp module");
    const capacity = module.lengthBytesUTF8(value) + 1;
    const pointer = module._malloc(capacity);
    if (!pointer) throw new Error("WebAssembly hết bộ nhớ");
    try {
      module.stringToUTF8(value, pointer, capacity);
      return body(pointer);
    } finally {
      module._free(pointer);
    }
  }

  private ensureFrameCapacity(required: number) {
    const module = this.requireModule();
    if (required <= this.frameCapacity) return;
    if (this.framePointer) module._free(this.framePointer);
    this.frameCapacity = Math.max(required, Math.ceil(required * 1.25));
    this.framePointer = module._malloc(this.frameCapacity);
    if (!this.framePointer) throw new Error("Không cấp phát được framebuffer WebAssembly");
  }

  private ensureDirectory(path: string) {
    if (!this.module) return;
    try {
      this.module.FS.mkdir(path);
    } catch (error) {
      const message = String(error).toLowerCase();
      if (!message.includes("exist")) throw error;
    }
  }

  private syncFileSystem(populate: boolean) {
    if (!this.module) return Promise.resolve();
    return new Promise<void>((resolve, reject) => {
      this.module!.FS.syncfs(populate, (error) => error ? reject(error) : resolve());
    });
  }
}
