# WebAPI HTML Resources

This directory contains HTML templates used by the Unreal Speccy Emulator WebAPI.

## Files

- `html/index.html` - API documentation homepage (served at `http://localhost:8090/`)
- `html/404.html` - Custom 404 error page

## Features

- **CORS Enabled**: All endpoints support Cross-Origin Resource Sharing
- **Responsive Design**: Mobile-friendly HTML pages
- **Interactive Testing**: Built-in tool guides (Swagger UI, Postman, curl)
- **Self-Documenting**: Links to OpenAPI specification

## Deployment

These resources must be accessible at runtime. The application searches for them in multiple locations:

### During Development
- `./resources/html/` - Current directory
- `../resources/html/` - Build root
- `./core/automation/webapi/resources/html/` - From project root

### macOS .app Bundle
- `unreal-qt.app/Contents/Resources/html/` - Standard macOS app bundle location

### Standard Installation (Linux/Unix)
- `/usr/local/share/unreal-speccy/resources/html/` - Local installation
- `/usr/share/unreal-speccy/resources/html/` - System installation

### Portable/Windows Deployment
- `bin/resources/html/` - Alongside executable

## Build Process

CMake automatically:
1. Copies resources to build directory during compilation
2. Installs resources to appropriate locations during `make install`

## Customization

You can freely edit these HTML files to customize the API documentation and error pages.
Changes take effect after:
1. Rebuilding (to copy to build directory), OR
2. Manually copying to the appropriate runtime location
3. Restarting the WebAPI server

No C++ recompilation is needed for HTML-only changes.

## Search Order

The application searches paths in order and uses the first match found. This allows:
- Development builds to work without installation
- Installed versions to find system resources
- macOS .app bundles to work correctly
- Portable deployments with resources alongside the executable

