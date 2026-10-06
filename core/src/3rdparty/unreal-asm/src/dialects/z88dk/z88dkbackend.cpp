#include "dialects/z88dk/z88dkbackend.h"

#include "dialects/common/classicbackend.h"

namespace unrealasm::dialects
{
BackendResult Z88dkBackend::Write(const ir::Program& program, const BackendOptions& options) const
{
    return WriteClassic(program, options, ClassicTarget::Z80asm);
}
}  // namespace unrealasm::dialects
