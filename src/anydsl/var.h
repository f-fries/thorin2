#ifndef ANYDSL_VAR_H
#define ANYDSL_VAR_H

#include "anydsl/def.h"
#include "anydsl/symbol.h"

namespace anydsl {

class Def;
class Type;

class Var {
public:

    Var(const Def* def)
        : def_(def)
    {}
    virtual ~Var() {}

    const Def* load() const { return def_; }
    virtual void store(const Def* def) { assert(false); }
    const Type* type() const { return def_->type(); }

protected:

    const Def* def_;
};

class RVar : public Var {
public:

    RVar(const Def* def)
        : Var(def)
    {}


private:

    anydsl::Symbol sym_;
};

class LVar : public Var {
public:

    LVar(const Def* def, const anydsl::Symbol symbol)
        : Var(def)
        , symbol_(symbol)
    {}

    anydsl::Symbol symbol() const { return symbol_; }

    virtual void store(const Def* def) { def_ = def; }

private:

    anydsl::Symbol symbol_;
};

} // namespace anydsl

#endif // ANYDSL_VAR_H
