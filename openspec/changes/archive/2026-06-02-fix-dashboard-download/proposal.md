## Why

The Debugging Dashboard displays a download button next to files in the filesystem display, but clicking it does not download the file. This change is needed to fix the broken download functionality and allow users to actually retrieve files from the device via the dashboard.

## What Changes

- Fix the file download endpoint or frontend logic to properly stream/serve the file content upon clicking the download button.
- Ensure the downloaded file has the correct filename and MIME type.

## Capabilities

### New Capabilities
- `dashboard-file-download`: The ability to successfully download files listed in the dashboard filesystem display.

### Modified Capabilities

## Impact

- Debugging Dashboard frontend UI (download button click handler).
- Node.js backend endpoint serving file downloads.
