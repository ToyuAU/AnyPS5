#include "StorageEdgeWriteWatch.hpp"
#include "prx/libc/include/GuestWriteWatch.hpp"
#include <algorithm>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

namespace {

constexpr std::uintptr_t PageBytes = 4096;
std::mutex watchMutex;
std::map<std::uintptr_t, std::uintptr_t> watched;
std::set<std::uintptr_t> dirty;

bool covers(std::uintptr_t address, std::size_t bytes) {
    if (bytes == 0 || bytes > std::numeric_limits<std::uintptr_t>::max() - address) return false;
    auto it = watched.upper_bound(address);
    if (it == watched.begin()) return false;
    --it;
    return it->first <= address && address + bytes <= it->second;
}

void noteWrite(std::uintptr_t address, std::size_t bytes) {
    if (!covers(address, bytes)) return;
    for (auto page = address & ~(PageBytes - 1); page < address + bytes; page += PageBytes) dirty.insert(page);
}

}

void StorageEdgeNoteCpuWrite(std::uintptr_t address, std::size_t bytes) {
    std::lock_guard lock(watchMutex);
    noteWrite(address, bytes);
}

namespace GuestWriteWatch {

extern "C" bool GuestWriteWatchAvailable_nid_postfix() {
    return true;
}

extern "C" void GuestWriteWatchRegister_nid_postfix(const void* pointer, std::size_t bytes) {
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    std::lock_guard lock(watchMutex);
    watched[address] = address + bytes;
    noteWrite(address, bytes);
}

extern "C" bool GuestWriteWatchUnregister_nid_postfix(const void* pointer, std::size_t bytes) {
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    const auto end = address + bytes;
    std::lock_guard lock(watchMutex);
    std::vector<std::pair<std::uintptr_t, std::uintptr_t>> retained;
    bool removed = false;
    for (auto it = watched.begin(); it != watched.end();) {
        if (it->second <= address || end <= it->first) {
            ++it;
            continue;
        }
        removed = true;
        if (it->first < address) retained.emplace_back(it->first, address);
        if (end < it->second) retained.emplace_back(end, it->second);
        it = watched.erase(it);
    }
    for (const auto& [begin, stop] : retained) watched.emplace(begin, stop);
    dirty.erase(dirty.lower_bound(address & ~(PageBytes - 1)), dirty.lower_bound(end));
    return removed;
}

extern "C" bool GuestWriteWatchCovers_nid_postfix(std::uintptr_t address, std::size_t bytes) {
    std::lock_guard lock(watchMutex);
    return covers(address, bytes);
}

extern "C" bool GuestWriteWatchCollect_nid_postfix(std::uintptr_t address, std::size_t bytes, void (*written)(void*, std::uintptr_t, std::uintptr_t), void* context) {
    std::vector<std::uintptr_t> collected;
    {
        std::lock_guard lock(watchMutex);
        if (!covers(address, bytes)) return false;
        const auto begin = dirty.lower_bound(address & ~(PageBytes - 1));
        const auto end = dirty.lower_bound(address + bytes);
        collected.assign(begin, end);
        dirty.erase(begin, end);
    }
    for (const auto page : collected) written(context, page, page + PageBytes);
    return true;
}

}
