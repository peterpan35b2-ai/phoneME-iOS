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

Suite, RMS, save game và filesystem được lưu bằng IDBFS/IndexedDB trong trình duyệt. Thư viện hiển thị được lưu trong `localStorage` và dữ liệu thực của suite nằm trong IndexedDB.

## Mạng

HTTP/HTTPS dùng networking của Emscripten. Game dùng `socket://` hoặc `datagram://` cần một WebSocket proxy tương thích Emscripten; cấu hình URL proxy trong màn hình Cài đặt trước khi tải runtime.

## Giới hạn hiện tại trên web

- Decoder ảnh native của web hỗ trợ PNG; JPEG/GIF hiện chỉ có bridge Apple trên iOS.
- Media đang dùng fallback của core, chưa bridge sang Web Audio API.
- TCP/UDP thô không thể kết nối trực tiếp từ trình duyệt và phụ thuộc WebSocket proxy.
