#pragma once
#include "stdafx.h"

#include <string>
#include <map>
#include <mutex>

using namespace std;

// Defining CallbackFunction as callback type.
// Corresponds to: void CallbackFunction();
typedef void (*CallbackFunction)();

typedef map<string, CallbackFunction> CallbacksMap;
typedef map<CallbackFunction, string> CallbacksReverseMap;

class CallbackCollection
{
// Synchronization primitives
protected:
	mutex _mutex;

public:
	CallbacksMap _callbacks;
	CallbacksReverseMap _callbacksReverse;

public:
	void Add(const string& tag, CallbackFunction func);
	void Remove(const string& tag);
	void Remove(CallbackFunction func);
	void RemoveAll();
};