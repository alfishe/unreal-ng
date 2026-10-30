/**
 *
 *  @file TcpServer.cc
 *  @author An Tao
 *
 *  Copyright 2018, An Tao.  All rights reserved.
 *  https://github.com/an-tao/trantor
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the License file.
 *
 *  Trantor
 *
 */

#include <trantor/net/TcpServer.h>
#include <trantor/utils/Logger.h>
#include <functional>
#include <future>
#include <vector>
#include "Acceptor.h"
#include "inner/TcpConnectionImpl.h"
using namespace trantor;
using namespace std::placeholders;

TcpServer::TcpServer(EventLoop *loop,
                     const InetAddress &address,
                     std::string name,
                     bool reUseAddr,
                     bool reUsePort)
    : loop_(loop),
      acceptorPtr_(new Acceptor(loop, address, reUseAddr, reUsePort)),
      serverName_(std::move(name)),
      recvMessageCallback_([](const TcpConnectionPtr &, MsgBuffer *buffer) {
          LOG_ERROR << "unhandled recv message [" << buffer->readableBytes()
                    << " bytes]";
          buffer->retrieveAll();
      }),
      ioLoops_({loop}),
      numIoLoops_(1)
{
    acceptorPtr_->setNewConnectionCallback(
        [this](int fd, const InetAddress &peer) { newConnection(fd, peer); });
}

TcpServer::~TcpServer()
{
    // loop_->assertInLoopThread();
    LOG_TRACE << "TcpServer::~TcpServer [" << serverName_ << "] destructing";
}

void TcpServer::setBeforeListenSockOptCallback(SockOptCallback cb)
{
    acceptorPtr_->setBeforeListenSockOptCallback(std::move(cb));
}

void TcpServer::setAfterAcceptSockOptCallback(SockOptCallback cb)
{
    acceptorPtr_->setAfterAcceptSockOptCallback(std::move(cb));
}

void TcpServer::newConnection(int sockfd, const InetAddress &peer)
{
    LOG_TRACE << "new connection:fd=" << sockfd
              << " address=" << peer.toIpPort();
    loop_->assertInLoopThread();
    EventLoop *ioLoop = ioLoops_[nextLoopIdx_];
    if (++nextLoopIdx_ >= numIoLoops_)
    {
        nextLoopIdx_ = 0;
    }
    TcpConnectionPtr newPtr;
    if (policyPtr_)
    {
        assert(sslContextPtr_);
        newPtr = std::make_shared<TcpConnectionImpl>(
            ioLoop,
            sockfd,
            InetAddress(Socket::getLocalAddr(sockfd)),
            peer,
            policyPtr_,
            sslContextPtr_);
    }
    else
    {
        newPtr = std::make_shared<TcpConnectionImpl>(
            ioLoop, sockfd, InetAddress(Socket::getLocalAddr(sockfd)), peer);
    }

    if (idleTimeout_ > 0)
    {
        assert(timingWheelMap_[ioLoop]);
        newPtr->enableKickingOff(idleTimeout_, timingWheelMap_[ioLoop]);
    }
    newPtr->setRecvMsgCallback(recvMessageCallback_);

    newPtr->setConnectionCallback(
        [this](const TcpConnectionPtr &connectionPtr) {
            if (connectionCallback_)
                connectionCallback_(connectionPtr);
        });
    newPtr->setWriteCompleteCallback(
        [this](const TcpConnectionPtr &connectionPtr) {
            if (writeCompleteCallback_)
                writeCompleteCallback_(connectionPtr);
        });

    newPtr->setCloseCallback([this](const TcpConnectionPtr &closeConnPtr) {
        connectionClosed(closeConnPtr);
    });
    connSet_.insert(newPtr);
    newPtr->connectEstablished();
}

