#pragma once

#ifndef MESSAGE_CENTER_MESSAGECENTER_FAST_H
#define MESSAGE_CENTER_MESSAGECENTER_FAST_H

#include "mpmc_queue.h"
#include "objectpool_lockfree.h"
#include "platform.h"

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

constexpr size_t MC_FAST_QUEUE_SIZE = 65536;
constexpr size_t MC_FAST_MAX_TOPICS = 1024;
constexpr size_t MC_FAST_INLINE_SIZE = 64;

struct MessageFast {
    uint16_t topicId;
    uint16_t flags;
    uint32_t size;
    alignas(8) char inlineData[MC_FAST_INLINE_SIZE];
    void* managedData;
    std::atomic<uint32_t> refCount;

    MessageFast() : topicId(0), flags(0), size(0), managedData(nullptr), refCount(0) {
        memset(inlineData, 0, sizeof(inlineData));
    }

    void reset() {
        if (managedData != nullptr) {
            free(managedData);
            managedData = nullptr;
        }
        topicId = 0;
        flags = 0;
        size = 0;
        refCount.store(0, std::memory_order_relaxed);
    }

    const void* data() const {
        return (flags & 1) ? managedData : inlineData;
    }
};

using ObserverCallbackFast = std::function<void(uint16_t topicId, const void* data, size_t size)>;

struct ObserverEntryFast {
    ObserverCallbackFast callback;
    void* userData;
};

class MessageCenterFast {
private:
    static MessageCenterFast* s_instance;

    MPMCQueue<MessageFast*, MC_FAST_QUEUE_SIZE> m_queue;
    ObjectPoolLockFree<MessageFast> m_pool;

    std::string m_topicNames[MC_FAST_MAX_TOPICS];
    std::map<std::string, uint16_t> m_topicMap;
    std::mutex m_topicMutex;
    std::atomic<uint16_t> m_nextTopicId{0};

    std::vector<ObserverEntryFast> m_observers[MC_FAST_MAX_TOPICS];
    std::mutex m_observerMutex;

    std::thread* m_dispatchThread{nullptr};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopping{false};

    std::mutex m_cvMutex;
    std::condition_variable m_cv;

    alignas(64) std::atomic<uint64_t> m_posted{0};
    alignas(64) std::atomic<uint64_t> m_dispatched{0};

public:
    static MessageCenterFast& DefaultMessageCenter(bool autostart = true) {
        if (s_instance == nullptr) {
            s_instance = new MessageCenterFast();
            if (autostart) {
                s_instance->start();
            }
        }
        return *s_instance;
    }

    static void DisposeDefaultMessageCenter() {
        if (s_instance != nullptr) {
            s_instance->stop();
            delete s_instance;
            s_instance = nullptr;
        }
    }

    MessageCenterFast() : m_pool(256, MC_FAST_QUEUE_SIZE * 2) {}
    ~MessageCenterFast() { stop(); }

    MessageCenterFast(const MessageCenterFast&) = delete;
    MessageCenterFast& operator=(const MessageCenterFast&) = delete;

    void start() {
        if (m_running.load(std::memory_order_acquire)) return;

        m_stopping.store(false, std::memory_order_release);
        m_running.store(true, std::memory_order_release);
        m_dispatchThread = new std::thread(&MessageCenterFast::dispatchLoop, this);
    }

    void stop() {
        if (!m_running.load(std::memory_order_acquire)) return;

        m_stopping.store(true, std::memory_order_release);
        m_cv.notify_all();

        if (m_dispatchThread != nullptr) {
            if (m_dispatchThread->joinable()) {
                m_dispatchThread->join();
            }
            delete m_dispatchThread;
            m_dispatchThread = nullptr;
        }

        m_running.store(false, std::memory_order_release);
    }

    uint16_t registerTopic(const std::string& name) {
        std::lock_guard<std::mutex> lock(m_topicMutex);

        auto it = m_topicMap.find(name);
        if (it != m_topicMap.end()) {
            return it->second;
        }

        uint16_t id = m_nextTopicId.fetch_add(1, std::memory_order_relaxed);
        if (id >= MC_FAST_MAX_TOPICS) {
            return UINT16_MAX;
        }

        m_topicNames[id] = name;
        m_topicMap[name] = id;
        return id;
    }

