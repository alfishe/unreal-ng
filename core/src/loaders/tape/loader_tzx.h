#pragma once

#ifndef UNREAL_LOADER_TZX_H
#define UNREAL_LOADER_TZX_H

#include "stdafx.h"

/// @see http://k1.spdns.de/Develop/Projects/zasm/Info/TZX%20format.html
/// @see MakeTZX tool sources https://github.com/mincebert/maketzx
/// @see https://github.com/dominicjprice/tap2tzx

class EmulatorContext;
class ModuleLogger;

class LoaderTZX
{
    /// region <Fields>
protected:
    EmulatorContext* _context;
    ModuleLogger* _logger;

    // File-related fields
    std::string _path;
    FILE* _file = nullptr;
    bool _fileValidated = false;
    size_t _fileSize = 0;
    void* _buffer = nullptr;

    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    LoaderTZX(EmulatorContext* context, std::string path);
    virtual ~LoaderTZX();
    /// endregion </Constructors / destructors>

    /// region <Helper methods>
protected:
    bool validateFile();
    bool parseTZX();

    void parseHardware(uint8_t* data);
    /// region </Helper methods>
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing / benchmark purposes
//
#ifdef _CODE_UNDER_TEST

class LoaderTZXCUT : public LoaderTZX
{
public:
    LoaderTZXCUT(EmulatorContext* context, std::string path) : LoaderTZX(context, path) {};

public:
    using LoaderTZX::_context;
    using LoaderTZX::_logger;
    using LoaderTZX::_path;
    using LoaderTZX::_file;

    using LoaderTZX::validateFile;
    using LoaderTZX::parseHardware;
};
#endif // _CODE_UNDER_TEST


#endif //UNREAL_LOADER_TZX_H
