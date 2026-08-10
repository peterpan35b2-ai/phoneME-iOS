import { createRoot } from "react-dom/client";
import { CssBaseline } from "@mui/material";
import App from "./App";
import "./styles.css";
import { installWebMediaBridge } from "./webMediaBridge";
import { initPwa } from "./pwa";

installWebMediaBridge();

if ("serviceWorker" in navigator) {
  if (import.meta.env.PROD) {
    // Register as soon as the entry bundle executes instead of waiting for the
    // load event. This gives Safari/iOS more time to finish the offline cache
    // before the user backgrounds or closes the newly installed PWA.
    void initPwa().catch((error) => {
      console.warn("phoneME PWA initialization failed", error);
    });
  } else {
    window.addEventListener("load", () => {
      // Never let a previously installed PWA worker/cache intercept Vite dev.
      // Stale workers can otherwise serve an old WASM/runtime after F5 and make
      // local networking/debugging look nondeterministic.
      void (async () => {
        const hadController = navigator.serviceWorker.controller !== null;
        const registrations = await navigator.serviceWorker.getRegistrations();
        await Promise.all(registrations.map((registration) => registration.unregister()));
        if ("caches" in window) {
          const keys = await caches.keys();
          await Promise.all(keys.filter((key) => key.startsWith("phoneme-")).map((key) => caches.delete(key)));
        }
        const reloadKey = "phoneme-dev-sw-reset";
        if (hadController && sessionStorage.getItem(reloadKey) !== "1") {
          sessionStorage.setItem(reloadKey, "1");
          window.location.reload();
          return;
        }
        sessionStorage.removeItem(reloadKey);
      })().catch(() => undefined);
    });
  }
}

const root = document.getElementById("root");
if (!root) throw new Error("Missing #root element");

createRoot(root).render(
  <>
    <CssBaseline />
    <App />
  </>
);
