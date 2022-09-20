#include "dialects/mem/passes/rw/alloc2malloc.h"

#include "dialects/mem/mem.h"
#include "dialects/core/core.h"

namespace thorin::mem {

const Def* Alloc2Malloc::rewrite(const Def* def) {
    if (auto alloc = match<mem::alloc>(def)) {
        auto [pointee, addr_space] = alloc->decurry()->args<2>();
        return op_malloc(pointee, alloc->arg(), alloc->dbg());
    } else if (auto slot = match<mem::slot>(def)) {
        auto [pointee, addr_space] = slot->decurry()->args<2>();
        auto [mem, id]             = slot->args<2>();
        return op_mslot(pointee, mem, id, slot->dbg());
    }

    return def;
}

static auto size(World& w, const Def* def) {
    return w.op(Trait::size, def);
}

static auto size(World& w, nat_t width) {
    return size(w, w.type_int_width(width));
}

const Def* NormDialectTypeSize::rewrite(const Def* def) {
    if (auto trait = isa<Tag::Trait>(def); trait && trait.sub() == Trait::size) {
        auto type = trait->arg();
        auto& w = world();
        if (match<mem::Ptr>(trait->arg())) return w.op(Trait::size, w.type_int_width(8));
        if (type->isa<Sigma>() || type->isa<Meet>()) {
            const Def* sum = w.lit_int_width(64, 0, nullptr);
            for (auto op: type->ops()) {
                auto sz = core::op_bitcast(w.type_int_width(64), size(w, op));
                sum = core::op(core::wrap::add, width2mod(64), sum, sz);
            }
            return core::op_bitcast(w.type_nat(), sum);
        }
    }
    return def;
}

} // namespace thorin::mem
