#include "stdafx.h"

#include <iostream>
#include "testclient.h"
#include "../../core/src/emulator/emulator.h"
#include "../../core/src/3rdparty/message-center/messagecenter.h"


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
	if (!_emulator->Init())
	{
		LOGERROR("Unable to initialize emulator. Exiting...");
		return;
	}

	//region Test
	MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
	string topic("CPU_RESET");
	ObserverCallbackFunc callback = [=](int id, Message* message)
	{
		if (message != nullptr)
		{
			const char* text = (const char*)message->obj;
			LOGINFO("CPU was reset with message '%s'", text);
		}
		else
		{
			LOGINFO("CPU was reset");
		}
	};
	messageCenter.AddObserver(topic, callback);

	topic = "Reg_PC";
	callback = [=](int id, Message* message)
	{
		if (message != nullptr)
		{
			const char* text = (const char*)message->obj;
			LOGINFO("PC: 0x%s", text);
		}
		else
		{
			LOGINFO("PC: ??");
		}
	};
	messageCenter.AddObserver(topic, callback);

	//_emulator->RunNCPUCycles(100);
	//return;
	//endregion Test


	_emulator->Start();
}

void TestClient::Stop()
{
	try
	{
		if (_emulator != nullptr)
		{
			if (_emulator->IsRunning() && !_emulator->IsPaused())
			{
				_emulator->Stop();
			}

			_emulator->Release();
			_emulator = nullptr;
		}
	}
	catch (...)
	{
	}
}

