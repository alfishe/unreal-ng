#ifndef MESSAGE_CENTER_MESSAGECENTER_H
#define MESSAGE_CENTER_MESSAGECENTER_H

#include "eventqueue.h"
#include <atomic>
#include <mutex>
#include <thread>

class MessageCenter : public EventQueue
{
protected:
    static MessageCenter* m_instance;
    std::mutex m_mutexThreads;
    std::thread* m_thread;

    std::atomic<bool> m_started;
    std::atomic<bool> m_requestStop;
    std::atomic<bool> m_stopped;


public:
    static MessageCenter& DefaultMessageCenter(bool autostart = true);
    static void DisposeDefaultMessageCenter();

protected:
    MessageCenter();                                // Disable ability to created class instances directly. Only GetInstance methods are allowed
    MessageCenter(const MessageCenter&);            // Prevent construction by copying
    MessageCenter& operator=(const MessageCenter&); // Prevent assignment
    virtual ~MessageCenter();                       // Prevent unwanted destruction

    void Start();
    void Stop();
    void ThreadWorker();
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing / benchmark purposes
//
#ifdef _CODE_UNDER_TEST

class MessageCenterCUT : public MessageCenter
{
public:
    static MessageCenterCUT* m_instanceCUT;
    MessageCenterCUT() : MessageCenter() {};
    virtual ~MessageCenterCUT() {};
    static MessageCenterCUT& DefaultMessageCenter(bool autostart = true)
    {
        if (m_instanceCUT == nullptr)
        {
            m_instanceCUT = new MessageCenterCUT();
            m_instance = m_instanceCUT;

            if (autostart)
            {
                m_instanceCUT->Start();
            }
        }

        return *m_instanceCUT;
    };

    static void DisposeDefaultMessageCenter()
    {
        MessageCenter::DisposeDefaultMessageCenter();

        m_instanceCUT = nullptr;
    }

public:
    using EventQueue::m_topicsResolveMap;
    using EventQueue::m_topicMax;

    using EventQueue::m_topicObservers;

    using EventQueue::m_messageQueue;

    using EventQueue::GetObservers;
    using EventQueue::GetQueueMessage;
    using EventQueue::Dispatch;

    using MessageCenter::m_instance;
    using MessageCenter::m_thread;
    using MessageCenter::m_requestStop;
    using MessageCenter::m_stopped;

    using MessageCenter::Start;
    using MessageCenter::Stop;
    using MessageCenter::ThreadWorker;
};
#endif // _CODE_UNDER_TEST

#endif //MESSAGE_CENTER_MESSAGECENTER_H
