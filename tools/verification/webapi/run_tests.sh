#!/bin/bash
# Comprehensive WebAPI verification using OpenAPI contract

# Directory setup
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$SCRIPT_DIR/../../.."

echo "Starting comprehensive WebAPI verification..."
echo "Ensure 'unreal-qt' is running on localhost:8090"
echo "You can start it with: ./unreal-qt &"
echo ""

# Install requirements if needed
echo "Installing/updating requirements..."
pip install -r "$SCRIPT_DIR/requirements.txt" --quiet
echo ""

# Set Python path
export PYTHONPATH="$SCRIPT_DIR:$PYTHONPATH"

# Step 1: Check API availability
echo "Step 1: Checking API availability..."
python3 "$SCRIPT_DIR/openapi_verification.py" --check-only

# Check if availability check passed
if [ $? -ne 0 ]; then
    echo ""
    echo "❌ Cannot proceed with verification - API service is not available"
    echo "Please ensure 'unreal-qt' is running on localhost:8090"
    exit 1
fi

echo ""

# Step 2: Run comprehensive OpenAPI verification
echo "Step 2: Running comprehensive API verification..."
VERIFICATION_OUTPUT=$(python3 "$SCRIPT_DIR/openapi_verification.py" --verbose 2>&1)

EXIT_CODE=$?

# Extract the report filename from the output (look for the line that says "Check 'filename'")
REPORT_FILE=$(echo "$VERIFICATION_OUTPUT" | grep "📄 Check '" | sed "s/.*📄 Check '\([^']*\)'.*/\1/")

# If we couldn't extract the filename, fall back to generating it
if [ -z "$REPORT_FILE" ]; then
    TODAY=$(date +%Y%m%d)
    REPORT_FILE="${TODAY}-api-verification-report.md"
fi

# Print the verification output
echo "$VERIFICATION_OUTPUT"

if [ $EXIT_CODE -eq 0 ]; then
    echo ""
    echo "✅ All WebAPI endpoints passed verification!"
    echo "📄 Check '${REPORT_FILE}' for detailed results"
else
    echo ""
    echo "❌ Some WebAPI endpoints failed verification"
    echo "📄 Check '${REPORT_FILE}' for detailed error logs"
fi

exit $EXIT_CODE
