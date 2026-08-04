import { useEffect, useMemo, useRef, useState } from "react";
import {
  Alert,
  Box,
  Button,
  Checkbox,
  Chip,
  FormControl,
  FormControlLabel,
  FormGroup,
  FormLabel,
  LinearProgress,
  List,
  ListItemButton,
  ListItemText,
  MenuItem,
  Radio,
  RadioGroup,
  Select,
  Slider,
  Stack,
  TextField,
  Typography
} from "@mui/material";
import type { PhoneMEWebRuntime } from "./phoneMEClient";
import type { LcduiChoice, LcduiCommand, LcduiEvent, LcduiItem, LcduiScreen } from "./types";

export type LcduiState = {
  activeScreenId: number | null;
  screens: Record<number, LcduiScreen>;
};

export const EMPTY_LCDUI_STATE: LcduiState = {
  activeScreenId: null,
  screens: {}
};

function emptyScreen(event: LcduiEvent): LcduiScreen {
  return {
    id: event.componentId,
    type: event.componentType,
    title: event.text,
    detail: event.detail,
    visible: false,
    index: event.index,
    arg0: event.arg0,
    arg1: event.arg1,
    arg2: event.arg2,
    arg3: event.arg3,
    value64: event.value64,
    generation: event.generation,
    items: [],
    commands: [],
    focusedItemId: null
  };
}

function emptyItem(event: LcduiEvent): LcduiItem {
  return {
    id: event.componentId,
    parentId: event.parentId,
    type: event.componentType,
    label: event.text,
    detail: event.detail,
    visible: false,
    index: event.index,
    arg0: event.arg0,
    arg1: event.arg1,
    arg2: event.arg2,
    arg3: event.arg3,
    value64: event.value64,
    generation: event.generation,
    choices: []
  };
}

function updateScreen(screen: LcduiScreen, event: LcduiEvent): LcduiScreen {
  return {
    ...screen,
    type: event.componentType || screen.type,
    title: event.text || screen.title,
    detail: event.detail || screen.detail,
    index: event.index,
    arg0: event.arg0,
    arg1: event.arg1,
    arg2: event.arg2,
    arg3: event.arg3,
    value64: event.value64,
    generation: event.generation
  };
}

function updateItem(item: LcduiItem, event: LcduiEvent): LcduiItem {
  return {
    ...item,
    parentId: event.parentId || item.parentId,
    type: event.componentType || item.type,
    label: event.text || item.label,
    detail: event.detail,
    index: event.index,
    arg0: event.arg0,
    arg1: event.arg1,
    arg2: event.arg2,
    arg3: event.arg3,
    value64: event.value64,
    generation: event.generation
  };
}

function sortItems(items: LcduiItem[]) {
  return [...items].sort((left, right) => {
    const leftIndex = left.index < 0 ? Number.MAX_SAFE_INTEGER : left.index;
    const rightIndex = right.index < 0 ? Number.MAX_SAFE_INTEGER : right.index;
    return leftIndex - rightIndex || left.id - right.id;
  });
}

function upsertChoice(choices: LcduiChoice[], event: LcduiEvent) {
  const choice: LcduiChoice = {
    index: event.index,
    text: event.text,
    selected: event.arg0 !== 0,
    disabled: event.arg1 !== 0,
    commandId: 0
  };
  return [...choices.filter((entry) => entry.index !== event.index), choice]
    .sort((left, right) => left.index - right.index);
}

