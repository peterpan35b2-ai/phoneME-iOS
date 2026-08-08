import { useEffect, useMemo, useRef, useState, type ReactNode } from "react";
import {
  Box,
  Button,
  Checkbox,
  CircularProgress,
  Divider,
  FormControl,
  FormLabel,
  IconButton,
  LinearProgress,
  List,
  ListItemButton,
  ListItemText,
  Menu,
  MenuItem,
  Radio,
  Select,
  Slider,
  Stack,
  TextField,
  Typography
} from "@mui/material";
import AlarmRounded from "@mui/icons-material/AlarmRounded";
import ArrowBackRounded from "@mui/icons-material/ArrowBackRounded";
import CheckRounded from "@mui/icons-material/CheckRounded";
import ChevronRightRounded from "@mui/icons-material/ChevronRightRounded";
import CloseRounded from "@mui/icons-material/CloseRounded";
import ErrorOutlineRounded from "@mui/icons-material/ErrorOutlineRounded";
import HelpOutlineRounded from "@mui/icons-material/HelpOutlineRounded";
import InfoOutlined from "@mui/icons-material/InfoOutlined";
import LogoutRounded from "@mui/icons-material/LogoutRounded";
import MoreHorizRounded from "@mui/icons-material/MoreHorizRounded";
import NotificationsNoneRounded from "@mui/icons-material/NotificationsNoneRounded";
import StopRounded from "@mui/icons-material/StopRounded";
import TouchAppRounded from "@mui/icons-material/TouchAppRounded";
import WarningAmberRounded from "@mui/icons-material/WarningAmberRounded";
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

const EVENT_RESET = 1;
const EVENT_SCREEN_CREATED = 2;
const EVENT_SCREEN_UPDATED = 3;
const EVENT_SCREEN_SHOWN = 4;
const EVENT_SCREEN_HIDDEN = 5;
const EVENT_SCREEN_DELETED = 6;
const EVENT_ITEM_CREATED = 7;
const EVENT_ITEM_UPDATED = 8;
const EVENT_ITEM_SHOWN = 9;
const EVENT_ITEM_HIDDEN = 10;
const EVENT_ITEM_DELETED = 11;
const EVENT_CHOICE = 12;
const EVENT_CHOICE_DELETED = 13;
const EVENT_COMMANDS_RESET = 14;
const EVENT_COMMAND = 15;
const EVENT_ITEM_FOCUSED = 16;

const TYPE_EXCLUSIVE_CHOICE = 0;
const TYPE_MULTIPLE_CHOICE = 1;
const TYPE_IMPLICIT_CHOICE = 2;
const TYPE_POPUP_CHOICE = 3;
const TYPE_CUSTOM_ITEM = 4;
const TYPE_DATE_FIELD = 5;
const TYPE_PROGRESS_GAUGE = 6;
const TYPE_INTERACTIVE_GAUGE = 7;
const TYPE_PLAIN_IMAGE = 8;
const TYPE_HYPERLINK_IMAGE = 9;
const TYPE_BUTTON_IMAGE = 10;
const TYPE_SPACER = 11;
const TYPE_PLAIN_STRING = 12;
const TYPE_HYPERLINK_STRING = 13;
const TYPE_BUTTON_STRING = 14;
const TYPE_TEXT_FIELD = 15;
const TYPE_NULL_ALERT = 16;
const TYPE_INFO_ALERT = 17;
const TYPE_WARNING_ALERT = 18;
const TYPE_ERROR_ALERT = 19;
const TYPE_ALARM_ALERT = 20;
const TYPE_CONFIRMATION_ALERT = 21;
const TYPE_CANVAS = 22;
const TYPE_FORM = 23;
const TYPE_MENU = 24;

const SCREEN_KIND_FORM = 0;
const SCREEN_KIND_LIST = 1;
const SCREEN_KIND_TEXT_BOX = 2;
const SCREEN_KIND_ALERT = 3;
const SCREEN_KIND_MENU = 4;

const TEXT_FIELD_METADATA = -1001;
const GAUGE_METADATA = -1002;
const DATE_FIELD_METADATA = -1003;
const IMAGE_METADATA = -1004;
const ITEM_STYLE_METADATA = -1005;
const SCREEN_KIND_METADATA = -1006;
const SCREEN_MODE_METADATA = -1007;
const ALERT_METADATA = -1009;

function unpackFont(value64: number) {
  const packed = Math.max(0, Math.trunc(value64));
  return {
    fontFace: packed % 0x1_0000,
    fontStyle: Math.floor(packed / 0x1_0000) % 0x1_0000,
    fontSize: Math.floor(packed / 0x1_0000_0000) % 0x1_0000
  };
}

function emptyScreen(event: LcduiEvent): LcduiScreen {
  return {
    id: event.componentId,
    type: event.componentType,
    title: "",
    detail: "",
    visible: false,
    contentWidth: 0,
    contentHeight: 0,
    scrollPosition: 0,
    fullScreen: false,
    nativeKind: null,
    generation: event.generation,
    items: [],
    commands: [],
    focusedItemId: null
  };
}

function applyScreenEvent(screen: LcduiScreen, event: LcduiEvent): LcduiScreen {
  const next = {
    ...screen,
    type: event.componentType,
    title: event.text,
    detail: event.detail,
    generation: Math.max(screen.generation, event.generation)
  };

  if (event.arg3 === SCREEN_KIND_METADATA) {
    next.nativeKind = event.arg0;
  } else if (event.arg3 === SCREEN_MODE_METADATA) {
    next.fullScreen = event.arg0 !== 0;
  } else if (event.arg3 !== IMAGE_METADATA && event.arg3 !== ALERT_METADATA &&
             (event.kind === EVENT_SCREEN_SHOWN || event.kind === EVENT_SCREEN_UPDATED)) {
    next.contentWidth = Math.max(event.arg0, next.contentWidth);
    next.contentHeight = Math.max(event.arg1, next.contentHeight);
    next.scrollPosition = Math.max(event.arg2, 0);
  }

  if (event.kind === EVENT_SCREEN_SHOWN) next.visible = true;
  if (event.kind === EVENT_SCREEN_HIDDEN) next.visible = false;
  return next;
}

