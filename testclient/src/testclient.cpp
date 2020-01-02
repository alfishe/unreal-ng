#include "stdafx.h"

#include <iostream>
#include "testclient.h"
#include "../../core/src/emulator/emulator.h"


int main()
{
    std::cout << "Hello World!\n";

	TestClient client;
	client.Start();
}

TestClient::TestClient()
{

}

TestClient::~TestClient()
{
	Stop();
}

void TestClient::Start()
{
	if (_emulator != nullptr)
	{
		Stop();
	}

	_emulator = new Emulator();
}

void TestClient::Stop()
{
	try
	{
		if (_emulator != nullptr)
		{
			if (_emulator->IsRunning() && !_emulator->IsPaused())
			{
				_emulator->Pause();
			}

			_emulator->Release();
			_emulator = nullptr;
		}
	}
	catch (...)
	{
	}
}

