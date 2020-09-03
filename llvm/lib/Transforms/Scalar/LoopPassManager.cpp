//===- LoopPassManager.cpp - Loop pass management -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/TimeProfiler.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Analysis/LoopInfo.h"

using namespace llvm;

namespace llvm {

/// Explicitly specialize the pass manager's run method to handle loop nest
/// structure updates.
PreservedAnalyses
PassManager<Loop, LoopAnalysisManager, LoopStandardAnalysisResults &,
            LPMUpdater &>::run(Loop &L, LoopAnalysisManager &AM,
                               LoopStandardAnalysisResults &AR, LPMUpdater &U) {
  PreservedAnalyses PA;

  if (DebugLogging)
    dbgs() << "Starting Loop pass manager run.\n";

  // Request PassInstrumentation from analysis manager, will use it to run
  // instrumenting callbacks for the passes later.
  if (!L.getParentLoop() && !LoopNestPasses.empty())
    PA = runWithLoopNestPasses(L, AM, AR, U);
  else
    PA = runWithoutLoopNestPasses(L, AM, AR, U);

  // Invalidation for the current loop should be handled above, and other loop
  // analysis results shouldn't be impacted by runs over this loop. Therefore,
  // the remaining analysis results in the AnalysisManager are preserved. We
  // mark this with a set so that we don't need to inspect each one
  // individually.
  // FIXME: This isn't correct! This loop and all nested loops' analyses should
  // be preserved, but unrolling should invalidate the parent loop's analyses.
  PA.preserveSet<AllAnalysesOn<Loop>>();

  if (DebugLogging)
    dbgs() << "Finished Loop pass manager run.\n";

  return PA;
}

PreservedAnalyses
LoopPassManager::runWithLoopNestPasses(Loop &L, LoopAnalysisManager &AM,
                                       LoopStandardAnalysisResults &AR,
                                       LPMUpdater &U) {
  PreservedAnalyses PA = PreservedAnalyses::all();
  PassInstrumentation PI = AM.getResult<PassInstrumentationAnalysis>(L, AR);

  unsigned LoopPassIndex = 0, LoopNestPassIndex = 0;
  std::unique_ptr<LoopNest> LoopNestPtr;
  bool IsLoopNestPtrValid = false;

  for (size_t I = 0, E = PassCategories.size(); I != E; ++I) {
    Optional<PreservedAnalyses> PassPA;
    if (!PassCategories.test(I)) {
      auto &Pass = LoopPasses[LoopPassIndex++];
      PassPA = runSinglePass(L, Pass, AM, AR, U, PI);
    } else {
      auto &Pass = LoopNestPasses[LoopNestPassIndex++];

      // If the loop-nest object calculated before is no longer valid,
      // re-calculate it here before running the loop-nest pass.
      if (!IsLoopNestPtrValid) {
        LoopNestPtr = LoopNest::getLoopNest(L, AR.SE);
        IsLoopNestPtrValid = true;
      }
      PassPA = runSinglePass(*LoopNestPtr, Pass, AM, AR, U, PI);
    }

    if (!PassPA)
      continue;

    // If the loop was deleted, abort the run and return to the outer walk.
    if (U.skipCurrentLoop()) {
      PA.intersect(std::move(*PassPA));
      break;
    }

    // Update the analysis manager as each pass runs and potentially
    // invalidates analyses.
    AM.invalidate(L, *PassPA);

    // Finally, we intersect the final preserved analyses to compute the
    // aggregate preserved set for this pass manager.
    PA.intersect(std::move(*PassPA));

    // Check if the current pass preserved the loop-nest object or not.
    IsLoopNestPtrValid &= PassPA->getChecker<LoopNestAnalysis>().preserved();

    // FIXME: Historically, the pass managers all called the LLVM context's
    // yield function here. We don't have a generic way to acquire the
    // context and it isn't yet clear what the right pattern is for yielding
    // in the new pass manager so it is currently omitted.
    // ...getContext().yield();
  }
  return PA;
}

PreservedAnalyses
LoopPassManager::runWithoutLoopNestPasses(Loop &L, LoopAnalysisManager &AM,
                                          LoopStandardAnalysisResults &AR,
                                          LPMUpdater &U) {
  PreservedAnalyses PA = PreservedAnalyses::all();
  PassInstrumentation PI = AM.getResult<PassInstrumentationAnalysis>(L, AR);
  for (auto &Pass : LoopPasses) {
    auto PassPA = runSinglePass(L, Pass, AM, AR, U, PI);
    if (!PassPA)
      continue;

    // If the loop was deleted, abort the run and return to the outer walk.
    if (U.skipCurrentLoop()) {
      PA.intersect(std::move(*PassPA));
      break;
    }

    // Update the analysis manager as each pass runs and potentially
    // invalidates analyses.
    AM.invalidate(L, *PassPA);

    // Finally, we intersect the final preserved analyses to compute the
    // aggregate preserved set for this pass manager.
    PA.intersect(std::move(*PassPA));

    // FIXME: Historically, the pass managers all called the LLVM context's
    // yield function here. We don't have a generic way to acquire the
    // context and it isn't yet clear what the right pattern is for yielding
    // in the new pass manager so it is currently omitted.
    // ...getContext().yield();
  }
  return PA;
}
} // namespace llvm

PrintLoopPass::PrintLoopPass() : OS(dbgs()) {}
PrintLoopPass::PrintLoopPass(raw_ostream &OS, const std::string &Banner)
    : OS(OS), Banner(Banner) {}

PreservedAnalyses PrintLoopPass::run(Loop &L, LoopAnalysisManager &,
                                     LoopStandardAnalysisResults &,
                                     LPMUpdater &) {
  printLoop(L, OS, Banner);
  return PreservedAnalyses::all();
}