function emptyItem(event: LcduiEvent): LcduiItem {
  return {
    id: event.componentId,
    parentId: event.parentId,
    formIndex: event.index >= 0 ? event.index : -1,
    type: event.componentType,
    label: "",
    text: "",
    frame: { x: 0, y: 0, width: 0, height: 0 },
    visible: false,
    layout: 0,
    appearanceMode: 0,
    fitPolicy: 0,
    focused: false,
    maxSize: 0,
    constraints: 0,
    caretPosition: 0,
    value: 0,
    maxValue: 100,
    interactive: false,
    inputMode: 0,
    fontFace: 0,
    fontStyle: 0,
    fontSize: 0,
    dateUnixSeconds: 0,
    imageWidth: 0,
    imageHeight: 0,
    imageGeneration: 0,
    generation: event.generation,
    choices: []
  };
}

function applyItemEvent(item: LcduiItem, event: LcduiEvent): LcduiItem {
  const next: LcduiItem = {
    ...item,
    parentId: event.parentId,
    formIndex: event.index >= 0 ? event.index : item.formIndex,
    type: event.componentType,
    label: event.text,
    generation: Math.max(item.generation, event.generation)
  };

  switch (event.arg3) {
    case TEXT_FIELD_METADATA:
      next.maxSize = Math.max(event.arg0, 0);
      next.constraints = event.arg1;
      next.caretPosition = Math.max(event.arg2, 0);
      next.text = event.detail;
      break;
    case GAUGE_METADATA:
      next.value = event.arg0;
      next.maxValue = event.arg1;
      next.interactive = event.arg2 !== 0;
      break;
    case DATE_FIELD_METADATA:
      next.inputMode = event.arg1;
      next.dateUnixSeconds = event.value64;
      next.text = event.detail;
      break;
    case IMAGE_METADATA:
      next.imageWidth = Math.max(event.arg0, 0);
      next.imageHeight = Math.max(event.arg1, 0);
      next.imageGeneration = Math.max(event.arg2, 0);
      next.text = event.detail;
      break;
    case ITEM_STYLE_METADATA: {
      next.layout = event.arg0;
      next.appearanceMode = event.arg1;
      next.fitPolicy = event.arg2;
      const font = unpackFont(event.value64);
      next.fontFace = font.fontFace;
      next.fontStyle = font.fontStyle;
      next.fontSize = font.fontSize;
      break;
    }
    default:
      next.frame = {
        x: event.arg0,
        y: event.arg1,
        width: Math.max(event.arg2, 0),
        height: Math.max(event.arg3, 0)
      };
      next.text = event.detail;
      break;
  }

  if (event.kind === EVENT_ITEM_SHOWN) next.visible = true;
  if (event.kind === EVENT_ITEM_HIDDEN) next.visible = false;
  return next;
}

function sortItems(items: LcduiItem[]) {
  return [...items].sort((left, right) => {
    const leftIndex = left.formIndex >= 0 ? left.formIndex : Number.MAX_SAFE_INTEGER;
    const rightIndex = right.formIndex >= 0 ? right.formIndex : Number.MAX_SAFE_INTEGER;
    return leftIndex - rightIndex || left.frame.y - right.frame.y || left.id - right.id;
  });
}

function upsertChoice(item: LcduiItem, event: LcduiEvent): LcduiItem {
  if (event.index < 0) return item;
  const font = unpackFont(event.value64);
  const choice: LcduiChoice = {
    index: event.index,
    text: event.text,
    selected: event.arg0 !== 0,
    imageKey: event.arg3 < 0 ? event.arg3 : null,
    fontFace: font.fontFace,
    fontStyle: font.fontStyle,
    fontSize: font.fontSize,
    generation: event.generation
  };
  const choices = [...item.choices.filter((entry) => entry.index !== event.index), choice]
    .sort((left, right) => left.index - right.index);
  return {
    ...item,
    type: event.componentType,
    fitPolicy: event.arg2,
    generation: Math.max(item.generation, event.generation),
    choices
  };
}

function screenContainingItem(state: LcduiState, itemId: number) {
  if (state.activeScreenId) {
    const active = state.screens[state.activeScreenId];
    if (active?.items.some((item) => item.id === itemId)) return active.id;
  }
  return Object.values(state.screens).find((screen) => screen.items.some((item) => item.id === itemId))?.id ?? null;
}

