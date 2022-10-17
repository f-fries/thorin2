#pragma once

#include "thorin/pass/pass.h"
#include "thorin/phase/phase.h"

#include "dialects/clos/clos.h"

namespace thorin {

namespace clos {

class ClosConvPrep : public RWPass<ClosConvPrep, Lam> {
public:
    ClosConvPrep(PassMan& man)
        : RWPass(man, "annot_nonloc") 
        , old2wrapper_()
        , visited_fncs_()
        , nonloc_wrapper_() {};

    /// @name PassMan hooks 
    /// @{
    void enter() override;
    const Def* rewrite(const Def* old_def) override;
    /// @}


    /// Get the innermost enclosing function scope (IFS) for lam.
    /// Note: This may be `nullptr` if @p lam is not contained in any function scope, i.e. lam is a BB and closed.
    Lam* get_ifs(Lam* lam) {
        if (lam->is_returning()) return lam;
        return bb2ifs_[lam];
    }

    /// Set bblam%s IFS to the IFS of `curr_nom()`.
    /// We need to track the IFS of all continuations that are created. 
    void set_ifs(Lam* bblam, Lam* parent = {}) {
        auto ifs = get_ifs(parent ? parent : curr_nom());
        bb2ifs_[bblam] = ifs;
        world().DLOG("scope {} -> {}", bblam, ifs);
    }

private:
    template<bool tag = true>
    auto annot(clos c, const Def* def, const Def* dbg = {}) {
        // A Lam or return-var can appear 'free' in one Lam and as 'regular' block
        // in another one. This can cause trouble since PassMan memorizes rewrites.
        // We tag br- and ret-annotations with curr_nom()'s GID to avoid this.
        // This is not an issue in other contexts (App's have different mem-args, other
        // uses are firstclass anyway).
        return op(c, tag ? curr_nom()->gid() : 0, def, dbg);
    }

    /// η-expand def and tag the wrapper with c. 
    auto eta_wrap(clos c, const Def* def, const std::string& dbg = {}, Lam* parent = {}) {
        auto& w = def->world();
        auto [entry, inserted] = old2wrapper_.emplace(annot<false>(c, def), nullptr);
        auto& wrapper = entry->second;
        if (inserted) {
            wrapper = w.nom_lam(def->type()->as<Pi>(), w.dbg(dbg));
            wrapper->as_nom<Lam>()->app(false, def, wrapper->as_nom<Lam>()->var());
            w.DLOG("η-expand: {} -> {}", def, op(c, wrapper));
            set_ifs(wrapper->as_nom<Lam>(), parent);
            if (c == clos::esc) nonloc_wrapper_.emplace(wrapper->as_nom<Lam>());
        }
        return annot(c, wrapper);
    }

    void annot_br();
    void annot_ret();


private:
    Def2Def old2wrapper_; 
    Lam2Lam bb2ifs_;      
    LamSet visited_fncs_;
    LamSet nonloc_wrapper_;  ///< η-wrapper for nonlocal BBs. These are ignored when computing free BBs.
};

} // namespace clos

} // namespace thorin
