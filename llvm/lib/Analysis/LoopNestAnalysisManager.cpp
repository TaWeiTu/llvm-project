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
template class OuterAnalysisManagerProxy<FunctionAnalysisManager, Loop,
                                         LoopStandardAnalysisResults &>;

bool LoopNestAnalysisManagerFunctionProxy::Result::invalidate(
    Function &F, const PreservedAnalyses &PA,
    FunctionAnalysisManager::Invalidator &Inv) {
  // If literally everything is preserved, we're done.
  if (PA.areAllPreserved())
    return false; // This is still a valid proxy.

  std::vector<Loop *> TopLevelLoops = LI->getTopLevelLoops();
  SmallVector<Loop *, 4> PreOrderLoops = LI->getLoopsInReverseSiblingPreorder();

  auto PAC = PA.getChecker<LoopNestAnalysisManagerFunctionProxy>();
  bool InvalidateMemorySSAAnalysis = false;
  if (MSSAUsed)
    InvalidateMemorySSAAnalysis = Inv.invalidate<MemorySSAAnalysis>(F, PA);
  if (!(PAC.preserved() || PAC.preservedSet<AllAnalysesOn<Function>>()) ||
      Inv.invalidate<AAManager>(F, PA) ||
      Inv.invalidate<AssumptionAnalysis>(F, PA) ||
      Inv.invalidate<DominatorTreeAnalysis>(F, PA) ||
      Inv.invalidate<LoopAnalysis>(F, PA) ||
      Inv.invalidate<ScalarEvolutionAnalysis>(F, PA) ||
      InvalidateMemorySSAAnalysis) {
    // Note that the LoopInfo may be stale at this point, however the loop
    // objects themselves remain the only viable keys that could be in the
    // analysis manager's cache. So we just walk the keys and forcibly clear
    // those results. Note that the order doesn't matter here as this will
    // just directly destroy the results without calling methods on them.
    //
    // Though we're dealing with loop nests here, the analysis results can
    // still be cleared via the root loops.
    //
    // Note that not only do we invalidate loop nest analyses on the root
    // loops, the loop anlayses on the subloops should also be cleared because
    // they depend on the standard analysis results as well.
    for (Loop *L : PreOrderLoops)
      InnerAM->clear(*L, "<possibly invalidated loop>");
    InnerAM = nullptr;
    return true;
  }

  // Directly check if the relevant set is preserved.
  bool AreLoopNestAnalysesPreserved =
      PA.allAnalysesInSetPreserved<AllAnalysesOn<LoopNest>>();

  // getTopLevelLoops() returns loops in "reversed" order. Reverse the list
  // again for correctness.
  for (Loop *L : reverse(TopLevelLoops)) {
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

void LoopNestAnalysisManager::invalidateSubLoopAnalyses(
    Loop &Root, const PreservedAnalyses &PA) {
  // We can return immediately if all the loop analyses are preserved.
  if (PA.areAllPreserved() ||
      PA.allAnalysesInSetPreserved<AllAnalysesOn<Loop>>())
    return;

  // Collect the loops in the subtree in post-order by performing DFS without
  // recursion using a stack.
  SmallVector<Loop *, 4> DFSStack(Root.begin(), Root.end()), SubLoops;
  while (!DFSStack.empty()) {
    Loop *L = DFSStack.pop_back_val();
    SubLoops.push_back(L);
    DFSStack.append(L->begin(), L->end());
  }

  // Visit the loops in reversed post-order and invalidate them.
  for (Loop *L : reverse(SubLoops)) {
    InternalLAM.invalidate(*L, PA);
  }
}

} // namespace llvm