export function applyLcduiEvents(current: LcduiState, events: LcduiEvent[]): LcduiState {
  let state = current;
  for (const event of events) {
    if (event.kind === EVENT_RESET) {
      state = EMPTY_LCDUI_STATE;
      continue;
    }

    if (event.kind >= EVENT_SCREEN_CREATED && event.kind <= EVENT_SCREEN_DELETED) {
      if (event.kind === EVENT_SCREEN_DELETED) {
        const screens = { ...state.screens };
        delete screens[event.componentId];
        state = {
          activeScreenId: state.activeScreenId === event.componentId ? null : state.activeScreenId,
          screens
        };
        continue;
      }

      const existing = state.screens[event.componentId] ?? emptyScreen(event);
      const next = applyScreenEvent(existing, event);
      let screens = { ...state.screens };
      if (event.kind === EVENT_SCREEN_SHOWN && state.activeScreenId && state.activeScreenId !== event.componentId) {
        const previous = screens[state.activeScreenId];
        if (previous) screens[state.activeScreenId] = { ...previous, visible: false };
      }
      screens[event.componentId] = next;
      state = {
        activeScreenId: event.kind === EVENT_SCREEN_SHOWN ? event.componentId : state.activeScreenId,
        screens
      };
      continue;
    }

    if (event.kind >= EVENT_ITEM_CREATED && event.kind <= EVENT_ITEM_DELETED) {
      const parentId = event.parentId || screenContainingItem(state, event.componentId) || state.activeScreenId;
      if (!parentId) continue;
      const screen = state.screens[parentId];
      if (!screen) continue;

      if (event.kind === EVENT_ITEM_DELETED) {
        state = {
          ...state,
          screens: {
            ...state.screens,
            [parentId]: {
              ...screen,
              items: screen.items.filter((item) => item.id !== event.componentId),
              focusedItemId: screen.focusedItemId === event.componentId ? null : screen.focusedItemId
            }
          }
        };
        continue;
      }

      const existing = screen.items.find((item) => item.id === event.componentId) ?? emptyItem(event);
      const nextItem = applyItemEvent(existing, event);
      const items = sortItems([...screen.items.filter((item) => item.id !== nextItem.id), nextItem]);
      state = { ...state, screens: { ...state.screens, [parentId]: { ...screen, items } } };
      continue;
    }

    if (event.kind === EVENT_CHOICE || event.kind === EVENT_CHOICE_DELETED) {
      const screenId = screenContainingItem(state, event.componentId);
      if (!screenId) continue;
      const screen = state.screens[screenId];
      const items = screen.items.map((item) => {
        if (item.id !== event.componentId) return item;
        if (event.kind === EVENT_CHOICE_DELETED) {
          return {
            ...item,
            generation: Math.max(item.generation, event.generation),
            choices: event.index < 0 ? [] : item.choices.filter((choice) => choice.index !== event.index)
          };
        }
        return upsertChoice(item, event);
      });
      state = { ...state, screens: { ...state.screens, [screenId]: { ...screen, items } } };
      continue;
    }

    if (event.kind === EVENT_COMMANDS_RESET) {
      const screens = Object.fromEntries(
        Object.entries(state.screens).map(([id, screen]) => [id, { ...screen, commands: [] }])
      ) as Record<number, LcduiScreen>;
      state = { ...state, screens };
      continue;
    }

    if (event.kind === EVENT_COMMAND) {
      const screenId = state.activeScreenId;
      if (!screenId || !state.screens[screenId]) continue;
      const command: LcduiCommand = {
        id: event.componentId,
        ownerId: event.arg3,
        label: event.text,
        longLabel: event.detail,
        commandType: event.arg0,
        priority: event.arg1,
        scope: event.arg2,
        order: event.index
      };
      const screen = state.screens[screenId];
      state = {
        ...state,
        screens: {
          ...state.screens,
          [screenId]: {
            ...screen,
            commands: [...screen.commands.filter((entry) => entry.id !== command.id), command]
          }
        }
      };
      continue;
    }

    if (event.kind === EVENT_ITEM_FOCUSED) {
      const screenId = screenContainingItem(state, event.componentId) ?? state.activeScreenId;
      if (!screenId || !state.screens[screenId]) continue;
      const screen = state.screens[screenId];
      state = {
        ...state,
        screens: {
          ...state.screens,
          [screenId]: {
            ...screen,
            focusedItemId: event.componentId,
            items: screen.items.map((item) => ({ ...item, focused: item.id === event.componentId }))
          }
        }
      };
    }
  }
  return state;
}

function visibleItems(screen: LcduiScreen) {
  return sortItems(screen.items.filter((item) => item.visible));
}

function isAlertType(type: number) {
  return type >= TYPE_NULL_ALERT && type <= TYPE_CONFIRMATION_ALERT;
}

function isChoiceType(type: number) {
  return type >= TYPE_EXCLUSIVE_CHOICE && type <= TYPE_POPUP_CHOICE;
}

function resolvedScreenKind(screen: LcduiScreen) {
  if (isAlertType(screen.type)) return SCREEN_KIND_ALERT;
  if (screen.type === TYPE_MENU) return SCREEN_KIND_MENU;
  if (screen.nativeKind !== null) return screen.nativeKind;
  const items = visibleItems(screen);
  if (screen.type === TYPE_FORM && items.length === 1 && !items[0].label) {
    if (isChoiceType(items[0].type)) return SCREEN_KIND_LIST;
    if (items[0].type === TYPE_TEXT_FIELD) return SCREEN_KIND_TEXT_BOX;
  }
  return SCREEN_KIND_FORM;
}

function orderedCommands(commands: LcduiCommand[]) {
  return [...commands].sort((left, right) =>
    left.order - right.order || left.priority - right.priority || left.id - right.id
  );
}

function negativeSoftKeyRank(type: number) {
  switch (type) {
    case 3: return 0;
    case 2: return 1;
    case 6: return 2;
    case 7: return 3;
    default: return null;
  }
}

function commandLayout(screen: LcduiScreen) {
  const kind = resolvedScreenKind(screen);
  const ordered = orderedCommands(screen.commands).filter((command) =>
    kind !== SCREEN_KIND_LIST || command.commandType !== 8
  );
  const negative = ordered.filter((command) => negativeSoftKeyRank(command.commandType) !== null);
  negative.sort((left, right) =>
    (negativeSoftKeyRank(left.commandType) ?? Number.MAX_SAFE_INTEGER) -
      (negativeSoftKeyRank(right.commandType) ?? Number.MAX_SAFE_INTEGER) ||
    left.priority - right.priority || left.order - right.order
  );
  const rightCommand = negative[0] ?? null;
  return {
    leftCommands: ordered.filter((command) => command.id !== rightCommand?.id),
    rightCommand
  };
}

function commandDisplayLabel(command: LcduiCommand) {
  if (command.label) return command.label;
  if (command.longLabel) return command.longLabel;
  switch (command.commandType) {
    case 2: return "Quay lại";
    case 3: return "Hủy";
    case 4: return "OK";
    case 5: return "Trợ giúp";
    case 6: return "Dừng";
    case 7: return "Thoát";
    case 8: return "Chọn";
    default: return "Chọn";
  }
}

