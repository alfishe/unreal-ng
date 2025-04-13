#pragma once

#ifndef MESSAGE_CENTER_EVENTQUEUE_H
#define MESSAGE_CENTER_EVENTQUEUE_H

#include "collectionhelper.h"
#include "streamhelper.h"

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

/// region <Constants>

// MAX_TOPICS - By default we're allocating descriptor table for 1024 topics
constexpr unsigned MAX_TOPICS = 1024;

/// endregion </Constants>

/// region <Types>

struct Observer;
struct Message;
typedef void (ObserverCallback)(int id, Message* message);                      // Classic callback
typedef void (Observer::* ObserverCallbackMethod)(int id, Message* message);    // Class method callback
typedef std::function<void(int id, Message* message)> ObserverCallbackFunc;     // For lambda usage
struct ObserverDescriptor
{
    ObserverCallback* callback;
    ObserverCallbackMethod callbackMethod;  // Uses Observer::_some_method(int id, Message* messsage) callback signature. Requires observerInstance to be defined.
    ObserverCallbackFunc callbackFunc;

    Observer* observerInstance;             // Used to hold class instance for callbackMethod
};

// Base class for all observer listeners. Derived class can implement method with any name
// But signature should be exactly void _custom_method_(int id, Message* message)
struct Observer
{
public:
    //virtual void ObserverCallbackMethod(int id, Message* message) = 0;
};

// Topic types
typedef std::map<std::string, int> TopicResolveMap;
typedef std::pair<std::string, int> TopicResolveRecord;

// Observer types
typedef std::vector<ObserverDescriptor*> ObserversVector;
typedef ObserversVector* ObserverVectorPtr;
typedef std::map<int, ObserverVectorPtr> TopicObserversMap;

// Base class for payload objects
class MessagePayload
{
public:
    MessagePayload() {};
    virtual ~MessagePayload() {};
};


// Message types
struct Message
{
public:
    unsigned tid;
    MessagePayload* obj;
    bool cleanupPayload;

    Message(unsigned tid, MessagePayload* obj = nullptr, bool cleanupPayload = true)
    {
        this->tid = tid;
        this->obj = obj;
        this->cleanupPayload = cleanupPayload;
    }
};
typedef std::deque<Message*> MessageQueue;

/// endregion </Types>

/// region <Predefined payload types>

/// Allows to pass text string in MessageCenter message
/// Example: messageCenter.Post(topic, new SimpleTextPayload("my text message");
class SimpleTextPayload : public MessagePayload
{
public:
    std::string _payloadText;

public:
    SimpleTextPayload(std::string& text) : MessagePayload() { _payloadText = std::string(text); };
    SimpleTextPayload(const char* text) : MessagePayload() { _payloadText = std::string(text); };
    virtual ~SimpleTextPayload() = default;
};

/// Allows to pass 32 bit numbers in MessageCenter message
/// Example: messageCenter.Post(topic, new SimpleNumberPayload(0x12345678);
class SimpleNumberPayload : public MessagePayload
{
public:
    uint32_t _payloadNumber;

public:
    SimpleNumberPayload(uint32_t value) : MessagePayload() { _payloadNumber = value; };
    virtual ~SimpleNumberPayload() = default;
};

/// Allows to transfer uint8_t data blocks (as std::vector<uint8_t> in MessageCenter message
/// std::move for parameter is mandatory since we don't want double copy for all content
/// Warning: payloads longer than 10k are not recommended. Copy constructors to transfer large data blocks will be slow.
/// Example:
///   std::vector<uint8_t> payload = { 0x00, 0x01, 0x02, 0x03 };
///   messageCenter.Post(topic, new SimpleByteDataPayload(std::move(payload)));
class SimpleByteDataPayload : public MessagePayload
{
public:
    std::vector<uint8_t> _payloadByteVector;

public:
    SimpleByteDataPayload(const std::vector<uint8_t>&& payload) : MessagePayload() { _payloadByteVector = payload; };
    virtual ~SimpleByteDataPayload()  = default;
};


/// endregion </Predefined payload types>
class EventQueue
{
// Synchronization primitives
protected:
    std::atomic<bool> m_initialized;
    std::mutex m_mutexObservers;

    std::mutex m_mutexMessages;
    std::condition_variable m_cvEvents;

// Fields
protected:
    std::string m_topics[MAX_TOPICS];
    TopicResolveMap m_topicsResolveMap;
    unsigned m_topicMax = 0;

    TopicObserversMap m_topicObservers;

    MessageQueue m_messageQueue;

// Class methods
public:
    EventQueue();
    virtual ~EventQueue();
    EventQueue(const EventQueue& that) = delete; 			// Disable copy constructor. C++11 feature
    EventQueue& operator =(EventQueue const&) = delete;		// Disable assignment operator. C++11 feature

// Initialization
public:
    bool init();
    void dispose();

// Public methods
public:
    int AddObserver(const std::string& topic, ObserverCallback callback);
    int AddObserver(const std::string& topic, Observer* instance, ObserverCallbackMethod callback);
    int AddObserver(const std::string& topic, ObserverCallbackFunc callback);
    int AddObserver(const std::string& topic, ObserverDescriptor* observer);

    void RemoveObserver(const std::string& topic, ObserverCallback callback);
    void RemoveObserver(const std::string& topic, Observer* instance, ObserverCallbackMethod callback);
    void RemoveObserver(const std::string& topic, ObserverCallbackFunc callback);
    void RemoveObserver(const std::string& topic, ObserverDescriptor* observer);

    int ResolveTopic(const char* topic);
    int ResolveTopic(const std::string& topic);
    int RegisterTopic(const char* topic);
    int RegisterTopic(const std::string& topic);
    std::string GetTopicByID(int id);
    void ClearTopics();

    void Post(int id, MessagePayload* obj = nullptr, bool autoCleanupPayload = true);
    void Post(std::string topic, MessagePayload* obj = nullptr, bool autoCleanupPayload = true);

protected:
    Message* GetQueueMessage();
    void Dispatch(int id, Message* message);

    ObserverVectorPtr GetObservers(int id);

#ifdef _DEBUG
    // Debug helpers
public:
    std::string DumpTopics();
    std::string DumpObservers();
    std::string DumpMessageQueue();
    std::string DumpMessageQueueNoLock();
#endif // _DEBUG
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing / benchmark purposes
//
#ifdef _CODE_UNDER_TEST

class EventQueueCUT : public EventQueue
{
public:
    EventQueueCUT() : EventQueue() {};

public:
    using EventQueue::m_topicsResolveMap;
    using EventQueue::m_topicMax;

    using EventQueue::m_topicObservers;

    using EventQueue::m_messageQueue;

    using EventQueue::GetObservers;
    using EventQueue::GetQueueMessage;
    using EventQueue::Dispatch;
};
#endif // _CODE_UNDER_TEST

#endif // MESSAGE_CENTER_EVENTQUEUE_H
