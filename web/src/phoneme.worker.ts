/// <reference lib="webworker" />

import { PhoneMEWebRuntime } from "./phoneME";
import type { GameEntry, JarMetadata } from "./types";

const scope = self as unknown as DedicatedWorkerGlobalScope;
const runtime = new PhoneMEWebRuntime();

type RequestMessage = {
  id: number;
  type: string;
  payload?: any;
};

type ResponseMessage = {
  id: number;
  ok: boolean;
  result?: unknown;
  error?: string;
};

function postResponse(message: ResponseMessage, transfer: Transferable[] = []) {
  scope.postMessage(message, transfer);
}

function postLog(line: string, isError: boolean) {
  scope.postMessage({ event: "log", line, isError });
}

function frameTransfer(frame: ReturnType<PhoneMEWebRuntime["copyFrame"]>) {
  return frame ? [frame.pixels.buffer as ArrayBuffer] : [];
}

async function handleRequest(message: RequestMessage) {
  const payload = message.payload ?? {};
  switch (message.type) {
  case "initialize":
    await runtime.initialize({
      websocketProxyUrl: payload.websocketProxyUrl,
      onLog: postLog
    });
    return undefined;
  case "installJar":
    return await runtime.installJar(
      payload.file as File,
      payload.metadata as JarMetadata
    );
  case "uninstall":
    await runtime.uninstall(payload.game as GameEntry, Boolean(payload.removeData));
    return undefined;
  case "launch":
    runtime.launch(
      payload.game as GameEntry,
      Number(payload.width),
      Number(payload.height)
    );
    return undefined;
  case "resize":
    runtime.resize(Number(payload.width), Number(payload.height));
    return undefined;
  case "pause":
    runtime.pause();
    return undefined;
  case "resume":
    runtime.resume();
    return undefined;
  case "stopMidlet":
    runtime.stopMidlet();
    return undefined;
  case "tick": {
    const includeFrame = Boolean(payload.includeFrame);
    if (!includeFrame) runtime.pump();
    const frame = includeFrame
      ? runtime.copyFrame(BigInt(payload.previousGeneration ?? 0))
      : null;
    return {
      events: runtime.pollLcduiEvents(),
      frame,
      state: runtime.state(),
      usedMemory: runtime.usedMemory()
    };
  }
  case "copyLcduiImage":
    return runtime.copyLcduiImage(Number(payload.componentId));
  case "sendKey":
    runtime.sendKey(Number(payload.keyCode), Boolean(payload.pressed));
    return undefined;
  case "sendPointer":
    runtime.sendPointer(Number(payload.x), Number(payload.y), Number(payload.action));
    return undefined;
  case "selectCommand":
    runtime.selectCommand(Number(payload.commandId));
    return undefined;
  case "selectListItemCommand":
    runtime.selectListItemCommand(
      Number(payload.componentId),
      Number(payload.elementIndex),
      Number(payload.commandId)
    );
    return undefined;
  case "focusItem":
    runtime.focusItem(Number(payload.componentId));
    return undefined;
  case "activateItem":
    runtime.activateItem(Number(payload.componentId));
    return undefined;
  case "setText":
    runtime.setText(
      Number(payload.componentId),
      String(payload.value ?? ""),
      Number(payload.caret)
    );
    return undefined;
  case "setChoice":
    runtime.setChoice(
      Number(payload.componentId),
      Number(payload.index),
      Boolean(payload.selected)
    );
    return undefined;
  case "setGauge":
    runtime.setGauge(Number(payload.componentId), Number(payload.value));
    return undefined;
  case "setDate":
    runtime.setDate(Number(payload.componentId), Number(payload.unixSeconds));
    return undefined;
  case "setScrollPosition":
    runtime.setScrollPosition(Number(payload.position));
    return undefined;
  case "flushStorage":
    await runtime.flushStorage();
    return undefined;
  case "dispose":
    runtime.dispose();
    scope.close();
    return undefined;
  default:
    throw new Error(`Lệnh Web Worker không hỗ trợ: ${message.type}`);
  }
}

scope.addEventListener("message", (event: MessageEvent<RequestMessage>) => {
  const message = event.data;
  void Promise.resolve()
    .then(() => handleRequest(message))
    .then((result) => {
      if (!message.id) return;
      const transfer = result && typeof result === "object" && "frame" in result
        ? frameTransfer((result as { frame: ReturnType<PhoneMEWebRuntime["copyFrame"]> }).frame)
        : result && typeof result === "object" && "pixels" in result
          ? [(result as { pixels: Uint8ClampedArray }).pixels.buffer as ArrayBuffer]
          : [];
      postResponse({ id: message.id, ok: true, result }, transfer);
    })
    .catch((error) => {
      if (!message.id) {
        postLog(error instanceof Error ? error.stack || error.message : String(error), true);
        return;
      }
      postResponse({
        id: message.id,
        ok: false,
        error: error instanceof Error ? error.message : String(error)
      });
    });
});