function commandMenuLabel(command: LcduiCommand) {
  return command.longLabel || commandDisplayLabel(command);
}

function commandIcon(command: LcduiCommand): ReactNode {
  switch (command.commandType) {
    case 2: return <ArrowBackRounded fontSize="small" />;
    case 3: return <CloseRounded fontSize="small" />;
    case 4: return <CheckRounded fontSize="small" />;
    case 5: return <HelpOutlineRounded fontSize="small" />;
    case 6: return <StopRounded fontSize="small" />;
    case 7: return <LogoutRounded fontSize="small" />;
    case 8: return <TouchAppRounded fontSize="small" />;
    default: return <MoreHorizRounded fontSize="small" />;
  }
}

function isDestructiveCommand(command: LcduiCommand) {
  return command.commandType === 6 || command.commandType === 7;
}

function fontSx(fontFace: number, fontStyle: number, fontSize: number, forceRegular = false) {
  return {
    fontFamily: fontFace === 32 ? "ui-monospace, SFMono-Regular, Menlo, monospace" : "inherit",
    fontSize: fontSize === 8 ? 13 : fontSize === 16 ? 21 : 17,
    fontWeight: forceRegular ? 400 : (fontStyle & 1) !== 0 ? 700 : 400,
    fontStyle: (fontStyle & 2) !== 0 ? "italic" : "normal",
    textDecoration: (fontStyle & 4) !== 0 ? "underline" : "none"
  } as const;
}

function horizontalJustify(item: LcduiItem) {
  switch (item.layout & 0x03) {
    case 2: return "flex-end";
    case 3: return "center";
    default: return "flex-start";
  }
}

function PixelImage({ runtime, componentId, generation, alt, className = "lcdui-image", fallback = null }: {
  runtime: PhoneMEWebRuntime;
  componentId: number;
  generation: number;
  alt: string;
  className?: string;
  fallback?: ReactNode;
}) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [available, setAvailable] = useState<boolean | null>(null);

  useEffect(() => {
    let active = true;
    setAvailable(null);
    void runtime.copyLcduiImage(componentId).then((frame) => {
      const canvas = canvasRef.current;
      if (!active) return;
      if (!frame || !canvas) {
        setAvailable(false);
        return;
      }
      canvas.width = frame.width;
      canvas.height = frame.height;
      canvas.getContext("2d")?.putImageData(
        new ImageData(frame.pixels, frame.width, frame.height),
        0,
        0
      );
      setAvailable(true);
    }).catch(() => {
      if (active) setAvailable(false);
    });
    return () => {
      active = false;
    };
  }, [componentId, generation, runtime]);

  return <>
    <canvas
      className={className}
      ref={canvasRef}
      role="img"
      aria-label={alt}
      style={{ display: available === false ? "none" : undefined }}
    />
    {available === false ? fallback : null}
  </>;
}

function ItemLabel({ item }: { item: LcduiItem }) {
  return item.label ? <FormLabel className="lcdui-item-label">{item.label}</FormLabel> : null;
}

function ChoiceImage({ choice, runtime }: { choice: LcduiChoice; runtime: PhoneMEWebRuntime }) {
  if (choice.imageKey === null) return null;
  return <Box className="lcdui-choice-icon">
    <PixelImage
      runtime={runtime}
      componentId={choice.imageKey}
      generation={choice.generation}
      alt=""
      className="lcdui-image lcdui-choice-image"
    />
  </Box>;
}

type ChoiceAccessory = "disclosure" | "checkbox" | "radio" | "progress";

function ChoiceCell({ choice, item, runtime, accessory, regularFont = false }: {
  choice: LcduiChoice;
  item: LcduiItem;
  runtime: PhoneMEWebRuntime;
  accessory: ChoiceAccessory;
  regularFont?: boolean;
}) {
  return <Box className="lcdui-choice-cell">
    <ChoiceImage choice={choice} runtime={runtime} />
    <Typography
      className="lcdui-choice-text"
      sx={{
        ...fontSx(choice.fontFace, choice.fontStyle, choice.fontSize, regularFont),
        overflow: "hidden",
        display: "-webkit-box",
        WebkitBoxOrient: "vertical",
        WebkitLineClamp: item.fitPolicy === 2 ? 1 : 2
      }}
    >{choice.text}</Typography>
    <Box className="lcdui-choice-accessory">
      {accessory === "disclosure" ? <ChevronRightRounded fontSize="small" /> : null}
      {accessory === "checkbox" ? <Checkbox checked={choice.selected} readOnly size="small" tabIndex={-1} /> : null}
      {accessory === "radio" ? <Radio checked={choice.selected} readOnly size="small" tabIndex={-1} /> : null}
      {accessory === "progress" ? <CircularProgress size={18} /> : null}
    </Box>
  </Box>;
}

