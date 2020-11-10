#ifndef THORIN_PASS_FP_ETA_CONV_H
#define THORIN_PASS_FP_ETA_CONV_H

#include "thorin/pass/pass.h"

namespace thorin {

/**
 * Performs η-conversion.
 * It uses the following strategy:
 *      1. η-reduction: <code>λx.e x -> e</code>, whenever <code>x</code> does not appear free in <code>e</code> and does not contradict rule 2).
 *      2. η-expansion: <code>f -> λx.f x</code>, if
 *          - <code>f</code> is a @p Lam that does not appear in callee position.
 *          - if it is <code>f</code>'s sole occurrence (optimistically), nothing will happen.
 *      This rule is a generalization of critical edge elimination.
 *      It gives other @p Pass%es such as @p SSAConstr the opportunity to change <code>f</code>'s signature (e.g. adding or removing @p Param%s).
 * @code
 *      expand_
 *       /   \
 * Callee     Once_Non_Callee
 *       \   /
 *        Bot
 * @endcode
 */
class EtaConv : public FPPass<EtaConv> {
public:
    EtaConv(PassMan& man, size_t index)
        : FPPass(man, "eta_conv", index)
    {}

    const Def* rewrite(Def*, const Def*) override;
    undo_t analyze(Def*, const Def*) override;

    enum Lattice { Callee, Once_Non_Callee };
    using Data = std::tuple<LamMap<Lattice>>;

private:
    Def2Def def2exp_;
    LamSet expand_;
    Def2Def def2expansion_;
};

}

#endif
