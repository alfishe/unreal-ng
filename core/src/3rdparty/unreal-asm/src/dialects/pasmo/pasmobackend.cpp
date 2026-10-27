#include "dialects/pasmo/pasmobackend.h"

#include "dialects/common/classicbackend.h"

namespace unrealasm::dialects
{
BackendResult PasmoBackend::Write(const ir::Program& program, const BackendOptions& options) const
{
    return WriteClassic(program, options, ClassicTarget::Pasmo);
}
}  // namespace unrealasm::dialects
