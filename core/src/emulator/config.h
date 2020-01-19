#pragma once
#include "stdafx.h"

#include "3rdparty/simpleini/simpleini.h"
#include "emulator/emulatorcontext.h"
#include <string>

struct CONFIG;

class Config
{
	// Ini file section names
private:
	static const char* misc;
	static const char* video;
	static const char* ula;
	static const char* beta128;
	static const char* leds;
	static const char* sound;
	static const char* input;
	static const char* colors;
	static const char* ay;
	static const char* saa1099;
	static const char* atm;
	static const char* hdd;
	static const char* rom;
	static const char* ngs;
	static const char* zc;

protected:
	EmulatorContext* _context;
	wstring _configFilePath;

public:
	static const char* GetDefaultConfig();

public:
	Config() = delete;	// Disable default constructor. C++ 11 feature
	Config(EmulatorContext* context);
	virtual ~Config();

public:
	[[nodiscard]] bool LoadConfig();
	[[nodiscard]] bool LoadConfig(wstring& filename);
	[[nodiscard]] bool ParseConfig(CSimpleIniA& inimanager);

	[[nodiscard]] bool DetermineModel();

	[[nodiscard]] unsigned LoadROM(wstring& path, uint8_t* bank, uint16_t max_banks = 1);
	void LoadROMSet(CONFIG* config, const char* romeset);

	// Helper methods
protected:
	void CopyStringValue(const char* src, char* dst, size_t dst_len);
	string stripComment(const char* src);
};