function FormChoiceItem({ item, runtime }: { item: LcduiItem; runtime: PhoneMEWebRuntime }) {
  const [pendingImplicitIndex, setPendingImplicitIndex] = useState<number | null>(null);

  if (item.type === TYPE_POPUP_CHOICE) {
    const selected = item.choices.find((choice) => choice.selected)?.index ?? item.choices[0]?.index ?? -1;
    return <Stack direction="row" spacing={2} className="lcdui-popup-choice" sx={{ alignItems: "center" }}>
      <Typography sx={{ flex: 1 }}>{item.label || "Lựa chọn"}</Typography>
      <FormControl size="small" sx={{ minWidth: 120, maxWidth: "70%" }}>
        <Select
          value={selected}
          onFocus={() => runtime.focusItem(item.id)}
          onChange={(event) => runtime.setChoice(item.id, Number(event.target.value), true)}
        >
          {item.choices.map((choice) => <MenuItem key={choice.index} value={choice.index}>
            <Stack direction="row" spacing={1.25} sx={{ minWidth: 0, alignItems: "center" }}>
              <ChoiceImage choice={choice} runtime={runtime} />
              <Typography noWrap>{choice.text}</Typography>
            </Stack>
          </MenuItem>)}
        </Select>
      </FormControl>
    </Stack>;
  }

  return <Stack spacing={0}>
    <ItemLabel item={item} />
    <Box className="lcdui-inline-choice-list">
      {item.choices.map((choice, index) => {
        const accessory: ChoiceAccessory = item.type === TYPE_MULTIPLE_CHOICE
          ? "checkbox"
          : item.type === TYPE_EXCLUSIVE_CHOICE
            ? "radio"
            : pendingImplicitIndex === choice.index ? "progress" : "disclosure";
        return <Box key={choice.index}>
          <Button
            className="lcdui-choice-button"
            fullWidth
            onClick={() => {
              runtime.focusItem(item.id);
              if (item.type === TYPE_IMPLICIT_CHOICE) {
                setPendingImplicitIndex(choice.index);
                window.setTimeout(() => setPendingImplicitIndex((current) => current === choice.index ? null : current), 900);
              }
              runtime.setChoice(
                item.id,
                choice.index,
                item.type === TYPE_MULTIPLE_CHOICE ? !choice.selected : true
              );
            }}
          >
            <ChoiceCell choice={choice} item={item} runtime={runtime} accessory={accessory} />
          </Button>
          {index < item.choices.length - 1 ? <Divider className={choice.imageKey === null ? "" : "with-icon"} /> : null}
        </Box>;
      })}
    </Box>
  </Stack>;
}

function textInputMode(constraints: number): "text" | "email" | "tel" | "url" | "numeric" | "decimal" {
  switch (constraints & 0xffff) {
    case 1: return "email";
    case 2: return "numeric";
    case 3: return "tel";
    case 4: return "url";
    case 5: return "decimal";
    default: return "text";
  }
}

function textFieldType(constraints: number) {
  if ((constraints & 0x10000) !== 0) return "password";
  switch (constraints & 0xffff) {
    case 1: return "email";
    case 3: return "tel";
    case 4: return "url";
    default: return "text";
  }
}

function autoCapitalize(constraints: number) {
  if ((constraints & 0x200000) !== 0) return "sentences";
  if ((constraints & 0x100000) !== 0) return "words";
  return "none";
}

function TextFieldItem({ item, runtime, textBox = false }: {
  item: LcduiItem;
  runtime: PhoneMEWebRuntime;
  textBox?: boolean;
}) {
  const [text, setText] = useState(item.text);
  useEffect(() => {
    setText((current) => current === item.text ? current : item.text);
  }, [item.text]);

  const uneditable = (item.constraints & 0x20000) !== 0;
  const nonPredictive = (item.constraints & 0x80000) !== 0;
  const sensitive = (item.constraints & 0x40000) !== 0;

  if (uneditable) {
    return <Stack spacing={1} className={textBox ? "lcdui-textbox" : undefined}>
      {!textBox ? <ItemLabel item={item} /> : null}
      <Box className="lcdui-readonly-text"><Typography sx={{ whiteSpace: "pre-wrap" }}>{text}</Typography></Box>
    </Stack>;
  }

  return <Stack spacing={1} className={textBox ? "lcdui-textbox" : undefined}>
    {!textBox ? <ItemLabel item={item} /> : null}
    <TextField
      fullWidth
      multiline={textBox}
      minRows={textBox ? 8 : undefined}
      maxRows={textBox ? undefined : 1}
      value={text}
      type={textBox ? undefined : textFieldType(item.constraints)}
      autoFocus={textBox}
      onFocus={() => runtime.focusItem(item.id)}
      onChange={(event) => {
        const next = item.maxSize > 0 ? event.target.value.slice(0, item.maxSize) : event.target.value;
        setText(next);
        runtime.setText(item.id, next, event.target.selectionStart ?? next.length);
      }}
      slotProps={{
        htmlInput: {
          maxLength: item.maxSize > 0 ? item.maxSize : undefined,
          inputMode: textInputMode(item.constraints),
          autoCapitalize: autoCapitalize(item.constraints),
          autoComplete: sensitive || nonPredictive ? "off" : undefined,
          spellCheck: nonPredictive ? false : undefined
        }
      }}
    />
    {item.maxSize > 0 ? <Typography className="lcdui-counter" variant="caption">{text.length} / {item.maxSize}</Typography> : null}
  </Stack>;
}

function formatDateInput(seconds: number, mode: number) {
  if (!seconds) return "";
  const date = new Date(seconds * 1000);
  const pad = (value: number) => String(value).padStart(2, "0");
  const ymd = `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}`;
  const hm = `${pad(date.getHours())}:${pad(date.getMinutes())}`;
  if (mode === 1) return hm;
  if (mode === 2) return ymd;
  return `${ymd}T${hm}`;
}

function parseDateInput(value: string, mode: number, fallbackSeconds: number) {
  if (!value) return 0;
  if (mode === 1) {
    const [hours, minutes] = value.split(":").map(Number);
    const date = fallbackSeconds ? new Date(fallbackSeconds * 1000) : new Date();
    date.setHours(hours || 0, minutes || 0, 0, 0);
    return Math.trunc(date.getTime() / 1000);
  }
  const parsed = mode === 2 ? new Date(`${value}T00:00`) : new Date(value);
  return Number.isFinite(parsed.getTime()) ? Math.trunc(parsed.getTime() / 1000) : fallbackSeconds;
}

function DateFieldItem({ item, runtime }: { item: LcduiItem; runtime: PhoneMEWebRuntime }) {
  const type = item.inputMode === 1 ? "time" : item.inputMode === 2 ? "date" : "datetime-local";
  return <TextField
    fullWidth
    label={item.label || "Ngày & giờ"}
    type={type}
    value={formatDateInput(item.dateUnixSeconds, item.inputMode)}
    onFocus={() => runtime.focusItem(item.id)}
    onChange={(event) => runtime.setDate(item.id, parseDateInput(event.target.value, item.inputMode, item.dateUnixSeconds))}
    slotProps={{ inputLabel: { shrink: true } }}
    helperText={item.text || undefined}
  />;
}

