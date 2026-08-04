import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import {
  Alert,
  AppBar,
  Avatar,
  BottomNavigation,
  BottomNavigationAction,
  Box,
  Button,
  Card,
  CardActionArea,
  CardContent,
  CardMedia,
  Chip,
  CircularProgress,
  Dialog,
  DialogActions,
  DialogContent,
  DialogContentText,
  DialogTitle,
  Divider,
  FormControl,
  FormControlLabel,
  InputLabel,
  MenuItem,
  Select,
  Snackbar,
  Stack,
  Switch,
  TextField,
  ThemeProvider,
  Toolbar,
  Tooltip,
  Typography,
  useMediaQuery
} from "@mui/material";
import {
  AddRounded,
  AppsRounded,
  DarkModeRounded,
  DeleteOutlineRounded,
  GamepadRounded,
  InfoOutlined,
  LightModeRounded,
  MemoryRounded,
  MonitorHeartRounded,
  PlayArrowRounded,
  SettingsRounded,
  StorageRounded,
  UploadFileRounded
} from "@mui/icons-material";
import { EmulatorScreen } from "./EmulatorScreen";
import { readJarMetadata } from "./jarMetadata";
import { PhoneMEWebRuntime } from "./phoneMEClient";
import { createPhoneMETheme } from "./theme";
import type { GameEntry, RuntimeSnapshot, ThemePreference, ViewId } from "./types";

const GAMES_KEY = "phoneme-web.games.v1";
const SETTINGS_KEY = "phoneme-web.settings.v1";

const SCREEN_PROFILES = [
  { label: "240 × 320 (dọc)", width: 240, height: 320 },
  { label: "320 × 240 (ngang)", width: 320, height: 240 },
  { label: "176 × 208", width: 176, height: 208 },
  { label: "128 × 160", width: 128, height: 160 },
  { label: "360 × 640", width: 360, height: 640 }
];

type AppSettings = {
  theme: ThemePreference;
  width: number;
  height: number;
  websocketProxyUrl: string;
  pixelated: boolean;
};

const DEFAULT_SETTINGS: AppSettings = {
  theme: "system",
  width: 240,
  height: 320,
  websocketProxyUrl: "",
  pixelated: true
};

const EMPTY_SNAPSHOT: RuntimeSnapshot = {
  phase: "loading",
  message: "Đang nạp phoneME WebAssembly",
  fps: 0,
  usedMemory: 0,
  frameWidth: 0,
  frameHeight: 0
};

function readJson<T>(key: string, fallback: T): T {
  try {
    const value = localStorage.getItem(key);
    return value ? JSON.parse(value) as T : fallback;
  } catch {
    return fallback;
  }
}

function formatBytes(value: number) {
  if (value < 1024) return `${value} B`;
  if (value < 1024 ** 2) return `${(value / 1024).toFixed(1)} KB`;
  return `${(value / 1024 ** 2).toFixed(1)} MB`;
}

function GameAvatar({ game, size = 56 }: { game: GameEntry; size?: number }) {
  return <Avatar
    src={game.iconDataUrl}
    variant="rounded"
    sx={{ width: size, height: size, bgcolor: "primary.container", color: "primary.main", borderRadius: 3 }}
  >{game.title.slice(0, 1).toUpperCase()}</Avatar>;
}