void TcpServer::start()
{
    loop_->runInLoop([this]() {
        assert(!started_);
        started_ = true;
        if (idleTimeout_ > 0)
        {
            for (EventLoop *loop : ioLoops_)
            {
                timingWheelMap_[loop] =
                    std::make_shared<TimingWheel>(loop,
                                                  idleTimeout_,
                                                  1.0F,
                                                  idleTimeout_ < 500
                                                      ? idleTimeout_ + 1
                                                      : 100);
            }
        }
        LOG_TRACE << "map size=" << timingWheelMap_.size();
        acceptorPtr_->listen();
    });
}
void TcpServer::stop()
{
    if (loop_->isInLoopThread())
    {
        closeAllInLoop();
    }
    else
    {
        std::promise<void> pro;
        auto f = pro.get_future();
        loop_->queueInLoop([this, &pro]() {
            closeAllInLoop();
            pro.set_value();
        });
        f.get();
    }
    loopPoolPtr_.reset();
    for (auto &iter : timingWheelMap_)
    {
        std::promise<void> pro;
        auto f = pro.get_future();
        iter.second->getLoop()->runInLoop([&iter, &pro]() mutable {
            iter.second.reset();
            pro.set_value();
        });
        f.get();
    }
}
// unreal-ng: the connections are closed on their own loops before stop()
// returns. A forceClose() only queued there ran after the owner had destroyed
// the server (drogon's quit resets the listeners right after stopListening)
// and its close callback then used the freed server (unreal-qt crashed on
// exit with a WebSocket client connected).
void TcpServer::closeAllInLoop()
{
    loop_->assertInLoopThread();
    acceptorPtr_.reset();
    // copy the connSet_ to a vector, use the vector to close the
    // connections to avoid the iterator invalidation.
    std::vector<TcpConnectionPtr> connPtrs;
    connPtrs.reserve(connSet_.size());
    for (auto &conn : connSet_)
    {
        connPtrs.push_back(conn);
    }
    for (auto &connection : connPtrs)
    {
        auto connLoop = connection->getLoop();
        if (connLoop->isInLoopThread())
        {
            connection->forceClose();
            continue;
        }
        std::promise<void> pro;
        auto f = pro.get_future();
        connLoop->queueInLoop([&connection, &pro]() {
            // in the connection's loop: forceClose closes at once
            connection->forceClose();
            pro.set_value();
        });
        f.get();
    }
}
void TcpServer::handleCloseInLoop(const TcpConnectionPtr &connectionPtr)
{
    size_t n = connSet_.erase(connectionPtr);
    (void)n;
    assert(n == 1);
    auto connLoop = connectionPtr->getLoop();

    // NOTE: always queue this operation in connLoop, because this connection
    // may be in loop_'s current active channels, waiting to be processed.
    // If `connectDestroyed()` is called here, we will be using an wild pointer
    // later.
    connLoop->queueInLoop(
        [connectionPtr]() { connectionPtr->connectDestroyed(); });
}
void TcpServer::connectionClosed(const TcpConnectionPtr &connectionPtr)
{
    LOG_TRACE << "connectionClosed";
    if (loop_->isInLoopThread())
    {
        handleCloseInLoop(connectionPtr);
    }
    else
    {
        // unreal-ng: this may run on loop_ after the server is gone (it is
        // destroyed on loop_, right after stop()): then only the connection
        // is finished
        loop_->queueInLoop(
            [this, alive = std::weak_ptr<bool>(alive_), connectionPtr]() {
                if (alive.lock())
                {
                    handleCloseInLoop(connectionPtr);
                    return;
                }
                connectionPtr->getLoop()->queueInLoop(
                    [connectionPtr]() { connectionPtr->connectDestroyed(); });
            });
    }
}

std::string TcpServer::ipPort() const
{
    return acceptorPtr_->addr().toIpPort();
}

const trantor::InetAddress &TcpServer::address() const
{
    return acceptorPtr_->addr();
}

void TcpServer::enableSSL(
    const std::string &certPath,
    const std::string &keyPath,
    bool useOldTLS,
    const std::vector<std::pair<std::string, std::string>> &sslConfCmds,
    const std::string &caPath)
{
    policyPtr_ = TLSPolicy::defaultServerPolicy(certPath, keyPath);
    policyPtr_->setUseOldTLS(useOldTLS)
        .setConfCmds(sslConfCmds)
        .setCaPath(caPath)
        .setValidate(caPath.empty() ? false : true);
    sslContextPtr_ = newSSLContext(*policyPtr_, true);
}

void TcpServer::reloadSSL()
{
    if (loop_->isInLoopThread())
    {
        if (policyPtr_)
        {
            sslContextPtr_ = newSSLContext(*policyPtr_, true);
        }
    }
    else
    {
        loop_->queueInLoop([this]() {
            if (policyPtr_)
            {
                sslContextPtr_ = newSSLContext(*policyPtr_, true);
            }
        });
    }
}