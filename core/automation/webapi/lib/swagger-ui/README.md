# Swagger UI Bundle

Single-file, self-contained Swagger UI for the Unreal Speccy Emulator WebAPI.

## Files

- `swagger-ui-bundle.html` - Complete Swagger UI in one HTML file (~1.5 MB)
- `build-swagger-ui.sh` - Script to regenerate from upstream

## Version

Swagger UI v5.17.14 (bundled 2026-08-26)

## Usage

The bundle is copied to `resources/html/docs.html` and served at `/api/v1/docs`.
It loads the OpenAPI spec from `/api/v1/openapi.json` automatically.

## Rebuilding

To update to a newer Swagger UI version:

1. Edit `build-swagger-ui.sh` and change `SWAGGER_UI_VERSION`
2. Run `./build-swagger-ui.sh`
3. Copy the output to resources: `cp swagger-ui-bundle.html ../resources/html/docs.html`
4. Rebuild the project

## License

Swagger UI is licensed under Apache 2.0.
See: https://github.com/swagger-api/swagger-ui/blob/master/LICENSE
