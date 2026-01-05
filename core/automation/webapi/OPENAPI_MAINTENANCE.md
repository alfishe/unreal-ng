# OpenAPI Specification Maintenance Guide

## ⚠️ CRITICAL: Manual Maintenance Required

The OpenAPI 3.0 specification for the Unreal Speccy Emulator WebAPI is **MANUALLY MAINTAINED** and **NOT auto-generated**.

This means that **every API change** requires **manual updates** to the OpenAPI JSON specification.

## Why Manual Maintenance?

- **No auto-generation tooling** is currently integrated
- **Complex API structure** with multiple endpoints and parameter types
- **Custom business logic** that can't be easily inferred from code
- **Performance considerations** - avoiding runtime generation overhead

## Maintenance Process

### When Adding/Modifying API Endpoints:

1. **Implement the API endpoint** in `emulator_api.cpp`
2. **Update the OpenAPI specification** in `EmulatorAPI::getOpenAPISpec()`
3. **Test with Swagger UI** to verify documentation accuracy
4. **Update documentation** (HTML pages, deployment guides)

### When Changing Parameters/Responses:

1. **Modify the API endpoint** implementation
2. **Update corresponding OpenAPI paths** in the specification
3. **Verify parameter schemas** match actual implementation
4. **Test with real API calls**

## OpenAPI Structure

The specification is built in `EmulatorAPI::getOpenAPISpec()` and includes:

- **Info section**: API title, description, version
- **Servers**: Available server URLs
- **Paths**: All API endpoints with methods, parameters, responses
- **Components**: Reusable schemas for requests/responses
- **Tags**: Grouping of related endpoints

## Current Endpoints Covered

✅ **Emulator Management**: List, create, get, delete emulators
✅ **Emulator Control**: Start, stop, pause, resume, reset
✅ **Audio State**: AY chips, beeper, GS, Covox state inspection
✅ **Memory State**: RAM, ROM inspection
✅ **Screen State**: Display mode, flash state
✅ **Settings**: Get/set emulator configuration

## Common Mistakes to Avoid

❌ **Adding new endpoint without updating OpenAPI spec**
❌ **Changing parameter names/types without updating schemas**
❌ **Modifying response formats without updating response schemas**
❌ **Forgetting to update path parameters or query parameters**

## Testing Documentation Accuracy

```bash
# 1. Start the API server
./unreal-qt

# 2. Start Swagger UI
docker run --name unreal-speccy-swagger-ui \
  -p 8081:8080 \
  -e SWAGGER_JSON_URL=http://localhost:8090/api/v1/openapi.json \
  swaggerapi/swagger-ui

# 3. Test each endpoint in Swagger UI
# 4. Verify parameters and responses match actual API
```

## Future Considerations

- **Auto-generation tooling** could be added in the future
- **Runtime validation** against OpenAPI spec could be implemented
- **API versioning** might require separate OpenAPI specs

## Files to Update

When making API changes, update these files:

1. **`core/automation/webapi/src/emulator_api.cpp`** - API implementation
2. **`core/automation/webapi/src/emulator_api.cpp`** - OpenAPI specification (same file)
3. **`core/automation/webapi/resources/html/index.html`** - HTML documentation
4. **`docs/emulator/design/control-interfaces/command-interface.md`** - Main documentation
5. **`core/automation/webapi/DEPLOYMENT.md`** - Deployment guide

## Contact

If you're unsure about updating the OpenAPI specification, please consult the development team to ensure API documentation remains accurate and complete.