function GaugeItem({ item, runtime }: { item: LcduiItem; runtime: PhoneMEWebRuntime }) {
  if (item.type === TYPE_PROGRESS_GAUGE || !item.interactive) {
    if (item.maxValue === -1) {
      const active = item.value === 2 || item.value === 3;
      return <Stack spacing={1}>
        <Stack direction="row" sx={{ justifyContent: "space-between", alignItems: "baseline" }}>
          <ItemLabel item={{ ...item, label: item.label || "Tiến trình" }} />
          <Typography variant="caption" color="text.secondary">{active ? "Đang tải…" : item.value === 1 ? "Tạm dừng" : "Chờ"}</Typography>
        </Stack>
        <LinearProgress variant={active ? "indeterminate" : "determinate"} value={item.value === 1 ? 100 : 0} />
      </Stack>;
    }
    const maximum = Math.max(item.maxValue, 1);
    const percent = Math.min(100, Math.max(0, item.value) / maximum * 100);
    return <Stack spacing={1}>
      <Stack direction="row" sx={{ justifyContent: "space-between", alignItems: "baseline" }}>
        <ItemLabel item={{ ...item, label: item.label || "Tiến trình" }} />
        <Typography variant="caption" color="text.secondary">{Math.round(percent)}%</Typography>
      </Stack>
      <LinearProgress variant="determinate" value={percent} />
    </Stack>;
  }

  return <Stack spacing={1}>
    <Stack direction="row" sx={{ justifyContent: "space-between", alignItems: "baseline" }}>
      <ItemLabel item={{ ...item, label: item.label || "Giá trị" }} />
      <Typography variant="caption" color="text.secondary">{item.value} / {Math.max(item.maxValue, 1)}</Typography>
    </Stack>
    <Slider
      value={item.value}
      min={0}
      max={Math.max(item.maxValue, 1)}
      step={1}
      onFocus={() => runtime.focusItem(item.id)}
      onChange={(_, value) => runtime.setGauge(item.id, Number(value))}
    />
  </Stack>;
}

function CustomItemView({ item, runtime }: { item: LcduiItem; runtime: PhoneMEWebRuntime }) {
  const activePointer = useRef<number | null>(null);
  const sendPointer = (event: React.PointerEvent<HTMLDivElement>, action: number) => {
    const bounds = event.currentTarget.getBoundingClientRect();
    const width = Math.max(bounds.width, 1);
    const height = Math.max(bounds.height, 1);
    const contentWidth = Math.max(item.imageWidth || item.frame.width || 1, 1);
    const contentHeight = Math.max(item.imageHeight || item.frame.height || 1, 1);
    const x = Math.max(0, Math.min(contentWidth - 1, Math.floor((event.clientX - bounds.left) * contentWidth / width)));
    const y = Math.max(0, Math.min(contentHeight - 1, Math.floor((event.clientY - bounds.top) * contentHeight / height)));
    runtime.sendPointer(x, y, action);
  };

  return <Box
    className="lcdui-custom-item"
    onPointerDown={(event) => {
      if (activePointer.current !== null || (event.pointerType === "mouse" && event.button !== 0)) return;
      event.preventDefault();
      activePointer.current = event.pointerId;
      runtime.focusItem(item.id);
      event.currentTarget.setPointerCapture?.(event.pointerId);
      sendPointer(event, 1);
    }}
    onPointerMove={(event) => {
      if (activePointer.current !== event.pointerId) return;
      event.preventDefault();
      sendPointer(event, 3);
    }}
    onPointerUp={(event) => {
      if (activePointer.current !== event.pointerId) return;
      event.preventDefault();
      activePointer.current = null;
      sendPointer(event, 2);
    }}
    onPointerCancel={(event) => {
      if (activePointer.current !== event.pointerId) return;
      activePointer.current = null;
      sendPointer(event, 2);
    }}
  >
    <PixelImage
      runtime={runtime}
      componentId={item.id}
      generation={item.imageGeneration || item.generation}
      alt={item.text || item.label || "Custom item"}
    />
  </Box>;
}

function LcduiItemView({ item, runtime }: {
  item: LcduiItem;
  runtime: PhoneMEWebRuntime;
}) {
  if (!item.visible && item.type !== TYPE_IMPLICIT_CHOICE) return null;
  if (isChoiceType(item.type)) return <FormChoiceItem item={item} runtime={runtime} />;
  if (item.type === TYPE_CUSTOM_ITEM) {
    return <Stack spacing={1} sx={{ alignItems: horizontalJustify(item) }}>
      <ItemLabel item={item} />
      <CustomItemView item={item} runtime={runtime} />
    </Stack>;
  }
  if (item.type === TYPE_DATE_FIELD) return <DateFieldItem item={item} runtime={runtime} />;
  if (item.type === TYPE_PROGRESS_GAUGE || item.type === TYPE_INTERACTIVE_GAUGE) return <GaugeItem item={item} runtime={runtime} />;
  if (item.type >= TYPE_PLAIN_IMAGE && item.type <= TYPE_BUTTON_IMAGE) {
    const image = <PixelImage
      runtime={runtime}
      componentId={item.id}
      generation={item.imageGeneration || item.generation}
      alt={item.text || item.label || "Ảnh"}
    />;
    return <Stack spacing={1} sx={{ alignItems: horizontalJustify(item) }}>
      <ItemLabel item={item} />
      {item.type === TYPE_PLAIN_IMAGE ? image : <Button
        variant={item.type === TYPE_BUTTON_IMAGE ? "contained" : "text"}
        onClick={() => runtime.activateItem(item.id)}
      >{image}</Button>}
    </Stack>;
  }
  if (item.type === TYPE_SPACER) {
    return <Box sx={{ width: Math.max(0, item.frame.width), height: Math.min(Math.max(item.frame.height, 8), 48) }} />;
  }
  if (item.type >= TYPE_PLAIN_STRING && item.type <= TYPE_BUTTON_STRING) {
    const text = <Typography sx={{ ...fontSx(item.fontFace, item.fontStyle, item.fontSize), whiteSpace: "pre-wrap" }}>{item.text}</Typography>;
    return <Stack spacing={1} sx={{ alignItems: horizontalJustify(item) }}>
      <ItemLabel item={item} />
      {item.type === TYPE_PLAIN_STRING ? text : <Button
        fullWidth={item.type === TYPE_BUTTON_STRING}
        variant={item.type === TYPE_BUTTON_STRING ? "contained" : "text"}
        onClick={() => runtime.activateItem(item.id)}
        sx={{ justifyContent: item.type === TYPE_HYPERLINK_STRING ? "flex-start" : "center", textAlign: "left" }}
      >{text}</Button>}
    </Stack>;
  }
  if (item.type === TYPE_TEXT_FIELD) return <TextFieldItem item={item} runtime={runtime} />;
  return null;
}

