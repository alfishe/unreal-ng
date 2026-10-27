#include "sdb/actionexec.h"

#include "sdb/actions.h"
#include "sdb/format.h"

namespace sdb::scn
{

json ActionCall::Body() const
{
    json b = json::object();
    if (!value.is_null())
        b["value"] = value;
    if (params.is_object() && !params.empty())
        b["params"] = params;
    return b;
}

ActionCall MakeActionCall(const json& step, int phase)
{
    ActionCall c;
    c.id = step.value("id", "");
    c.params = json::object();
    const json v = step.contains("value") ? step["value"] : json();
    auto pressFlag = [&]() -> json { return phase == 0 ? json() : json(phase > 0); };
    if (v.is_null())
        c.value = pressFlag();
    else if (v.is_object() && v.contains("d"))
    {
        json delta{{"delta", v["d"]}};
        if (v.contains("rate") && v["rate"].is_number())
            delta["rate"] = v["rate"];
        c.value = delta;
        for (const auto& [k, x] : v.items())
            if (k != "d" && k != "rate")
                c.params[k] = x;
    }
    else if (v.is_object() && v.contains("text") && v["text"].is_string())
    {
        c.value = v["text"];
        for (const auto& [k, x] : v.items())
            if (k != "text")
                c.params[k] = x;
    }
    else if (v.is_object())
    {
        c.params = v;
        c.value = pressFlag();
    }
    else
        c.value = v;  // a string (text), a number (level / delta), a bool (press / release)
    if (step.contains("params") && step["params"].is_object())
        for (const auto& [k, x] : step["params"].items())
            c.params[k] = x;
    return c;
}

namespace
{

class RestActionExecutor final : public ActionExecutor
{
public:
    RestActionExecutor(RestPost post, std::function<std::string()> instance)
        : _post(std::move(post)), _instance(std::move(instance))
    {
    }

    ActionResult Run(const ActionCall& call) override
    {
        if (call.id.empty())
            return {false, "action: no id"};
        json body = call.Body();
        if (_instance)
            if (const std::string id = _instance(); !id.empty())
                body["instance"] = id;
        const HttpResponse r = _post("/api/v1/controllers/actions/" + call.id, body);
        const std::string what = "action " + call.id;
        if (r.status == 0)
            return {false, what + " -> " + (r.error.empty() ? "no answer" : r.error)};
        std::string message;
        bool ok = r.Ok();
        try
        {
            const json j = json::parse(r.body);
            ok = ok && j.value("ok", true);
            message = j.value("message", "");
            if (!ok && message.empty())
                message = j.value("error", "");
            else if (!ok && j.contains("error") && j["error"].is_string() && j["error"] != message)
                message = j["error"].get<std::string>() + ": " + message;
        }
        catch (...)
        {
        }
        std::string text = sdb::Format("{} -> {} ({:.0f} ms)", what, r.status, r.ms);
        if (!message.empty())
            text += ": " + message;
        return {ok, text};
    }

private:
    RestPost _post;
    std::function<std::string()> _instance;
};

}  // namespace

std::unique_ptr<ActionExecutor> MakeRestActionExecutor(RestPost post, std::function<std::string()> instance)
{
    return std::make_unique<RestActionExecutor>(std::move(post), std::move(instance));
}

}  // namespace sdb::scn
