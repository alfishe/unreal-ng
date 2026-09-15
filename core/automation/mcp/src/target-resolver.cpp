// TargetResolver implementation — see target-resolver.h

#include "target-resolver.h"

#include <sstream>

namespace mcp
{

/// region <TargetResolver>

void TargetResolver::Resolve(const std::string& target, IApiCaller& caller, ResolveCallback done)
{
    if (!target.empty() && target != "auto")
    {
        // Explicit id — validate it against the running instances
        caller.Call("GET", "/api/v1/emulator/" + target, nullptr,
                    [target, done](int status, Json::Value body) {
                        if (status == 200)
                        {
                            done(true, target);
                        }
                        else if (status == 404)
                        {
                            done(false, "Emulator '" + target +
                                            "' not found. Use emulator_manage action:'list' to see available instances.");
                        }
                        else
                        {
                            done(false, "WebAPI returned HTTP " + std::to_string(status) + " while validating target '" + target + "'.");
                        }
                    });
        return;
    }

    // Auto mode: inspect the instance list
    caller.Call("GET", "/api/v1/emulator", nullptr, [&caller, done](int status, Json::Value body) {
        if (status != 200)
        {
            done(false, "Cannot enumerate emulators (WebAPI returned HTTP " + std::to_string(status) +
                            "). Is the emulator process running with WebAPI enabled (port 8090)?");
            return;
        }

        const Json::Value& emulators = body["emulators"];
        if (!emulators.isArray())
        {
            done(false, "Unexpected WebAPI response: 'emulators' is not an array");
            return;
        }

        if (emulators.size() == 1)
        {
            done(true, emulators[0]["id"].asString());
            return;
        }

        if (emulators.size() > 1)
        {
            std::ostringstream ids;
            for (Json::ArrayIndex i = 0; i < emulators.size(); ++i)
            {
                if (i > 0)
                {
                    ids << ", ";
                }
                ids << emulators[i]["id"].asString();
            }
            done(false, "Multiple emulators are running (" + ids.str() +
                            "). Specify 'target' with one of these ids, or remove extra instances via emulator_manage action:'destroy'.");
            return;
        }

        // Zero instances — auto-create a default machine
        Json::Value createBody;
        createBody["model"] = kDefaultAutoCreateModel;
        caller.Call("POST", "/api/v1/emulator/start", &createBody, [done](int createStatus, Json::Value createResponse) {
            if (createStatus == 201 || createStatus == 200)
            {
                std::string id = createResponse["id"].asString();
                if (!id.empty())
                {
                    done(true, id);
                    return;
                }
            }
            done(false, "No emulator was running and auto-creation of a default " +
                            std::string(kDefaultAutoCreateModel) + " machine failed (HTTP " + std::to_string(createStatus) +
                            "). Create one manually via emulator_manage action:'create'.");
        });
    });
}

void TargetResolver::ResolveFromArgs(const Json::Value& args, IApiCaller& caller, ResolveCallback done)
{
    std::string target = "auto";
    if (args.isObject() && args.isMember("target") && args["target"].isString())
    {
        target = args["target"].asString();
    }
    Resolve(target, caller, std::move(done));
}

/// endregion </TargetResolver>

} // namespace mcp
