#pragma once

#include <string>
#include "3rdparty/rapidyaml/ryml_all.hpp"
#include "emulator/emulatorcontext.h"
#include "loaders/snapshot/uns/serializers/isnapshotserializer.h"
#include "loaders/snapshot/uns/dto/snapshot_dto.h"

// @brief YAML serializer/deserializer for snapshot DTOs
class YamlSnapshotSerializer : public ISnapshotSerializer
{
public:
    // @brief Load snapshot from YAML file and populate DTO
    bool load(const std::string& filePath, SnapshotDTO& out) override;

    // @brief Save snapshot DTO to YAML file
    bool save(const std::string& filePath, const SnapshotDTO& in) override;

    // @brief Get last error message
    std::string lastError() const override;

protected:
    // Sectional load helpers
    bool loadMetadata(const ryml::ConstNodeRef& node, MetadataDTO& dto);
    bool loadMachine(const ryml::ConstNodeRef& node, MachineDTO& dto);
    bool loadMemory(const ryml::ConstNodeRef& node, MemoryDTO& dto);
    bool loadZ80(const ryml::ConstNodeRef& node, Z80DTO& dto);
    bool loadPeripherals(const ryml::ConstNodeRef& node, PeripheralsDTO& dto);
    bool loadMedia(const ryml::ConstNodeRef& node, MediaDTO& dto);
    bool loadDebug(const ryml::ConstNodeRef& node, DebugDTO& dto);
    bool loadEmulatorConfig(const ryml::ConstNodeRef& node, EmulatorConfigDTO& dto);
    bool loadPsg(const ryml::ConstNodeRef& node, PSGDTO& dto);
    bool loadWd1793(const ryml::ConstNodeRef& node, WD1793DTO& dto);

    // Sectional save helpers
    bool saveMetadata(ryml::NodeRef& node, const MetadataDTO& dto);
    bool saveMachine(ryml::NodeRef& node, const MachineDTO& dto);
    bool saveMemory(ryml::NodeRef& node, const MemoryDTO& dto);
    bool saveZ80(ryml::NodeRef& node, const Z80DTO& dto);
    bool savePeripherals(ryml::NodeRef& node, const PeripheralsDTO& dto);
    bool saveMedia(ryml::NodeRef& node, const MediaDTO& dto);
    bool saveDebug(ryml::NodeRef& node, const DebugDTO& dto);
    bool saveEmulatorConfig(ryml::NodeRef& node, const EmulatorConfigDTO& dto);

    // @brief Safely get a value from a YAML node with a default value
    template<typename T>
    void safeGet(const ryml::ConstNodeRef& node, T& target, const T& defaultValue = T{})
    {
        if (node.readable() && !node.is_seed())
        {
            node >> target;
        }
        else
        {
            target = defaultValue;
        }
    }

    // @brief Last error message
    std::string _lastError;
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing / benchmark purposes
//
#ifdef _CODE_UNDER_TEST

class YamlSnapshotSerializerCUT : public YamlSnapshotSerializer
{
public:
    YamlSnapshotSerializerCUT() = default;
    
    // Expose load methods
    using YamlSnapshotSerializer::loadMetadata;
    using YamlSnapshotSerializer::loadMachine;
    using YamlSnapshotSerializer::loadMemory;
    using YamlSnapshotSerializer::loadZ80;
    using YamlSnapshotSerializer::loadPeripherals;
    using YamlSnapshotSerializer::loadMedia;
    using YamlSnapshotSerializer::loadDebug;
    using YamlSnapshotSerializer::loadEmulatorConfig;
    using YamlSnapshotSerializer::loadPsg;
    using YamlSnapshotSerializer::loadWd1793;
    
    // Expose save methods
    using YamlSnapshotSerializer::saveMetadata;
    using YamlSnapshotSerializer::saveMachine;
    using YamlSnapshotSerializer::saveMemory;
    using YamlSnapshotSerializer::saveZ80;
    using YamlSnapshotSerializer::savePeripherals;
    using YamlSnapshotSerializer::saveMedia;
    using YamlSnapshotSerializer::saveDebug;
    using YamlSnapshotSerializer::saveEmulatorConfig;
    
    // Expose utility methods
    using YamlSnapshotSerializer::safeGet;
    using YamlSnapshotSerializer::lastError;
};
#endif