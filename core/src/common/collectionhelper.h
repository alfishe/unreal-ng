#pragma once
#include "stdafx.h"

#ifndef COMMON_HELPERS_COLLECTIONHELPER_H_
#define COMMON_HELPERS_COLLECTIONHELPER_H_

#include <map>

// Helps with freeing up memory from objects pointers to those stored in STL collections like list, vector
// Advice: better to create shared_ptr version of collection list<shared_ptr<Obj>> instead
// Example:
//   vector<object*> objects;
//   objects.push_back(new object());
//
//   deleter(objects.begin(), objects.end()); // Delete stored objects
//	 objects.clear(); // Clear the collection content
template<typename T>
inline void deleter(T from, T to)
{
   while (from != to)
   {
	   if (*from != nullptr)
	   {
		   delete *from;
	   }

       from++;
   }
}

// Helper for std::map to check whether key exists or not
// Example:
//  std::map<int, int> some_map;
//  key_exists(some_map, 1) ? std::cout << "yes" : std::cout << "no"
template <typename T, typename Key>
inline bool key_exists(const T& container, const Key& key)
{
    return (container.find(key) != std::end(container));
}

// Helper for std::map allowing to check whether key exists and execute different lambdas when found and not
// Example:
//    std::map<int, int> some_map;
//	  find_and_execute(some_map, 1,
//      [](int key, int value){ std::cout << "key " << key << " found, value: " << value << std::endl; },
//      [](int key){ std::cout << "key " << key << " not found" << std::endl; });
template <typename T, typename Key, typename FoundFunction, typename NotFoundFunction>
inline void find_and_execute(T& container, const Key& key, FoundFunction found_function, NotFoundFunction not_found_function)
{
    auto it = container.find(key);
    if (it != std::end(container))
    {
        found_function(key, it->second);
    }
    else
    {
        not_found_function(key);
    }
}

/// Erase key'ed element if such element exists
/// \tparam T
/// \tparam Key
/// \param container
/// \param key
template <typename T, typename Key>
inline void erase_from_collection(T& container, const Key& key)
{
    auto it = container.find(key);
    if (it != std::end(container))
    {
        container.erase(key);
    }
}

/// Erase key'ed element from collection only if it has nested colleciton and it is empty
/// \tparam T
/// \tparam Key
/// \param container
/// \param key
template <typename T, typename Key>
inline void erase_entry_if_empty(T& container, const Key& key)
{
	auto it = container.find(key);
	if (it != std::end(container))
	{
		if (it->second.size() == 0)
		{
			container.erase(key);
		}
	}
}

#endif // COMMON_HELPERS_COLLECTIONHELPER_H_
