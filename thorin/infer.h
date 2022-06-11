#ifndef THORIN_INFER_H
#define THORIN_INFER_H

#include "thorin/axiom.h"

namespace thorin {

/// This node is a hole in the IR that is inferred by its context later on.
/// It is modelled as a *nom*inal Def.
/// If inference was successful,
class Infer : public Def {
private:
    Infer(const Def* type, const Def* dbg)
        : Def(Node, type, 1, 0, dbg) {}

public:
    /// @name op
    ///@{
    const Def* op() const { return Def::op(0); }
    Infer* set(const Def* op) { return Def::set(0, op)->as<Infer>(); }
    ///@}

    /// @name virtual methods
    ///@{
    Infer* stub(World&, const Def*, const Def*) override;
    ///@}

    static constexpr auto Node = Node::Infer;
    friend class World;
};

template<class AxTag>
const Def* invoke(AxTag sub, Defs);

}

#endif
