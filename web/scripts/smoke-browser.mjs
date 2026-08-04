import { spawn } from "node:child_process";
import { access } from "node:fs/promises";
import path from "node:path";
import process from "node:process";

const root = path.resolve(import.meta.dirname, "..");
const customJarPath = process.argv[2];
const jarPath = path.resolve(
  customJarPath ??
    path.join(
      root,
      "../Core/build/core-audit-canvas-graphics6/canvas-graphics-host-tests.task.90018.qux7Zj/canvas-graphics-fixture.jar"
    )
);
const chromePath = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome";
const port = 4175;
const debuggingPort = 9227;

await access(jarPath);
await access(chromePath);

const preview = spawn(
  "npm",
  ["run", "preview", "--", "--host", "127.0.0.1", "--port", String(port)],
  { cwd: root, stdio: ["ignore", "pipe", "pipe"] }
);
const chrome = spawn(
  chromePath,
  [
    "--headless=new",
    "--disable-gpu",
    "--no-first-run",
    "--no-default-browser-check",
    `--remote-debugging-port=${debuggingPort}`,
    `--user-data-dir=/tmp/phoneme-web-smoke-${process.pid}`,
    `http://127.0.0.1:${port}`
  ],
  { stdio: ["ignore", "pipe", "pipe"] }
);

let browserErrors = "";
chrome.stderr.on("data", (chunk) => {
  browserErrors += String(chunk);
});

const sleep = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));

async function waitForJson(url, timeoutMs = 15_000) {
  const deadline = Date.now() + timeoutMs;
  let lastError;
  while (Date.now() < deadline) {
    try {
      const response = await fetch(url);
      if (response.ok) return await response.json();
    } catch (error) {
      lastError = error;
    }
    await sleep(100);
  }
  throw lastError ?? new Error(`Timed out waiting for ${url}`);
}

console.log("[smoke] waiting for Chrome");
const pages = await waitForJson(`http://127.0.0.1:${debuggingPort}/json`);
const page = pages.find((entry) => entry.type === "page");
if (!page?.webSocketDebuggerUrl) throw new Error("Chrome page target not found");

const socket = new WebSocket(page.webSocketDebuggerUrl);
await new Promise((resolve, reject) => {
  socket.addEventListener("open", resolve, { once: true });
  socket.addEventListener("error", reject, { once: true });
});

let commandId = 0;
const pending = new Map();
const exceptions = [];
socket.addEventListener("message", (event) => {
  const message = JSON.parse(String(event.data));
  if (message.id) {
    const callback = pending.get(message.id);
    if (callback) {
      pending.delete(message.id);
      if (message.error) callback.reject(new Error(message.error.message));
      else callback.resolve(message.result);
    }
    return;
  }
  if (message.method === "Runtime.exceptionThrown") {
    exceptions.push(message.params.exceptionDetails);
  }
});

function command(method, params = {}) {
  const id = ++commandId;
  socket.send(JSON.stringify({ id, method, params }));
  return new Promise((resolve, reject) => pending.set(id, { resolve, reject }));
}

async function evaluate(expression) {
  const result = await command("Runtime.evaluate", {
    expression,
    awaitPromise: true,
    returnByValue: true
  });
  if (result.exceptionDetails) {
    throw new Error(result.exceptionDetails.text || "Runtime.evaluate failed");
  }
  return result.result?.value;
}

async function waitForExpression(expression, timeoutMs = 20_000) {
  const deadline = Date.now() + timeoutMs;
  let lastValue;
  while (Date.now() < deadline) {
    lastValue = await evaluate(expression);
    if (lastValue) return lastValue;
    await sleep(100);
  }
  throw new Error(`Timed out waiting for expression: ${expression}\nLast value: ${String(lastValue)}`);
}

let exitCode = 0;
try {
  console.log("[smoke] connected to DevTools");
  await command("Runtime.enable");
  await command("DOM.enable");
  await command("Page.enable");

  console.log("[smoke] waiting for runtime");
  await waitForExpression("document.body?.innerText.includes('phoneME Web đã sẵn sàng')");
  console.log("[smoke] runtime ready");
  const documentNode = await command("DOM.getDocument", { depth: -1, pierce: true });
  const inputNode = await command("DOM.querySelector", {
    nodeId: documentNode.root.nodeId,
    selector: "input[type=file]"
  });
  if (!inputNode.nodeId) throw new Error("JAR file input not found");
  console.log("[smoke] selecting JAR");
  await command("DOM.setFileInputFiles", {
    files: [jarPath],
    nodeId: inputNode.nodeId
  });

  console.log("[smoke] waiting for installation");
  await waitForExpression("document.body?.innerText.includes('1 ứng dụng đã cài')", 30_000);
  console.log("[smoke] installed");
  const installedTitle = await evaluate(`(() => {
    const card = document.querySelector('.game-card');
    const titled = card?.querySelector('[title]');
    return titled?.getAttribute('title') || titled?.textContent?.trim() || '';
  })()`);
  if (!installedTitle) throw new Error("Installed MIDlet title not found");
  await evaluate(`(() => {
    const button = [...document.querySelectorAll('button')].find((element) => element.textContent?.trim() === 'Chạy');
    if (!button) return false;
    button.click();
    return true;
  })()`);

  console.log("[smoke] launching MIDlet");
  const expectedTitleLiteral = JSON.stringify(installedTitle);
  const strictFixture = !customJarPath;
  const result = await waitForExpression(`(() => {
    const body = document.body?.innerText || '';
    const error = body.includes('Không thể chạy ứng dụng');
    const canvas = document.querySelector('canvas.emulator-canvas');
    const lcdui = document.querySelector('.native-lcdui');
    const running = body.includes('Đang chạy ' + ${expectedTitleLiteral});
    if (error) return { status: 'error', body };
    if (canvas && running && canvas.width === 240 && canvas.height === 320) {
      return { status: 'canvas', width: canvas.width, height: canvas.height, body };
    }
    if (${strictFixture ? "false" : "true"} && lcdui && running) {
      return { status: 'lcdui', body };
    }
    return null;
  })()`, 30_000);

  if (result.status === "error") {
    throw new Error(`MIDlet launch failed:\n${result.body}`);
  }
  if (exceptions.length) {
    throw new Error(`Browser exceptions: ${JSON.stringify(exceptions, null, 2)}`);
  }

  console.log(JSON.stringify({ ok: true, jarPath, result }, null, 2));
} catch (error) {
  exitCode = 1;
  console.error(error instanceof Error ? error.stack : String(error));
  try {
    const diagnostic = await evaluate(`(() => ({
      body: document.body?.innerText || '',
      canvas: (() => {
        const element = document.querySelector('canvas.emulator-canvas');
        return element ? { width: element.width, height: element.height } : null;
      })(),
      live: document.querySelector('.sr-only')?.textContent || ''
    }))()`);
    console.error("[smoke] diagnostic", JSON.stringify(diagnostic, null, 2));
  } catch (diagnosticError) {
    console.error("[smoke] diagnostic failed", String(diagnosticError));
  }
} finally {
  socket.close();
  chrome.kill("SIGTERM");
  preview.kill("SIGTERM");
  await sleep(200);
  if (/TypeError|ReferenceError|RuntimeError/.test(browserErrors)) {
    console.error(browserErrors);
  }
  process.exit(exitCode);
}
