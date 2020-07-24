//===- LoopNestAnalysisManager.h - LoopNest analysis management ---------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_LOOPNESTANALYSISMANAGER_H
#define LLVM_ANALYSIS_LOOPNESTANALYSISMANAGER_H

#include "llvm/Analysis/LoopNestAnalysis.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

extern template class AnalysisManager<LoopNest>;
using LoopNestAnalysisManager = AnalysisManager<LoopNest>;

using LoopNestAnalysisManagerFunctionProxy =
    InnerAnalysisManagerProxy<LoopNestAnalysisManager, Function>;

/// A specialized result for the \c LoopNestAnalysisManagerFunctionProxy which
/// retains a \c LoopInfo reference.
///
/// This allows it to collect loop nest objects for which analysis results may
/// be cached in the \c LoopNestAnalysisManager.
template <> class LoopNestAnalysisManagerFunctionProxy::Result {
public:
  explicit Result(LoopNestAnalysisManager &InnerAM, LoopInfo &LI)
      : InnerAM(&InnerAM), LI(&LI), MSSAUsed(false) {}
  Result(Result &&Arg)
      : InnerAM(std::move(Arg.InnerAM)), LI(Arg.LI), MSSAUsed(Arg.MSSAUsed) {
    // We have to null out the analysis manager in the moved-from state
    // because we are taking ownership of the responsibilty to clear the
    // analysis state.
    Arg.InnerAM = nullptr;
  }
  Result &operator=(Result &&RHS) {
    InnerAM = RHS.InnerAM;
    LI = RHS.LI;
    MSSAUsed = RHS.MSSAUsed;
    // We have to null out the analysis manager in the moved-from state
    // because we are taking ownership of the responsibilty to clear the
    // analysis state.
    RHS.InnerAM = nullptr;
    return *this;
  }
  ~Result() {
    // InnerAM is cleared in a moved from state where there is nothing to do.
    if (!InnerAM)
      return;

    // Clear out the analysis manager if we're being destroyed -- it means we
    // didn't even see an invalidate call when we got invalidated.
    InnerAM->clear();
  }

  /// Mark MemorySSA as used so we can invalidate self if MSSA is invalidated.
  void markMSSAUsed() { MSSAUsed = true; }

  /// Accessor for the analysis manager.
  LoopNestAnalysisManager &getManager() { return *InnerAM; }

  /// Handler for invalidation of the proxy for a particular function.
  ///
  /// If the proxy, \c LoopInfo, and associated analyses are preserved, this
  /// will merely forward the invalidation event to any cached loop analysis
  /// results for loops within this function.
  ///
  /// If the necessary loop infrastructure is not preserved, this will forcibly
  /// clear all of the cached analysis results that are keyed on the \c
  /// LoopInfo for this function.
  bool invalidate(Function &F, const PreservedAnalyses &PA,
                  FunctionAnalysisManager::Invalidator &Inv);

private:
  LoopNestAnalysisManager *InnerAM;
  LoopInfo *LI;
  bool MSSAUsed;
};

template <>
LoopNestAnalysisManagerFunctionProxy::Result
LoopNestAnalysisManagerFunctionProxy::run(Function &F,
                                          FunctionAnalysisManager &AM);

extern template class InnerAnalysisManagerProxy<LoopNestAnalysisManager,
                                                Function>;

extern template class InnerAnalysisManagerProxy<LoopAnalysisManager, LoopNest>;
using LoopAnalysisManagerLoopNestProxy =
    InnerAnalysisManagerProxy<LoopAnalysisManager, LoopNest>;

} // namespace llvm

#endif // LLVM_ANALYSIS_LOOPNESTANALYSISMANAGER_H
