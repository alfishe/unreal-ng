# WebAPI Media Upload — Technical Design

**Status:** Implemented  
**Date:** 2026-09-19  
**Related docs:** [iOS Host Design](ios-host-design.md) §7, [OpenAPI Maintenance](../../../OPENAPI_MAINTENANCE.md)

## Summary

Extend the WebAPI to accept media content (snapshots, disk images, tape images) embedded
directly in POST requests, removing the requirement for files to pre-exist on the emulator
host. MCP piggybacks on this: when an LLM specifies a local filename, the MCP bridge reads
the file and forwards embedded content to the WebAPI.

---

## 1. Motivation

| Scenario | Current limitation |
|---|---|
| iOS headless host | No shell access; files must be transferred via Files.app or a separate upload endpoint |
| Remote automation | Scripts must first upload files, then call load endpoints with server paths |
| MCP from LLM | LLM knows local paths; MCP bridge cannot transparently load them into the emulator |

A single request that carries the media payload simplifies all three.

---

## 2. API Changes

### 2.1 Existing endpoints (path-based)

```
POST /api/v1/emulator/{id}/snapshot/load   { "path": "/path/on/host.sna" }
POST /api/v1/emulator/{id}/tape/load       { "path": "/path/on/host.tap" }
POST /api/v1/emulator/{id}/disk/{drive}/insert { "path": "/path/on/host.trd" }
```

These remain unchanged for backward compatibility.

### 2.2 New: embedded content via multipart

Same endpoints accept `multipart/form-data`:

```
POST /api/v1/emulator/{id}/snapshot/load
Content-Type: multipart/form-data; boundary=...

--boundary
Content-Disposition: form-data; name="file"; filename="game.sna"
Content-Type: application/octet-stream

<binary content>
--boundary--
```

| Field | Required | Notes |
|---|---|---|
| `file` | yes | The media file; filename extension determines format |
| `autostart` | no | `true`/`false` (disk only, default `false`) |

### 2.3 New: embedded content via raw body

For simpler clients, accept raw binary with metadata in headers:

```
POST /api/v1/emulator/{id}/tape/load
Content-Type: application/octet-stream
X-Filename: game.tap
Content-Length: 12345

<binary content>
```

| Header | Required | Notes |
|---|---|---|
| `X-Filename` | yes | Filename with extension (format detection) |
| `X-Autostart` | no | `true`/`false` (disk only) |

### 2.4 Response

Success (same as existing):
```json
{ "success": true }
```

Error:
```json
{ "success": false, "error": "Unsupported format: .xyz" }
```

### 2.5 Size limits

| Media | Typical | Max | Rationale |
|---|---|---|---|
| Snapshot | 50–200 KB | 4 MB | ZX Evolution full state |
| Disk image | 640–800 KB | 1 MB | Single TRD/SCL/FDI |
| Tape image | 10–200 KB | 1 MB | Large TAP/TZX compilations rare |

Over-limit returns `413 Payload Too Large`. No config needed — these are hard physical limits.

---

## 3. Implementation

### 3.1 Drogon body size configuration

**Critical**: Drogon's default `clientMaxMemoryBodySize` is ~64KB. Bodies larger than this
are written to temp files, causing `req->body()` to return an empty string. We set both
limits in `automation-webapi.cpp`:

```cpp
#include "api/upload_helper.h"   // MAX_UPLOAD_BODY_SIZE constant (5 MB)

app.setClientMaxBodySize(api::v1::MAX_UPLOAD_BODY_SIZE);
app.setClientMaxMemoryBodySize(api::v1::MAX_UPLOAD_BODY_SIZE);
```

### 3.2 Drogon multipart handling

Drogon provides `HttpRequestPtr::getFile(name)` for multipart. The handler:

```cpp
void SnapshotController::load(const HttpRequestPtr& req,
                              std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& emulatorId)
{
    std::string path;
    std::vector<uint8_t> content;

    if (req->contentType() == CT_MULTIPART_FORM_DATA) {
        auto file = req->getFile("file");
        if (file.getFileName().empty()) {
            return error(callback, "Missing 'file' field");
        }
        content.assign(file.fileData(), file.fileData() + file.fileLength());
        path = derivePathFromFilename(file.getFileName());
    } else if (req->contentType() == CT_APPLICATION_OCTET_STREAM) {
        auto filename = req->getHeader("X-Filename");
        if (filename.empty()) {
            return error(callback, "Missing X-Filename header");
        }
        content.assign(req->bodyData(), req->bodyData() + req->bodyLength());
        path = derivePathFromFilename(filename);
    } else {
        // Existing JSON path-based flow
        auto json = req->getJsonObject();
        path = (*json)["path"].asString();
    }

    if (!content.empty()) {
        path = stageToTempFile(content, path);  // writes to writable_root/uploads/
    }

    auto result = loadSnapshot(emulatorId, path);
    // ...
}
```

### 3.3 Temp file staging