function LibraryView({ games, loading, onFiles, onLaunch, onDelete }: {
  games: GameEntry[];
  loading: boolean;
  onFiles: (files: File[]) => void;
  onLaunch: (game: GameEntry) => void;
  onDelete: (game: GameEntry) => void;
}) {
  const inputRef = useRef<HTMLInputElement>(null);
  const [dragging, setDragging] = useState(false);
  return <Stack spacing={3}>
    <Box
      className={`import-zone ${dragging ? "dragging" : ""}`}
      onDragEnter={(event) => { event.preventDefault(); setDragging(true); }}
      onDragOver={(event) => event.preventDefault()}
      onDragLeave={(event) => {
        if (!event.currentTarget.contains(event.relatedTarget as Node | null)) setDragging(false);
      }}
      onDrop={(event) => {
        event.preventDefault();
        setDragging(false);
        onFiles(Array.from(event.dataTransfer.files));
      }}
    >
      <input
        ref={inputRef}
        hidden
        type="file"
        accept=".jar,application/java-archive,application/zip"
        multiple
        onChange={(event) => {
          onFiles(Array.from(event.target.files ?? []));
          event.currentTarget.value = "";
        }}
      />
      <Stack spacing={1.5} sx={{ alignItems: "center", textAlign: "center" }}>
        <Box className="import-icon"><UploadFileRounded /></Box>
        <Box>
          <Typography variant="h6">Thêm game hoặc ứng dụng J2ME</Typography>
          <Typography color="text.secondary">Thả file JAR vào đây hoặc chọn từ máy</Typography>
        </Box>
        <Button
          variant="contained"
          startIcon={loading ? <CircularProgress size={18} color="inherit" /> : <AddRounded />}
          disabled={loading}
          onClick={() => inputRef.current?.click()}
        >{loading ? "Đang cài đặt" : "Chọn file JAR"}</Button>
      </Stack>
    </Box>

    <Stack direction="row" sx={{ alignItems: "center", justifyContent: "space-between" }}>
      <Box>
        <Typography variant="h5">Thư viện</Typography>
        <Typography color="text.secondary">{games.length} ứng dụng đã cài</Typography>
      </Box>
      <Chip icon={<StorageRounded />} label="Dữ liệu lưu trong trình duyệt" />
    </Stack>

    {games.length ? <Box className="game-grid">
      {games.map((game) => <Card key={game.id} className="game-card">
        <CardActionArea onClick={() => onLaunch(game)} className="game-card-action">
          {game.iconDataUrl ? <CardMedia component="img" image={game.iconDataUrl} alt="" className="game-cover" /> : <Box className="game-cover-placeholder">
            <GamepadRounded />
          </Box>}
          <CardContent>
            <Typography variant="subtitle1" noWrap title={game.title}>{game.title}</Typography>
            <Typography variant="body2" color="text.secondary" noWrap title={game.vendor}>{game.vendor}</Typography>
            <Stack direction="row" spacing={1} sx={{ mt: 2 }}>
              {game.version ? <Chip size="small" label={`v${game.version}`} /> : null}
              <Chip size="small" label={game.fileName.replace(/\.jar$/i, "")} />
            </Stack>
          </CardContent>
        </CardActionArea>
        <Divider />
        <Stack direction="row" sx={{ p: 1, justifyContent: "space-between" }}>
          <Button startIcon={<PlayArrowRounded />} onClick={() => onLaunch(game)}>Chạy</Button>
          <Tooltip title="Gỡ ứng dụng">
            <Button color="error" onClick={() => onDelete(game)}><DeleteOutlineRounded /></Button>
          </Tooltip>
        </Stack>
      </Card>)}
    </Box> : <Box className="empty-library">
      <AppsRounded />
      <Typography variant="h6">Chưa có ứng dụng</Typography>
      <Typography color="text.secondary">Nhập một file JAR để bắt đầu.</Typography>
    </Box>}
  </Stack>;
}

