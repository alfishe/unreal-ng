# WebAPI Verification Testing

When testing WebAPI changes, follow this sequence:

```bash
# 1. Check if port 8090 is already in use
if lsof -i :8090 >/dev/null 2>&1; then
  echo "WARNING: Port 8090 in use. Another session may be running unreal-qt."
  echo "Either use that instance, or ask the user before killing it."
  lsof -i :8090
  exit 1
fi

# 2. Start the freshly built emulator (macOS path)
./cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt &
sleep 4

# 3. Verify WebAPI is responding
curl -s http://localhost:8090/api/v1/emulator | jq .

# 4. Create an emulator instance
curl -s -X POST "http://localhost:8090/api/v1/emulator/start" \
  -H "Content-Type: application/json" \
  -d '{"model": "128k"}' | jq .
# Save the returned "id" for subsequent calls

# 5. Test your feature (example: symbolic disassembly with labels)
EMU_ID="<id-from-step-5>"
curl -s -X POST "http://localhost:8090/api/v1/emulator/$EMU_ID/labels" \
  -H "Content-Type: application/json" \
  -d '{"name": "TEST_LABEL", "address": 4, "type": "code"}'
curl -s "http://localhost:8090/api/v1/emulator/$EMU_ID/disasm?address=0&count=10" | jq '.instructions[]'

# 6. Cleanup YOUR instance when done (only if you started it)
# Ask user before pkill - another session may own the emulator
```

**Do NOT blindly `pkill unreal-qt`** - other sessions may be using it. Check with the user first.

## Platform-specific binary paths

| Platform | Path |
|----------|------|
| macOS | `./cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt` |
| Linux | `./cmake-build-agent-release/bin/unreal-qt` |
| Windows | `./cmake-build-agent-release/bin/unreal-qt.exe` |

## Available models

Runtime-authoritative list: `GET /api/v1/emulator/models` (each entry has a `creatable` flag).

See [_common/machines.md](../_common/machines.md) for the model table and per-machine recipes in [machines/](../machines/).
