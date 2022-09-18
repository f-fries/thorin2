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
        auto annot_ext = man.add<AnnotExt>();
        man.add<AnnotBr>(annot_ext);
        man.add<AnnotRet>(annot_ext);
    }

    ClosConvPrep(World& w)
        : Pipeline(w) {
        auto man = std::make_unique<PassMan>(w);
        addPasses(*man); 
        add<PassManPhase>(std::move(man));
    }

protected:
    class AnnotExt : public RWPass<AnnotExt, Lam> {
    public:
        AnnotExt(PassMan& man)
            : RWPass(man, "annot_ext") 
            , old2wrapper_()
            , visited_fncs_()
            , ext_wrapper_() {};

    public:
        /// Get the innermost enclosing function scope (IFS) for lam.
        /// Note: This may be `nullptr` if @p lam is not contained in any function scope, i.e. lam is as BB and closed.
        Lam* get_ifs(Lam* lam) {
            if (lam->is_returning()) return lam;
            return bb2ifs_[lam];
        }

        /// Set bblam%s IFS to the IFS of `curr_nom()`.
        /// We need to track the IFS of all continuations that are created. 
        void set_ifs(Lam* bblam) {
            auto ifs = get_ifs(curr_nom());
            bb2ifs_[bblam] = ifs;
            world().DLOG("scope {} -> {}", bblam, ifs);
        }

        /// η-expand def and tag the wrapper with c. 
        auto eta_wrap(clos c, const Def* def, const std::string& dbg = {}) {
            auto& w = def->world();
            auto [entry, inserted] = old2wrapper_.emplace(op(c, def), nullptr);
            auto& wrapper = entry->second;
            if (inserted) {
                wrapper = w.nom_lam(def->type()->as<Pi>(), w.dbg(dbg));
                wrapper->as_nom<Lam>()->app(false, def, wrapper->as_nom<Lam>()->var());
                w.DLOG("η-expand: {} -> {}", def, op(c, wrapper));
                set_ifs(wrapper->as_nom<Lam>());
                if (c == clos::ext) ext_wrapper_.emplace(wrapper->as_nom<Lam>());
            }
            return op(c, wrapper);
        }

        bool is_free_bb(const Def* def);

        /// @name PassMan hooks 
        /// @{
        void enter() override;
        const Def* rewrite(const Def* old_def) override;
        /// @}

    private:
        Def2Def old2wrapper_; 
        Lam2Lam bb2ifs_;      
        LamSet visited_fncs_;
        LamSet ext_wrapper_;  ///< η-wrapper for nonlocal BB. These are ignored when computing free BBs.
    };

    class AnnotBr : public RWPass<AnnotBr, Lam> {
    public:
        AnnotBr(PassMan& man, AnnotExt* annot_ext)
            : RWPass(man, "annot_br") 
            , annot_ext_(annot_ext) {}

        void enter() override;
        const Def* rewrite(const Def* old_def) override;

    private:
        AnnotExt* annot_ext_;
    };

    class AnnotRet : public RWPass<AnnotRet, Lam> {
    public:
        AnnotRet(PassMan& man, AnnotExt* annot_ext)
            : RWPass(man, "annot_ret") 
            , annot_ext_(annot_ext) {};

        void enter() override;
        const Def* rewrite(const Def* old) override;

    private:
        AnnotExt* annot_ext_;
    };
};

} // namespace clos

} // namespace thorin