function SettingsView({ settings, onChange, runtimeReady }: {
  settings: AppSettings;
  onChange: (settings: AppSettings) => void;
  runtimeReady: boolean;
}) {
  const profileValue = `${settings.width}x${settings.height}`;
  return <Stack spacing={3} sx={{ maxWidth: 760 }}>
    <Box>
      <Typography variant="h5">Cài đặt</Typography>
      <Typography color="text.secondary">Thiết lập hiển thị và kết nối cho phoneME Web.</Typography>
    </Box>

    <Card>
      <CardContent>
        <Stack spacing={2.5}>
          <Stack direction="row" spacing={2} sx={{ alignItems: "center" }}>
            {settings.theme === "dark" ? <DarkModeRounded /> : <LightModeRounded />}
            <Box sx={{ flex: 1 }}>
              <Typography variant="subtitle1">Giao diện</Typography>
              <Typography variant="body2" color="text.secondary">Theo hệ thống, sáng hoặc tối.</Typography>
            </Box>
            <FormControl size="small" sx={{ minWidth: 150 }}>
              <Select value={settings.theme} onChange={(event) => onChange({ ...settings, theme: event.target.value as ThemePreference })}>
                <MenuItem value="system">Theo hệ thống</MenuItem>
                <MenuItem value="light">Sáng</MenuItem>
                <MenuItem value="dark">Tối</MenuItem>
              </Select>
            </FormControl>
          </Stack>
          <Divider />
          <Stack direction={{ xs: "column", sm: "row" }} spacing={2} sx={{ alignItems: { sm: "center" } }}>
            <MonitorHeartRounded />
            <Box sx={{ flex: 1 }}>
              <Typography variant="subtitle1">Kích thước màn hình J2ME</Typography>
              <Typography variant="body2" color="text.secondary">Áp dụng khi mở hoặc đổi game.</Typography>
            </Box>
            <FormControl size="small" sx={{ minWidth: 190 }}>
              <InputLabel id="screen-profile-label">Màn hình</InputLabel>
              <Select
                labelId="screen-profile-label"
                label="Màn hình"
                value={profileValue}
                onChange={(event) => {
                  const [width, height] = String(event.target.value).split("x").map(Number);
                  onChange({ ...settings, width, height });
                }}
              >
                {SCREEN_PROFILES.map((profile) => <MenuItem key={profile.label} value={`${profile.width}x${profile.height}`}>{profile.label}</MenuItem>)}
              </Select>
            </FormControl>
          </Stack>
          <Divider />
          <FormControlLabel
            control={<Switch checked={settings.pixelated} onChange={(_, checked) => onChange({ ...settings, pixelated: checked })} />}
            label={<Box>
              <Typography variant="subtitle1">Giữ nét pixel</Typography>
              <Typography variant="body2" color="text.secondary">Tắt nội suy khi phóng Canvas game.</Typography>
            </Box>}
          />
        </Stack>
      </CardContent>
    </Card>

    <Card>
      <CardContent>
        <Stack spacing={2}>
          <Box>
            <Typography variant="subtitle1">WebSocket proxy cho socket/UDP</Typography>
            <Typography variant="body2" color="text.secondary">
              Trình duyệt không mở TCP/UDP thô. Nhập proxy tương thích Emscripten để game online dùng socket:// hoặc datagram://.
            </Typography>
          </Box>
          <TextField
            fullWidth
            label="Địa chỉ proxy"
            placeholder="wss://proxy.example.com:8080"
            value={settings.websocketProxyUrl}
            onChange={(event) => onChange({ ...settings, websocketProxyUrl: event.target.value.trim() })}
            helperText={runtimeReady ? "Thay đổi địa chỉ cần tải lại trang để áp dụng cho runtime hiện tại." : "Địa chỉ sẽ dùng khi core khởi động."}
          />
        </Stack>
      </CardContent>
    </Card>

    <Alert severity="info" icon={<InfoOutlined />}>
      Dữ liệu suite, RMS và filesystem được đồng bộ vào IndexedDB. Xóa dữ liệu trang web trong trình duyệt sẽ xóa toàn bộ thư viện phoneME Web.
    </Alert>
  </Stack>;
}

