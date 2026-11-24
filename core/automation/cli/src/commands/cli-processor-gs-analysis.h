#pragma once

/// @file cli-processor-gs-analysis.h
/// @brief CLI 'gs analysis|coverage|heatmap': the GS card CPU analysis (gsanalysisreport.h, gs-debugger P5.6)

#include <string>
#include <vector>

class EmulatorContext;

/// Handles `args` when args[0] is analysis, coverage or heatmap: true with the reply in @p out (lines ending in
/// "\n"), false for any other subcommand
///   gs analysis [status|on|off|reset|release] [--cpu gs|ms] [--decay N]
///   gs coverage [--cpu gs|ms] [--space rom|ram] [--page N] [--ranges N] [--raw]
///   gs heatmap [--cpu gs|ms] [--channels a,b] [--recent a,b] [--frames N] [--cells N] [--format base64|hex|none]
bool HandleGSAnalysisCommand(EmulatorContext* context, const std::vector<std::string>& args, std::string& out);
