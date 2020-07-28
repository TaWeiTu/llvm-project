//===- LoopNestAnalysisManager.cpp - LoopNest analysis management ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/LoopNestAnalysisManager.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionAliasAnalysis.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/PassManagerImpl.h"

using namespace llvm;

namespace llvm {

template class AnalysisManager<LoopNest>;
template class InnerAnalysisManagerProxy<LoopNestAnalysisManager, Function>;
template class InnerAnalysisManagerProxy<LoopAnalysisManager, LoopNest>;
template class OuterAnalysisManagerProxy<FunctionAnalysisManager, LoopNest,
                                         LoopStandardAnalysisResults &>;

bool LoopNestAnalysisManagerFunctionProxy::Result::invalidate(
    Function &F, const PreservedAnalyses &PA,
    FunctionAnalysisManager::Invalidator &Inv) {
  // If literally everything is preserved, we're done.
  if (PA.areAllPreserved())
    return false; // This is still a valid proxy.

  const std::vector<Loop *> &Loops = LI->getTopLevelLoops();

  auto PAC = PA.getChecker<LoopNestAnalysisManagerFunctionProxy>();
  bool invalidateMemorySSAAnalysis = false;
  if (MSSAUsed)
    invalidateMemorySSAAnalysis = Inv.invalidate<MemorySSAAnalysis>(F, PA);
  if (!PAC.preserved() && !PAC.preservedSet<AllAnalysesOn<Function>>()) {
    if (Inv.invalidate<AAManager>(F, PA) ||
        Inv.invalidate<AssumptionAnalysis>(F, PA) ||
        Inv.invalidate<DominatorTreeAnalysis>(F, PA) ||
        Inv.invalidate<LoopAnalysis>(F, PA) ||
        Inv.invalidate<ScalarEvolutionAnalysis>(F, PA) ||
        invalidateMemorySSAAnalysis) {
      // Note that the LoopInfo may be stale at this point, however the loop
      // objects themselves remain the only viable keys that could be in the
      // analysis manager's cache. So we just walk the keys and forcibly clear
      // those results. Note that the order doesn't matter here as this will
      // just directly destroy the results without calling methods on them.
      //
      // Though we're dealing with loop nests here, the analysis results can
      // still be cleared via the root loops.
      for (Loop *L : Loops)
        InnerAM->clear(*L, "<possibly invalidated loop>");
      InnerAM = nullptr;
      return true;
    }
  }

  // Directly check if the relevant set is preserved.
  bool AreLoopNestAnalysesPreserved =
      PA.allAnalysesInSetPreserved<AllAnalysesOn<LoopNest>>();

  for (Loop *L : Loops) {
    Optional<PreservedAnalyses> LoopNestPA;

    // Check to see whether the preserved set needs to be pruned based on
    // function-level analysis invalidation that triggers deferred invalidation
    // registered with the outer analysis manager proxy for this loop nest.
    if (auto *OuterProxy =
            InnerAM->getCachedResult<FunctionAnalysisManagerLoopNestProxy>(
                *L)) {
      for (const auto &OuterInvalidationPair :
           OuterProxy->getOuterInvalidations()) {
        AnalysisKey *OuterAnalysisID = OuterInvalidationPair.first;
        const auto &InnerAnalysisIDs = OuterInvalidationPair.second;
        if (Inv.invalidate(OuterAnalysisID, F, PA)) {
          if (!LoopNestPA)
            LoopNestPA = PA;
          for (AnalysisKey *InnerAnalysisID : InnerAnalysisIDs)
            LoopNestPA->abandon(InnerAnalysisID);
        }
      }
    }

    // Check if we needed a custom PA set, and if so we'll need to run the
    // inner invalidation.
    if (LoopNestPA) {
      InnerAM->invalidate(*L, *LoopNestPA);
      continue;
    }

    // Otherwise we only need to do invalidation if the original PA set didn't
    // preserve all loop nest analyses.
    if (!AreLoopNestAnalysesPreserved)
      InnerAM->invalidate(*L, PA);
  }

  // Return false to indicate that this result is still a valid proxy.
  return false;
}

template <>
LoopNestAnalysisManagerFunctionProxy::Result
LoopNestAnalysisManagerFunctionProxy::run(Function &F,
                                          FunctionAnalysisManager &AM) {
  return Result(*InnerAM, AM.getResult<LoopAnalysis>(F));
}

} // namespace llvm