export function applyLcduiEvents(current: LcduiState, events: LcduiEvent[]): LcduiState {
  let state = current;
  for (const event of events) {
    if (event.kind === 1) {
      state = EMPTY_LCDUI_STATE;
      continue;
    }

    if (event.kind >= 2 && event.kind <= 6) {
      const existing = state.screens[event.componentId] ?? emptyScreen(event);
      if (event.kind === 6) {
        const screens = { ...state.screens };
        delete screens[event.componentId];
        state = {
          activeScreenId: state.activeScreenId === event.componentId ? null : state.activeScreenId,
          screens
        };
        continue;
      }
      const next = updateScreen(existing, event);
      next.visible = event.kind === 4 ? true : event.kind === 5 ? false : next.visible;
      state = {
        activeScreenId: event.kind === 4 ? event.componentId : state.activeScreenId,
        screens: { ...state.screens, [event.componentId]: next }
      };
      continue;
    }

    if (event.kind >= 7 && event.kind <= 11) {
      const parentId = event.parentId || state.activeScreenId;
      if (!parentId) continue;
      const screen = state.screens[parentId];
      if (!screen) continue;
      if (event.kind === 11) {
        state = {
          ...state,
          screens: {
            ...state.screens,
            [parentId]: { ...screen, items: screen.items.filter((item) => item.id !== event.componentId) }
          }
        };
        continue;
      }
      const existing = screen.items.find((item) => item.id === event.componentId) ?? emptyItem(event);
      const nextItem = updateItem(existing, event);
      nextItem.visible = event.kind === 9 ? true : event.kind === 10 ? false : nextItem.visible;
      const items = sortItems([...screen.items.filter((item) => item.id !== nextItem.id), nextItem]);
      state = { ...state, screens: { ...state.screens, [parentId]: { ...screen, items } } };
      continue;
    }

    if (event.kind === 12 || event.kind === 13) {
      const screenId = Object.values(state.screens)
        .find((screen) => screen.items.some((item) => item.id === event.componentId))?.id;
      if (!screenId) continue;
      const screen = state.screens[screenId];
      const items = screen.items.map((item) => {
        if (item.id !== event.componentId) return item;
        if (event.kind === 13) {
          return { ...item, choices: event.index < 0 ? [] : item.choices.filter((choice) => choice.index !== event.index) };
        }
        return { ...item, choices: upsertChoice(item.choices, event), type: event.componentType || item.type };
      });
      state = { ...state, screens: { ...state.screens, [screenId]: { ...screen, items } } };
      continue;
    }

    if (event.kind === 14) {
      const screenId = state.activeScreenId;
      if (!screenId || !state.screens[screenId]) continue;
      state = {
        ...state,
        screens: { ...state.screens, [screenId]: { ...state.screens[screenId], commands: [] } }
      };
      continue;
    }

    if (event.kind === 15) {
      const screenId = state.activeScreenId;
      if (!screenId || !state.screens[screenId]) continue;
      const command: LcduiCommand = {
        id: event.componentId,
        ownerId: event.arg3,
        label: event.text,
        longLabel: event.detail,
        commandType: event.arg0,
        priority: event.arg1
      };
      const screen = state.screens[screenId];
      const commands = [...screen.commands.filter((entry) => entry.id !== command.id), command]
        .sort((left, right) => left.priority - right.priority || left.id - right.id);
      state = { ...state, screens: { ...state.screens, [screenId]: { ...screen, commands } } };
      continue;
    }

    if (event.kind === 16) {
      const screenId = state.activeScreenId;
      if (!screenId || !state.screens[screenId]) continue;
      state = {
        ...state,
        screens: { ...state.screens, [screenId]: { ...state.screens[screenId], focusedItemId: event.componentId } }
      };
    }
  }
  return state;
}

function PixelImage({ runtime, componentId, generation, alt }: {
  runtime: PhoneMEWebRuntime;
  componentId: number;
  generation: number;
  alt: string;
}) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  useEffect(() => {
    let active = true;
    void runtime.copyLcduiImage(componentId).then((frame) => {
      const canvas = canvasRef.current;
      if (!active || !frame || !canvas) return;
      canvas.width = frame.width;
      canvas.height = frame.height;
      canvas.getContext("2d")?.putImageData(
        new ImageData(Uint8ClampedArray.from(frame.pixels), frame.width, frame.height),
        0,
        0
      );
    });
    return () => {
      active = false;
    };
  }, [componentId, generation, runtime]);
  return <canvas className="lcdui-image" ref={canvasRef} role="img" aria-label={alt} />;
}

function ItemLabel({ item }: { item: LcduiItem }) {
  return item.label ? <FormLabel sx={{ display: "block", mb: 1 }}>{item.label}</FormLabel> : null;
}