    uint16_t resolveTopic(const std::string& name) {
        std::lock_guard<std::mutex> lock(m_topicMutex);
        auto it = m_topicMap.find(name);
        return (it != m_topicMap.end()) ? it->second : UINT16_MAX;
    }

    void addObserver(uint16_t topicId, ObserverCallbackFast callback, void* userData = nullptr) {
        if (topicId >= MC_FAST_MAX_TOPICS) return;

        std::lock_guard<std::mutex> lock(m_observerMutex);
        m_observers[topicId].push_back({std::move(callback), userData});
    }

    void addObserver(const std::string& topic, ObserverCallbackFast callback, void* userData = nullptr) {
        uint16_t id = registerTopic(topic);
        if (id != UINT16_MAX) {
            addObserver(id, std::move(callback), userData);
        }
    }

    bool post(uint16_t topicId, const void* data, size_t size) {
        if (topicId >= MC_FAST_MAX_TOPICS) return false;

        MessageFast* msg = m_pool.acquire();
        if (msg == nullptr) return false;

        msg->topicId = topicId;
        msg->size = static_cast<uint32_t>(size);

        if (size <= MC_FAST_INLINE_SIZE) {
            msg->flags = 0;
            if (data && size > 0) {
                memcpy(msg->inlineData, data, size);
            }
        } else {
            msg->flags = 1;
            msg->managedData = malloc(size);
            if (msg->managedData && data) {
                memcpy(msg->managedData, data, size);
            }
        }

        if (!m_queue.try_push(msg)) {
            msg->reset();
            m_pool.release(msg);
            return false;
        }

        m_posted.fetch_add(1, std::memory_order_release);
        m_cv.notify_one();
        return true;
    }

    bool post(const std::string& topic, const void* data, size_t size) {
        uint16_t id = resolveTopic(topic);
        if (id == UINT16_MAX) {
            id = registerTopic(topic);
        }
        return post(id, data, size);
    }

    uint64_t pending() const {
        return m_posted.load(std::memory_order_relaxed) - m_dispatched.load(std::memory_order_relaxed);
    }

    void flush(uint32_t timeoutMs = 5000) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (pending() > 0 && std::chrono::steady_clock::now() < deadline) {
            m_cv.notify_one();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

private:
    void dispatchLoop() {
        int emptyCount = 0;

        while (!m_stopping.load(std::memory_order_acquire)) {
            MessageFast* msg = nullptr;

            if (m_queue.try_pop(msg)) {
                dispatch(msg);
                msg->reset();
                m_pool.release(msg);
                m_dispatched.fetch_add(1, std::memory_order_relaxed);
                emptyCount = 0;
            } else {
                emptyCount++;
                if (emptyCount < 100) {
                    std::this_thread::yield();
                } else {
                    std::unique_lock<std::mutex> lock(m_cvMutex);
                    m_cv.wait_for(lock, std::chrono::milliseconds(10), [this]() {
                        return m_stopping.load(std::memory_order_relaxed) || !m_queue.empty();
                    });
                }
            }
        }

        MessageFast* msg = nullptr;
        while (m_queue.try_pop(msg)) {
            dispatch(msg);
            msg->reset();
            m_pool.release(msg);
            m_dispatched.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void dispatch(MessageFast* msg) {
        if (msg == nullptr || msg->topicId >= MC_FAST_MAX_TOPICS) return;

        const void* data = msg->data();
        size_t size = msg->size;
        uint16_t topicId = msg->topicId;

        std::lock_guard<std::mutex> lock(m_observerMutex);
        const auto& observers = m_observers[topicId];

        for (const auto& entry : observers) {
            if (entry.callback) {
                entry.callback(topicId, data, size);
            }
        }
    }
};

#endif // MESSAGE_CENTER_MESSAGECENTER_FAST_H
