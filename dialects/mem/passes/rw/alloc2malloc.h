#pragma once

#include "thorin/pass/pass.h"


namespace thorin::mem {

class Alloc2Malloc : public RWPass<Alloc2Malloc, Lam> {
public:
    Alloc2Malloc(PassMan& man)
        : RWPass(man, "alloc2malloc") {}

    const Def* rewrite(const Def*) override;
};

class NormDialectTypeSize : public RWPass<NormDialectTypeSize, Lam> {
public:
    NormDialectTypeSize(PassMan& man)
        : RWPass(man, "norm_ptr_size") {}

    const Def* rewrite(const Def* def) override;
};

} // namespace thorin::mem
