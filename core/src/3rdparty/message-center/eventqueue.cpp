#include "eventqueue.h"


EventQueue::EventQueue()
{
    init();
}

EventQueue::~EventQueue()
{
    dispose();
}

bool EventQueue::init()
{
    bool result = false;

    m_initialized = true;

    return result;
}

void EventQueue::dispose()
{
    // Cleanup message queue
    if (m_messageQueue.size() > 0)
    {
        std::lock_guard<std::mutex> lock(m_mutexMessages);

        for (auto it : m_messageQueue)
        {
            if (it != nullptr)
            {
                // Delete payload if ownership was transferred to the message
                if (it->cleanupPayload && it->obj != nullptr)
                {
                    delete it->obj;
                }
                delete it;
            }
        }

        m_messageQueue.clear();
    }

    // Cleanup observers
    if (m_topicObservers.size() > 0)
    {
        std::lock_guard<std::mutex> lock(m_mutexObservers);

        for (auto it : m_topicObservers)
        {
            if (it.second != nullptr)
            {
                for (auto observerDescriptor : *it.second)
                {
                    if (observerDescriptor != nullptr)
                    {
                        delete observerDescriptor;
                    }
                }

                it.second->clear();
                delete it.second;  // Free the vector itself
            }
        }

        m_topicObservers.clear();
    }
}

uint64_t EventQueue::AddObserver(const std::string& topic, ObserverCallback callback)
{
    ObserverDescriptor* observer = new ObserverDescriptor();
    observer->callback = callback;
    return AddObserver(topic, observer);
}

// Add class method as observer
// 1. Class should be derived from Observer
// 2. Method signature should be void method(int id, Message* message);
// Example:
// class TestObservers_ClassMethod_class : public Observer
// {
// public:
//     void ObserverTestMethod(int id, Message* message) { str::cout << "It works!" << std::endl; }
// }
//
// EventQueue queue;
// // Register topics... other prep. actions
//
// TestObservers_ClassMethod_class observerDerivedInstance;
// Observer* observerInstance = static_cast<Observer*>(&observerDerivedInstance);
// ObserverCallbackMethod callback = static_cast<ObserverCallbackMethod>(&TestObservers_ClassMethod_class::ObserverTestMethod);
//
// queue.AddObserver(topic, observerInstance, callback);
uint64_t EventQueue::AddObserver(const std::string& topic, Observer* instance, ObserverCallbackMethod callback)
{
    ObserverDescriptor* observer = new ObserverDescriptor();
    observer->callbackMethod = callback;
    observer->observerInstance = instance;

    return AddObserver(topic, observer);
}

uint64_t EventQueue::AddObserver(const std::string& topic, ObserverCallbackFunc callback)
{
    ObserverDescriptor* observer = new ObserverDescriptor();
    observer->callbackFunc = callback;
    return AddObserver(topic, observer);
}

uint64_t EventQueue::AddObserver(const std::string& topic, ObserverDescriptor* observer)
{
    // Register Topic (or get TopicID if already registered)
    int topicId = RegisterTopic(topic);

    if (topicId < 0)
        return 0;  // Invalid topic

    // Assign unique observer ID
    uint64_t observerId = m_nextObserverId.fetch_add(1, std::memory_order_relaxed);
    observer->observerId = observerId;

    {
        // Lock parallel threads to access (active till return from method and lock destruction)
        std::lock_guard<std::mutex> lock(m_mutexObservers);

        ObserverVectorPtr observers = GetObservers(topicId);
        if (observers == nullptr)
        {
            // Observers list not created yet - create vector and register it in topic-observers map collection
            observers = new ObserversVector();
            m_topicObservers.insert( {topicId, observers });
        }

        if (observers != nullptr)
        {
            observers->push_back(observer);
        }
    }

    return observerId;
}

