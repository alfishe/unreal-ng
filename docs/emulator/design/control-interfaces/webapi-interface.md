# WebAPI - HTTP/REST Interface

## Overview

The WebAPI interface provides a RESTful HTTP API for controlling the emulator. Designed for web frontends, dashboards, and remote management applications.

**Status**: ✅ Partially Implemented  
**Implementation**: `core/automation/webapi/`  
**Framework**: Drogon (C++ HTTP framework)  
**Port**: Configurable (default: TBD)  
**Protocol**: HTTP/1.1, JSON request/response bodies  

## Architecture

```
┌─────────────────────┐
│  Web Frontend       │  (React, Vue, Angular, etc.)
│  SPA / Dashboard    │
└──────────┬──────────┘
           │ HTTP/REST
           ▼
┌─────────────────────┐
│ AutomationWebAPI    │  Drogon HTTP server
│                     │
│ • emulator_api.cpp  │  Emulator endpoints
│ • hello_world_api   │  Test endpoints
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐
│ Emulator Manager    │
│ & Emulator Core     │
└─────────────────────┘
```

## REST API Design

### Base URL
```
http://localhost:<port>/api/v1
```

### Stateless Design
Unlike CLI (stateful sessions), WebAPI is stateless:
- No persistent session context
- Emulator ID must be in URL path or query parameter
- Each request is independent
- Suitable for load balancing and horizontal scaling

### API Versioning
- Version in URL path: `/api/v1/...`
- Future versions: `/api/v2/...` without breaking existing clients
- Version negotiation via `Accept` header (future)

## Implemented Endpoints

### 1. List Emulators
**Endpoint**: `GET /api/v1/emulators`  
**Description**: Get list of all emulator instances  

**Request**:
```http
GET /api/v1/emulators HTTP/1.1
Host: localhost:8080
Accept: application/json
```

**Response**:
```json
{
  "emulators": [
    {
      "id": "550e8400-e29b-41d4-a716-446655440000",
      "symbolic_id": "main",
      "created_at": "2026-01-03T10:30:00Z",
      "last_activity": "2026-01-03T10:35:22Z",
      "state": "running",
      "uptime": "00:05:22"
    }
  ],
  "count": 1
}
```

### 2. Get Emulator Status
**Endpoint**: `GET /api/v1/emulators/status`  
**Description**: Get overall status of all emulators  

**Response**:
```json
{
  "status": "ok",
  "emulators": [
    {
      "id": "550e8400-e29b-41d4-a716-446655440000",
      "state": "running",
      "fps": 50.0,
      "cpu_usage": 45.2
    }
  ]
}
```

### 3. Get Emulator Details
**Endpoint**: `GET /api/v1/emulators/{id}`  
**Description**: Get detailed information about specific emulator  

**Parameters**:
- `id` (path): Emulator UUID or symbolic ID

**Response**:
```json
{
  "id": "550e8400-e29b-41d4-a716-446655440000",
  "symbolic_id": "main",
  "state": "paused",
  "created_at": "2026-01-03T10:30:00Z",
  "uptime": "00:05:22",
  "registers": {
    "af": "0x44C4",
    "bc": "0x3F00",
    "de": "0x0000",
    "hl": "0x5C00",
    "pc": "0x0605",
    "sp": "0xFF24"
  }
}
```

### 4. Control Emulator State

#### Pause Emulator
**Endpoint**: `POST /api/v1/emulators/{id}/pause`  

**Request**:
```http
POST /api/v1/emulators/550e8400-e29b-41d4-a716-446655440000/pause HTTP/1.1
Content-Type: application/json

{}
```

**Response**:
```json
{
  "status": "success",
  "message": "Emulator paused",
  "state": "paused"
}
```

#### Resume Emulator
**Endpoint**: `POST /api/v1/emulators/{id}/resume`  

#### Start Emulator
**Endpoint**: `POST /api/v1/emulators/{id}/start`  

#### Stop Emulator
**Endpoint**: `POST /api/v1/emulators/{id}/stop`  

