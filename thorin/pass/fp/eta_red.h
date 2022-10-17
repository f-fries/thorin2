#pragma once

#include "thorin/pass/pass.h"

namespace thorin {

/// Performs η-reduction.
/// Rewrites `λx.e x` to `e`, whenever `x` does (optimistically) not appear free in `e`.
class EtaRed : public FPPass<EtaRed, Def> {
public:
    EtaRed(PassMan& man, bool lam_only_ = false)
        : FPPass(man, "eta_red")
        , lam_only_(lam_only_) {}

    enum Lattice {
        Bot,         ///< Never seen.
        Reduce,      ///< η-reduction performed.
        Irreducible, ///< η-reduction not possible as we stumbled upon a Var.
    };

    using Data = LamMap<Lattice>;
    void mark_irreducible(Lam* lam) { irreducible_.emplace(lam); }

private:
    const bool lam_only_;
    const Def* rewrite(const Def*) override;
    undo_t analyze(const Var*) override;

    const App* eta_rule(Lam* lam);

    LamSet irreducible_;
};

} // namespace thorin
