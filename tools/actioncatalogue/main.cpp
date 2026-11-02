// unreal-action-catalogue FILE: writes the shipped action catalogue (data/streamdeck/catalogue.json: the core's
// actions and the Qt client's declared ones, actions::ShippedCatalogueJson) to FILE when it differs. Run by the build
// (the action-catalogue target), so the file follows the registry; the studio's palette and `scenario check` read it
// offline.
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include "emulator/actions/actionregistry.h"
#include "emulator/actions/catalogue.h"

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: unreal-action-catalogue FILE\n");
        return 2;
    }
    const std::string text = actions::ShippedCatalogueJson(actions::ActionRegistry::Instance().List());
    {
        std::ifstream in(argv[1], std::ios::binary);
        if (in && std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()) == text)
            return 0;
    }
    std::ofstream out(argv[1], std::ios::binary | std::ios::trunc);
    out << text;
    out.close();
    if (!out)
    {
        std::fprintf(stderr, "unreal-action-catalogue: cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("unreal-action-catalogue: wrote %s\n", argv[1]);
    return 0;
}