function FormScreen({ screen, runtime }: { screen: LcduiScreen; runtime: PhoneMEWebRuntime }) {
  const items = visibleItems(screen);
  const scrollRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const element = scrollRef.current;
    if (!element || screen.scrollPosition <= 0) return;
    if (Math.abs(element.scrollTop - screen.scrollPosition) > 12) element.scrollTop = screen.scrollPosition;
  }, [screen.scrollPosition]);

  if (!items.length) {
    return <Box className="lcdui-empty"><Typography variant="body2" color="text.secondary">Form chưa có nội dung.</Typography></Box>;
  }

  return <Box
    ref={scrollRef}
    className="native-lcdui-content lcdui-form-scroll"
    onScroll={(event) => runtime.setScrollPosition(Math.round(event.currentTarget.scrollTop))}
  >
    <Box className="lcdui-form-card">
      {items.map((item, index) => {
        const hidesSeparator = item.type === TYPE_SPACER || isChoiceType(item.type) || item.type === TYPE_CUSTOM_ITEM;
        return <Box key={item.id}>
          <Box
            className={`lcdui-form-row ${item.type === TYPE_SPACER ? "spacer-row" : ""}`}
            onFocus={() => runtime.focusItem(item.id)}
          >
            <LcduiItemView item={item} runtime={runtime} />
          </Box>
          {index < items.length - 1 && !hidesSeparator ? <Divider className="lcdui-form-divider" /> : null}
        </Box>;
      })}
    </Box>
  </Box>;
}

function ListScreen({ screen, item, runtime }: { screen: LcduiScreen; item: LcduiItem; runtime: PhoneMEWebRuntime }) {
  const [pendingImplicitIndex, setPendingImplicitIndex] = useState<number | null>(null);
  const [commandMenu, setCommandMenu] = useState<{ anchor: HTMLElement; choiceIndex: number } | null>(null);
  const itemCommands = orderedCommands(screen.commands).filter((command) => command.commandType === 8);
  const effectiveType = item.type === TYPE_POPUP_CHOICE ? TYPE_IMPLICIT_CHOICE : item.type;

  if (!item.choices.length) {
    return <Box className="lcdui-empty"><Typography variant="body2" color="text.secondary">Danh sách đang trống.</Typography></Box>;
  }

  return <List className="lcdui-list" disablePadding>
    {item.choices.map((choice) => {
      const accessory: ChoiceAccessory = effectiveType === TYPE_MULTIPLE_CHOICE
        ? "checkbox"
        : effectiveType === TYPE_EXCLUSIVE_CHOICE
          ? "radio"
          : pendingImplicitIndex === choice.index ? "progress" : "disclosure";
      return <Box className="lcdui-list-row" key={choice.index}>
        <ListItemButton
          className="lcdui-list-main-action"
          selected={effectiveType !== TYPE_IMPLICIT_CHOICE && choice.selected}
          onClick={() => {
            runtime.focusItem(item.id);
            if (effectiveType === TYPE_IMPLICIT_CHOICE) {
              setPendingImplicitIndex(choice.index);
              window.setTimeout(() => setPendingImplicitIndex((current) => current === choice.index ? null : current), 900);
            }
            runtime.setChoice(item.id, choice.index, effectiveType === TYPE_MULTIPLE_CHOICE ? !choice.selected : true);
          }}
        >
          <ChoiceCell choice={choice} item={item} runtime={runtime} accessory={accessory} regularFont />
        </ListItemButton>
        {itemCommands.length ? <IconButton
          className="lcdui-list-item-menu"
          aria-label={`Thao tác cho ${choice.text}`}
          onClick={(event) => setCommandMenu({ anchor: event.currentTarget, choiceIndex: choice.index })}
        ><MoreHorizRounded /></IconButton> : null}
      </Box>;
    })}
    <Menu
      anchorEl={commandMenu?.anchor ?? null}
      open={Boolean(commandMenu)}
      onClose={() => setCommandMenu(null)}
    >
      {itemCommands.map((command) => <MenuItem
        key={command.id}
        onClick={() => {
          if (commandMenu) runtime.selectListItemCommand(item.id, commandMenu.choiceIndex, command.id);
          setCommandMenu(null);
        }}
        sx={{ color: isDestructiveCommand(command) ? "error.main" : undefined }}
      >
        <Box className="lcdui-command-menu-icon">{commandIcon(command)}</Box>
        {commandMenuLabel(command)}
      </MenuItem>)}
    </Menu>
  </List>;
}

function TextBoxScreen({ item, runtime }: { item: LcduiItem; runtime: PhoneMEWebRuntime }) {
  return <Box className="native-lcdui-content lcdui-textbox-container">
    <TextFieldItem item={item} runtime={runtime} textBox />
  </Box>;
}

