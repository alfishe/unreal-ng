#!/bin/bash
# Build script to create a single-file, minified Swagger UI bundle
# This script downloads Swagger UI dist files and combines them into one HTML file
#
# Usage: ./build-swagger-ui.sh
# Output: swagger-ui-bundle.html (single file with all CSS/JS inlined)
#
# This is a ONE-TIME build script. The output is committed to the repo.
# Re-run only when upgrading Swagger UI version.

set -e

SWAGGER_UI_VERSION="5.17.14"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WORK_DIR="$SCRIPT_DIR/.build-tmp"
OUTPUT_FILE="$SCRIPT_DIR/swagger-ui-bundle.html"

echo "Building Swagger UI $SWAGGER_UI_VERSION single-file bundle..."

# Create temp work directory
rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"
cd "$WORK_DIR"

# Download Swagger UI dist files
echo "Downloading Swagger UI dist files..."
SWAGGER_BASE="https://unpkg.com/swagger-ui-dist@$SWAGGER_UI_VERSION"

curl -sL "$SWAGGER_BASE/swagger-ui-bundle.js" -o swagger-ui-bundle.js
curl -sL "$SWAGGER_BASE/swagger-ui.css" -o swagger-ui.css

# Get file sizes for reporting
JS_SIZE=$(wc -c < swagger-ui-bundle.js | tr -d ' ')
CSS_SIZE=$(wc -c < swagger-ui.css | tr -d ' ')
echo "Downloaded: swagger-ui-bundle.js ($JS_SIZE bytes), swagger-ui.css ($CSS_SIZE bytes)"

# Create the bundled HTML file
echo "Creating single-file bundle..."

cat > "$OUTPUT_FILE" << 'HTMLEOF'
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Unreal Speccy Emulator - API Documentation</title>
    <style>
HTMLEOF

# Inline the CSS (minified by removing comments and extra whitespace)
cat swagger-ui.css | \
    sed 's|/\*[^*]*\*+([^/*][^*]*\*+)*/||g' | \
    tr '\n' ' ' | \
    sed 's/  */ /g' >> "$OUTPUT_FILE"

cat >> "$OUTPUT_FILE" << 'HTMLEOF'

        /* Custom overrides for dark theme compatibility */
        body { margin: 0; padding: 0; }
        .swagger-ui .topbar { display: none; }
        .swagger-ui .info { margin: 20px 0; }
        .swagger-ui .scheme-container { padding: 15px 0; }
    </style>
</head>
<body>
    <div id="swagger-ui"></div>
    <script>
HTMLEOF

# Inline the JS
cat swagger-ui-bundle.js >> "$OUTPUT_FILE"

cat >> "$OUTPUT_FILE" << 'HTMLEOF'
    </script>
    <script>
        window.onload = function() {
            window.ui = SwaggerUIBundle({
                url: "/api/v1/openapi.json",
                dom_id: '#swagger-ui',
                deepLinking: true,
                presets: [
                    SwaggerUIBundle.presets.apis,
                    SwaggerUIBundle.SwaggerUIStandalonePreset
                ],
                plugins: [
                    SwaggerUIBundle.plugins.DownloadUrl
                ],
                layout: "BaseLayout",
                defaultModelsExpandDepth: 1,
                defaultModelExpandDepth: 1,
                docExpansion: "list",
                filter: true,
                showExtensions: true,
                showCommonExtensions: true,
                tryItOutEnabled: true
            });
        };
    </script>
</body>
</html>
HTMLEOF

# Cleanup
cd "$SCRIPT_DIR"
rm -rf "$WORK_DIR"

# Report results
BUNDLE_SIZE=$(wc -c < "$OUTPUT_FILE" | tr -d ' ')
echo ""
echo "Bundle created: $OUTPUT_FILE"
echo "Total size: $BUNDLE_SIZE bytes ($(($BUNDLE_SIZE / 1024)) KB)"
echo ""
echo "To create compressed version:"
echo "  gzip -9 -k $OUTPUT_FILE"
