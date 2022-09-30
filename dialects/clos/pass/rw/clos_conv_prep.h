#pragma once

#include "thorin/pass/pass.h"
#include "thorin/phase/phase.h"

#include "dialects/clos/clos.h"

namespace thorin {

namespace clos {

class ClosConvPrep : public Pipeline {
public:

    /// Adds the passes to a PassMan.
    /// IMPORTANT: DO NOT COMBINE THIS WITH OTHER PASSES!
    static void addPasses(PassMan& man) {
        auto annot_nonloc = man.add<AnnotNonLoc>();
        man.add<AnnotBr>(annot_nonloc);
        man.add<AnnotRet>(annot_nonloc);
    }

    ClosConvPrep(World& w)
        : Pipeline(w) {
        auto man = std::make_unique<PassMan>(w);
        addPasses(*man); 
        add<PassManPhase>(std::move(man));
    }

    class CleanupAnnots : public RWPass<CleanupAnnots, Lam> {
    public:
        CleanupAnnots(PassMan& man)
            : RWPass(man, "cleaunp_annots") {}

        const Def* rewrite(const Def* old_def) {
            if (auto q = match<clos>(old_def)) return q->arg();
            return old_def;
        }
    };

protected:
    class AnnotNonLoc : public RWPass<AnnotNonLoc, Lam> {
    public:
        AnnotNonLoc(PassMan& man)
            : RWPass(man, "annot_nonloc") 
            , old2wrapper_()
            , visited_fncs_()
            , nonloc_wrapper_() {};

    public:
        /// Get the innermost enclosing function scope (IFS) for lam.
        /// Note: This may be `nullptr` if @p lam is not contained in any function scope, i.e. lam is as BB and closed.
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

        template<bool tag = true>
        auto annot(clos c, const Def* def, const Def* dbg = {}) {
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
                if (c == clos::nonlocal) nonloc_wrapper_.emplace(wrapper->as_nom<Lam>());
            }
            return annot(c, wrapper);
        }

        /// @name PassMan hooks 
        /// @{
        void enter() override;
        const Def* rewrite(const Def* old_def) override;
        /// @}

    private:
        Def2Def old2wrapper_; 
        Lam2Lam bb2ifs_;      
        LamSet visited_fncs_;
        LamSet nonloc_wrapper_;  ///< η-wrapper for nonlocal BB. These are ignored when computing free BBs.
    };

    class AnnotBr : public RWPass<AnnotBr, Lam> {
    public:
        AnnotBr(PassMan& man, AnnotNonLoc* annot_nonloc)
            : RWPass(man, "annot_br") 
            , annot_nonloc_(annot_nonloc) {}

        void enter() override;
        const Def* rewrite(const Def* old_def) override;

    private:
        AnnotNonLoc* annot_nonloc_;
    };

    class AnnotRet : public RWPass<AnnotRet, Lam> {
    public:
        AnnotRet(PassMan& man, AnnotNonLoc* annot_nonloc)
            : RWPass(man, "annot_ret") 
            , annot_nonloc_(annot_nonloc) {};

        void enter() override;
        const Def* rewrite(const Def* old) override;

    private:
        AnnotNonLoc* annot_nonloc_;
    };
};

} // namespace clos

} // namespace thorin
