import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import {
  Box,
  Button,
  Chip,
  IconButton,
  Paper,
  Stack,
  Tooltip,
  Typography
} from "@mui/material";
import {
  CloseRounded,
  FullscreenRounded,
  KeyboardArrowDownRounded,
  KeyboardArrowLeftRounded,
  KeyboardArrowRightRounded,
  KeyboardArrowUpRounded,
  PauseRounded,
  PlayArrowRounded,
  RefreshRounded
} from "@mui/icons-material";
import { applyLcduiEvents, EMPTY_LCDUI_STATE, NativeLcduiView, type LcduiState } from "./lcdui";
import type { PhoneMEWebRuntime } from "./phoneMEClient";
import type { GameEntry, RuntimeSnapshot } from "./types";

const KEYBOARD_MAP: Record<string, number> = {
  ArrowUp: -1,
  ArrowDown: -2,
  ArrowLeft: -3,
  ArrowRight: -4,
  Enter: -5,
  " ": -5,
  q: -6,
  Q: -6,
  e: -7,
  E: -7,
  "0": 48,
  "1": 49,
  "2": 50,
  "3": 51,
  "4": 52,
  "5": 53,
  "6": 54,
  "7": 55,
  "8": 56,
  "9": 57,
  "*": 42,
  "#": 35
};

const KEYPAD = [
  ["1", 49], ["2", 50], ["3", 51],
  ["4", 52], ["5", 53], ["6", 54],
  ["7", 55], ["8", 56], ["9", 57],
  ["*", 42], ["0", 48], ["#", 35]
] as const;

function formatBytes(value: number) {
  if (value < 1024) return `${value} B`;
  if (value < 1024 ** 2) return `${(value / 1024).toFixed(1)} KB`;
  return `${(value / 1024 ** 2).toFixed(1)} MB`;
}

function KeyButton({ label, keyCode, runtime, children }: {
  label: string;
  keyCode: number;
  runtime: PhoneMEWebRuntime;
  children?: React.ReactNode;
}) {
  const release = useCallback(() => runtime.sendKey(keyCode, false), [keyCode, runtime]);
  return <Button
    className="virtual-key"
    variant="contained"
    aria-label={label}
    onPointerDown={(event) => {
      event.currentTarget.setPointerCapture(event.pointerId);
      runtime.sendKey(keyCode, true);
    }}
    onPointerUp={release}
    onPointerCancel={release}
    onPointerLeave={(event) => {
      if (event.buttons) release();
    }}
  >{children ?? label}</Button>;
}

function VirtualKeypad({ runtime }: { runtime: PhoneMEWebRuntime }) {
  return <Paper className="virtual-keypad" variant="outlined">
    <Box className="soft-key-row">
      <KeyButton label="Phím mềm trái" keyCode={-6} runtime={runtime}>Menu</KeyButton>
      <KeyButton label="Phím mềm phải" keyCode={-7} runtime={runtime}>Trở lại</KeyButton>
    </Box>
    <Box className="dpad">
      <Box />
      <KeyButton label="Lên" keyCode={-1} runtime={runtime}><KeyboardArrowUpRounded /></KeyButton>
      <Box />
      <KeyButton label="Trái" keyCode={-3} runtime={runtime}><KeyboardArrowLeftRounded /></KeyButton>
      <KeyButton label="Chọn" keyCode={-5} runtime={runtime}>OK</KeyButton>
      <KeyButton label="Phải" keyCode={-4} runtime={runtime}><KeyboardArrowRightRounded /></KeyButton>
      <Box />
      <KeyButton label="Xuống" keyCode={-2} runtime={runtime}><KeyboardArrowDownRounded /></KeyButton>
      <Box />
    </Box>
    <Box className="number-pad">
      {KEYPAD.map(([label, keyCode]) => <KeyButton key={label} label={label} keyCode={keyCode} runtime={runtime} />)}
    </Box>
  </Paper>;
}

export type EmulatorScreenProps = {
  runtime: PhoneMEWebRuntime;
  game: GameEntry;
  width: number;
  height: number;
  onStop: () => void;
  onSnapshot: (snapshot: RuntimeSnapshot) => void;
};

