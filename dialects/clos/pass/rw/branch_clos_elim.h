#pragma once

#include <map>
#include <tuple>

#include "thorin/check.h"

#include "thorin/pass/pass.h"

namespace thorin::clos {

/// BranchClosElim η-expands closures in branches, which enables CopyProp
/// to specialize their environment.

class BranchClosElim : public RWPass<BranchClosElim, Lam> {
public:
    BranchClosElim(PassMan& man)
        : RWPass(man, "branch_clos_elim")
        , clos2wrapper_() {}

    void enter() override;

private:
    DefMap<Lam*> clos2wrapper_;
};

}; // namespace thorin::clos
