#pragma once

#include "thorin/pass/pass.h"

#include "dialects/clos/clos.h"
#include "dialects/mem/mem.h"

namespace thorin::clos {

class Clos2SJLJ : public FPPass<Clos2SJLJ, Lam> {
    public:
        Clos2SJLJ(PassMan& man, bool ignore_closed_ = false)
            : FPPass(man, "clos2sjlj")
            , has_fstclass_()
            , ignore_closed_(ignore_closed_)
            , ignore_()
            , dom2throw_()
            , clos2lpad_()
            , clos2tag_() {}

    private:
        using Def2Lam = DefMap<Lam*>;

        static const nat_t tag_size = 32;

        const Def* rewrite(const Def*) override;
        void enter() override;
        undo_t analyze(const Def*) override;

        const Def* jump_buf_type() { return mem::type_ptr(world().type_int_width(8)); }
        const Def* arg_buf_type() { return mem::type_ptr(world().sigma()); }
        const Def* tag_type() { return world().type_int_width(tag_size); }

        const Def* get_dbg(const std::string& prefix, const Def* def = nullptr) {
            def = def ? def : curr_nom();
            auto name = def->name().empty() ? prefix : prefix + "_" + def->name();
            return def->world().dbg(name);
        }

        Lam* get_throw(const Def* res_type);
        Lam* get_lpad(ClosLit clos);
        Lam* wrap_app(const App* app);

        LamSet has_fstclass_;
        const bool ignore_closed_;
        LamSet ignore_;

        Def2Lam dom2throw_;
        Def2Lam clos2lpad_;
        Def2Lam app2wrapper_;

        DefMap<int> clos2tag_;
        const Def* jump_buf_ = nullptr;
        const Def* arg_buf_ptr_ = nullptr;
        const Def* arg_buf_mem_ = nullptr;
};

} // namespace thorin::clos
