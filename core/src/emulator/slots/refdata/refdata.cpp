#include "refdata.h"

namespace slots::refdata
{

const Collection& All()
{
    static const Collection collection{ Cards(), Machines(), Adapters(), Exceptions(), Sources() };
    return collection;
}

} // namespace slots::refdata
