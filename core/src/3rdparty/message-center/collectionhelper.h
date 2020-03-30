#ifndef MESSAGE_CENTER_COLLECTIONHELPER_H
#define MESSAGE_CENTER_COLLECTIONHELPER_H

#include <map>

namespace mc
{
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

// Helper for STL collections to delete empty sub-structures
// Example:
//   std::map<int, vector<int>> some_map;
//   some_map.insert( { 1, new vector<int>() }
//   erase_entry_if_empty(some_map, 1);
// That will delete vector container since it's empty
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

// TODO: Totally untested!
// std::vector<int> some_vector;
// for_each<int>(some_vector, [&](int const & func) { action(); });
template <typename C, class T, typename Function>
inline void for_each(C& container, Function function)
{
    auto begin = container.begin();
    auto end = container.end();

    for_each(begin, end, [&](T const & func)
    {
        func();
    });
}
} // End of namespace mc

#endif //MESSAGE_CENTER_COLLECTIONHELPER_H
