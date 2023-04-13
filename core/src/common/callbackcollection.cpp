#include "stdafx.h"

#include "callbackcollection.h"
#include "sysdefs.h"
#include "common/logger.h"
#include "common/collectionhelper.h"

void CallbackCollection::Add(const string& tag, CallbackFunction func)
{
	if (tag.empty())
	{
		LOGWARNING("%s: Empty name parameter supplied", __PRETTY_FUNCTION__);
		return;
	}

	// Lock parallel threads to access (active till return from method and lock destruction)
    std::lock_guard<std::mutex> lock(_mutex);

	// Add observer with correspondent tag into forward map
	if (!key_exists(_callbacks, tag))
	{
		_callbacks.insert({ tag, func });
	}
	else
	{
		_callbacks[tag] = func;
	}


	// Register observer in reverse map (for faster removal)
	if (!key_exists(_callbacksReverse, func))
	{
		_callbacksReverse.insert({ func, tag });
	}
	else
	{
		_callbacksReverse[func] = tag;
	}
}

void CallbackCollection::Remove(const string&  tag)
{
	if (tag.empty())
	{
		LOGWARNING("%s: Empty tag parameter supplied", __PRETTY_FUNCTION__);
		return;
	}

	// Lock parallel threads to access (active till return from method and lock destruction)
    std::lock_guard<std::mutex> lock(_mutex);

	// Perform observer lookup in forward map
	if (key_exists(_callbacks, tag))
	{
		// Delete from reverse map
		CallbackFunction func = _callbacks[tag];
		if (func != nullptr)
		{
			_callbacksReverse.erase(func);
		}

		// Delete from forward map
		_callbacks.erase(tag);
	}
}

void CallbackCollection::Remove(CallbackFunction func)
{
	if (func == nullptr)
	{
		LOGWARNING("%s: nullptr parameter supplied", __PRETTY_FUNCTION__);
		return;
	}

	// Lock parallel threads to access (active till return from method and lock destruction)
    std::lock_guard<std::mutex> lock(_mutex);

	// Perform observer lookup in reverse map
	if (key_exists(_callbacksReverse, func))
	{
		// Delete from forward map
		string tag = _callbacksReverse[func];
		_callbacks.erase(tag);

		// Delete from reverse map
		_callbacksReverse.erase(func);
	}
}

void CallbackCollection::RemoveAll()
{
	// Lock parallel threads to access (active till return from method and lock destruction)
    std::lock_guard<std::mutex> lock(_mutex);

	_callbacks.clear();
	_callbacksReverse.clear();
}