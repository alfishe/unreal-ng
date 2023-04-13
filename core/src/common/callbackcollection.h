#pragma once
#include "stdafx.h"

#include <string>
#include <map>
#include <mutex>

// Defining CallbackFunction as callback type.
// Corresponds to: void CallbackFunction();
typedef void (*CallbackFunction)();

typedef std::map<string, CallbackFunction> CallbacksMap;
typedef std::map<CallbackFunction, string> CallbacksReverseMap;

class CallbackCollection
{
// Synchronization primitives
protected:
    std::mutex _mutex;

public:
	CallbacksMap _callbacks;
	CallbacksReverseMap _callbacksReverse;

public:
	void Add(const std::string& tag, CallbackFunction func);
	void Remove(const std::string& tag);
	void Remove(CallbackFunction func);
	void RemoveAll();
};