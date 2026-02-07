    // Capture Commands endpoints
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["summary"] = "OCR text from screen";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["tags"].append("Capture");
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["description"] =
        "Extract text from screen using ROM font bitmap matching (OCR). "
        "Returns 24 lines x 32 characters. Uses ZX Spectrum ROM font patterns.";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["responses"]["200"]["description"] = "Screen OCR result";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["rows"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["cols"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["lines"]["type"] = "array";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["text"]["type"] = "string";

    // Capture screen endpoint
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["summary"] = "Capture screen as image";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["tags"].append("Capture");
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["description"] =
        "Capture screen as GIF or PNG image. Returns base64-encoded data.";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][1]["name"] = "format";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][1]["required"] = false;
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][1]["schema"]["enum"].append("gif");
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][1]["schema"]["enum"].append("png");
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][2]["name"] = "mode";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][2]["in"] = "query";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][2]["required"] = false;
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][2]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][2]["schema"]["enum"].append("screen");
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][2]["schema"]["enum"].append("full");
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["responses"]["200"]["description"] =
        "Screen captured successfully";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["format"]["type"] = "string";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["width"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["height"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["size"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["data"]["type"] = "string";

