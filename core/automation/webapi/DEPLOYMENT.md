# WebAPI Deployment Guide

## Overview

The Unreal Speccy Emulator WebAPI uses external HTML files for its documentation and error pages. These must be properly deployed for the application to function correctly.

## Features

- **CORS Enabled**: All endpoints support Cross-Origin Resource Sharing for web integration
- **OpenAPI 3.0**: Machine-readable API documentation available at `/api/v1/openapi.json`
- **Self-Documenting**: Root URL (`/`) redirects to interactive documentation
- **Custom 404**: User-friendly error pages that guide users to documentation

## Quick Reference

| Deployment Type | Resource Location | Automatic? |
|----------------|-------------------|------------|
| **Development Build** | `cmake-build-debug/bin/resources/html/` | ✅ Yes (POST_BUILD) |
| **macOS .app Bundle** | `unreal-qt.app/Contents/Resources/html/` | ✅ Yes (cmake install) |
| **Linux Install** | `/usr/local/share/unreal-speccy/resources/html/` | ✅ Yes (cmake install) |
| **Portable/Windows** | `bin/resources/html/` (alongside exe) | ✅ Yes (cmake install) |

### Testing with Swagger UI

```bash
# Pull the image first (optional but recommended)
docker pull swaggerapi/swagger-ui

# Start Swagger UI with pre-configured API spec
docker run --name unreal-speccy-swagger-ui \
  -p 8081:8080 \
  -e SWAGGER_JSON_URL=http://localhost:8090/api/v1/openapi.json \
  swaggerapi/swagger-ui

# Access at: http://localhost:8081
# API spec loads automatically - no manual entry needed!
```

### ⚠️ Manual OpenAPI Maintenance

**Important**: The OpenAPI specification at `/api/v1/openapi.json` is **manually maintained** and **NOT auto-generated**.

**When making API changes:**
1. Update the API endpoint code in the appropriate modular file:
   - `api/lifecycle_api.cpp` - Emulator lifecycle management
   - `api/tape_disk_api.cpp` - Tape and disk control
   - `api/snapshot_api.cpp` - Snapshot management
   - `api/settings_api.cpp` - Settings management
   - `api/state_memory_api.cpp` - Memory state inspection
   - `api/state_screen_api.cpp` - Screen state inspection
   - `api/state_audio_api.cpp` - Audio state inspection
2. **Manually update** the OpenAPI JSON specification in `openapi_spec.cpp`
3. Add autodoc comments above the method implementation
4. Test with Swagger UI to ensure documentation matches implementation
5. Update HTML documentation and deployment guides

**Failure to update the OpenAPI spec will result in:**
- Documentation being out of sync with actual API
- Swagger UI showing incorrect information
- Integration issues for API consumers

## Resource Search Order

The application searches for HTML resources in this order:

1. `./resources/html/` - Current working directory
2. `../resources/html/` - Parent directory (from bin/)
3. `./core/automation/webapi/resources/html/` - Development source tree
4. `../../resources/html/` - Two levels up
5. `../Resources/html/` - macOS .app bundle (Contents/Resources)
6. `/usr/local/share/unreal-speccy/resources/html/` - Unix local install
7. `/usr/share/unreal-speccy/resources/html/` - Unix system install

**First match wins** - the application stops searching after finding the first valid path.

## Deployment Scenarios

### 1. Development (Running from Build Directory)

**Automatic** - CMake copies resources during build:

```bash
cd unreal-qt/cmake-build-debug
./bin/unreal-qt
```

Resources are automatically available at:
- `cmake-build-debug/resources/html/`
- `cmake-build-debug/bin/resources/html/`

### 2. macOS .app Bundle

**Automatic** - CMake install handles this:

```bash
cd unreal-qt/cmake-build-debug
cmake --install . --prefix /Applications
```

Resources installed to:
```
/Applications/unreal-qt.app/
  └── Contents/
      ├── MacOS/
      │   └── unreal-qt
      └── Resources/
          └── html/
              ├── index.html
              └── 404.html
```

