## Context

The Debugging Dashboard currently displays a list of files residing on the ESP32's microSD card. It renders an attractive download icon (SVG) next to each file, but there is no click handler attached and no backend endpoint to serve the actual file content.

## Goals / Non-Goals

**Goals:**
- Implement a backend endpoint in `dashboard.cpp` that serves the contents of requested files from the microSD card.
- Update the frontend Javascript inside `dashboard.cpp` to trigger a file download when the SVG icon is clicked.

**Non-Goals:**
- We will not implement file deletion, uploading, or complex file management features.
- We will not handle massive files gracefully if it exceeds memory, though we should stream the file content in chunks to avoid blowing up the ESP32 heap.

## Decisions

- **Backend Endpoint**: Add a new URI handler in `dashboard.cpp` for `/download` (or `GET /download?file=<filename>`).
  - *Rationale*: We need an HTTP endpoint. Query parameters are standard for passing the requested filename.
  - *Alternative*: `GET /file/<filename>`. Either is fine, query param is easier to parse with ESP-IDF HTTPD.
- **Frontend Download Trigger**: Wrap the SVG or add an `onclick` event that calls `window.open('/download?file=' + encodeURIComponent(file.name), '_blank')` or creates a temporary `<a>` tag with the `download` attribute.
  - *Rationale*: The simplest way to trigger a file download from a single-page app without relying on complex fetch streams is using standard browser navigation to an endpoint with the `Content-Disposition: attachment` header.
- **Streaming Response**: The backend will read the file in chunks and send it using `httpd_resp_send_chunk`.
  - *Rationale*: MicroSD files can be megabytes in size, which won't fit into the ESP32's limited RAM.

## Risks / Trade-offs

- **Memory limits** → Mitigation: Use a small buffer (e.g., 1024 bytes) to read from the SD card and send over HTTP in chunks.
- **Path Traversal Attacks** → Mitigation: Basic sanitization. The filename parameter shouldn't contain `/` or `\\` and should only fetch from the designated `CONFIG_APP_SD_MOUNT_POINT`.
