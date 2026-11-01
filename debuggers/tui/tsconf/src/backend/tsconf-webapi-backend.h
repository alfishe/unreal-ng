// tsconf-webapi-backend.h - the WebAPI backend with the fork's additions (TDD-DBG-02 §5): the TS-Conf register board
// from GET /state/tsconf and the PC history from GET /debug/pchist, each read once per stop.
#pragma once

#include <memory>
#include <vector>

#include "backend/webapi-backend.h"

namespace dbg {

class TsConfWebApiBackend : public WebApiBackend {
public:
    explicit TsConfWebApiBackend(std::unique_ptr<IApiTransport> transport)
        : WebApiBackend(std::move(transport)) {}

    std::vector<PcHistEntry> GetPcHistory(int cpu) override;
    bool GetTsConf(TsConfState& out) override;

private:
    uint64_t tsAt_ = 0;    // Snapshots() the board was read at (0: never)
    bool tsValid_ = false;
    TsConfState ts_;
    uint64_t histAt_ = 0;  // Snapshots() the history was read at
    std::vector<PcHistEntry> hist_;
};

}  // namespace dbg
