#include "dialects/clos/pass/rw/clos_conv_prep.h"

#include "dialects/clos/clos.h"

namespace thorin::clos {

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
            new_brs[i] = annot_nonloc_->annot(clos::br, br);
        }
        auto new_callee = proj->refine(0, world().tuple(new_brs));
        curr_nom()->set_body(app->refine(0, new_callee));
    }
}

const Def* ClosConvPrep::AnnotBr::rewrite(const Def* old_def) {
    // We really only need to expand retvars in branches but this simplifies the other rules.
    if (auto br = match(clos::br, old_def); br && !br->arg()->isa_nom<Lam>()) {
        return annot_nonloc_->eta_wrap(clos::br, br->arg(), "eta_br");
    }
    return old_def;
}

void ClosConvPrep::AnnotRet::enter() {
    if (auto app = curr_nom()->body()->isa<App>(); app && app->callee_type()->is_returning()) {
        auto n        = app->num_args() - 1;
        if (match(clos::ret, app->arg(n))) return;
        auto new_args = DefArray(app->num_args(), [&](auto i) {
            return annot_nonloc_->annot(i == n ? clos::ret : clos::bot, app->arg(i));
        });
        curr_nom()->set_body(app->refine(1, world().tuple(new_args)));
    }
}

const Def* ClosConvPrep::AnnotRet::rewrite(const Def* old_def) {
    if (auto retcn = match(clos::ret, old_def)) {
        if (retcn->arg()->isa_nom<Lam>() || is_retvar_of(retcn->arg())) return old_def;
        return annot_nonloc_->eta_wrap(clos::ret, retcn->arg(), "eta_ret");
    }
    return old_def;
}

void ClosConvPrep::AnnotNonLoc::enter() {
    if (curr_nom()->type()->is_returning() && !visited_fncs_.contains(curr_nom())) {
        visited_fncs_.emplace(curr_nom()); // We only do this once since we track all new lam's via set_ifs(). 
        auto scope = Scope(curr_nom());
        for (auto def : scope.bound())
            if (auto bb_lam = def->isa_nom<Lam>(); bb_lam && bb_lam->is_basicblock()) set_ifs(bb_lam);
    }
}

const Def* ClosConvPrep::AnnotNonLoc::rewrite(const Def* old_def) {
    auto& w = world();
    auto cur_ifs = get_ifs(curr_nom());
    assert(cur_ifs);
    for (size_t i = 0; i < old_def->num_ops(); i++) {
        auto cur_op = old_def->op(i);
        if (auto bb_lam = is_bb(cur_op); bb_lam && !nonloc_wrapper_.contains(bb_lam) && cur_ifs != get_ifs(bb_lam)) {
            if (!get_ifs(bb_lam)) err("clos_conv_prep: in {}: found live toplevel basic block {}", curr_nom(), bb_lam);
            w.DLOG("found free bb: {}", cur_op);
            return old_def->refine(i, eta_wrap(clos::freeBB, cur_op, "eta_free", bb_lam));
        }
        if (auto lam = is_retvar_of(cur_op); lam && lam != cur_ifs) {
            w.DLOG("found free return: {}", cur_op);
            return old_def->refine(i, eta_wrap(clos::freeBB, cur_op, "eta_free", lam));
        }
        if (isa_callee(old_def, i) || match<clos>(old_def)) continue;
        if (auto bb_lam = is_bb(cur_op); bb_lam && bb_lam->is_basicblock()) {
            w.DLOG("found firstclass bb: {}", cur_op);
            return old_def->refine(i, eta_wrap(clos::fstclassBB, cur_op, "eta_ext"));
        }
        if (is_retvar_of(cur_op)) {
            w.DLOG("found firstclass return: {}", cur_op);
            return old_def->refine(i, eta_wrap(clos::fstclassBB, cur_op, "eta_ext"));
        }
    }
    return old_def;
}

} // namespace thorin::clos