export default function App() {
  const prefersDark = useMediaQuery("(prefers-color-scheme: dark)");
  const compact = useMediaQuery("(max-width: 760px)");
  const runtimeRef = useRef(new PhoneMEWebRuntime());
  const [settings, setSettings] = useState<AppSettings>(() => ({ ...DEFAULT_SETTINGS, ...readJson<AppSettings>(SETTINGS_KEY, DEFAULT_SETTINGS) }));
  const [games, setGames] = useState<GameEntry[]>(() => readJson<GameEntry[]>(GAMES_KEY, []));
  const [view, setView] = useState<ViewId>("library");
  const [activeGame, setActiveGame] = useState<GameEntry | null>(null);
  const [runtimeSnapshot, setRuntimeSnapshot] = useState<RuntimeSnapshot>(EMPTY_SNAPSHOT);
  const [installing, setInstalling] = useState(false);
  const [deleteTarget, setDeleteTarget] = useState<GameEntry | null>(null);
  const [deleteData, setDeleteData] = useState(true);
  const [snackbar, setSnackbar] = useState<{ message: string; severity: "success" | "error" | "info" } | null>(null);
  const [logs, setLogs] = useState<string[]>([]);

  const paletteMode = settings.theme === "system" ? (prefersDark ? "dark" : "light") : settings.theme;
  const theme = useMemo(() => createPhoneMETheme(paletteMode), [paletteMode]);
  const runtime = runtimeRef.current;

  const saveSettings = useCallback((next: AppSettings) => {
    setSettings(next);
    localStorage.setItem(SETTINGS_KEY, JSON.stringify(next));
  }, []);

  const saveGames = useCallback((next: GameEntry[]) => {
    setGames(next);
    localStorage.setItem(GAMES_KEY, JSON.stringify(next));
  }, []);

  useEffect(() => {
    let active = true;
    void runtime.initialize({
      websocketProxyUrl: settings.websocketProxyUrl || undefined,
      onLog: (line, error) => {
        if (!active) return;
        setLogs((current) => [...current.slice(-199), `${error ? "ERR" : "LOG"} ${line}`]);
      }
    }).then(() => {
      if (!active) return;
      setRuntimeSnapshot({ ...EMPTY_SNAPSHOT, phase: "ready", message: "phoneME Web đã sẵn sàng" });
    }).catch((error) => {
      if (!active) return;
      const message = error instanceof Error ? error.message : String(error);
      setRuntimeSnapshot({ ...EMPTY_SNAPSHOT, phase: "error", message });
      setSnackbar({ message, severity: "error" });
    });
    const flush = () => void runtime.flushStorage();
    window.addEventListener("pagehide", flush);
    return () => {
      active = false;
      window.removeEventListener("pagehide", flush);
      void runtime.flushStorage();
      runtime.dispose();
    };
    // Runtime options are intentionally fixed for one page lifecycle.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [runtime]);

  const importFiles = useCallback(async (files: File[]) => {
    const jars = files.filter((file) => file.name.toLowerCase().endsWith(".jar"));
    if (!jars.length) {
      setSnackbar({ message: "Vui lòng chọn file .jar", severity: "error" });
      return;
    }
    if (!runtime.ready) {
      setSnackbar({ message: "Core WebAssembly chưa sẵn sàng", severity: "error" });
      return;
    }
    setInstalling(true);
    const installed: GameEntry[] = [];
    try {
      for (const file of jars) {
        const metadata = await readJarMetadata(file);
        installed.push(await runtime.installJar(file, metadata));
      }
      const next = [...games, ...installed]
        .filter((game, index, all) => all.findIndex((entry) => entry.suiteId === game.suiteId) === index)
        .sort((left, right) => right.installedAt - left.installedAt);
      saveGames(next);
      setSnackbar({ message: `Đã cài ${installed.length} ứng dụng`, severity: "success" });
    } catch (error) {
      setSnackbar({ message: error instanceof Error ? error.message : String(error), severity: "error" });
      if (installed.length) saveGames([...games, ...installed]);
    } finally {
      setInstalling(false);
    }
  }, [games, runtime, saveGames]);

  const launch = useCallback((game: GameEntry) => {
    if (!runtime.ready) {
      setSnackbar({ message: "Core WebAssembly chưa sẵn sàng", severity: "error" });
      return;
    }
    setActiveGame(game);
    setView("emulator");
  }, [runtime]);

  const confirmDelete = useCallback(async () => {
    if (!deleteTarget) return;
    try {
      await runtime.uninstall(deleteTarget, deleteData);
      const next = games.filter((game) => game.id !== deleteTarget.id);
      saveGames(next);
      if (activeGame?.id === deleteTarget.id) {
        setActiveGame(null);
        setView("library");
      }
      setSnackbar({ message: `Đã gỡ ${deleteTarget.title}`, severity: "success" });
    } catch (error) {
      setSnackbar({ message: error instanceof Error ? error.message : String(error), severity: "error" });
    } finally {
      setDeleteTarget(null);
    }
  }, [activeGame?.id, deleteData, deleteTarget, games, runtime, saveGames]);

  const navigation = [
    { id: "library" as const, label: "Thư viện", icon: <AppsRounded /> },
    { id: "emulator" as const, label: "Trình giả lập", icon: <GamepadRounded /> },
    { id: "settings" as const, label: "Cài đặt", icon: <SettingsRounded /> }
  ];

  const content = view === "emulator" && activeGame ? <EmulatorScreen
    runtime={runtime}
    game={activeGame}
    width={settings.width}
    height={settings.height}
    onStop={() => {
      setActiveGame(null);
      setRuntimeSnapshot({ ...EMPTY_SNAPSHOT, phase: "ready", message: "phoneME Web đã sẵn sàng" });
      setView("library");
    }}
    onSnapshot={setRuntimeSnapshot}
  /> : view === "settings" ? <SettingsView settings={settings} onChange={saveSettings} runtimeReady={runtime.ready} /> : <LibraryView
    games={games}
    loading={installing}
    onFiles={(files) => void importFiles(files)}
    onLaunch={launch}
    onDelete={(game) => { setDeleteData(true); setDeleteTarget(game); }}
  />;

  return <ThemeProvider theme={theme}>
    <Box className={`app-root ${settings.pixelated ? "pixelated" : "smooth"}`}>
      {!compact ? <Box component="nav" className="navigation-rail" aria-label="Điều hướng chính">
        <Box className="brand-mark"><GamepadRounded /></Box>
        <Stack spacing={1} sx={{ width: "100%" }}>
          {navigation.map((item) => <Button
            key={item.id}
            className={`navigation-rail-action ${view === item.id ? "active" : ""}`}
            color={view === item.id ? "primary" : "inherit"}
            onClick={() => setView(item.id)}
            startIcon={item.icon}
          >{item.label}</Button>)}
        </Stack>
      </Box> : null}

      <Box className="app-column">
        <AppBar position="sticky" color="transparent" elevation={0} className="top-app-bar">
          <Toolbar>
            <Stack direction="row" spacing={1.5} sx={{ alignItems: "center", minWidth: 0, flex: 1 }}>
              {compact ? <Box className="mobile-brand"><GamepadRounded /></Box> : null}
              <Box sx={{ minWidth: 0 }}>
                <Typography variant="h6" noWrap>phoneME Web</Typography>
                <Typography variant="body2" color="text.secondary" noWrap>{runtimeSnapshot.message}</Typography>
              </Box>
            </Stack>
            <Stack direction="row" spacing={1} sx={{ alignItems: "center" }}>
              {activeGame ? <Chip size="small" icon={<MemoryRounded />} label={formatBytes(runtimeSnapshot.usedMemory)} /> : null}
              {activeGame ? <Chip size="small" label={`${runtimeSnapshot.fps.toFixed(0)} FPS`} /> : null}
              <Chip
                size="small"
                color={runtimeSnapshot.phase === "error" ? "error" : runtimeSnapshot.phase === "loading" ? "default" : "success"}
                label={runtimeSnapshot.phase === "loading" ? "Đang nạp" : runtimeSnapshot.phase === "error" ? "Lỗi" : "WASM"}
              />
            </Stack>
          </Toolbar>
        </AppBar>

        <Box component="main" className="main-content">{content}</Box>
      </Box>

      {compact ? <BottomNavigation className="bottom-navigation" value={view} onChange={(_, value) => setView(value as ViewId)} showLabels>
        {navigation.map((item) => <BottomNavigationAction key={item.id} value={item.id} label={item.label} icon={item.icon} />)}
      </BottomNavigation> : null}

      <Dialog open={Boolean(deleteTarget)} onClose={() => setDeleteTarget(null)}>
        <DialogTitle>Gỡ {deleteTarget?.title}?</DialogTitle>
        <DialogContent>
          <DialogContentText sx={{ mb: 2 }}>Ứng dụng sẽ bị xóa khỏi thư viện.</DialogContentText>
          <FormControlLabel
            control={<Switch checked={deleteData} onChange={(_, checked) => setDeleteData(checked)} />}
            label="Xóa cả RMS, save game và dữ liệu ứng dụng"
          />
        </DialogContent>
        <DialogActions>
          <Button onClick={() => setDeleteTarget(null)}>Hủy</Button>
          <Button color="error" variant="contained" onClick={() => void confirmDelete()}>Gỡ ứng dụng</Button>
        </DialogActions>
      </Dialog>

      <Snackbar open={Boolean(snackbar)} autoHideDuration={4500} onClose={() => setSnackbar(null)}>
        {snackbar ? <Alert severity={snackbar.severity} variant="filled" onClose={() => setSnackbar(null)}>{snackbar.message}</Alert> : undefined}
      </Snackbar>

      <Box className="sr-only" aria-live="polite">{logs.at(-1)}</Box>
    </Box>
  </ThemeProvider>;
}
