#pragma once

#include <string>

namespace api::v1
{
/// Removes the `<pid>-<random>` session folders in `uploadsDir` whose process is gone (it crashed or was killed before
/// its exit cleanup ran). Other names are not the upload helper's, and a running process's folder stays. Drogon-free,
/// so core-tests compile it
void SweepUploadSessions(const std::string& uploadsDir);
} // namespace api::v1
