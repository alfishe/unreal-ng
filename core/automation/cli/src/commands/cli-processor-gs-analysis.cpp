#include "cli-processor-gs-analysis.h"

#include <cstdlib>
#include <map>

#include "emulator/sound/chips/gs/gsanalysisreport.h"

namespace
{
constexpr const char* kUsage =
    "Usage: gs analysis [status|on|off|reset|release] [--cpu gs|ms] [--decay N]\n"
    "       gs coverage [--cpu gs|ms] [--space rom|ram] [--page N] [--ranges N] [--raw]\n"
    "       gs heatmap [--cpu gs|ms] [--channels fetch,read,write,dac,sd_dma,zx_dma|all] [--recent <channels>] "
    "[--frames N] [--cells N] [--format base64|hex|none]\n";

/// `--key value` pairs (and the bare flag --raw) after args[0]; positional words in @p words
bool ParseOptions(const std::vector<std::string>& args, std::map<std::string, std::string>& options,
                  std::vector<std::string>& words, std::string& error)
{
    for (size_t i = 1; i < args.size(); i++)
    {
        const std::string& a = args[i];
        if (a.rfind("--", 0) != 0)
        {
            words.push_back(a);
            continue;
        }
        const std::string key = a.substr(2);
        if (key == "raw")
        {
            options[key] = "1";
            continue;
        }
        if (i + 1 >= args.size())
        {
            error = "'" + a + "' needs a value";
            return false;
        }
        options[key] = args[++i];
    }
    return true;
}
}  // namespace

bool HandleGSAnalysisCommand(EmulatorContext* context, const std::vector<std::string>& args, std::string& out)
{
    if (args.empty())
        return false;
    const std::string& sub = args[0];
    if (sub != "analysis" && sub != "coverage" && sub != "heatmap")
        return false;
    std::map<std::string, std::string> options;
    std::vector<std::string> words;
    std::string error;
    if (!ParseOptions(args, options, words, error))
    {
        out = "Error: " + error + "\n" + kUsage;
        return true;
    }
    auto get = [&options](const std::string& key) {
        const auto it = options.find(key);
        return it == options.end() ? std::string() : it->second;
    };

    if (sub == "analysis")
    {
        GSAnalysisControlRequest request;
        request.action = words.empty() ? std::string("status") : words[0];
        request.cpu = get("cpu");
        const std::string decay = get("decay");
        if (!decay.empty())
        {
            char* end = nullptr;
            const long value = std::strtol(decay.c_str(), &end, 10);
            if (!end || *end != '\0' || value < 0 || value > 255)
            {
                out = "Error: '--decay' is 0-255 (heat lost per frame)\n" + std::string(kUsage);
                return true;
            }
            request.decay = static_cast<int>(value);
        }
        const GSAnalysisReply reply = GSAnalysisControl(context, request);
        if (reply.status != 200)
        {
            const StateNode* e = reply.body.find("error");
            out = "Error: " + (e ? e->s : std::string("failed")) + "\n";
            return true;
        }
        const StateNode* text = reply.body.find("text");
        out = text ? text->s : std::string();
        return true;
    }

    if (!words.empty())
    {
        out = "Error: unexpected '" + words[0] + "'\n" + kUsage;
        return true;
    }
    GSAnalysisQuery query;
    if (sub == "heatmap" && get("format").empty())
        options["format"] = "none";  // the grid is for programs; the text lists counts and recent ranges
    if (!GSAnalysisQueryFromParams(get, query, error))
    {
        out = "Error: " + error + "\n" + kUsage;
        return true;
    }
    const StateNode report = sub == "coverage" ? DeviceState::GsCoverage(context, query)
                                               : DeviceState::GsHeatmap(context, query);
    const StateNode* available = report.find("available");
    if (available && !available->b)
    {
        const StateNode* d = report.find("description");
        out = "Error: " + (d ? d->s : std::string("not available")) + "\n";
        return true;
    }
    const StateNode* text = report.find("text");
    out = text ? text->s : std::string();
    if (sub == "heatmap" && query.format == "hex")
        if (const StateNode* channels = report.find("channels"))
            for (const StateNode& c : channels->items)
                if (const StateNode* rows = c.find("rows"))
                {
                    out += c.find("name")->s + ":\n";
                    for (const StateNode& row : rows->items)
                        out += "  " + row.s + "\n";
                }
    return true;
}