void EventQueue::RemoveObserver(const std::string& topic, ObserverCallback callback)
{
    int result = ResolveTopic(topic);

    {
        // Lock parallel threads to access (active till return from method and lock destruction)
        std::lock_guard<std::mutex> lock(m_mutexObservers);

        ObserverVectorPtr observers = GetObservers(result);
        if (observers != nullptr)
        {
            ObserversVector::const_iterator it;
            for (it = observers->begin(); it != observers->end(); )
            {
                ObserverDescriptor* observer = *it;

                if (observer->callback == callback)
                {
                    // Erase current element and get next iterator value
                    it = observers->erase(it);

                    // Destroy descriptor object
                    delete observer;
                }
                else
                {
                    it++;
                }
            }
        }
    }

    // In-flight dispatches may still be invoking this observer's handler from
    // a pre-removal snapshot - wait for them (see WaitForDispatchesToComplete)
    WaitForDispatchesToComplete();
}

void EventQueue::RemoveObserver(const std::string& topic, Observer* instance, ObserverCallbackMethod callback)
{
    int result = ResolveTopic(topic);

    {
        // Lock parallel threads to access (active till return from method and lock destruction)
        std::lock_guard<std::mutex> lock(m_mutexObservers);

        ObserverVectorPtr observers = GetObservers(result);
        if (observers != nullptr)
        {
            ObserversVector::const_iterator it;
            for (it = observers->begin(); it != observers->end(); )
            {
                ObserverDescriptor* observer = *it;

                if (observer->observerInstance == instance && observer->callbackMethod == callback)
                {
                    // Erase current element and get next iterator value
                    it = observers->erase(it);

                    // Destroy descriptor object
                    delete observer;
                }
                else
                {
                    it++;
                }
            }
        }
    }

    // In-flight dispatches may still be invoking this observer's handler from
    // a pre-removal snapshot - wait for them (see WaitForDispatchesToComplete)
    WaitForDispatchesToComplete();
}

void EventQueue::RemoveObserverById(const std::string& topic, uint64_t observerId)
{
    if (observerId == 0)
        return;  // Invalid ID

    int topicId = ResolveTopic(topic);

    {
        // Lock parallel threads to access (active till return from method and lock destruction)
        std::lock_guard<std::mutex> lock(m_mutexObservers);

        ObserverVectorPtr observers = GetObservers(topicId);
        if (observers != nullptr)
        {
            ObserversVector::const_iterator it;
            for (it = observers->begin(); it != observers->end(); )
            {
                ObserverDescriptor* observer = *it;

                if (observer->observerId == observerId)
                {
                    // Erase current element and get next iterator value
                    it = observers->erase(it);

                    // Destroy descriptor object
                    delete observer;
                    break;  // IDs are unique, no need to continue
                }
                else
                {
                    it++;
                }
            }
        }
    }

    // In-flight dispatches may still be invoking this observer's handler from
    // a pre-removal snapshot - wait for them (see WaitForDispatchesToComplete)
    WaitForDispatchesToComplete();
}

int EventQueue::ResolveTopic(const char* topic)
{
    std::string strTopic(topic);
    int result = ResolveTopic(strTopic);

    return result;
}


int EventQueue::ResolveTopic(const std::string& topic)
{
    int result = -1;

    if (topic.length() > 0)
    {
        // The registry is mutated by RegisterTopic() from any thread while
        // Post() resolves topics from the dispatching path - guard the access
        std::lock_guard<std::mutex> lock(m_mutexTopics);

        if (mc::key_exists(m_topicsResolveMap, topic))
        {
            result = m_topicsResolveMap[topic];
        }
    }

    return result;
}

int EventQueue::RegisterTopic(const char* topic)
{
    std::string strTopic(topic);
    int result = RegisterTopic(strTopic);

    return result;
}

int EventQueue::RegisterTopic(const std::string& topic)
{
    int result = -1;

    if (topic.length() > 0)
    {
        // The registry is read by ResolveTopic() from any thread - guard the access
        std::lock_guard<std::mutex> lock(m_mutexTopics);

        if (mc::key_exists(m_topicsResolveMap, topic))
        {
            // Already registered. Returning it's ID
            result = m_topicsResolveMap[topic];
        }
        else
        {
            if (static_cast<unsigned>(m_topicMax) < MAX_TOPICS)
            {
                // Registering new ID
                m_topicsResolveMap.insert({ topic, m_topicMax });
                m_topics[m_topicMax] = topic;

                result = m_topicMax;
                m_topicMax++;
            }
            else
            {
                // Array for topic descriptors is full
                result = -2;
            }
        }
    }

    return result;
}

