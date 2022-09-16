#pragma once

#include "thorin/pass/pass.h"
#include "thorin/phase/phase.h"

#include "thorin/pass/fp/eta_exp.h"

#include "dialects/clos/clos.h"

namespace thorin {

namespace clos {

// class ClosConvPrep : public RWPass<ClosConvPrep, Lam> {{{{
// public:
//     ClosConvPrep(PassMan& man, EtaExp* eta_exp)
//         : RWPass(man, "eta_cont")
//         , eta_exp_(eta_exp)
//         , old2wrapper_()
//         , lam2fscope_()
//         , cur_body_(nullptr) {}

//     void enter() override;
//     const Def* rewrite(const Def*) override;

//     Lam* scope(Lam* lam);

//     bool from_outer_scope(Lam* lam) { return scope(lam) && scope(lam) != scope(curr_nom()); }

//     const Def* eta_wrap(const Def* def, clos c, const std::string& dbg) {
//         auto& w                = world();
//         auto [entry, inserted] = old2wrapper_.emplace(def, nullptr);
//         auto& wrapper          = entry->second;
//         if (inserted) {
//             wrapper = w.nom_lam(def->type()->as<Pi>(), w.dbg(dbg));
//             wrapper->app(false, def, wrapper->var());
//             lam2fscope_[wrapper] = scope(curr_nom());
//             wrapper_.emplace(wrapper);
//         }
//         return op(c, wrapper);
//     }

// private:
//     EtaExp* eta_exp_;
//     DefMap<Lam*> old2wrapper_;
//     DefSet wrapper_;
//     Lam2Lam lam2fscope_;
//     const App* cur_body_;
// };}}}

class ClosConvPrep : public Pipeline {
public:

    static void addPasses(PassMan& man) {
        man.add<AnnotBr>();
        man.add<AnnotRet>();
        // auto eta_exp = man->add<EtaExp>(nullptr);
        // man->add<AnnotExt>(eta_exp);
        // add<PassManPhase>(std::move(man));
    }

    ClosConvPrep(World& w)
        : Pipeline(w) {
        auto man = std::make_unique<PassMan>(w);
        addPasses(*man); 
        add<PassManPhase>(std::move(man));
    }

    template<clos c = clos::bot>
    static auto eta_wrap(Def2Def& old2wrap, const Def* def, const std::string& dbg) {
        auto& w = def->world();
        auto [entry, inserted] = old2wrap.emplace(def, nullptr);
        auto& wrapper = entry->second;
        if (inserted) {
            wrapper = w.nom_lam(def->type()->as<Pi>(), w.dbg(dbg));
            wrapper->as_nom<Lam>()->app(false, def, wrapper->as_nom<Lam>()->var());
        }
        return op(c, wrapper);
    }

protected:

    class AnnotBr : public RWPass<AnnotBr, Lam> {
    public:
        AnnotBr(PassMan& man)
            : RWPass(man, "annot_br") {}

        void enter() override;
    };

    class AnnotRet : public RWPass<AnnotRet, Lam> {
    public:
        AnnotRet(PassMan& man)
            : RWPass(man, "annot_ret") 
            , old2wrapper_() {};

        void enter() override;

        const Def* rewrite(const Def* old) override;

    private:
        Def2Def old2wrapper_;
    };

    class AnnotExt : public RWPass<AnnotExt, Lam> {
    public:
        AnnotExt(PassMan& man, EtaExp* eta_exp)
            : RWPass(man, "annot_ext") 
            , eta_exp_(eta_exp) 
            , old2wrapper_()
            , visited_fncs_() {};

        void enter() override;

        const Def* rewrite(const Def* old_def) override;
        
        const Def* eta_wrap(const Def* def, const std::string& dbg = "eta_ext") {
            return ClosConvPrep::eta_wrap<clos::ext>(this->old2wrapper_, def, dbg);
        }

        Lam* bb2ifs(Lam* bb) {
            bb = eta_exp_->new2old(bb);
            return bb2ifs_[bb];
        }

        bool is_free_bb(const Def* def);

    private:
        EtaExp* eta_exp_;
        Def2Def old2wrapper_;
        Lam2Lam bb2ifs_;
        LamSet visited_fncs_;
    };
};

} // namespace clos

} // namespace thorin
