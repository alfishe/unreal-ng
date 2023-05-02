#pragma once
#include "stdafx.h"

class Emulator;

class TestClient
{
protected:
	Emulator* _emulator = nullptr;

public:
	TestClient();
	virtual ~TestClient();

	void Start();
	void Stop();

	Emulator* GetEmulator();

    /// region <Helper methods>
protected:
    void SetUpLogging();
    /// endregion </Helper methods>
};