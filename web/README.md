# phoneME Web

Frontend Material UI và bản build WebAssembly của phoneME Core.

## Yêu cầu

- Node.js và npm
- CMake
- Emscripten (`emcc`, `emcmake`)

## Chạy phát triển

```bash
cd /Users/duypham/Developer/phoneME-iOS/web
npm install
npm run dev
```

Vite tự thêm các header COOP/COEP cần cho WebAssembly đa luồng.

## Build production

```bash
cd /Users/duypham/Developer/phoneME-iOS/web
npm run build
npm run preview
```

`npm run build` sẽ:

1. Build `phoneMECore` sang WebAssembly bằng `Core/Tools/build-wasm.sh`.
2. Chép `phoneme.js` và `phoneme.wasm` vào `web/public/wasm`.
3. Type-check frontend và tạo bundle production trong `web/dist`.

Khi deploy, máy chủ phải giữ các header trong `web/public/_headers`:

- `Cross-Origin-Opener-Policy: same-origin`
- `Cross-Origin-Embedder-Policy: require-corp`
- `Cross-Origin-Resource-Policy: same-origin`

## Dữ liệu

Suite, RMS, save game và filesystem được mount bằng IDBFS/IndexedDB tại `/phoneme`. IDBFS bật `autoPersist`, runtime vẫn flush khi app bị ẩn, `pagehide`, `beforeunload`, dừng MIDlet và khi thay đổi suite. Web cũng yêu cầu `navigator.storage.persist()` khi trình duyệt hỗ trợ để giảm nguy cơ eviction. Thư viện/profiles của giao diện vẫn dùng `localStorage`; dữ liệu thực của suite nằm trong IndexedDB.

## Mạng

HTTP/HTTPS của MIDlet đi qua bridge cùng origin `POST /api/http`. Core gửi nguyên method/header/body trong binary envelope, server thực hiện request upstream rồi trả lại status/header/body; vì vậy game không còn phụ thuộc CORS hoặc mixed-content policy của request trực tiếp từ browser. Vite dev/preview có middleware bridge tích hợp; Cloudflare Pages dùng Function `functions/api/http.ts`.

`socket://` vẫn dùng Emscripten SOCKFS và route qua websockify/WebSocket proxy được cấu hình trong Cài đặt. Proxy Docker mặc định dùng TLS-only tại `wss://127.0.0.1:38473`. Container giữ một local CA và server certificate trong Docker volume `/certs`; server certificate có SAN cho `localhost`, `127.0.0.1` và `host.docker.internal`, tự renew trước khi hết hạn. CA public được export tại `Artifacts/phoneME-websockify-local-ca.crt`; trên macOS chạy `sh web/trust-websockify-ca-macos.sh` một lần rồi thoát hẳn/mở lại Chrome hoặc Safari để browser tin WSS. Production nên mount CA/certificate phù hợp hoặc dùng TLS public. Trình duyệt không cung cấp raw TCP/UDP socket trực tiếp, nên các protocol này vẫn cần proxy/bridge ở phía server.

## Media

MMAPI và Nokia Sound trên WebAssembly được bridge sang Web Audio API trên main browser thread. WAV/MP3/AAC và các codec mà browser giải mã được dùng `decodeAudioData`; tone sequence của J2ME được core render thành WAV; MIDI có renderer tương thích phía web. AudioContext được unlock/resume bằng thao tác thật của người dùng để hoạt động trên Safari/iOS.

## PWA

`manifest.webmanifest` và `sw.js` biến frontend thành PWA cài được. Service worker cache app shell, bundle và WASM versioned để mở emulator khi offline; suite/RMS/filesystem vẫn lấy từ IndexedDB. Endpoint `/api/http` không được cache.

## Giới hạn nền tảng web

- Decoder ảnh native của web hỗ trợ PNG; JPEG/GIF hiện vẫn phụ thuộc khả năng/bridge decoder tương ứng của core.
- Raw TCP/UDP không thể được browser mở trực tiếp; game dùng `socket://`/`datagram://` cần transport proxy phù hợp.