### 5. Create Emulator
**Endpoint**: `POST /api/v1/emulators`  
**Description**: Create new emulator instance  

**Request**:
```json
{
  "symbolic_id": "test_instance",
  "config": {
    "model": "spectrum128",
    "rom": "128k.rom"
  }
}
```

**Response**:
```json
{
  "status": "success",
  "id": "550e8400-e29b-41d4-a716-446655440001",
  "symbolic_id": "test_instance"
}
```

### 6. Remove Emulator
**Endpoint**: `DELETE /api/v1/emulators/{id}`  
**Description**: Destroy emulator instance  

**Response**:
```json
{
  "status": "success",
  "message": "Emulator removed"
}
```

## Planned Endpoints (Not Yet Implemented)

### Execution Control
```
POST /api/v1/emulators/{id}/reset
POST /api/v1/emulators/{id}/step
POST /api/v1/emulators/{id}/stepover
POST /api/v1/emulators/{id}/steps?count=10
```

### State Inspection
```
GET /api/v1/emulators/{id}/registers
GET /api/v1/emulators/{id}/memory?address=0x8000&length=256
GET /api/v1/emulators/{id}/disassemble?address=0x8000&count=10
```

### Breakpoints
```
GET    /api/v1/emulators/{id}/breakpoints
POST   /api/v1/emulators/{id}/breakpoints
DELETE /api/v1/emulators/{id}/breakpoints/{bp_id}
PATCH  /api/v1/emulators/{id}/breakpoints/{bp_id}
```

### Media Operations
```
POST /api/v1/emulators/{id}/tape/load
POST /api/v1/emulators/{id}/tape/eject
POST /api/v1/emulators/{id}/disk/{drive}/insert
POST /api/v1/emulators/{id}/disk/{drive}/eject
```

### Snapshots
```
GET  /api/v1/emulators/{id}/snapshot
POST /api/v1/emulators/{id}/snapshot/save
POST /api/v1/emulators/{id}/snapshot/load
```

## WebSocket Support (Future)

Real-time streaming for live updates without polling.

**Endpoint**: `WS /api/v1/emulators/{id}/stream`  

**Use Cases**:
- Live screen updates (video streaming)
- Real-time register monitoring
- Event notifications (breakpoint hit, state change)
- Audio streaming

**Message Format**:
```json
{
  "type": "register_update",
  "timestamp": "2026-01-03T10:35:22.123Z",
  "data": {
    "pc": "0x0605",
    "af": "0x44C4"
  }
}
```

## HTTP Headers & CORS

### CORS Configuration
```
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS
Access-Control-Allow-Headers: Content-Type, Authorization
Access-Control-Max-Age: 86400
```

**Note**: Wildcard origin (`*`) for development. Restrict in production:
```
Access-Control-Allow-Origin: https://yourdomain.com
```

### Content Negotiation
```
Accept: application/json
Content-Type: application/json
```

### Authentication (Future)
```
Authorization: Bearer <token>
X-API-Key: <api-key>
```

## Error Handling

### HTTP Status Codes

| Code | Meaning | Example |
| :--- | :--- | :--- |
| 200 | OK | Successful GET request |
| 201 | Created | Emulator created |
| 204 | No Content | Successful DELETE |
| 400 | Bad Request | Invalid JSON or parameters |
| 404 | Not Found | Emulator ID doesn't exist |
| 409 | Conflict | Emulator already exists |
| 500 | Internal Server Error | Emulator crash or bug |
| 503 | Service Unavailable | Server overloaded |

### Error Response Format
```json
{
  "status": "error",
  "error": {
    "code": "EMULATOR_NOT_FOUND",
    "message": "Emulator with ID '12345' does not exist",
    "details": {
      "requested_id": "12345",
      "available_ids": ["550e8400-..."]
    }
  }
}
```

### Common Error Codes
- `EMULATOR_NOT_FOUND` - Invalid emulator ID
- `INVALID_STATE` - Operation not valid in current state
- `INVALID_PARAMETER` - Bad request parameter
- `INTERNAL_ERROR` - Server-side error

