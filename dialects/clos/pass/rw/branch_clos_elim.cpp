#include "dialects/clos/pass/rw/branch_clos_elim.h"

#include "dialects/clos/clos.h"

namespace thorin::clos {

void BranchClosElim::enter() {
    auto app = curr_nom()->body()->isa<App>();
    if (!app || !app->callee_type()->is_basicblock() || !app->callee()->isa<Extract>()) return;

    auto clos = app->callee()->as<Extract>()->tuple();
    auto branch = clos->isa<Extract>();
    if (!branch || !isa_clos_type(clos->type()) || !branch->tuple()->isa<Tuple>()) return;

    DefVec new_branches;
    auto& w = world();
    for (auto op: branch->tuple()->ops()) {
        auto [entry, inserted] = clos2wrapper_.emplace(op, nullptr);
        auto& wrapper      = entry->second;
        if (inserted || !wrapper) {
            wrapper = w.nom_lam(clos_type_to_pi(clos->type()), w.dbg("eta_br"));
            wrapper->set_body(clos_apply(op, wrapper->var()));
            wrapper->set_filter(false);
        }
        new_branches.push_back(wrapper);
    }

    auto new_branch = w.extract(w.tuple(new_branches), branch->index());
    curr_nom()->set_body(w.app(new_branch, clos_remove_env(app->arg())));
}

}; // namespace thorin::clos
