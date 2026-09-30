#pragma once

/// @file dnsmessage.h
/// @brief The small part of DNS the virtual network needs: read the question
/// of a query, build an answer with A records (RFC 1035 §4). Used to answer
/// guest queries from the host resolver or from the hosts table.

#include <cstdint>
#include <string>
#include <vector>

namespace dns
{

constexpr uint16_t kTypeA = 1;
constexpr uint16_t kClassIn = 1;

constexpr uint8_t kRcodeNoError = 0;
constexpr uint8_t kRcodeFormErr = 1;
constexpr uint8_t kRcodeServFail = 2;
constexpr uint8_t kRcodeNxDomain = 3;
constexpr uint8_t kRcodeNotImp = 4;

struct Question
{
    uint16_t id = 0;
    uint16_t flags = 0;
    std::string name;       ///< lower case, dot separated, no trailing dot
    uint16_t qtype = 0;
    uint16_t qclass = 0;
    size_t questionEnd = 0; ///< offset just after the first question
};

/// Parse the header and the first question of a query. False when the bytes
/// are not a standard query with at least one question.
bool ParseQuery(const uint8_t* data, size_t length, Question& out);

/// Build the answer to `query` (the original bytes): the question echoed, one
/// A record per address (TTL `ttl`), or no answer with `rcode`.
std::vector<uint8_t> BuildAnswer(const uint8_t* query, size_t length, const Question& q,
                                 const std::vector<uint32_t>& addresses, uint8_t rcode, uint32_t ttl = 60);

/// A standard recursive query for the A record of `name` (the emulator's own
/// lookups: a COM port peer given by name). Empty when the name is not valid
std::vector<uint8_t> BuildQuery(uint16_t id, const std::string& name);

/// The A records of an answer to query `id`. False when the bytes are not an
/// answer to it; `rcode` is the answer's code (NXDOMAIN and the like)
bool ParseAnswer(const uint8_t* data, size_t length, uint16_t id, std::vector<uint32_t>& addresses, uint8_t& rcode);

} // namespace dns