## Client Examples

### JavaScript (Fetch API)
```javascript
// List emulators
const response = await fetch('http://localhost:8080/api/v1/emulators');
const data = await response.json();
console.log(data.emulators);

// Pause emulator
await fetch('http://localhost:8080/api/v1/emulators/550e8400-.../pause', {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: '{}'
});

// Get registers
const regs = await fetch('http://localhost:8080/api/v1/emulators/550e8400-.../registers');
console.log(await regs.json());
```

### Python (requests library)
```python
import requests

base_url = 'http://localhost:8080/api/v1'

# List emulators
r = requests.get(f'{base_url}/emulators')
emulators = r.json()['emulators']

# Pause emulator
emu_id = emulators[0]['id']
requests.post(f'{base_url}/emulators/{emu_id}/pause')

# Get status
status = requests.get(f'{base_url}/emulators/{emu_id}').json()
print(f"State: {status['state']}")
```

### cURL
```bash
# List emulators
curl http://localhost:8080/api/v1/emulators

# Pause emulator
curl -X POST http://localhost:8080/api/v1/emulators/550e8400-.../pause

# Create emulator
curl -X POST http://localhost:8080/api/v1/emulators \
  -H "Content-Type: application/json" \
  -d '{"symbolic_id":"test","config":{"model":"spectrum128"}}'
```

## Rate Limiting (Future)

```
X-RateLimit-Limit: 100
X-RateLimit-Remaining: 99
X-RateLimit-Reset: 1609459200
```

**Default Limits**:
- 100 requests per minute per IP
- 1000 requests per hour per API key

## Performance Characteristics

- **Latency**: ~5-20ms per request (local)
- **Throughput**: ~1000-5000 requests/second (depends on endpoint)
- **Concurrent Connections**: ~10,000 (Drogon limit)
- **Memory**: ~1-2MB per connection

## Implementation Details

### Framework: Drogon
- High-performance C++ HTTP framework
- Asynchronous I/O (epoll/kqueue)
- Built-in JSON support
- WebSocket support
- Middleware pipeline

### Source Files
- `core/automation/webapi/src/automation-webapi.cpp` - Server initialization
- `core/automation/webapi/src/emulator_api.cpp` - Emulator endpoints
- `core/automation/webapi/src/emulator_websocket.cpp` - WebSocket handler

### Configuration
```json
{
  "listeners": [
    {
      "address": "0.0.0.0",
      "port": 8080,
      "https": false
    }
  ],
  "threads": 4,
  "enable_compression": true,
  "max_connections": 10000
}
```

## Security Considerations

### Current Limitations
- No authentication
- No HTTPS (plain HTTP only)
- No rate limiting
- CORS allows all origins

### Production Recommendations
1. **Enable HTTPS**: Use TLS certificates
2. **Add Authentication**: JWT tokens or API keys
3. **Restrict CORS**: Whitelist specific origins
4. **Rate Limiting**: Prevent abuse
5. **Input Validation**: Sanitize all inputs
6. **Firewall**: Restrict access by IP

### HTTPS Setup (Future)
```json
{
  "listeners": [
    {
      "address": "0.0.0.0",
      "port": 8443,
      "https": true,
      "cert": "/path/to/cert.pem",
      "key": "/path/to/key.pem"
    }
  ]
}
```

## Troubleshooting

### Server won't start
- Check port not in use: `netstat -an | grep 8080`
- Verify configuration file syntax
- Check logs for error messages

### 404 Not Found
- Verify endpoint URL (case-sensitive)
- Check API version in path (`/api/v1/...`)
- Ensure emulator ID is correct

### CORS errors in browser
- Check CORS headers in response
- Verify origin is allowed
- Use browser DevTools Network tab

### Slow responses
- Check emulator is not hung
- Monitor server CPU/memory
- Consider caching for frequently accessed data

## See Also

- [Command Interface Overview](./command-interface.md)
- [CLI Interface](./cli-interface.md)
- [Python Bindings](./python-interface.md)
- [Drogon Framework Documentation](https://drogon.org/)
