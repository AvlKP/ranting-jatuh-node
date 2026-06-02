# dashboard-file-download Specification

## Purpose
TBD - created by archiving change fix-dashboard-download. Update Purpose after archive.
## Requirements
### Requirement: Download Endpoint
The system SHALL provide an HTTP GET endpoint to download a specific file from the microSD card.

#### Scenario: Requesting an existing file
- **WHEN** an HTTP GET request is made to `/download?file=filename.txt` and `filename.txt` exists
- **THEN** the system streams the file content back with a 200 OK response and `Content-Disposition: attachment; filename="filename.txt"` header

#### Scenario: Requesting a non-existing file
- **WHEN** an HTTP GET request is made to `/download?file=missing.txt` and `missing.txt` does not exist
- **THEN** the system returns a 404 Not Found response

### Requirement: Frontend Download Trigger
The debugging dashboard SHALL initiate a file download when the download icon is clicked.

#### Scenario: Clicking the download icon
- **WHEN** the user clicks the download SVG icon next to a file in the dashboard
- **THEN** the browser initiates a download for that file via the `/download` endpoint

