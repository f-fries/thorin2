#include "dialects/clos/pass/rw/clos_conv_prep.h"

#include "thorin/pass/fp/eta_exp.h"

#include "dialects/clos/clos.h"

namespace thorin::clos {

// FIXME: these guys do not work if another pass rewrites curr_nom()'s body
/* static bool isa_cont(const App* body, const Def* def, size_t i) { *//*{{{*/
/*     return body->callee_type()->is_returning() && body->arg() == def && i == def->num_ops() - 1; */
/* } */

/* static const Def* isa_br(const App* body, const Def* def) { */
/*     if (!body->callee_type()->is_cn()) return nullptr; */
/*     auto proj = body->callee()->isa<Extract>(); */
/*     return (proj && proj->tuple() == def && proj->tuple()->isa<Tuple>()) ? proj->tuple() : nullptr; */
/* } */

/* static bool isa_callee_br(const App* body, const Def* def, size_t i) { */
/*     if (!body->callee_type()->is_cn()) return false; */
/*     return isa_callee(def, i) || isa_br(body, def); */
/* } */

/* static Lam* isa_retvar(const Def* def) { */
/*     if (auto [var, lam] = ca_isa_var<Lam>(def); var && lam && var == lam->ret_var()) return lam; */
/*     return nullptr; */
/* } */

/* Lam* ClosConvPrep::scope(Lam* lam) { */
/*     if (eta_exp_) lam = eta_exp_->new2old(lam); */
/*     return lam2fscope_[lam]; */
/* } */


/* const Def* ClosConvPrep::rewrite(const Def* def) { */
/*     auto& w = world(); */
/*     if (!cur_body_ || match<clos>(def) || def->isa<Var>()) return def; */
/*     for (auto i = 0u; i < def->num_ops(); i++) { */
/*         auto op     = def->op(i); */
/*         auto refine = [&](const Def* new_op) { */
/*             auto new_def = def->refine(i, new_op); */
/*             if (def == cur_body_->callee()) */
/*                 cur_body_ = cur_body_->refine(0, new_def)->as<App>(); */
/*             else if (def == cur_body_->arg()) */
/*                 cur_body_ = cur_body_->refine(1, new_def)->as<App>(); */
/*             else if (isa_br(cur_body_, def)) */
/*                 cur_body_ = cur_body_->refine(0, cur_body_->callee()->refine(0, new_def))->as<App>(); */
/*             return new_def; */
/*         }; */
/*         if (auto lam = isa_retvar(op); lam && from_outer_scope(lam)) { */
/*             w.DLOG("found return var from enclosing scope: {}", op); */
/*             return refine(eta_wrap(op, clos::freeBB, "free_ret")); */
/*         } */
/*         if (auto bb_lam = op->isa_nom<Lam>(); bb_lam && bb_lam->is_basicblock() && from_outer_scope(bb_lam)) { */
/*             w.DLOG("found BB from enclosing scope {}", op); */
/*             return refine(thorin::clos::op(clos::freeBB, op)); */
/*         } */
/*         if (isa_cont(cur_body_, def, i)) { */
/*             if (match<clos>(clos::ret, op) || isa_retvar(op)) { */
/*                 return def; */
/*             } else if (auto contlam = op->isa_nom<Lam>()) { */
/*                 return refine(thorin::clos::op(clos::ret, contlam)); */
/*             } else { */
/*                 auto wrapper = eta_wrap(op, clos::ret, "eta_cont"); */
/*                 w.DLOG("eta expanded return cont: {} -> {}", op, wrapper); */
/*                 return refine(wrapper); */
/*             } */
/*         } */
/*         if (auto bb_lam = op->isa_nom<Lam>(); bb_lam && bb_lam->is_basicblock() && !isa_callee_br(cur_body_, def, i)) { */
/*             w.DLOG("found firstclass use of BB: {}", bb_lam); */
/*             return refine(thorin::clos::op(clos::fstclassBB, bb_lam)); */
/*         } */
/*         // TODO: If EtaRed eta-reduces branches, we have to wrap them again! */
/*         if (isa_retvar(op) && !isa_callee_br(cur_body_, def, i)) { */
/*             w.DLOG("found firstclass use of return var: {}", op); */
/*             return refine(eta_wrap(op, clos::fstclassBB, "fstclass_ret")); */
/*         } */
/*     } */

/*     // Eta-Expand branches */
/*     if (auto app = def->isa<App>(); app && app->callee_type()->is_cn()) { */
/*         auto br = app->callee()->isa<Extract>(); */
/*         if (!br) return def; */
/*         auto branches = br->tuple(); */
/*         if (!branches->isa<Tuple>() || !branches->type()->isa<Sigma>()) return def; */
/*         for (auto i = 0u; i < branches->num_ops(); i++) { */
/*             if (!branches->op(i)->isa_nom<Lam>()) { */
/*                 auto wrapper = eta_wrap(branches->op(i), clos::bot, "eta_br"); */
/*                 w.DLOG("eta wrap branch: {} -> {}", branches->op(i), wrapper); */
/*                 branches = branches->refine(i, wrapper); */
/*             } */
/*         } */
/*         cur_body_ = app->refine(0, app->callee()->refine(0, branches))->as<App>(); */
/*         return cur_body_; */
/*     } */

/*     return def; */
/* } *//*}}}*/

static Lam* is_bb(const Def* def) {
    auto bb = def->isa_nom<Lam>();
    return (bb && bb->is_basicblock()) ? bb : nullptr;
}

static Lam* is_retvar_of(const Def* def) {
    if (auto [var, lam] = ca_isa_var<Lam>(def); var && lam && var == lam->ret_var())
        return lam;
    return nullptr;
}

void ClosConvPrep::AnnotBr::enter() {
    if (auto app = curr_nom()->body()->isa<App>(); app && app->callee_type()->is_cn()) {
        auto proj = app->callee()->isa<Extract>();
        if (!proj) return;
        auto new_brs = DefVec(proj->tuple()->num_projs());
        for (size_t i = 0; i < proj->tuple()->num_projs(); i++) {
            auto br = proj->tuple()->proj(i);
            if (match(clos::br, br) || !br->type()->isa<Pi>()) return;
            new_brs[i] = op(clos::br, br);
        }
        auto new_callee = proj->refine(0, world().tuple(new_brs));
        curr_nom()->set_body(app->refine(0, new_callee));
    }
}

void ClosConvPrep::AnnotRet::enter() {
    if (auto app = curr_nom()->body()->isa<App>(); app && app->callee_type()->is_returning()) {
        auto n        = app->num_args() - 1;
        if (match(clos::ret, app->arg(n))) return;
        auto new_args = world().insert(app->arg(), n, op(clos::ret, app->arg(n)));
        curr_nom()->set_body(app->refine(1, new_args));
    }
}

const Def* ClosConvPrep::AnnotRet::rewrite(const Def* def) {
    if (auto retcn = match(clos::ret, def)) {
        if (retcn->arg()->isa_nom<Lam>()) return def;
        if (is_retvar_of(def)) return def;
        return ClosConvPrep::eta_wrap(this->old2wrapper_, retcn->arg(), "eta_ret");
    }
    return def;
}

const Def* ClosConvPrep::AnnotExt::rewrite(const Def* old_def) {
    return old_def;
}

bool ClosConvPrep::AnnotExt::is_free_bb(const Def* def) { 
    Lam* ifs = is_retvar_of(def);
    if (!ifs) {
        if (auto bb = is_bb(def)) ifs = bb2ifs(bb);
    }

    // There are 3 possible cases if !ifs:
    // (a) def is neither a BB-lam or retvar; 
    // (b) def's ifs has not been visited => def is a toplevel (free & closed) BB-like lam;
    // (c) def is an η-wrapper introduced by this pass or AnnotRet
    return ifs && ifs == bb2ifs(curr_nom());
}

void ClosConvPrep::AnnotExt::enter() {
    if (curr_nom()->type()->is_returning() && !visited_fncs_.contains(curr_nom())) {
        visited_fncs_.emplace(curr_nom());
        bb2ifs_[curr_nom()] = curr_nom();
        world().DLOG("scope {} -> {}", curr_nom(), curr_nom());
        auto scope = Scope(curr_nom());
        for (auto def : scope.bound()) {
            assert(def);
            if (auto bb_lam = def->isa_nom<Lam>(); bb_lam && bb_lam->is_basicblock()) {
                world().DLOG("scope {} -> {}", bb_lam, curr_nom());
                bb2ifs_[bb_lam] = curr_nom();
            }
        }
    }
    // if (auto body = curr_nom()->body()->isa<App>();
    //     !wrapper_.contains(curr_nom()) && body && body->callee_type()->is_cn())
    //     cur_body_ = body;
    // else
    //     cur_body_ = nullptr;
}

} // namespace thorin::clos