function ChoiceItem({ item, runtime, implicitCommand }: {
  item: LcduiItem;
  runtime: PhoneMEWebRuntime;
  implicitCommand?: LcduiCommand;
}) {
  if (item.type === 2) {
    return <List disablePadding>
      {item.choices.map((choice) => <ListItemButton
        key={choice.index}
        selected={choice.selected}
        disabled={choice.disabled}
        onClick={() => {
          runtime.setChoice(item.id, choice.index, true);
          if (implicitCommand) runtime.selectListItemCommand(item.id, choice.index, implicitCommand.id);
        }}
      >
        <ListItemText primary={choice.text} />
      </ListItemButton>)}
    </List>;
  }
  if (item.type === 3) {
    const selected = item.choices.find((choice) => choice.selected)?.index ?? "";
    return <FormControl fullWidth>
      <ItemLabel item={item} />
      <Select
        value={selected}
        onChange={(event) => runtime.setChoice(item.id, Number(event.target.value), true)}
      >
        {item.choices.map((choice) => <MenuItem key={choice.index} value={choice.index} disabled={choice.disabled}>
          {choice.text}
        </MenuItem>)}
      </Select>
    </FormControl>;
  }
  if (item.type === 1) {
    const selected = item.choices.find((choice) => choice.selected)?.index ?? -1;
    return <FormControl>
      <ItemLabel item={item} />
      <RadioGroup value={selected} onChange={(_, value) => runtime.setChoice(item.id, Number(value), true)}>
        {item.choices.map((choice) => <FormControlLabel
          key={choice.index}
          value={choice.index}
          disabled={choice.disabled}
          control={<Radio />}
          label={choice.text}
        />)}
      </RadioGroup>
    </FormControl>;
  }
  return <FormControl component="fieldset">
    <ItemLabel item={item} />
    <FormGroup>
      {item.choices.map((choice) => <FormControlLabel
        key={choice.index}
        disabled={choice.disabled}
        control={<Checkbox
          checked={choice.selected}
          onChange={(_, selected) => runtime.setChoice(item.id, choice.index, selected)}
        />}
        label={choice.text}
      />)}
    </FormGroup>
  </FormControl>;
}

