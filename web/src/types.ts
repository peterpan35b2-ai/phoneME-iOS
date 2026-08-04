export type ViewId = "library" | "emulator" | "settings";

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
  disabled: boolean;
  commandId: number;
};

export type LcduiItem = {
  id: number;
  parentId: number;
  type: number;
  label: string;
  detail: string;
  visible: boolean;
  index: number;
  arg0: number;
  arg1: number;
  arg2: number;
  arg3: number;
  value64: number;
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
};

export type LcduiScreen = {
  id: number;
  type: number;
  title: string;
  detail: string;
  visible: boolean;
  index: number;
  arg0: number;
  arg1: number;
  arg2: number;
  arg3: number;
  value64: number;
  generation: number;
  items: LcduiItem[];
  commands: LcduiCommand[];
  focusedItemId: number | null;
};