function MenuScreen({ screen, runtime }: { screen: LcduiScreen; runtime: PhoneMEWebRuntime }) {
  return <List className="lcdui-list" disablePadding>
    {orderedCommands(screen.commands).map((command) => <ListItemButton
      key={command.id}
      onClick={() => runtime.selectCommand(command.id)}
      sx={{ color: isDestructiveCommand(command) ? "error.main" : undefined }}
    >
      <Box className="lcdui-menu-command-icon">{commandIcon(command)}</Box>
      <ListItemText
        primary={commandDisplayLabel(command)}
        secondary={command.longLabel && command.longLabel !== command.label ? command.longLabel : undefined}
      />
      <ChevronRightRounded className="lcdui-disclosure" fontSize="small" />
    </ListItemButton>)}
  </List>;
}

function alertFallback(type: number) {
  switch (type) {
    case TYPE_INFO_ALERT: return <InfoOutlined />;
    case TYPE_WARNING_ALERT: return <WarningAmberRounded />;
    case TYPE_ERROR_ALERT: return <ErrorOutlineRounded />;
    case TYPE_ALARM_ALERT: return <AlarmRounded />;
    case TYPE_CONFIRMATION_ALERT: return <HelpOutlineRounded />;
    default: return <NotificationsNoneRounded />;
  }
}

function AlertScreen({ screen, runtime }: { screen: LcduiScreen; runtime: PhoneMEWebRuntime }) {
  const items = visibleItems(screen);
  return <Box className="native-lcdui-content lcdui-alert-container">
    <Box className="lcdui-alert-card">
      <Box className={`lcdui-alert-icon alert-type-${screen.type}`}>
        <PixelImage
          runtime={runtime}
          componentId={screen.id}
          generation={screen.generation}
          alt=""
          className="lcdui-image lcdui-alert-image"
          fallback={alertFallback(screen.type)}
        />
      </Box>
      {screen.title ? <Typography variant="h6" className="lcdui-alert-title">{screen.title}</Typography> : null}
      {screen.detail ? <Typography color="text.secondary" className="lcdui-alert-detail">{screen.detail}</Typography> : null}
      {items.length ? <>
        <Divider sx={{ width: "100%" }} />
        <Stack spacing={1.5} sx={{ width: "100%" }}>
          {items.map((item) => <LcduiItemView key={item.id} item={item} runtime={runtime} />)}
        </Stack>
      </> : null}
    </Box>
  </Box>;
}

export function LCDUICommandBar({ screen, runtime }: { screen: LcduiScreen; runtime: PhoneMEWebRuntime }) {
  const [menuAnchor, setMenuAnchor] = useState<HTMLElement | null>(null);
  const layout = useMemo(() => commandLayout(screen), [screen]);
  if (!layout.leftCommands.length && !layout.rightCommand) return null;

  const renderCommand = (command: LcduiCommand, prominent: boolean) => <Button
    key={command.id}
    fullWidth
    variant={prominent ? "contained" : "outlined"}
    color={isDestructiveCommand(command) ? "error" : "primary"}
    startIcon={commandIcon(command)}
    onClick={() => runtime.selectCommand(command.id)}
  >{commandDisplayLabel(command)}</Button>;

  return <Box className="native-lcdui-actions">
    <Box className="lcdui-softkey-slot">
      {layout.leftCommands.length === 1
        ? renderCommand(layout.leftCommands[0], !isDestructiveCommand(layout.leftCommands[0]))
        : layout.leftCommands.length > 1
          ? <Button
              fullWidth
              variant="contained"
              startIcon={<MoreHorizRounded />}
              onClick={(event) => setMenuAnchor(event.currentTarget)}
            >Tùy chọn</Button>
          : null}
    </Box>
    <Box className="lcdui-softkey-slot">
      {layout.rightCommand ? renderCommand(layout.rightCommand, false) : null}
    </Box>
    <Menu anchorEl={menuAnchor} open={Boolean(menuAnchor)} onClose={() => setMenuAnchor(null)}>
      {layout.leftCommands.map((command) => <MenuItem
        key={command.id}
        sx={{ color: isDestructiveCommand(command) ? "error.main" : undefined }}
        onClick={() => {
          runtime.selectCommand(command.id);
          setMenuAnchor(null);
        }}
      >
        <Box className="lcdui-command-menu-icon">{commandIcon(command)}</Box>
        {commandMenuLabel(command)}
      </MenuItem>)}
    </Menu>
  </Box>;
}

export function NativeLcduiView({ runtime, screen }: {
  runtime: PhoneMEWebRuntime;
  screen: LcduiScreen;
}) {
  const kind = resolvedScreenKind(screen);
  const items = visibleItems(screen);

  return <Box className={`native-lcdui lcdui-kind-${kind}`}>
    {kind !== SCREEN_KIND_ALERT && screen.detail ? <Box className="lcdui-screen-header">
      <Typography variant="body2" color="text.secondary" sx={{ whiteSpace: "pre-wrap" }}>{screen.detail}</Typography>
    </Box> : null}

    <Box className="lcdui-screen-body">
      {kind === SCREEN_KIND_ALERT ? <AlertScreen screen={screen} runtime={runtime} /> : null}
      {kind === SCREEN_KIND_LIST ? (items[0]
        ? <ListScreen screen={screen} item={items[0]} runtime={runtime} />
        : <Box className="lcdui-empty"><Typography color="text.secondary">Đang chuẩn bị danh sách…</Typography></Box>) : null}
      {kind === SCREEN_KIND_TEXT_BOX ? (items[0]
        ? <TextBoxScreen item={items[0]} runtime={runtime} />
        : <Box className="lcdui-empty"><Typography color="text.secondary">Đang chuẩn bị nội dung…</Typography></Box>) : null}
      {kind === SCREEN_KIND_MENU ? <MenuScreen screen={screen} runtime={runtime} /> : null}
      {kind === SCREEN_KIND_FORM ? <FormScreen screen={screen} runtime={runtime} /> : null}
    </Box>

    <LCDUICommandBar screen={screen} runtime={runtime} />
  </Box>;
}
