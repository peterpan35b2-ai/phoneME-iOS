import { createTheme, type PaletteMode } from "@mui/material/styles";

export function createPhoneMETheme(mode: PaletteMode) {
  return createTheme({
    cssVariables: true,
    palette: mode === "dark" ? {
      mode,
      primary: { main: "#b9c3ff", contrastText: "#0f267f" },
      secondary: { main: "#c2c5dd" },
      background: { default: "#121318", paper: "#1b1b21" },
      success: { main: "#8cd6a5" },
      error: { main: "#ffb4ab" }
    } : {
      mode,
      primary: { main: "#4355b9", contrastText: "#ffffff" },
      secondary: { main: "#595d72" },
      background: { default: "#fbf8ff", paper: "#ffffff" },
      success: { main: "#276b42" },
      error: { main: "#ba1a1a" }
    },
    typography: {
      fontFamily: '"Google Sans Variable", "Google Sans", system-ui, sans-serif',
      button: { textTransform: "none", fontWeight: 650 },
      h4: { fontWeight: 650, letterSpacing: "-0.025em" },
      h5: { fontWeight: 650, letterSpacing: "-0.02em" },
      h6: { fontWeight: 650 },
      subtitle1: { fontWeight: 650 },
      subtitle2: { fontWeight: 650 }
    },
    shape: { borderRadius: 16 },
    components: {
      MuiButton: {
        defaultProps: { disableElevation: true },
        styleOverrides: {
          root: { minHeight: 40, borderRadius: 999, paddingInline: 18 }
        }
      },
      MuiIconButton: {
        styleOverrides: { root: { borderRadius: 999 } }
      },
      MuiCard: {
        defaultProps: { elevation: 0 },
        styleOverrides: {
          root: { border: "1px solid", borderColor: "var(--mui-palette-divider)" }
        }
      },
      MuiPaper: {
        styleOverrides: { rounded: { borderRadius: 20 } }
      },
      MuiChip: {
        styleOverrides: { root: { borderRadius: 10 } }
      },
      MuiTextField: {
        defaultProps: { variant: "outlined" }
      }
    }
  });
}