std::string EventQueue::GetTopicByID(int id)
{
    std::string result;

    // The registry is mutated by RegisterTopic() from any thread - guard the access
    std::lock_guard<std::mutex> lock(m_mutexTopics);

    if (id > 0 && static_cast<unsigned>(id) < MAX_TOPICS)
    {
        result = m_topics[id];
    }

    return result;
}

void EventQueue::ClearTopics()
{
    std::lock_guard<std::mutex> lock(m_mutexTopics);

    m_topicsResolveMap.clear();
    for (auto& topic : m_topics)
    {
        topic.clear();
    }
    m_topicMax = 0;
}

void EventQueue::Post(int id, MessagePayload* obj, bool autoCleanupPayload)
{
    if (id >= 0)
    {
        // Lock parallel threads to access (unique_lock can be unlocked arbitrarily)
        std::unique_lock<std::mutex> lock(m_mutexMessages);

        Message* message = new Message(id, obj, autoCleanupPayload);
        m_messageQueue.push_back(message);
        lock.unlock();

        m_cvEvents.notify_one();
    }
}

void EventQueue::Post(std::string topic, MessagePayload* obj, bool autoCleanupPayload)
{
    int id = ResolveTopic(topic);
    if (id >= 0)
    {
        Post(id, obj, autoCleanupPayload);
    }
    else if (autoCleanupPayload && obj != nullptr)
    {
        // Topic not registered - clean up payload to prevent leak
        delete obj;
    }
}

// Lookup for observer list for topic with <id>
// Returns: ObserverVectorPtr aka vector<Observer*>
ObserverVectorPtr EventQueue::GetObservers(int id)
{
    ObserverVectorPtr result = nullptr;

    if (mc::key_exists(m_topicObservers, id))
    {
        result = m_topicObservers[id];
    }

    return result;
}

// Retrieve topmost message in Message Queue
Message* EventQueue::GetQueueMessage()
{
    Message* result = nullptr;

    std::lock_guard<std::mutex> lock(m_mutexMessages);
    if (!m_messageQueue.empty())
    {
        result = m_messageQueue.front();
        m_messageQueue.pop_front();
    }

    return result;
}

// Dispatch message to all subscribers of the topic
void EventQueue::Dispatch(int id, Message* message)
{
    if (message == nullptr)
        return;

    // Snapshot the observer descriptors under m_mutexObservers and register
    // this dispatch as active before releasing the lock: AddObserver() and
    // RemoveObserver() mutate the very same topic vectors (and erase + delete
    // the descriptors) under that mutex, so iterating the live storage from
    // the worker thread raced with removals and crashed on freed descriptors
    // (segfaults observed in parallel sharded test runs). Copying descriptor
    // VALUES also keeps a snapshot invocation valid after the originals are
    // deleted. The lock is deliberately NOT held while handlers run, so
    // handlers may freely call AddObserver()/RemoveObserver()/Post().
    std::vector<ObserverDescriptor> observersSnapshot;
    {
        std::lock_guard<std::mutex> lock(m_mutexObservers);

        ObserverVectorPtr observers = GetObservers(id);
        if (observers != nullptr)
        {
            observersSnapshot.reserve(observers->size());
            for (const ObserverDescriptor* observer : *observers)
            {
                if (observer != nullptr)
                {
                    observersSnapshot.push_back(*observer);
                }
            }
        }

        MarkDispatchActive();
    }

    for (const ObserverDescriptor& observer : observersSnapshot)
    {
        if (observer.callback != nullptr)
        {
            (*observer.callback)(id, message);
        }
        else if (observer.callbackMethod != nullptr && observer.observerInstance != nullptr)
        {
            ObserverCallbackMethod callbackMethod = observer.callbackMethod;
            (observer.observerInstance->*callbackMethod)(id, message);
        }
        else if (observer.callbackFunc != nullptr)
        {
            (observer.callbackFunc)(id, message);
        }
    }

    MarkDispatchComplete();
    // Cleanup message when delivered
    if (message)
    {
        if (message->cleanupPayload && message->obj)
        {
            delete message->obj;
        }

        delete message;
    }
}

