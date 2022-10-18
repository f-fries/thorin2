#include "dialects/clos/phase/clos_conv.h"

#include "thorin/check.h"

#include "thorin/analyses/scope.h"

#include "dialects/mem/autogen.h"
#include "dialects/mem/mem.h"

namespace thorin::clos {

/* auxillary functions */

// Adjust the index of an argument to account for the env param
static size_t shift_env(size_t i) { return (i < Clos_Env_Param) ? i : i - 1_u64; }

// Same but skip the env param
static size_t skip_env(size_t i) { return (i < Clos_Env_Param) ? i : i + 1_u64; }

const Def* clos_insert_env(size_t i, const Def* env, std::function<const Def*(size_t)> f) {
    return (i == Clos_Env_Param) ? env : f(shift_env(i));
}

const Def* clos_remove_env(size_t i, std::function<const Def*(size_t)> f) { return f(skip_env(i)); }

static const Def* ctype(World& w, Defs doms, const Def* env_type = nullptr) {
    if (!env_type) {
        auto sigma = w.nom_sigma(w.type(), 3_u64, w.dbg("closure_type"));
        sigma->set(0_u64, w.type());
        sigma->set(1_u64, ctype(w, doms, sigma->var(0_u64)));
        sigma->set(2_u64, sigma->var(0_u64));
        return sigma;
    }
    return w.cn(DefArray(doms.size() + 1,
                         [&](auto i) { return clos_insert_env(i, env_type, [&](auto j) { return doms[j]; }); }));
}

Sigma* clos_type(const Pi* pi) { return ctype(pi->world(), pi->doms(), nullptr)->as_nom<Sigma>(); }

const Pi* clos_type_to_pi(const Def* ct, const Def* new_env_type) {
    assert(isa_clos_type(ct));
    auto& w = ct->world();
    auto pi = ct->op(1_u64)->isa<Pi>();
    assert(pi);
    auto new_dom = new_env_type ? clos_sub_env(pi->dom(), new_env_type) : clos_remove_env(pi->dom());
    return w.cn(new_dom);
}

const Sigma* isa_clos_type(const Def* def) {
    auto& w  = def->world();
    auto sig = def->isa_nom<Sigma>();
    if (!sig || sig->num_ops() < 3 || sig->op(0_u64) != w.type()) return nullptr;
    auto var = sig->var(0_u64);
    if (sig->op(2_u64) != var) return nullptr;
    auto pi = sig->op(1_u64)->isa<Pi>();
    return (pi && pi->is_cn() && pi->num_ops() > 1_u64 && pi->dom(Clos_Env_Param) == var) ? sig : nullptr;
}

const Def* clos_pack_dbg(const Def* env, const Def* lam, const Def* dbg, const Def* ct) {
    assert(env && lam);
    assert(!ct || isa_clos_type(ct));
    auto& w = env->world();
    auto pi = lam->type()->isa<Pi>();
    assert(pi && env->type() == pi->dom(Clos_Env_Param));
    ct = (ct) ? ct : clos_type(w.cn(clos_remove_env(pi->dom())));
    return w.tuple(ct, {env->type(), lam, env}, dbg)->isa<Tuple>();
}

std::tuple<const Def*, const Def*, const Def*> clos_unpack(const Def* c) {
    assert(c && isa_clos_type(c->type()));
    // auto& w       = c->world();
    // auto env_type = c->proj(0_u64);
    // // auto pi       = clos_type_to_pi(c->type(), env_type);
    // auto fn       = w.extract(c, w.lit_int(3, 1));
    // auto env      = w.extract(c, w.lit_int(3, 2));
    // return {env_type, fn, env};
    auto [ty, pi, env] = c->projs<3>();
    return {ty, pi, env};
}

const Def* clos_apply(const Def* closure, const Def* args) {
    auto& w           = closure->world();
    auto [_, fn, env] = clos_unpack(closure);
    auto pi           = fn->type()->as<Pi>();
    return w.app(fn, DefArray(pi->num_doms(), [&](auto i) { return clos_insert_env(i, env, args); }));
}

ClosLit isa_clos_lit(const Def* def, bool lambda_or_branch) {
    auto tpl = def->isa<Tuple>();
    if (tpl && isa_clos_type(def->type())) {
        auto cc  = clos::bot;
        auto fnc = std::get<1_u64>(clos_unpack(tpl));
        if (auto q = match<clos>(fnc)) {
            fnc = q->arg();
            cc  = q.id();
        }
        if (!lambda_or_branch || fnc->isa<Lam>()) return ClosLit(tpl, cc);
    }
    return ClosLit(nullptr, clos::bot);
}

const Def* ClosLit::env() {
    assert(def_);
    return std::get<2_u64>(clos_unpack(def_));
}

const Def* ClosLit::env_var() { return thorin::clos::env_var(fnc_as_lam()); }

const Def* ClosLit::fnc() {
    assert(def_);
    return std::get<1_u64>(clos_unpack(def_));
}

Lam* ClosLit::fnc_as_lam() {
    auto f = fnc();
    if (auto q = match<clos>(f)) f = q->arg();
    return f->isa_nom<Lam>();
}

/* Closure Conversion */

void ClosConv::start() {
    auto externals = std::vector(world().externals().begin(), world().externals().end());
    auto subst     = Def2Def();
    for (auto [_, ext_def] : externals) rewrite(ext_def, subst);
    while (!worklist_.empty()) {
        auto def = worklist_.front();
        subst    = Def2Def();
        worklist_.pop();
        if (auto i = closures_.find(def); i != closures_.end()) {
            rewrite_body(i->second.fn, subst);
        } else {
            // world().DLOG("RUN: rewrite def {}", def);
            rewrite(def, subst);
        }
    }
}

void ClosConv::rewrite_body(Lam* new_lam, Def2Def& subst) {
    auto& w = world();
    auto it = closures_.find(new_lam);
    assert(it != closures_.end() && "closure should have a stub if rewrite_body is called!");
    auto [old_fn, num_fvs, env, new_fn] = it->second;

    if (!old_fn->is_set()) return;

    w.DLOG("rw body: {} [old={}, env={}]\nt", new_fn, old_fn, env);
    auto env_param = new_fn->var(Clos_Env_Param, w.dbg("closure_env"));
    if (num_fvs == 1) {
        subst.emplace(env, env_param);
    } else {
        for (size_t i = 0; i < num_fvs; i++) {
            auto fv  = env->op(i);
            auto dbg = (fv->name().empty()) ? w.dbg("fv_" + std::to_string(i)) : w.dbg("fv_" + env->op(i)->name());
            subst.emplace(fv, env_param->proj(i, dbg));
        }
    }

    auto params =
        w.tuple(DefArray(old_fn->num_doms(), [&](auto i) { return new_lam->var(skip_env(i), old_fn->var(i)->dbg()); }));
    subst.emplace(old_fn->var(), params);

    auto filter = rewrite(new_fn->filter(), subst);
    auto body   = rewrite(new_fn->body(), subst);
    new_fn->set(filter, body);
}

const Def* ClosConv::rewrite(const Def* def, Def2Def& subst) {
    switch (def->node()) {
        case Node::Type:
        case Node::Univ:
        case Node::Nat:
        case Node::Bot: // TODO We probably want to rewrite their types too!
        case Node::Top: return def;
        default: break;
    }

    auto& w  = world();
    auto map = [&](const Def* new_def) {
        subst[def] = new_def;
        assert(subst[def] == new_def);
        return new_def;
    };

    if (auto i = subst.find(def); i != subst.end()) {
        return i->second;
    } else if (auto pi = def->isa<Pi>(); pi && pi->is_cn()) {
        return map(closure_type(pi, subst));
    } else if (auto lam = def->isa_nom<Lam>(); lam && lam->type()->is_cn()) {
        auto& w                       = world();
        auto [_, __, fv_env, new_lam] = make_stub(lam, subst);
        auto closure_type             = rewrite(lam->type(), subst);
        auto env                      = rewrite(fv_env, subst);
        auto closure                  = clos_pack(env, new_lam, closure_type);
        // w.DLOG("RW: pack {} ~> {} : {}", lam, closure, closure_type);
        return map(closure);
    } else if (auto q = match<clos>(def)) {
        if (q.id() == clos::ret && !is_retvar_of(q->arg())) {
            auto ret_type = q->arg()->type()->isa<Pi>();
            assert(ret_type && ret_type->is_basicblock());
            auto new_ret = rewrite(q->arg(), subst);
            assert(isa_clos_type(new_ret->type()));
            auto wrapper = w.nom_lam(rewrite_cont_type(ret_type, subst), w.dbg("eta_ret"));
            wrapper->set_body(clos_apply(new_ret, wrapper->var()));
            wrapper->set_filter(false);
            return wrapper;
        }
        return rewrite(q->arg(), subst);
    } else if (auto lam = is_retvar_of(def)) {
        // We can't rebuild def here because we will compute a closure type for
        // retvar instead of a continuation type.
        return map(make_stub(lam, subst).fn->ret_var());
    } else if (def->isa<Axiom>()) {
        // FIXME: I don't know why this was added but it's probably wrong -
        // if the axiom is applied to a continuation, we have to adjust its type as well.
        assert(def->type() == rewrite(def->type(), subst) && "axiom requires rewrite!");
        return def;
    }

    auto new_type = rewrite(def->type(), subst);
    auto new_dbg  = (def->dbg()) ? rewrite(def->dbg(), subst) : nullptr;

    if (auto nom = def->isa_nom()) {
        if (auto global = def->isa_nom<Global>()) {
            if (auto i = glob_noms_.find(global); i != glob_noms_.end()) return i->second;
            auto subst             = Def2Def();
            return glob_noms_[nom] = rewrite_nom(global, new_type, new_dbg, subst);
        }
        assert(!isa_clos_type(nom));
        // w.DLOG("RW: nom {}", nom);
        auto new_nom = rewrite_nom(nom, new_type, new_dbg, subst);
        // Try to reduce the amount of noms that are created
        if (!nom->isa_nom<Global>() && Checker(w).equiv(nom, new_nom, nullptr)) return map(nom);
        if (auto restruct = new_nom->restructure()) return map(restruct);
        return map(new_nom);
    } else {
        auto new_ops = DefArray(def->num_ops(), [&](auto i) { return rewrite(def->op(i), subst); });
        if (auto app = def->isa<App>(); app && new_ops[0]->type()->isa<Sigma>())
            return map(clos_apply(new_ops[0], new_ops[1]));
        else
            return map(def->rebuild(w, new_type, new_ops, new_dbg));
    }
}

Def* ClosConv::rewrite_nom(Def* nom, const Def* new_type, const Def* new_dbg, Def2Def& subst) {
    auto& w      = world();
    auto new_nom = nom->stub(w, new_type, new_dbg);
    subst.emplace(nom, new_nom);
    for (size_t i = 0; i < nom->num_ops(); i++) {
        if (nom->op(i)) new_nom->set(i, rewrite(nom->op(i), subst));
    }
    return new_nom;
}

const Pi* ClosConv::rewrite_cont_type(const Pi* pi, Def2Def& subst) {
    assert(pi->is_basicblock());
    auto new_ops = DefArray(pi->num_doms(), [&](auto i) { return rewrite(pi->dom(i), subst); });
    return world().cn(new_ops);
}

const Def* ClosConv::closure_type(const Pi* pi, Def2Def& subst, const Def* env_type) {
    if (auto i = glob_noms_.find(pi); i != glob_noms_.end() && !env_type) return i->second;
    auto& w       = world();
    auto new_doms = DefArray(pi->num_doms(), [&](auto i) {
        return (i == pi->num_doms() - 1 && pi->is_returning()) ? rewrite_cont_type(pi->ret_pi(), subst)
                                                               : rewrite(pi->dom(i), subst);
    });
    auto ct       = ctype(w, new_doms, env_type);
    if (!env_type) {
        glob_noms_.emplace(pi, ct);
        // w.DLOG("C-TYPE: pct {} ~~> {}", pi, ct);
    } else {
        // w.DLOG("C-TYPE: ct {}, env = {} ~~> {}", pi, env_type, ct);
    }
    return ct;
}

ClosConv::ClosureStub ClosConv::make_stub(const DefSet& fvs, Lam* old_lam, Def2Def& subst) {
    auto& w          = world();
    auto env         = w.tuple(DefArray(fvs.begin(), fvs.end()));
    auto num_fvs     = fvs.size();
    auto env_type    = rewrite(env->type(), subst);
    auto new_fn_type = closure_type(old_lam->type(), subst, env_type)->as<Pi>();
    auto new_lam     = old_lam->stub(w, new_fn_type, w.dbg(old_lam->name()));
    new_lam->set_debug_name((old_lam->is_external() || !old_lam->is_set()) ? "cc_" + old_lam->name() : old_lam->name());
    if (!isa_workable(old_lam)) {
        auto new_ext_type = w.cn(clos_remove_env(new_fn_type->dom()));
        auto new_ext_lam  = old_lam->stub(w, new_ext_type, w.dbg(old_lam->name()));
        w.DLOG("wrap ext lam: {} -> stub: {}, ext: {}", old_lam, new_lam, new_ext_lam);
        if (old_lam->is_set()) {
            old_lam->make_internal();
            new_ext_lam->make_external();
            new_ext_lam->app(false, new_lam, clos_insert_env(env, new_ext_lam->var()));
            new_lam->set(old_lam->filter(), old_lam->body());
        } else {
            new_ext_lam->set(nullptr, nullptr);
            new_lam->app(false, new_ext_lam, clos_remove_env(new_lam->var()));
        }
    } else {
        new_lam->set(old_lam->filter(), old_lam->body());
    }
    w.DLOG("STUB {} ~~> ({}, {})", old_lam, env, new_lam);
    auto closure = ClosureStub{old_lam, num_fvs, env, new_lam};
    closures_.emplace(old_lam, closure);
    closures_.emplace(closure.fn, closure);
    return closure;
}

ClosConv::ClosureStub ClosConv::make_stub(Lam* old_lam, Def2Def& subst) {
    if (auto i = closures_.find(old_lam); i != closures_.end()) return i->second;
    auto closure = make_stub(fva_.run(old_lam), old_lam, subst);
    worklist_.emplace(closure.fn);
    return closure;
}

/* Free variable analysis */

static bool is_toplevel(const Def* fd) {
    return fd->dep_const() || fd->isa_nom<Global>() || fd->isa<Axiom>() || fd->sort() != Sort::Term;
}

static bool is_memop_res(const Def* fd) {
    auto proj = fd->isa<Extract>();
    if (!proj) return false;
    auto types = proj->tuple()->type()->ops();
    return std::any_of(types.begin(), types.end(), [](auto d) { return match<mem::M>(d); });
}

void FreeDefAna::split_fd(Node* node, const Def* fd, bool& init_node, NodeQueue& worklist) {
    assert(!match<mem::M>(fd->type()) && "mem tokens must not be free");
    if (is_toplevel(fd)) return;
    if (auto [var, lam] = is_var_of<Lam>(fd); var && lam) {
        if (var != lam->ret_var()) node->fvs.emplace(fd);
    } else if (auto q = match(clos::esc, fd); q && q->arg() != node->nom) {
        node->add_fvs(fd);
    } else if (auto pred = fd->isa_nom()) {
        if (pred != node->nom) {
            auto [pnode, inserted] = build_node(pred, worklist);
            node->preds.push_back(pnode);
            pnode->succs.push_back(node);
            init_node |= inserted;
        }
    } else if (fd->dep() == Dep::Var && !fd->isa<Tuple>()) {
        // Note: Var's can still have Def::Top, if their type is a nom!
        // So the first case is *not* redundant
        node->add_fvs(fd);
    } else if (is_memop_res(fd)) {
        // Results of memops must not be floated down
        node->add_fvs(fd);
    } else {
        for (auto op : fd->ops()) split_fd(node, op, init_node, worklist);
    }
}

std::pair<FreeDefAna::Node*, bool> FreeDefAna::build_node(Def* nom, NodeQueue& worklist) {
    auto& w            = world();
    auto [p, inserted] = lam2nodes_.emplace(nom, nullptr);
    if (!inserted) return {p->second.get(), false};
    w.DLOG("FVA: create node: {}", nom);
    p->second      = std::make_unique<Node>(Node{nom, {}, {}, {}, 0});
    auto node      = p->second.get();
    auto scope     = Scope(nom);
    bool init_node = false;
    for (auto v : scope.free_defs()) { split_fd(node, v, init_node, worklist); }
    if (!init_node) {
        worklist.push(node);
        w.DLOG("FVA: init {}", nom);
    }
    return {node, true};
}

void FreeDefAna::run(NodeQueue& worklist) {
    // auto& w = world();
    int iter = 0;
    while (!worklist.empty()) {
        auto node = worklist.front();
        worklist.pop();
        world().DLOG("FA: iter {}: {}", iter, node->nom);
        if (is_done(node)) continue;
        auto changed = is_bot(node);
        mark(node);
        for (auto p : node->preds) {
            auto& pfvs = p->fvs;
            for (auto&& pfv : pfvs) { changed |= node->add_fvs(pfv).second; }
            // w.DLOG("\tFV({}) ∪= FV({}) = {{{, }}}\b", node->nom, p->nom, pfvs);
        }
        if (changed) {
            for (auto s : node->succs) { worklist.push(s); }
        }
        iter++;
    }
    // w.DLOG("FVA: done");
}

DefSet& FreeDefAna::run(Lam* lam) {
    auto worklist  = NodeQueue();
    auto [node, _] = build_node(lam, worklist);
    if (!is_done(node)) {
        cur_pass_id++;
        run(worklist);
    }

    return node->fvs;
}

} // namespace thorin::clos
