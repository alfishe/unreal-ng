#pragma once

#include <algorithm>
#include <thread>
#include <vector>

/// Run fn(firstRow, endRow) over [0, rows) on a few threads: the CRT tables and compositions are per row
template <typename Fn>
void ForRows(int rows, const Fn& fn)
{
    const int hardware = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    const int parts = std::min({hardware, 8, std::max(1, rows / 64)});
    if (parts <= 1)
    {
        fn(0, rows);
        return;
    }
    std::vector<std::thread> threads;
    threads.reserve(static_cast<size_t>(parts - 1));
    const int chunk = (rows + parts - 1) / parts;
    for (int part = 1; part < parts; ++part)
    {
        const int first = part * chunk;
        const int end = std::min(rows, first + chunk);
        if (first < end)
            threads.emplace_back([&fn, first, end]() { fn(first, end); });
    }
    fn(0, std::min(rows, chunk));
    for (std::thread& thread : threads)
        thread.join();
}