export function EmulatorScreen({ runtime, game, width, height, onStop, onSnapshot }: EmulatorScreenProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const displayRef = useRef<HTMLDivElement>(null);
  const generationRef = useRef(0n);
  const frameCounterRef = useRef({ count: 0, startedAt: performance.now() });
  const pressedKeysRef = useRef(new Set<number>());
  const tickInFlightRef = useRef(false);
  const usedMemoryRef = useRef(0);
  const [lcdui, setLcdui] = useState<LcduiState>(EMPTY_LCDUI_STATE);
  const [paused, setPaused] = useState(false);
  const [runtimeError, setRuntimeError] = useState("");
  const activeScreen = lcdui.activeScreenId ? lcdui.screens[lcdui.activeScreenId] : undefined;
  const hasNativeScreen = Boolean(activeScreen?.visible && activeScreen.type !== 22);

  useEffect(() => {
    let active = true;
    generationRef.current = 0n;
    usedMemoryRef.current = 0;
    setLcdui(EMPTY_LCDUI_STATE);
    setRuntimeError("");
    void runtime.launch(game, width, height).then(() => {
      if (!active) return;
      onSnapshot({
        phase: "running",
        message: `Đang chạy ${game.title}`,
        fps: 0,
        usedMemory: 0,
        frameWidth: width,
        frameHeight: height
      });
    }).catch((error) => {
      if (!active) return;
      const message = error instanceof Error ? error.message : String(error);
      setRuntimeError(message);
      onSnapshot({ phase: "error", message, fps: 0, usedMemory: 0, frameWidth: width, frameHeight: height });
    });
    return () => {
      active = false;
      for (const keyCode of pressedKeysRef.current) runtime.sendKey(keyCode, false);
      pressedKeysRef.current.clear();
    };
  }, [game, height, onSnapshot, runtime, width]);

  useEffect(() => {
    void runtime.resize(width, height).catch((error) => {
      setRuntimeError(error instanceof Error ? error.message : String(error));
    });
  }, [height, runtime, width]);

  useEffect(() => {
    const handleDown = (event: KeyboardEvent) => {
      const keyCode = KEYBOARD_MAP[event.key];
      if (keyCode === undefined || event.repeat) return;
      if ((event.target as HTMLElement | null)?.matches("input, textarea, select, [contenteditable=true]")) return;
      event.preventDefault();
      pressedKeysRef.current.add(keyCode);
      runtime.sendKey(keyCode, true);
    };
    const handleUp = (event: KeyboardEvent) => {
      const keyCode = KEYBOARD_MAP[event.key];
      if (keyCode === undefined) return;
      event.preventDefault();
      pressedKeysRef.current.delete(keyCode);
      runtime.sendKey(keyCode, false);
    };
    window.addEventListener("keydown", handleDown);
    window.addEventListener("keyup", handleUp);
    return () => {
      window.removeEventListener("keydown", handleDown);
      window.removeEventListener("keyup", handleUp);
    };
  }, [runtime]);

  useEffect(() => {
    let animationFrame = 0;
    let active = true;
    let lastStatsAt = performance.now();
    const tick = () => {
      if (!paused && !runtimeError && !tickInFlightRef.current) {
        tickInFlightRef.current = true;
        void runtime.tick(generationRef.current, !hasNativeScreen).then((result) => {
          if (!active) return;
          usedMemoryRef.current = result.usedMemory;
          if (result.events.length) {
            setLcdui((current) => applyLcduiEvents(current, result.events));
          }
          const frame = result.frame;
          const canvas = canvasRef.current;
          if (frame && canvas) {
            generationRef.current = frame.generation;
            canvas.width = frame.width;
            canvas.height = frame.height;
            canvas.getContext("2d", { alpha: false })?.putImageData(
              new ImageData(Uint8ClampedArray.from(frame.pixels), frame.width, frame.height),
              0,
              0
            );
            frameCounterRef.current.count += 1;
          }
        }).catch((error) => {
          if (active) setRuntimeError(error instanceof Error ? error.message : String(error));
        }).finally(() => {
          tickInFlightRef.current = false;
        });
      }

      const now = performance.now();
      if (now - lastStatsAt >= 1000) {
        const elapsed = Math.max(1, now - frameCounterRef.current.startedAt);
        const fps = frameCounterRef.current.count * 1000 / elapsed;
        frameCounterRef.current = { count: 0, startedAt: now };
        lastStatsAt = now;
        onSnapshot({
          phase: runtimeError ? "error" : paused ? "paused" : "running",
          message: runtimeError || (paused ? "Đã tạm dừng" : `Đang chạy ${game.title}`),
          fps,
          usedMemory: usedMemoryRef.current,
          frameWidth: width,
          frameHeight: height
        });
      }
      animationFrame = requestAnimationFrame(tick);
    };
    animationFrame = requestAnimationFrame(tick);
    return () => {
      active = false;
      cancelAnimationFrame(animationFrame);
    };
  }, [game.title, hasNativeScreen, height, onSnapshot, paused, runtime, runtimeError, width]);

  const setPauseState = () => {
    const operation = paused ? runtime.resume() : runtime.pause();
    void operation.then(() => {
      setPaused((current) => !current);
    }).catch((error) => {
      setRuntimeError(error instanceof Error ? error.message : String(error));
    });
  };

  const stop = () => {
    void runtime.stopMidlet();
    onStop();
  };

  const pointerPosition = (event: React.PointerEvent<HTMLCanvasElement>) => {
    const canvas = event.currentTarget;
    const bounds = canvas.getBoundingClientRect();
    return {
      x: Math.max(0, Math.min(canvas.width - 1, Math.round((event.clientX - bounds.left) * canvas.width / bounds.width))),
      y: Math.max(0, Math.min(canvas.height - 1, Math.round((event.clientY - bounds.top) * canvas.height / bounds.height)))
    };
  };

  const statusChips = useMemo(() => [
    `${width} × ${height}`,
    hasNativeScreen ? "LCDUI native" : "Canvas",
    paused ? "Tạm dừng" : "Đang chạy"
  ], [hasNativeScreen, height, paused, width]);

  return <Box className="emulator-layout">
    <Paper className="emulator-stage-card" elevation={0}>
      <Stack className="emulator-toolbar" direction="row" sx={{ alignItems: "center", justifyContent: "space-between" }}>
        <Stack sx={{ minWidth: 0 }}>
          <Typography variant="subtitle1" noWrap>{game.title}</Typography>
          <Typography variant="body2" color="text.secondary" noWrap>{game.vendor}</Typography>
        </Stack>
        <Stack direction="row" spacing={0.5}>
          <Tooltip title={paused ? "Tiếp tục" : "Tạm dừng"}>
            <IconButton onClick={setPauseState}>{paused ? <PlayArrowRounded /> : <PauseRounded />}</IconButton>
          </Tooltip>
          <Tooltip title="Khởi động lại">
            <IconButton onClick={() => {
              void runtime.stopMidlet().then(() => {
                setLcdui(EMPTY_LCDUI_STATE);
                generationRef.current = 0n;
                return runtime.launch(game, width, height);
              }).catch((error) => {
                setRuntimeError(error instanceof Error ? error.message : String(error));
              });
            }}><RefreshRounded /></IconButton>
          </Tooltip>
          <Tooltip title="Toàn màn hình">
            <IconButton onClick={() => void displayRef.current?.requestFullscreen()}><FullscreenRounded /></IconButton>
          </Tooltip>
          <Tooltip title="Đóng">
            <IconButton onClick={stop}><CloseRounded /></IconButton>
          </Tooltip>
        </Stack>
      </Stack>

      <Stack direction="row" spacing={1} useFlexGap sx={{ px: 2, pb: 1.5, flexWrap: "wrap" }}>
        {statusChips.map((label) => <Chip key={label} size="small" label={label} />)}
      </Stack>

      <Box className="emulator-display-shell" ref={displayRef}>
        <Box className="emulator-display" sx={{ aspectRatio: `${width} / ${height}` }}>
          {runtimeError ? <Box className="emulator-error">
            <Typography variant="subtitle1">Không thể chạy ứng dụng</Typography>
            <Typography color="text.secondary">{runtimeError}</Typography>
          </Box> : hasNativeScreen && activeScreen ? <NativeLcduiView runtime={runtime} screen={activeScreen} /> : <canvas
            ref={canvasRef}
            className="emulator-canvas"
            onPointerDown={(event) => {
              event.currentTarget.setPointerCapture(event.pointerId);
              const point = pointerPosition(event);
              runtime.sendPointer(point.x, point.y, 0);
            }}
            onPointerMove={(event) => {
              if (!event.buttons) return;
              const point = pointerPosition(event);
              runtime.sendPointer(point.x, point.y, 1);
            }}
            onPointerUp={(event) => {
              const point = pointerPosition(event);
              runtime.sendPointer(point.x, point.y, 2);
            }}
          />}
        </Box>
      </Box>
    </Paper>
    <VirtualKeypad runtime={runtime} />
  </Box>;
}
