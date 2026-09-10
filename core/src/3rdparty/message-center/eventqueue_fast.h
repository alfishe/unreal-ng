#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

constexpr size_t FAST_QUEUE_SIZE = 4096;
constexpr size_t FAST_MAX_TOPICS = 256;
constexpr size_t FAST_MAX_OBSERVERS = 32;

struct FastMessage;
typedef void (*FastObserverCallback)(int id, FastMessage* message);

struct FastMessagePayload
{
    virtual ~FastMessagePayload() = default;
};

struct FastMessage
{
    int topicId;
    FastMessagePayload* payload;
    bool ownsPayload;
};

class FastEventQueue
{
public:
    FastEventQueue()
    {
        for (auto& slot : m_queue)
        {
            slot.sequence.store(0, std::memory_order_relaxed);
        }
        m_head.store(0, std::memory_order_relaxed);
        m_tail.store(0, std::memory_order_relaxed);

        for (auto& count : m_observerCounts)
        {
            count = 0;
        }
    }

    int RegisterTopic(const std::string& topic)
    {
        for (size_t i = 0; i < m_topicCount; i++)
        {
            if (m_topics[i] == topic)
                return static_cast<int>(i);
        }

        if (m_topicCount >= FAST_MAX_TOPICS)
            return -1;

        size_t idx = m_topicCount++;
        m_topics[idx] = topic;
        return static_cast<int>(idx);
    }

    void AddObserver(const std::string& topic, FastObserverCallback callback)
    {
        int id = RegisterTopic(topic);
        if (id < 0) return;

        size_t& count = m_observerCounts[id];
        if (count < FAST_MAX_OBSERVERS)
        {
            m_observers[id][count++] = callback;
        }
    }

    void Post(int topicId, FastMessagePayload* payload = nullptr, bool ownsPayload = false)
    {
        if (topicId < 0) return;

        // Dispatch inline (synchronous) for lowest latency
        FastMessage msg{topicId, payload, ownsPayload};

        size_t count = m_observerCounts[topicId];
        for (size_t i = 0; i < count; i++)
        {
            m_observers[topicId][i](topicId, &msg);
        }

        if (ownsPayload && payload)
        {
            delete payload;
        }
    }

    void Post(const std::string& topic, FastMessagePayload* payload = nullptr, bool ownsPayload = false)
    {
        int id = RegisterTopic(topic);
        Post(id, payload, ownsPayload);
    }

private:
    struct Slot
    {
        std::atomic<size_t> sequence;
        FastMessage message;
    };

    alignas(64) std::atomic<size_t> m_head;
    alignas(64) std::atomic<size_t> m_tail;
    std::array<Slot, FAST_QUEUE_SIZE> m_queue;

    std::array<std::string, FAST_MAX_TOPICS> m_topics;
    size_t m_topicCount = 0;

    std::array<std::array<FastObserverCallback, FAST_MAX_OBSERVERS>, FAST_MAX_TOPICS> m_observers;
    std::array<size_t, FAST_MAX_TOPICS> m_observerCounts;
};

// Payload types for benchmarks
struct FastNumberPayload : FastMessagePayload
{
    uint32_t value;
    FastNumberPayload(uint32_t v) : value(v) {}
};

struct FastTextPayload : FastMessagePayload
{
    std::string text;
    FastTextPayload(const std::string& t) : text(t) {}
};

struct FastFramePayload : FastMessagePayload
{
    std::string emulatorId;
    uint32_t frameNumber;
    FastFramePayload(const std::string& id, uint32_t frame) : emulatorId(id), frameNumber(frame) {}
};
