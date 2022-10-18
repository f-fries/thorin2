#include "dialects/clos/clos.h"

#include <thorin/config.h>
#include <thorin/pass/pass.h>

#include "thorin/dialects.h"

#include "thorin/pass/fp/eta_exp.h"
#include "thorin/pass/fp/eta_red.h"
#include "thorin/pass/rw/scalarize.h"

#include "dialects/clos/pass/fp/lower_typed_clos_prep.h"
#include "dialects/clos/pass/rw/branch_clos_elim.h"
#include "dialects/clos/pass/rw/clos2sjlj.h"
#include "dialects/clos/pass/rw/clos_conv_prep.h"
#include "dialects/clos/phase/clos_conv.h"
#include "dialects/clos/phase/lower_typed_clos.h"
#include "dialects/mem/mem.h"
#include "dialects/mem/passes/fp/copy_prop.h"

using namespace thorin;

class ClosConvWrapper : public RWPass<ClosConvWrapper, Lam> {
public:
    ClosConvWrapper(PassMan& man)
        : RWPass(man, "clos_conv") {}

    void prepare() override { clos::ClosConv(world()).run(); }
};

class LowerTypedClosWrapper : public RWPass<LowerTypedClosWrapper, Lam> {
public:
    LowerTypedClosWrapper(PassMan& man)
        : RWPass(man, "lower_typed_clos") {}

    void prepare() override { clos::LowerTypedClos(world()).run(); }
};

extern "C" THORIN_EXPORT DialectInfo thorin_get_dialect_info() {
    return {"clos",
            [](PipelineBuilder& builder) {
                // closure conv
                builder.extend_opt_phase([](PassMan& man) { man.add<clos::ClosConvPrep>(); });
                builder.extend_opt_phase([](PassMan& man) { man.add<ClosConvWrapper>(); });

                // lower closures + cleanup
                builder.extend_opt_phase([](PassMan& man) {
                    auto er = man.add<EtaRed>(true);
                    auto ee = man.add<EtaExp>(er);
                    man.add<Scalerize>(ee);
                    man.add<clos::BranchClosElim>();
                    man.add<mem::CopyProp>(nullptr, ee, true);
                });
                builder.extend_opt_phase([](PassMan& man) { man.add<clos::Clos2SJLJ>(); });
                builder.extend_opt_phase([](PassMan& man) { man.add<clos::LowerTypedClosPrep>(); });
                builder.extend_opt_phase([](PassMan& man) { man.add<LowerTypedClosWrapper>(); });
            },
            nullptr, [](Normalizers& normalizers) { clos::register_normalizers(normalizers); }};
}
