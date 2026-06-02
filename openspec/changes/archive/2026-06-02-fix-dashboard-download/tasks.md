## 1. Backend Endpoint

- [x] 1.1 Implement `download_get_handler` in `dashboard.cpp` that reads a file specified in the `file` query parameter and streams its contents back.
- [x] 1.2 Ensure `download_get_handler` includes `Content-Disposition: attachment` headers.
- [x] 1.3 Register the `/download` URI with `server_` in `Dashboard::start()`.

## 2. Frontend Integration

- [x] 2.1 Update the frontend JS in `dashboard.cpp` to trigger a download (e.g., via `window.open` or an invisible anchor element) when the SVG icon is clicked.