namespace
{
    // Set on a thread currently inside Dispatch()'s invocation loop; removals
    // issued from a handler must not wait for their own dispatch to complete
    thread_local bool tl_inDispatch = false;
}

void EventQueue::MarkDispatchActive()
{
    {
        std::lock_guard<std::mutex> lock(m_mutexDispatches);
        m_activeDispatches++;
    }

    tl_inDispatch = true;
}

void EventQueue::MarkDispatchComplete()
{
    tl_inDispatch = false;

    std::lock_guard<std::mutex> lock(m_mutexDispatches);
    m_activeDispatches--;
    if (m_activeDispatches == 0)
    {
        m_cvDispatches.notify_all();
    }
}

void EventQueue::WaitForDispatchesToComplete()
{
    // RemoveObserver() contract: once it returns, no removed handler is
    // running and none will start later - Dispatch() takes its snapshot under
    // the observers mutex, so any dispatch that already snapshotted the
    // removed descriptor is still mid-invocation here. Callers rely on this
    // to destroy handler state (stack locals, observer instances) right
    // after removal.
    if (tl_inDispatch)
    {
        // A handler removing an observer must not wait for its own dispatch
        return;
    }

    std::unique_lock<std::mutex> lock(m_mutexDispatches);
    m_cvDispatches.wait(lock, [this]() { return m_activeDispatches == 0; });
}

#ifdef _DEBUG

#include <sstream>

std::string EventQueue::DumpTopics()
{
    std::string result;
    std::stringstream ss;

    ss << "Topics map contains: " << m_topicsResolveMap.size();
    if (m_topicsResolveMap.size() > 0)
        ss << std::endl;

    for (auto topic : m_topicsResolveMap)
    {
        ss << "  tid: " << topic.second << "; topic:'" << topic.first << '\'';
        ss << std::endl;
    }

    if (m_topicsResolveMap.size() > 0)
        ss << std::endl;

    result = ss.str();
    return result;
}
std::string EventQueue::DumpObservers()
{
    std::string result;
    std::stringstream ss;

    ss << "Observers registered for: " << m_topicObservers.size() << " topics";
    if (m_topicsResolveMap.size() > 0)
        ss << std::endl;

    for (auto topic : m_topicObservers)
    {
        ss << "tid:" << topic.first;
        if (topic.second != nullptr)
        {
            ss << " has " << topic.second->size() << " observers" << std::endl;

            int callbackCounter = 0;
            for (auto observer : *topic.second)
            {
                ss << "  [" << callbackCounter << "] ";

                if (observer->callback != nullptr)
                {
                    using namespace mc_function_display;
                    ss << "callback: " <<  observer->callback << std::endl;
                }
                else if (observer->callbackFunc != nullptr)
                {
                    using namespace mc_lambda_display;
                    ss << "callbackFunc: " << observer->callbackFunc << std::endl;
                }
                else if (observer->callbackMethod != nullptr)
                {
                    using namespace mc_class_method_display;
                    ss << "callbackMethod: " << observer->callbackMethod << std::endl;
                }
                else
                {
                    ss << "No callbacks registered" << std::endl;
                }

                callbackCounter++;
            }
        }
        else
        {
            ss << " has no observers" << std::endl;
        }
    }

    if (m_topicsResolveMap.size() > 0)
        ss << std::endl;

    result = ss.str();
    return result;
}

std::string EventQueue::DumpMessageQueue()
{
    // Lock parallel threads to access (unique_lock allows arbitrary lock/unlock)
    std::lock_guard<std::mutex> lock(m_mutexMessages);

    return DumpMessageQueueNoLock();
}

std::string EventQueue::DumpMessageQueueNoLock()
{
    std::string result;

    std::stringstream ss;

    ss << "Message queue contains: " << m_messageQueue.size() << " messages";
    if (m_messageQueue.size() > 0)
        ss << std::endl;

    for (auto message : m_messageQueue)
    {
        ss << "  tid: " << message->tid << "; obj*: " << message->obj;
        ss << std::endl;
    }

    if (m_messageQueue.size() > 0)
        ss << std::endl;

    result = ss.str();
    return result;
}

#endif // _DEBUG