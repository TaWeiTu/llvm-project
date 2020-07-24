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

bool LoopNestAnalysisManagerFunctionProxy::Result::invalidate(
    Function &F, const PreservedAnalyses &PA,
    FunctionAnalysisManager::Invalidator &Inv) {
  // If literally everything is preserved, we're done.
  if (PA.areAllPreserved())
    return false; // This is still a valid proxy.

  auto PAC = PA.getChecker<LoopNestAnalysisManagerFunctionProxy>();
  if (!PAC.preserved() && !PAC.preservedSet<AllAnalysesOn<Function>>()) {
    if (Inv.invalidate<AAManager>(F, PA) ||
        Inv.invalidate<AssumptionAnalysis>(F, PA) ||
        Inv.invalidate<DominatorTreeAnalysis>(F, PA) ||
        Inv.invalidate<LoopAnalysis>(F, PA) ||
        Inv.invalidate<ScalarEvolutionAnalysis>(F, PA)) {
      InnerAM->clear();
      return true;
    }
  }

  // bool AreFunctionAnalysesPreserved;
  // TODO: unimplemented
  return false;
}

template <>
bool LoopAnalysisManagerLoopNestProxy::Result::invalidate(
    LoopNest &LN, const PreservedAnalyses &PA,
    LoopNestAnalysisManager::Invalidator &Inv) {
  // TODO: unimplemented
  return false;
}

template <>
LoopNestAnalysisManagerFunctionProxy::Result
LoopNestAnalysisManagerFunctionProxy::run(Function &F,
                                          FunctionAnalysisManager &AM) {
  return Result(*InnerAM, AM.getResult<LoopAnalysis>(F));
}

} // namespace llvm
