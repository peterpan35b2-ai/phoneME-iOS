export type ViewId = "library" | "configure" | "emulator" | "settings" | "storage";

export type ManagedStorageKind = "files" | "rms";

export type ManagedStorageEntry = {
  name: string;
  path: string;
  isDirectory: boolean;
  size: number;
  modifiedAt: number;
};

export type ManagedStorageExport = {
  name: string;
  isDirectory: boolean;
  files: Array<{
    path: string;
    data: Uint8Array<ArrayBuffer>;
  }>;
  archive?: Blob;
};

export type ThemePreference = "system" | "light" | "dark";

export type GameEntry = {
  id: string;
  suiteId: number;
  title: string;
  vendor: string;
  version: string;
  mainClass: string;
  fileName: string;
  installedAt: number;
  iconDataUrl?: string;
};

export type JarMetadata = {
  title: string;
  vendor: string;
  version: string;
  mainClass: string;
  iconDataUrl?: string;
};

export type RuntimePhase =
  | "idle"
  | "loading"
  | "ready"
  | "running"
  | "paused"
  | "error";

export type RuntimeSnapshot = {
  phase: RuntimePhase;
  message: string;
  fps: number;
  usedMemory: number;
  frameWidth: number;
  frameHeight: number;
};

export type LcduiEvent = {
  kind: number;
  componentId: number;
  parentId: number;
  componentType: number;
  index: number;
  arg0: number;
  arg1: number;
  arg2: number;
  arg3: number;
  value64: number;
  generation: number;
  text: string;
  detail: string;
};

export type LcduiChoice = {
  index: number;
  text: string;
  selected: boolean;
  imageKey: number | null;
  fontFace: number;
  fontStyle: number;
  fontSize: number;
  generation: number;
};

export type LcduiFrame = {
  x: number;
  y: number;
  width: number;
  height: number;
};

export type LcduiItem = {
  id: number;
  parentId: number;
  formIndex: number;
  type: number;
  label: string;
  text: string;
  frame: LcduiFrame;
  visible: boolean;
  layout: number;
  appearanceMode: number;
  fitPolicy: number;
  focused: boolean;
  maxSize: number;
  constraints: number;
  caretPosition: number;
  value: number;
  maxValue: number;
  interactive: boolean;
  inputMode: number;
  fontFace: number;
  fontStyle: number;
  fontSize: number;
  dateUnixSeconds: number;
  imageWidth: number;
  imageHeight: number;
  imageGeneration: number;
  generation: number;
  choices: LcduiChoice[];
};

export type LcduiCommand = {
  id: number;
  ownerId: number;
  label: string;
  longLabel: string;
  commandType: number;
  priority: number;
  scope: number;
  order: number;
};

export type LcduiScreen = {
  id: number;
  type: number;
  title: string;
  detail: string;
  visible: boolean;
  contentWidth: number;
  contentHeight: number;
  scrollPosition: number;
  fullScreen: boolean;
  nativeKind: number | null;
  generation: number;
  items: LcduiItem[];
  commands: LcduiCommand[];
  focusedItemId: number | null;
};
