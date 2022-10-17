#include "dialects/clos/pass/rw/clos_conv_prep.h"

#include "dialects/clos/clos.h"

namespace thorin::clos {

static Lam* is_bb(const Def* def) {
    auto bb = def->isa_nom<Lam>();
    return (bb && bb->is_basicblock()) ? bb : nullptr;
}

void ClosConvPrep::annot_br() {
    if (auto app = curr_nom()->body()->isa<App>(); app && app->callee_type()->is_basicblock()) {
        auto proj = app->callee()->isa<Extract>();
        if (!proj) return;
        auto new_brs = DefVec(proj->tuple()->num_projs());
        for (size_t i = 0; i < proj->tuple()->num_projs(); i++) {
            auto br = proj->tuple()->proj(i);
            if (match(clos::br, br) || !br->type()->isa<Pi>()) return;
            new_brs[i] = annot(clos::br, br);
        }
        auto new_callee = proj->refine(0, world().tuple(new_brs));
        curr_nom()->set_body(app->refine(0, new_callee));
    }
}

void ClosConvPrep::annot_ret() {
    if (auto app = curr_nom()->body()->isa<App>(); app && app->callee_type()->is_returning()) {
        auto n = app->num_args() - 1;
        if (match(clos::ret, app->arg(n))) return;
        auto new_args =
            DefArray(app->num_args(), [&](auto i) { return annot(i == n ? clos::ret : clos::bot, app->arg(i)); });
        curr_nom()->set_body(app->refine(1, world().tuple(new_args)));
    }
}

void ClosConvPrep::enter() {
    annot_br();
    annot_ret();
    if (curr_nom()->type()->is_returning() && !visited_fncs_.contains(curr_nom())) {
        visited_fncs_.emplace(curr_nom()); // We only do this once since we track all new lam's via set_ifs().
        auto scope = Scope(curr_nom());
        for (auto def : scope.bound())
            if (auto bb_lam = def->isa_nom<Lam>(); bb_lam && bb_lam->is_basicblock()) set_ifs(bb_lam);
    }
}

const Def* ClosConvPrep::rewrite(const Def* def) {
    auto& w      = world();
    auto cur_ifs = get_ifs(curr_nom());
    assert(cur_ifs);
    for (size_t i = 0; i < def->num_ops(); i++) {
        auto cur_op = def->op(i);
        if (auto bb_lam = is_bb(cur_op)) {
            if (!nonloc_wrapper_.contains(bb_lam) && cur_ifs != get_ifs(bb_lam)) {
                if (!get_ifs(bb_lam))
                    err("clos_conv_prep: in {}: found live toplevel basic block {}", curr_nom(), bb_lam);
                w.DLOG("found free bb: {}", cur_op);
                return def->refine(i, annot(clos::esc, cur_op, w.dbg("free_bb")));
            }
            if (isa_callee(def, i) || match<clos>(def)) continue;
            w.DLOG("found firstclass bb: {}", bb_lam);
            return def->refine(i, annot(clos::esc, cur_op, w.dbg("ext_bb")));
        }
        if (auto lam = is_retvar_of(cur_op)) {
            if (lam != cur_ifs) {
                w.DLOG("found free return: {}", cur_op);
                return def->refine(i, eta_wrap(clos::esc, cur_op, "eta_free", lam));
            }
            if (isa_callee(def, i) || match(clos::ret, def)) continue;
            if (match(clos::br, def)) { // Needs to be expanded to make types check out, but not hoisted up
                w.DLOG("found return in br: {}", cur_op);
                return def->refine(i, eta_wrap(clos::bot, cur_op, "eta_ret"));
            }
            assert(!match(clos::esc, def));
            w.DLOG("found firstclass return: {}", cur_op);
            return def->refine(i, eta_wrap(clos::esc, cur_op, "eta_ext"));
        }
    }
    return def;
}

} // namespace thorin::clos
