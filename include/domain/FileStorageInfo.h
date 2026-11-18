#pragma once

#include <cstdint>
#include <string>

struct FileStorageInfo {
    std::string storage_key;
    std::uint64_t size_bytes = 0;
};