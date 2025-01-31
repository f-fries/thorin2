#include "dialects/clos/clos.h"

namespace thorin::clos {

template<clos o>
const Def* normalize_clos(const Def* type, const Def* callee, const Def* arg, const Def* dbg) {
    auto& w = type->world();
    return o == clos::bot ? arg : w.raw_app(callee, arg, dbg);
}

THORIN_clos_NORMALIZER_IMPL

} // namespace thorin::clos
