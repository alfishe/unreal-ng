#pragma once

#include <string>
#include <vector>

class BlockTagMap;
struct AnalysisContext;

/// @brief Interface for tag classifiers that assign semantic tags to memory regions.
class ITagClassifier
{
public:
    virtual ~ITagClassifier() = default;

    /// @brief Get the classifier name (for logging/debugging).
    virtual std::string name() const = 0;

    /// @brief Get names of classifiers this one depends on (must run first).
    virtual std::vector<std::string> dependencies() const { return {}; }

    /// @brief Run classification and add tags to the tag map.
    /// @param ctx    Analysis context with data sources
    /// @param tagMap Tag map to modify (add tags via addTag)
    virtual void classify(const AnalysisContext& ctx, BlockTagMap& tagMap) = 0;
};
