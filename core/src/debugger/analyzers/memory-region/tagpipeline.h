#pragma once

#include <memory>
#include <string>
#include <vector>

#include "itagclassifier.h"

class BlockTagMap;
struct AnalysisContext;

/// @brief Orchestrates tag classifiers with dependency resolution.
class TagPipeline
{
public:
    TagPipeline() = default;

    /// @brief Add a classifier to the pipeline.
    void addClassifier(std::unique_ptr<ITagClassifier> classifier);

    /// @brief Run all classifiers in dependency order.
    /// @param ctx    Analysis context
    /// @param tagMap Tag map to populate
    void runAll(const AnalysisContext& ctx, BlockTagMap& tagMap);

    /// @brief Get number of registered classifiers.
    size_t size() const { return _classifiers.size(); }

    /// @brief Get list of classifier names.
    std::vector<std::string> getClassifierNames() const
    {
        std::vector<std::string> names;
        names.reserve(_classifiers.size());
        for (const auto& c : _classifiers)
            names.push_back(c->name());
        return names;
    }

private:
    std::vector<std::unique_ptr<ITagClassifier>> _classifiers;

    /// @brief Sort classifiers by dependencies (topological sort).
    std::vector<ITagClassifier*> getSortedClassifiers() const;
};
