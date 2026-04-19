#include "Blackboard.hpp"

namespace CatEngine {

// Blackboard is implemented as a header-only template container: the `get<T>`
// and `set<T>` templates must be instantiated at each call site, so moving
// them into a .cpp would force explicit instantiations for every type the
// AI ever stores. This file is kept as a compilation unit so the linker
// has a canonical translation unit to attach Blackboard's definitions to
// (preventing ODR surprises across targets that include the header
// through different paths) and so clang-tidy / static analyzers see the
// same include ordering the rest of the AI system uses.

} // namespace CatEngine
