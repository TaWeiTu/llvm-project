//===- LoopNestPassManager.cpp - Loop nest pass management ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/LoopNestPassManager.h"

namespace llvm {

template <>
PreservedAnalyses
PassManager<LoopNest, LoopNestAnalysisManager, LoopStandardAnalysisResults &,
            LNPMUpdater &>::run(LoopNest &LN, LoopNestAnalysisManager &AM,
                                LoopStandardAnalysisResults &AR,
                                LNPMUpdater &U) {
  PreservedAnalyses PA = PreservedAnalyses::all();

  // Request PassInstrumentation from analysis manager, will use it to run
  // instrumenting callbacks for the passes later.
  PassInstrumentation PI = AM.getResult<PassInstrumentationAnalysis>(LN, AR);

  if (DebugLogging)
    dbgs() << "Starting LoopNest pass manager run.\n";

  for (unsigned I = 0, E = Passes.size(); I != E; ++I) {
    auto *Pass = Passes[I].get();

    // Check the PassInstrumentation's BeforePass callbacks before running the
    // pass, skip its execution completely if asked to (callback returns
    // false).
    if (!PI.runBeforePass<LoopNest>(*Pass, LN))
      continue;

    PreservedAnalyses PassPA;
    {
      TimeTraceScope TimeScope(Pass->name(), LN.getName());
      PassPA = Pass->run(LN, AM, AR, U);
    }

    // Do not pass deleted LoopNest into the instrumentation
    if (U.skipCurrentLoopNest())
      PI.runAfterPassInvalidated<LoopNest>(*Pass);
    else
      PI.runAfterPass<LoopNest>(*Pass, LN);

    if (U.skipCurrentLoopNest()) {
      PA.intersect(std::move(PassPA));
      break;
    }

    // We shouldn't allow invalidating LoopNestAnalysis in AM since otherwise
    // LN will be dangling. Currently the loop nest passes cannot explicitly
    // update the LoopNest structure, so we must first check whether
    // LoopNestAnalysis is preserved, and mark it as preserved
    // regardlessly afterward. If the analysis is not preserved in the first
    // place, we would have to manually reconstruct the LoopNest.
    // FIXME: This is quite inefficient. Consider reimplementing LoopNest to
    // allow dynamic modifications by the loop nest passes to avoid
    // reconstructing it every time.
    bool IsLoopNestPreserved =
        PassPA.getChecker<LoopNestAnalysis>().preserved();

    // No need to invalidate other loop nest analyses since they are running on
    // Loop and hence can be updated dynamically.
    PassPA.preserve<LoopNestAnalysis>();
    AM.invalidate(LN, PassPA);

    if (!IsLoopNestPreserved)
      // The LoopNest structure had been altered, reconstruct it here.
      LN.reconstructInplace(AR.SE);
    PA.intersect(std::move(PassPA));
  }

  // Invalidation for the current loop nest should be handled above, and other
  // loop nest analysis results shouldn't be impacted by runs over this loop
  // nest. Therefore, the remaining analysis results in the AnalysisManager are
  // preserved. We mark this with a set so that we don't need to inspect each
  // one individually.
  PA.preserveSet<AllAnalysesOn<LoopNest>>();
  // All analyses on Loops are preserved as well.
  PA.preserveSet<AllAnalysesOn<Loop>>();

  if (DebugLogging)
    dbgs() << "Finished LoopNest pass manager run.\n";

  return PA;
}

template class PassManager<LoopNest, LoopNestAnalysisManager,
                           LoopStandardAnalysisResults &, LNPMUpdater &>;

PrintLoopNestPass::PrintLoopNestPass() : OS(dbgs()) {}
PrintLoopNestPass::PrintLoopNestPass(raw_ostream &OS, const std::string &Banner)
    : OS(OS), Banner(Banner) {}

PreservedAnalyses PrintLoopNestPass::run(LoopNest &LN,
                                         LoopNestAnalysisManager &,
                                         LoopStandardAnalysisResults &,
                                         LNPMUpdater &) {
  OS << LN << "\n";
  return PreservedAnalyses::all();
}

} // namespace llvm
