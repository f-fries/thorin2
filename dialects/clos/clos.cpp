#include "dialects/clos/clos.h"

#include <thorin/config.h>
#include <thorin/pass/pass.h>

#include "thorin/dialects.h"
#include "dialects/mem/mem.h"
#include "dialects/clos/pass/rw/branch_clos_elim.h"
#include "dialects/clos/pass/rw/clos2sjlj.h"
#include "thorin/pass/fp/eta_exp.h"
#include "thorin/pass/rw/scalarize.h"

#include "thorin/pass/fp/eta_red.h"
#include "dialects/clos/pass/rw/clos_conv_prep.h"
#include "dialects/clos/phase/clos_conv.h"
#include "dialects/clos/phase/lower_typed_clos.h"
#include "dialects/mem/passes/fp/copy_prop.h"
#include "dialects/clos/pass/fp/lower_typed_clos_prep.h"

using namespace thorin;

template<class P>
class PhasePassWrapper : public RWPass<PhasePassWrapper<P>, Lam> {
public:
    template<typename ...Args>
    PhasePassWrapper(PassMan& man, Args&&... args)
        : RWPass<PhasePassWrapper<P>, Lam>(man, "phase_wrapper")
        , phase_(man.world(), std::forward<Args>(args)...) {};

    void prepare() override {
        phase_.start();
    }

private:
    P phase_;
};

class ClosConvWrapper : public RWPass<ClosConvWrapper, Lam>{
public:
    bool single = true;
    ClosConvWrapper(PassMan& man)
            : RWPass(man, "clos_conv") {}

    void prepare() override{
        clos::ClosConv(world()).run();
    }
};

class LowerTypedClosWrapper : public RWPass<LowerTypedClosWrapper, Lam>{
public:
    LowerTypedClosWrapper(PassMan& man)
            : RWPass(man, "lower_typed_clos") {}

    void prepare() override{
        clos::LowerTypedClos(world()).run();
    }

};

extern "C" THORIN_EXPORT DialectInfo thorin_get_dialect_info() {
    return {"clos",
            [](PipelineBuilder& builder) {

                //closure_conv
                builder.extend_opt_phase([](PassMan& man) {
                    clos::ClosConvPrep::addPasses(man);
                });
                // builder.extend_opt_phase([](PassMan& man) {
                //     man.add<EtaExp>(nullptr);
                // });
                builder.extend_opt_phase([](PassMan& man) {
                    man.add<ClosConvWrapper>();
                });
                builder.extend_opt_phase([](PassMan& man) {
                    auto er = man.add<EtaRed>(true);
                    auto ee = man.add<EtaExp>(er);
                    man.add<Scalerize>(ee);
                    man.add<clos::ClosConvPrep::CleanupAnnots>();
                });

                //lower_closures

                // builder.extend_opt_phase([](PassMan& man) {
                //     man.add<Scalerize>(nullptr);
                //     man.add<clos::BranchClosElim>();
                //     man.add<mem::CopyProp>(nullptr, nullptr, true);
                //     man.add<clos::LowerTypedClosPrep>();
                //     man.add<clos::Clos2SJLJ>();
                // });

                // builder.extend_opt_phase([](PassMan& man) {
                //     man.add<LowerTypedClosWrapper>();
                // });
            },
            nullptr, [](Normalizers& normalizers) { clos::register_normalizers(normalizers); }};
}