Per-session upload folder: `writable_root/uploads/<session_uuid>/`
- Created on first upload of the session
- Cleaned up entirely on `app_shutdown()` or app exit
- Files named `<upload_seq>_<filename>` (sequence avoids collisions)
- Extension preserved for format detection

No periodic cleanup, no stale-file timers — session lifecycle handles it.

### 3.4 Core loader changes

None required — loaders already take paths. The staging approach reuses existing code.

Future optimization: `LoadSnapshotFromMemory(const uint8_t*, size_t)` to avoid disk I/O
(Phase 2, shared with UE buffer-based loaders).

### 3.5 Resource manager (iOS/Android hosts only)

A lightweight `HostResourceManager` lives in `unrealng_embed`, **not in core**:

```cpp
// embed/host_resources.h — iOS/Android only
class HostResourceManager {
public:
    static HostResourceManager& Instance();
    
    void SetSessionRoot(const std::string& writable_root);
    std::string StageUpload(const uint8_t* data, size_t size, const std::string& filename);
    void Cleanup();  // called on shutdown
    
private:
    std::string _sessionDir;
    uint32_t _uploadSeq = 0;
};
```

- Owns the session upload folder lifecycle
- Called by WebAPI handlers when embedded content arrives
- `Cleanup()` registered via `atexit()` and called from `app_shutdown()`
- Desktop builds (Qt) don't need this — they have shell access and existing paths work

---

## 4. MCP Bridge

### 4.1 Current flow

```
LLM: load_software(type="disk", path="/Users/dev/game.trd", drive=0)
  ↓
MCP bridge: POST /api/v1/emulator/{id}/disk/0/insert { "path": "/Users/dev/game.trd" }
  ↓
WebAPI: tries to open "/Users/dev/game.trd" on the emulator host → fails if remote
```

### 4.2 New flow

```
LLM: load_software(type="disk", path="/Users/dev/game.trd", drive=0)
  ↓
MCP bridge:
  1. Detects path is local (file exists on MCP host)
  2. Reads file content
  3. POST /api/v1/emulator/{id}/disk/0/insert
     Content-Type: application/octet-stream
     X-Filename: game.trd
     <binary content>
  ↓
WebAPI: stages content, loads from staged path → success
```

### 4.3 Implementation

Implemented in `core/automation/mcp/src/mcp-tools.cpp` (C++):

- `TryReadLocalFile()` — checks if path exists on MCP host and reads content (max 4MB)
- `ExtractFilename()` — extracts basename from path for X-Filename header
- `IApiCaller::CallRaw()` — new method for binary body + headers (in `webapi-client.h`)
- `ForwardCallRaw()` — helper in `mcp-tool-utils.h` wrapping CallRaw

The `load_software` tool now:
1. Tries to read the path as a local file
2. If found: uploads via raw body with X-Filename header (shows "(uploaded)" in result)
3. If not found: falls back to JSON body with path (existing behavior)

### 4.4 Tool schema update

The `load_software` MCP tool description notes that `path` can be:
- A path on the MCP host (uploaded automatically)
- A path on the emulator host (if accessible there)
- A URL (future: fetch and upload)

---

## 5. OpenAPI Spec Updates

Add to `openapi.json` for each endpoint:

```yaml
requestBody:
  content:
    application/json:
      schema:
        type: object
        properties:
          path:
            type: string
            description: Path to file on emulator host
    multipart/form-data:
      schema:
        type: object
        properties:
          file:
            type: string
            format: binary
            description: Media file content
          autostart:
            type: boolean
    application/octet-stream:
      schema:
        type: string
        format: binary
      headers:
        X-Filename:
          schema:
            type: string
          required: true
          description: Filename with extension for format detection
```

---

## 6. Verification

| Test | Method |
|---|---|
| Multipart upload | `curl -F "file=@game.sna" http://host:8090/api/v1/emulator/0/snapshot/load` |
| Raw body upload | `curl -X POST -H "X-Filename: game.tap" --data-binary @game.tap http://host:8090/.../tape/load` |
| Size limit | Upload >1 MB disk image → expect 413 |
| MCP local file | LLM calls `load_software` with local path → file appears in emulator |
| MCP remote path | Path not on MCP host → falls back to JSON body |
| Session cleanup | `app_shutdown()` → session upload folder removed |

---

## 7. Phases

| Phase | Scope | Estimate | Status |
|---|---|---|---|
| 1 | Multipart + raw body for all three endpoints; temp staging; size limits | 1–2 days | ✅ Done |
| 2 | MCP bridge piggybacking; tool schema update | 0.5 day | ✅ Done |
| 3 | OpenAPI spec; integration tests | 0.5 day | ✅ Done |
| 4 | (Future) In-memory loaders to skip temp files | — | — |

---

## 8. Security Notes

- Uploaded files are staged in `writable_root/uploads/`, not arbitrary paths
- Filename is sanitized (no path traversal: `../`, absolute paths stripped)
- Content is validated by the loader (invalid format → error, not crash)
- No execution of uploaded content (binary blobs only)
