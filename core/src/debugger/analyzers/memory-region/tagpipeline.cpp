#include "tagpipeline.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include "blocktagmap.h"
#include "analysiscontext.h"

void TagPipeline::addClassifier(std::unique_ptr<ITagClassifier> classifier)
{
    _classifiers.push_back(std::move(classifier));
}

std::vector<ITagClassifier*> TagPipeline::getSortedClassifiers() const
{
    // Build name -> classifier map
    std::unordered_map<std::string, ITagClassifier*> byName;
    for (const auto& c : _classifiers)
    {
        byName[c->name()] = c.get();
    }

    // Topological sort via Kahn's algorithm
    std::unordered_map<std::string, int> inDegree;
    std::unordered_map<std::string, std::vector<std::string>> dependents;

    for (const auto& c : _classifiers)
    {
        inDegree[c->name()] = 0;
    }

    for (const auto& c : _classifiers)
    {
        for (const auto& dep : c->dependencies())
        {
            if (byName.count(dep))
            {
                inDegree[c->name()]++;
                dependents[dep].push_back(c->name());
            }
        }
    }

    // Start with nodes that have no dependencies
    std::vector<std::string> queue;
    for (const auto& [name, degree] : inDegree)
    {
        if (degree == 0)
            queue.push_back(name);
    }

    std::vector<ITagClassifier*> result;
    while (!queue.empty())
    {
        std::string current = queue.back();
        queue.pop_back();

        if (byName.count(current))
            result.push_back(byName[current]);

        for (const auto& dep : dependents[current])
        {
            if (--inDegree[dep] == 0)
                queue.push_back(dep);
        }
    }

    return result;
}

void TagPipeline::runAll(const AnalysisContext& ctx, BlockTagMap& tagMap)
{
    auto sorted = getSortedClassifiers();
    for (auto* classifier : sorted)
    {
        classifier->classify(ctx, tagMap);
    }
}
