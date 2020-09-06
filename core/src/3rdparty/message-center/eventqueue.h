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

// MAX_TOPICS - By default we're allocating descriptor table for 1024 topics
constexpr unsigned MAX_TOPICS = 1024;

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

// Message types
struct Message
{
public:
    unsigned tid;
    void* obj;

    Message(unsigned tid, void* obj = nullptr)
    {
        this->tid = tid;
        this->obj = obj;
    }
};
typedef std::deque<Message*> MessageQueue;

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
    int m_topicMax = 0;

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
    int AddObserver(std::string& topic, ObserverCallback callback);
    int AddObserver(std::string& topic, Observer* instance, ObserverCallbackMethod callback);
    int AddObserver(std::string& topic, ObserverCallbackFunc callback);
    int AddObserver(std::string& topic, ObserverDescriptor* observer);

    void RemoveObserver(std::string& topic, ObserverCallback callback);
    void RemoveObserver(std::string& topic, Observer* instance, ObserverCallbackMethod callback);
    void RemoveObserver(std::string& topic, ObserverCallbackFunc callback);
    void RemoveObserver(std::string& topic, ObserverDescriptor* observer);

    int ResolveTopic(const char* topic);
    int ResolveTopic(std::string& topic);
    int RegisterTopic(const char* topic);
    int RegisterTopic(std::string& topic);
    std::string GetTopicByID(int id);
    void ClearTopics();

    void Post(int id, void* obj = nullptr);
    void Post(std::string topic, void* obj = nullptr);

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
