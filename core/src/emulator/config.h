#pragma once
#include "stdafx.h"

struct CONFIG;

class Config
{
public:
	Config();
	virtual ~Config();

public:
	void LoadConfig(const char* filename);

	unsigned LoadROM(const char* path, uint8_t* bank, uint16_t max_banks = 1);
	void LoadROMSet(CONFIG* config, const char* romeset);
};