import { createTheme, type PaletteMode } from "@mui/material/styles";

// Appbar = neutral MD3 surface (surface-container). The PWA status-bar theme-color
// must mirror this exactly; keep these consts the single source of truth shared with
// index.html's static fallbacks and App.tsx's runtime <meta name="theme-color">.
// Values must match the --md3-surface-container tokens in styles.css.
export const APPBAR_THEME_COLOR = {
  light: "#efedf4",
  dark: "#1f1f25"
} as const;

export function appbarThemeColor(mode: PaletteMode) {
  return mode === "dark" ? APPBAR_THEME_COLOR.dark : APPBAR_THEME_COLOR.light;
}

export function createPhoneMETheme(mode: PaletteMode) {
  return createTheme({
    cssVariables: true,
    palette: mode === "dark" ? {
      mode,
      primary: { main: "#b9c3ff", contrastText: "#0f1a78" },
      secondary: { main: "#c6c4dd" },
      background: { default: "#121318", paper: "#1b1b21" },
      text: { primary: "#e5e1e9", secondary: "#c6c5d0", disabled: "#8a8a94" },
      divider: "#45464f",
      success: { main: "#8cd6a5" },
      error: { main: "#ffb4ab" },
      warning: { main: "#ffc480" }
    } : {
      mode,
      primary: { main: "#4355b9", contrastText: "#ffffff" },
      secondary: { main: "#595d72" },
      background: { default: "#fbf8ff", paper: "#ffffff" },
      text: { primary: "#1b1b21", secondary: "#46464f", disabled: "#8a8a94" },
      divider: "#c7c5d0",
      success: { main: "#276b42" },
      error: { main: "#ba1a1a" },
      warning: { main: "#7d5700" }
    },
    // MD3 type scale: appbar headline = Title Large (22), the rest mapped to roles.
    typography: {
      fontFamily: '"Google Sans Variable", "Google Sans", system-ui, sans-serif',
      htmlFontSize: 16,
      fontSize: 14,
      h4: { fontSize: "1.75rem", lineHeight: "2.25rem", fontWeight: 500, letterSpacing: 0 },
      h5: { fontSize: "1.5rem", lineHeight: "2rem", fontWeight: 500, letterSpacing: 0 },
      h6: { fontSize: "1.375rem", lineHeight: "1.75rem", fontWeight: 500, letterSpacing: 0 },
      subtitle1: { fontSize: "1rem", lineHeight: "1.5rem", fontWeight: 500, letterSpacing: "0.009em" },
      subtitle2: { fontSize: "0.875rem", lineHeight: "1.25rem", fontWeight: 500, letterSpacing: "0.007em" },
      body1: { fontSize: "1rem", lineHeight: "1.5rem", fontWeight: 400, letterSpacing: "0.031em" },
      body2: { fontSize: "0.875rem", lineHeight: "1.25rem", fontWeight: 400, letterSpacing: "0.018em" },
      button: { textTransform: "none", fontWeight: 500, fontSize: "0.875rem", lineHeight: "1.25rem", letterSpacing: "0.007em" },
      caption: { fontSize: "0.6875rem", lineHeight: "1rem", fontWeight: 500, letterSpacing: "0.025em" },
      overline: { textTransform: "none", fontSize: "0.75rem", lineHeight: "1rem", fontWeight: 500, letterSpacing: "0.031em" }
    },
    // MD3 shape: large 16 (cards/sections), full 999 (buttons/search), extra-large 28 (dialogs).
    shape: { borderRadius: 16 },
    components: {
      MuiAppBar: {
        defaultProps: { color: "transparent", elevation: 0 },
        styleOverrides: {
          root: { backgroundImage: "none", backgroundColor: "transparent", boxShadow: "none", color: "inherit" }
        }
      },
      MuiButton: {
        defaultProps: { disableElevation: true },
        styleOverrides: {
          root: { textTransform: "none", borderRadius: 999, minHeight: 40, paddingInline: 24, fontWeight: 500 },
          outlined: { paddingInline: 24 },
          text: { paddingInline: 12 }
        }
      },
      MuiIconButton: {
        styleOverrides: { root: { borderRadius: 999 } }
      },
      MuiPaper: {
        styleOverrides: { rounded: { borderRadius: 16 } }
      },
      MuiCard: {
        defaultProps: { elevation: 0 },
        styleOverrides: {
          root: {
            borderRadius: 16,
            border: "1px solid var(--md3-outline-variant)",
            backgroundColor: "var(--md3-surface-container-low)"
          }
        }
      },
      MuiChip: {
        styleOverrides: { root: { borderRadius: 8, fontWeight: 500 } }
      },
      MuiDialog: {
        styleOverrides: { paper: { borderRadius: 28 } }
      },
      MuiTextField: {
        defaultProps: { variant: "outlined" }
      }
    }
  });
}
