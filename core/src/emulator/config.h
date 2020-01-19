#pragma once
#include "stdafx.h"

struct CONFIG;

class Config
{
public:
	static const char* GetDefaultConfig();

public:
	Config();
	virtual ~Config();

public:
	[[nodiscard]] bool LoadConfig();
	[[nodiscard]] bool LoadConfig(wstring& filename);

	[[nodiscard]] unsigned LoadROM(wstring& path, uint8_t* bank, uint16_t max_banks = 1);
	void LoadROMSet(CONFIG* config, const char* romeset);
};