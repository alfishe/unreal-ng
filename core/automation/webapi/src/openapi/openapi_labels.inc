    // Labels/Symbols endpoints
    paths["/api/v1/emulator/{id}/labels"]["get"]["summary"] = "List labels with optional filtering";
    paths["/api/v1/emulator/{id}/labels"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/labels"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/labels"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/labels"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/labels"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/labels"]["get"]["parameters"][1]["name"] = "module";
    paths["/api/v1/emulator/{id}/labels"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/labels"]["get"]["parameters"][1]["required"] = false;
    paths["/api/v1/emulator/{id}/labels"]["get"]["parameters"][1]["description"] = "Filter by module name";
    paths["/api/v1/emulator/{id}/labels"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/labels"]["get"]["parameters"][2]["name"] = "type";
    paths["/api/v1/emulator/{id}/labels"]["get"]["parameters"][2]["in"] = "query";
    paths["/api/v1/emulator/{id}/labels"]["get"]["parameters"][2]["required"] = false;
    paths["/api/v1/emulator/{id}/labels"]["get"]["parameters"][2]["description"] = "Filter by type (code/data/const)";
    paths["/api/v1/emulator/{id}/labels"]["get"]["parameters"][2]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/labels"]["get"]["parameters"][3]["name"] = "bank";
    paths["/api/v1/emulator/{id}/labels"]["get"]["parameters"][3]["in"] = "query";
    paths["/api/v1/emulator/{id}/labels"]["get"]["parameters"][3]["required"] = false;
    paths["/api/v1/emulator/{id}/labels"]["get"]["parameters"][3]["description"] = "Filter by memory bank";
    paths["/api/v1/emulator/{id}/labels"]["get"]["parameters"][3]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/labels"]["get"]["parameters"][4]["name"] = "from";
    paths["/api/v1/emulator/{id}/labels"]["get"]["parameters"][4]["in"] = "query";
    paths["/api/v1/emulator/{id}/labels"]["get"]["parameters"][4]["required"] = false;
    paths["/api/v1/emulator/{id}/labels"]["get"]["parameters"][4]["description"] = "Filter by address range start";
    paths["/api/v1/emulator/{id}/labels"]["get"]["parameters"][4]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/labels"]["get"]["parameters"][5]["name"] = "to";
    paths["/api/v1/emulator/{id}/labels"]["get"]["parameters"][5]["in"] = "query";
    paths["/api/v1/emulator/{id}/labels"]["get"]["parameters"][5]["required"] = false;
    paths["/api/v1/emulator/{id}/labels"]["get"]["parameters"][5]["description"] = "Filter by address range end";
    paths["/api/v1/emulator/{id}/labels"]["get"]["parameters"][5]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/labels"]["get"]["responses"]["200"]["description"] = "Label list with count";

    paths["/api/v1/emulator/{id}/labels"]["post"]["summary"] = "Add a new label";
    paths["/api/v1/emulator/{id}/labels"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/labels"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/labels"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/labels"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/labels"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/labels"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["name"]["type"] = "string";
    paths["/api/v1/emulator/{id}/labels"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["address"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/labels"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["type"]["type"] = "string";
    paths["/api/v1/emulator/{id}/labels"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["module"]["type"] = "string";
    paths["/api/v1/emulator/{id}/labels"]["post"]["responses"]["200"]["description"] = "Label added";
    paths["/api/v1/emulator/{id}/labels"]["post"]["responses"]["409"]["description"] = "Label already exists";

    paths["/api/v1/emulator/{id}/labels"]["delete"]["summary"] = "Clear all labels";
    paths["/api/v1/emulator/{id}/labels"]["delete"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/labels"]["delete"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/labels"]["delete"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/labels"]["delete"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/labels"]["delete"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/labels"]["delete"]["responses"]["200"]["description"] = "All labels cleared";

    paths["/api/v1/emulator/{id}/labels/{name}"]["get"]["summary"] = "Get label by name";
    paths["/api/v1/emulator/{id}/labels/{name}"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/labels/{name}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/labels/{name}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/labels/{name}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/labels/{name}"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/labels/{name}"]["get"]["parameters"][1]["name"] = "name";
    paths["/api/v1/emulator/{id}/labels/{name}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/labels/{name}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/labels/{name}"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/labels/{name}"]["get"]["responses"]["200"]["description"] = "Label details";
    paths["/api/v1/emulator/{id}/labels/{name}"]["get"]["responses"]["404"]["description"] = "Label not found";

    paths["/api/v1/emulator/{id}/labels/{name}"]["delete"]["summary"] = "Remove label by name";
    paths["/api/v1/emulator/{id}/labels/{name}"]["delete"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/labels/{name}"]["delete"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/labels/{name}"]["delete"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/labels/{name}"]["delete"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/labels/{name}"]["delete"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/labels/{name}"]["delete"]["parameters"][1]["name"] = "name";
    paths["/api/v1/emulator/{id}/labels/{name}"]["delete"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/labels/{name}"]["delete"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/labels/{name}"]["delete"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/labels/{name}"]["delete"]["responses"]["200"]["description"] = "Label removed";
    paths["/api/v1/emulator/{id}/labels/{name}"]["delete"]["responses"]["404"]["description"] = "Label not found";

    paths["/api/v1/emulator/{id}/labels/{name}"]["put"]["summary"] = "Update label properties";
    paths["/api/v1/emulator/{id}/labels/{name}"]["put"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/labels/{name}"]["put"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/labels/{name}"]["put"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/labels/{name}"]["put"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/labels/{name}"]["put"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/labels/{name}"]["put"]["parameters"][1]["name"] = "name";
    paths["/api/v1/emulator/{id}/labels/{name}"]["put"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/labels/{name}"]["put"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/labels/{name}"]["put"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/labels/{name}"]["put"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["active"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/labels/{name}"]["put"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["comment"]["type"] = "string";
    paths["/api/v1/emulator/{id}/labels/{name}"]["put"]["responses"]["200"]["description"] = "Label updated";
    paths["/api/v1/emulator/{id}/labels/{name}"]["put"]["responses"]["404"]["description"] = "Label not found";

    paths["/api/v1/emulator/{id}/symbols/load"]["post"]["summary"] = "Load symbols from file";
    paths["/api/v1/emulator/{id}/symbols/load"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/symbols/load"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/symbols/load"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/symbols/load"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/symbols/load"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/symbols/load"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["path"]["type"] = "string";
    paths["/api/v1/emulator/{id}/symbols/load"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["path"]["description"] = "Path to symbol file (.sld, .sym, .map)";
    paths["/api/v1/emulator/{id}/symbols/load"]["post"]["responses"]["200"]["description"] = "Symbols loaded";
    paths["/api/v1/emulator/{id}/symbols/load"]["post"]["responses"]["400"]["description"] = "Failed to load symbols";

    paths["/api/v1/emulator/{id}/symbols/save"]["post"]["summary"] = "Save symbols to file";
    paths["/api/v1/emulator/{id}/symbols/save"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/symbols/save"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/symbols/save"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/symbols/save"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/symbols/save"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/symbols/save"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["path"]["type"] = "string";
    paths["/api/v1/emulator/{id}/symbols/save"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["path"]["description"] = "Path to save symbols";
    paths["/api/v1/emulator/{id}/symbols/save"]["post"]["responses"]["200"]["description"] = "Symbols saved";
    paths["/api/v1/emulator/{id}/symbols/save"]["post"]["responses"]["400"]["description"] = "Failed to save symbols";
