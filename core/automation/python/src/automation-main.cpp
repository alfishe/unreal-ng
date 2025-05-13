#include <fstream>
#include <iostream>
#include <pybind11/embed.h> // everything needed for embedding
namespace py = pybind11;

int main()
{
    py::scoped_interpreter guard{};

    // Load a file
    std::string path = "script.py";
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cout << "Could not open file " << path << std::endl;
        return 1;
    }

    std::string str((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    py::exec(str);
}