function LcduiItemView({ item, runtime, commands }: {
  item: LcduiItem;
  runtime: PhoneMEWebRuntime;
  commands: LcduiCommand[];
}) {
  const ownerCommand = commands.find((command) => command.ownerId === item.id);
  if (!item.visible && item.type !== 2) return null;
  if (item.type >= 1 && item.type <= 3) {
    return <ChoiceItem item={item} runtime={runtime} implicitCommand={commands.find((command) => command.commandType === 1)} />;
  }
  if (item.type === 4) {
    return <Box onClick={() => runtime.activateItem(item.id)} sx={{ cursor: "pointer" }}>
      <ItemLabel item={item} />
      <PixelImage runtime={runtime} componentId={item.id} generation={item.arg2} alt={item.label || "Custom item"} />
    </Box>;
  }
  if (item.type === 5) {
    const date = item.value64 ? new Date(item.value64 * 1000).toISOString().slice(0, item.arg1 === 1 ? 10 : 16) : "";
    return <TextField
      label={item.label}
      type={item.arg1 === 1 ? "date" : "datetime-local"}
      value={date}
      onChange={(event) => runtime.setDate(item.id, new Date(event.target.value).getTime() / 1000)}
      slotProps={{ inputLabel: { shrink: true } }}
      fullWidth
    />;
  }
  if (item.type === 6) {
    const maximum = Math.max(1, item.arg1);
    return <Stack spacing={1}>
      <ItemLabel item={item} />
      <LinearProgress variant="determinate" value={Math.max(0, Math.min(100, item.arg0 / maximum * 100))} />
    </Stack>;
  }
  if (item.type === 7) {
    return <Stack spacing={1}>
      <ItemLabel item={item} />
      <Slider value={item.arg0} min={0} max={Math.max(1, item.arg1)} onChange={(_, value) => runtime.setGauge(item.id, Number(value))} />
    </Stack>;
  }
  if (item.type >= 8 && item.type <= 10) {
    const image = <PixelImage runtime={runtime} componentId={item.id} generation={item.arg2} alt={item.detail || item.label || "Ảnh"} />;
    return <Stack spacing={1} sx={{ alignItems: "flex-start" }}>
      <ItemLabel item={item} />
      {item.type === 8 ? image : <Button variant={item.type === 10 ? "contained" : "text"} onClick={() => runtime.activateItem(item.id)}>{image}</Button>}
      {item.detail ? <Typography variant="body2" color="text.secondary">{item.detail}</Typography> : null}
    </Stack>;
  }
  if (item.type === 11) return <Box sx={{ width: Math.max(0, item.arg2), height: Math.max(0, item.arg3) }} />;
  if (item.type >= 12 && item.type <= 14) {
    const content = <Stack spacing={0.5} sx={{ alignItems: "flex-start" }}>
      <ItemLabel item={item} />
      <Typography sx={{ whiteSpace: "pre-wrap" }}>{item.detail}</Typography>
    </Stack>;
    if (item.type === 12 && !ownerCommand) return content;
    return <Button
      variant={item.type === 14 ? "contained" : "text"}
      onClick={() => ownerCommand ? runtime.selectCommand(ownerCommand.id) : runtime.activateItem(item.id)}
      sx={{ justifyContent: "flex-start", textAlign: "left" }}
    >{content}</Button>;
  }
  if (item.type === 15) {
    const constraint = item.arg1 & 0xffff;
    return <TextField
      fullWidth
      label={item.label}
      value={item.detail}
      disabled={(item.arg1 & 0x20000) !== 0}
      type={constraint === 2 || constraint === 5 ? "number" : constraint === 1 ? "email" : constraint === 3 ? "tel" : constraint === 4 ? "url" : "text"}
      slotProps={{ htmlInput: { maxLength: item.arg0 > 0 ? item.arg0 : undefined } }}
      onChange={(event) => runtime.setText(item.id, event.target.value, event.target.selectionStart ?? event.target.value.length)}
    />;
  }
  return <Stack spacing={0.5}>
    <ItemLabel item={item} />
    <Typography sx={{ whiteSpace: "pre-wrap" }}>{item.detail}</Typography>
  </Stack>;
}

export function NativeLcduiView({ runtime, screen }: {
  runtime: PhoneMEWebRuntime;
  screen: LcduiScreen;
}) {
  const alertSeverity = useMemo(() => {
    if (screen.type === 18) return "warning";
    if (screen.type === 19) return "error";
    if (screen.type === 21) return "success";
    return "info";
  }, [screen.type]);
  const [scrollTop, setScrollTop] = useState(0);

  return <Box className="native-lcdui">
    <Box
      className="native-lcdui-content"
      onScroll={(event) => {
        const value = event.currentTarget.scrollTop;
        setScrollTop(value);
        runtime.setScrollPosition(Math.round(value));
      }}
      data-scroll={scrollTop}
    >
      {screen.detail && screen.type < 16 ? <Chip size="small" label={screen.detail} sx={{ mb: 2 }} /> : null}
      {screen.type >= 16 && screen.type <= 21 ? <Alert severity={alertSeverity} sx={{ mb: 2, whiteSpace: "pre-wrap" }}>
        {screen.detail || screen.title}
      </Alert> : null}
      <Stack spacing={2.25}>
        {screen.items.map((item) => <Box
          key={item.id}
          onFocus={() => runtime.focusItem(item.id)}
          sx={{ minWidth: 0 }}
        >
          <LcduiItemView item={item} runtime={runtime} commands={screen.commands} />
        </Box>)}
      </Stack>
    </Box>
    {screen.commands.length ? <Stack className="native-lcdui-actions" direction="row" spacing={1}>
      {screen.commands.map((command, index) => <Button
        key={command.id}
        variant={index === 0 ? "contained" : "text"}
        onClick={() => runtime.selectCommand(command.id)}
      >{command.label || command.longLabel}</Button>)}
    </Stack> : null}
  </Box>;
}
