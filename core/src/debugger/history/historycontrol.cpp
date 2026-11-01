#include "debugger/history/historycontrol.h"

#include "debugger/history/devicehistory.h"
#include "emulator/emulatorcontext.h"

namespace
{

HistoryReply Error(int status, const std::string& message)
{
    HistoryReply reply;
    reply.httpStatus = status;
    reply.body = StateNode::Object();
    reply.body["error"] = message;
    return reply;
}

}  // namespace

namespace HistoryControl
{

HistoryReply Execute(EmulatorContext* context, const HistoryRequest& request)
{
    if (context == nullptr)
    {
        return Error(404, "no emulator");
    }
    if (request.verb == "list")
    {
        HistoryReply reply;
        reply.body = StateNode::Object();
        reply.body["histories"] = context->histories.List();
        return reply;
    }
    if (request.verb != "read" && request.verb != "config" && request.verb != "clear" && request.verb != "pause" &&
        request.verb != "resume")
    {
        return Error(400, "unknown verb '" + request.verb + "' (list, read, config, clear, pause, resume)");
    }

    HistoryReply reply;
    const bool found = context->histories.With(request.device, [&](history::IDeviceHistory& h) {
        if (request.verb == "config")
        {
            history::Config config = h.GetStats().config;
            if (request.mode && !history::ParseMode(*request.mode, config.mode))
            {
                reply = Error(400, "mode '" + *request.mode + "': off, ring or capture");
                return;
            }
            if (request.limit)
                config.limit = static_cast<size_t>(*request.limit);
            if (request.stream)
                config.stream = *request.stream;
            if (request.streamLimit)
                config.streamLimit = *request.streamLimit;
            std::string error;
            if (!h.Configure(config, error))
            {
                reply = Error(400, error);
                return;
            }
        }
        else if (request.verb == "clear")
        {
            h.Clear();
        }
        else if (request.verb == "pause" || request.verb == "resume")
        {
            h.Pause(request.verb == "pause");
        }
        reply.body = history::StatsNode(h);
        if (request.verb == "read")
        {
            reply.body["records"] = h.Records(request.last);
        }
    });
    if (!found)
    {
        return Error(404, "no history for device '" + request.device + "' (see the list)");
    }
    return reply;
}

}  // namespace HistoryControl
