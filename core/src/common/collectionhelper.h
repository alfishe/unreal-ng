#pragma once
#include <cassert>
#include <cstddef>
#include <map>
#include <type_traits>

#include "stdafx.h"

// Helps with freeing up memory from objects pointers to those stored in STL collections like list, vector
// Advice: better to create shared_ptr version of collection list<shared_ptr<Obj>> instead
// Example:
//   vector<object*> objects;
//   objects.push_back(new object());
//
//   deleter(objects.begin(), objects.end()); // Delete stored objects
//	 objects.clear(); // Clear the collection content
template <typename T>
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
inline void find_and_execute(T& container, const Key& key, FoundFunction found_function,
                             NotFoundFunction not_found_function)
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

/// A lightweight, non-owning view of a contiguous sequence of objects
/// Similar to std::span in C++20 but compatible with C++17
/// Use this to safely pass buffer/length pairs without copying data
template <typename T>
class ByteSpan
{
public:
    using element_type = T;
    using value_type = std::remove_cv_t<T>;
    using size_type = std::size_t;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using iterator = pointer;
    using const_iterator = const_pointer;

    /// Default constructor - creates an empty span
    constexpr ByteSpan() noexcept : data_(nullptr), size_(0) {}

    /// Constructor from pointer and size
    constexpr ByteSpan(pointer data, size_type size) noexcept : data_(data), size_(size) {}

    /// Constructor from array
    template <size_type N>
    constexpr ByteSpan(element_type (&arr)[N]) noexcept : data_(arr), size_(N)
    {
    }

    /// Access element at specified index with bounds checking
    constexpr reference at(size_type idx) const
    {
        assert(idx < size_ && "ByteSpan index out of range");
        return data_[idx];
    }

    /// Access element at specified index without bounds checking
    constexpr reference operator[](size_type idx) const noexcept
    {
        return data_[idx];
    }

    /// Returns a pointer to the beginning of the span
    constexpr pointer data() const noexcept
    {
        return data_;
    }

    /// Returns the number of elements in the span
    constexpr size_type size() const noexcept
    {
        return size_;
    }

    /// Returns true if the span is empty
    constexpr bool empty() const noexcept
    {
        return size_ == 0;
    }

    /// Returns an iterator to the beginning
    constexpr iterator begin() const noexcept
    {
        return data_;
    }

    /// Returns an iterator to the end
    constexpr iterator end() const noexcept
    {
        return data_ + size_;
    }

    /// Creates a subspan starting at offset with specified count
    constexpr ByteSpan<T> subspan(size_type offset, size_type count) const
    {
        assert(offset <= size_ && "Offset out of range");
        assert(count <= size_ - offset && "Count out of range");
        return ByteSpan<T>(data_ + offset, count);
    }

    /// Creates a subspan starting at offset to the end
    constexpr ByteSpan<T> subspan(size_type offset) const
    {
        assert(offset <= size_ && "Offset out of range");
        return ByteSpan<T>(data_ + offset, size_ - offset);
    }

private:
    pointer data_;
    size_type size_;
};
