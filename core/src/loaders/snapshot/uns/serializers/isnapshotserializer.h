#pragma once
#include <string>
#include "loaders/snapshot/uns/dto/snapshot_dto.h"

// @brief Interface for snapshot serialization/deserialization
class ISnapshotSerializer
{
public:
    // @brief Virtual destructor
    virtual ~ISnapshotSerializer() = default;

    // @brief Load snapshot from file and populate DTO
    // @param filePath Path to YAML (or other) file
    // @param out Output DTO to populate
    // @return true on success, false on error
    virtual bool load(const std::string& filePath, SnapshotDTO& out) = 0;

    // @brief Save snapshot DTO to file
    // @param filePath Path to output file
    // @param in Input DTO to serialize
    // @return true on success, false on error
    virtual bool save(const std::string& filePath, const SnapshotDTO& in) = 0;

    // @brief Get last error message
    // @return Last error string, or empty if no error
    virtual std::string lastError() const = 0;
}; 