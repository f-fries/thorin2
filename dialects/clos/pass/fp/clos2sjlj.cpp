#include "dialects/clos/pass/fp/clos2sjlj.h"

#include "dialects/core/core.h"

namespace thorin::clos {

undo_t Clos2SJLJ::analyze(const Def* def) {
    if (auto clos = isa_clos_lit(def); clos && clos.is_basicblock() && !ignore_.contains(clos.fnc_as_lam())) {
        if (ignore_closed_) {
            auto&& scope = Scope(clos.fnc_as_lam());
            if (scope.free_defs().empty()) {
                ignore_.insert(clos.fnc_as_lam());
                return No_Undo;
            }
        }
        world().DLOG("found BB-closure: {}", clos.fnc());
        has_fstclass_.insert(curr_nom());
        return curr_undo();
    }
    return No_Undo;
}

void Clos2SJLJ::enter() {
    if (!has_fstclass_.contains(curr_nom())) return;
    auto& w = world();
    w.DLOG("start rewrite of {}", curr_nom());
    clos2tag_.clear();
    auto [jm, jb] = op_alloc_jumpbuf(mem::mem_var(curr_nom()), curr_nom()->gid(), get_dbg("jmp_buf"))->projs<2>();
    jump_buf_     = jb;
    auto [am, ab] = mem::op_slot(arg_buf_type(), jm, get_dbg("arg_buf"))->projs<2>();
    arg_buf_ptr_  = ab;
    arg_buf_mem_  = am;
}

const Def* Clos2SJLJ::rewrite(const Def* def) {
    if (!has_fstclass_.contains(curr_nom())) return def;
    auto& w = world();
    for (size_t i = 0; i < def->num_ops(); i++) {
        if (auto q = match<alloc_jmpbuf>(def); q && isa_lit(q->decurry()->arg()) == curr_nom()->gid()) break;
        if (def->op(i) == mem::mem_var(curr_nom())) return def->refine(i, arg_buf_mem_);
    }
    if (auto clos = isa_clos_lit(def); clos && clos.is_basicblock() && !ignore_.contains(clos.fnc_as_lam())) {
        w.DLOG("rewrite bb closure {}", clos.fnc_as_lam());
        auto [_, tag] = clos2tag_.emplace(clos, clos2tag_.size() + 1);
        auto env      = w.tuple({jump_buf_, arg_buf_ptr_, w.lit_int_width(tag_size, tag)});
        return clos_pack_dbg(env, get_throw(clos.fnc_type()->dom()), get_dbg("throw", clos.fnc()), clos.type());
    }
    if (auto app = def->isa<App>(); app && app->callee_type()->is_cn() && !clos2tag_.empty()) {
        DefVec branches(clos2tag_.size() + 1);
        branches[0] = wrap_app(app);
        for (auto [clos, tag] : clos2tag_) branches[tag] = get_lpad(isa_clos_lit(clos));
        auto m = app->arg(0);
        assert(m->type() == mem::type_mem(w));
        auto [m1, tag] = op_setjmp(m, jump_buf_, get_dbg("sj"))->projs<2>();
        tag            = op(core::conv::s2s, w.type_int(branches.size()), tag);
        auto branch    = w.extract(w.tuple(branches), tag);
        clos2tag_.clear();
        return w.app(branch, m1);
    }
    return def;
}

static const Def* get_args(const Def* args) {
    auto& w = args->world();
    args    = clos_remove_env(args);
    assert(args->proj(0)->type() == mem::type_mem(w));
    return w.tuple(DefArray(args->num_projs() - 1, [&](auto i) { return args->proj(i + 1); }));
}

Lam* Clos2SJLJ::get_throw(const Def* dom) {
    auto& w            = world();
    auto [p, inserted] = dom2throw_.emplace(dom, nullptr);
    auto& tlam         = p->second;
    if (inserted || !tlam) {
        auto pi   = w.cn(clos_sub_env(dom, w.sigma({jump_buf_type(), mem::type_ptr(arg_buf_type()), tag_type()})));
        tlam      = w.nom_lam(pi, w.dbg("throw"));
        auto args = get_args(tlam->var());
        auto [jbuf, arg_buf_ptr, tag] = env_var(tlam)->projs<3>({w.dbg("jmp_buf"), w.dbg("arg_buf_ptr"), w.dbg("tag")});
        auto [m, arg_buf]             = mem::op_alloc(args->type(), mem::mem_var(tlam), w.dbg("arg_buf"))->projs<2>();
        auto m1                       = mem::op_store(m, arg_buf, get_args(tlam->var()), w.dbg("store_arg_m"));
        arg_buf_ptr                   = core::op_bitcast(mem::type_ptr(arg_buf->type()), arg_buf_ptr);
        auto m2                       = mem::op_store(m1, arg_buf_ptr, arg_buf, w.dbg("store_ab_m"));
        tlam->set(false, op_longjmp(m2, jbuf, tag));
        ignore_.insert(tlam);
    }
    return tlam;
}

Lam* Clos2SJLJ::wrap_app(const App* app) {
    auto [p, inserted] = app2wrapper_.emplace(app, nullptr);
    auto wrapper       = p->second;
    if (inserted) {
        auto& w       = world();
        wrapper       = w.nom_lam(w.cn(mem::type_mem(w)), get_dbg("sjlj_wrap"));
        auto new_args = app->args();
        assert(new_args[0]->type() == mem::type_mem(w));
        new_args[0] = mem::mem_var(wrapper, w.dbg("wrapper_mem"));
        wrapper->app(false, app->callee(), new_args);
    }
    return wrapper;
}

Lam* Clos2SJLJ::get_lpad(ClosLit clos) {
    auto& w            = world();
    auto [p, inserted] = clos2lpad_.emplace(w.tuple({clos, arg_buf_ptr_}), nullptr);
    auto& lpad         = p->second;
    if (inserted || !lpad) {
        auto name         = clos.fnc()->name();
        lpad              = w.nom_lam(w.cn(mem::type_mem(w)), get_dbg("lpad", clos.fnc()));
        auto [m, arg_buf] = mem::op_load(mem::mem_var(lpad), arg_buf_ptr_, w.dbg("lpad_arg_buf"))->projs<2>();
        auto arg_type     = get_args(clos.fnc_as_lam()->var())->type();
        arg_buf           = core::op_bitcast(mem::type_ptr(arg_type), arg_buf);
        auto [m1, args]   = mem::op_load(m, arg_buf, w.dbg("lpad_arg"))->projs<2>();
        assert(Clos_Env_Param == 1);
        args = w.tuple(DefArray(clos.fnc_type()->num_doms(), [&](auto i) {
            return i == 0 ? m1 : i == 1 ? clos.env() : args->proj(i - 2);
        }));
        lpad->set(clos.fnc_as_lam()->reduce(args));
    }
    return lpad;
}

} // namespace thorin::clos