The executable in `Contents/MacOS/` will find resources in `../Resources/html/`.

### 3. Linux System Installation

**Automatic** - Standard Unix installation:

```bash
cd unreal-qt/cmake-build-debug
sudo cmake --install . --prefix /usr/local
```

Resources installed to:
```
/usr/local/
  ├── bin/
  │   └── unreal-qt
  └── share/unreal-speccy/resources/html/
      ├── index.html
      └── 404.html
```

### 4. Portable Deployment (Windows/Cross-platform)

**Automatic** - Resources alongside executable:

```bash
cmake --install . --prefix ./portable-install
```

Creates structure:
```
portable-install/
  └── bin/
      ├── unreal-qt (or unreal-qt.exe)
      └── resources/
          └── html/
              ├── index.html
              └── 404.html
```

### 5. Custom Installation

Manual deployment:

```bash
# Copy executable
cp cmake-build-debug/bin/unreal-qt /custom/location/

# Copy resources (choose one location)
cp -r core/automation/webapi/resources/html /custom/location/resources/html/
# OR
cp -r core/automation/webapi/resources/html /custom/location/../resources/html/
```

## Verification

Test if resources are correctly deployed:

1. Start the application
2. Open browser to `http://localhost:8090/`
3. **Success**: Should see formatted API documentation page
4. **Failure**: Shows "Resource Not Found" with searched paths

Check WebAPI startup logs for any resource loading warnings.

## Troubleshooting

### "Resource Not Found" Error

**Symptom**: Browser shows plain error message instead of styled page.

**Solutions**:
1. Verify resources exist in expected location
2. Check file permissions (must be readable)
3. Run from correct directory:
   ```bash
   cd /path/to/installation
   ./bin/unreal-qt
   ```
4. For .app bundles, ensure structure is correct:
   ```bash
   ls -la unreal-qt.app/Contents/Resources/html/
   ```

### Resources Not Updating

**Symptom**: HTML changes don't appear after editing.

**Solutions**:
1. **Development**: Rebuild to copy new resources
   ```bash
   cmake --build cmake-build-debug
   ```
2. **Installed**: Re-run install
   ```bash
   cmake --install cmake-build-debug
   ```
3. **Manual**: Copy files directly
   ```bash
   cp resources/html/*.html /path/to/runtime/resources/html/
   ```
4. Restart WebAPI server

### macOS .app Bundle Issues

**Symptom**: Resources not found when running .app.

**Check**:
```bash
# Verify bundle structure
ls -la unreal-qt.app/Contents/MacOS/unreal-qt
ls -la unreal-qt.app/Contents/Resources/html/

# Test relative path from executable
cd unreal-qt.app/Contents/MacOS
ls -la ../Resources/html/
```

## Customization

To customize HTML pages:

1. Edit source files in `core/automation/webapi/resources/html/`
2. Rebuild (development) or reinstall (production)
3. Restart server
4. **No C++ recompilation needed** (unless adding new pages)

## CI/CD Integration

For automated deployments:

```bash
# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Install/Package
cmake --install build --prefix dist/

# Verify resources
test -f dist/bin/resources/html/index.html || exit 1
test -f dist/bin/resources/html/404.html || exit 1
```

## See Also

- `resources/README.md` - Resource file documentation
- `CMakeLists.txt` - Build and installation rules
- `OPENAPI_MAINTENANCE.md` - OpenAPI specification maintenance guide
- WebAPI modular implementation:
  - `src/emulator_api.h` - Main API interface (with region markers)
  - `src/emulator_api.cpp` - Core infrastructure
  - `src/openapi_spec.cpp` - OpenAPI specification (638 lines, 52 endpoints)
  - `src/api/` - Modular endpoint implementations
- Resource loading in:
  - `src/emulator_api.cpp::loadHtmlFile()`
  - `src/automation-webapi.cpp::loadHtmlFile()`

