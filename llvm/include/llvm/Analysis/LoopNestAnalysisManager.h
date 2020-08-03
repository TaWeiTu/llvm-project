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

class LNPMUpdater;

/// The loop nest analysis manager.
///
/// The loop nest analyses should run on \Loop instead of LoopNests since
/// \c LoopNest are constantly invalidated by both loop nest passes and loop
/// passes. Generally speaking, the passes should update the analysis results
/// dynamically when possible, and running on Loops prevent the analyses from
/// being invalidated when the loop structures change.
///
/// \c LoopNestAnalysisManager is a wrapper around \c LoopAnalysisManager and
/// provide all the public APIs that \c AnalysisManager has so that is seems to
/// be operating on \c LoopNest. \c LoopNestAnalysisManager also provides the
/// ability to construct \c LoopNest from the top-level \c Loop. The loop nest
/// analyses can also obtain the \c LoopNest object from the \c
/// LoopAnalysisManager.
///
/// The \c LoopNest object will be invalidated after the loop nest passes unless
/// \c LoopNestAnalysis is explicitly marked as preserved.
template <> class AnalysisManager<LoopNest, LoopStandardAnalysisResults &> {
public:
  class Invalidator {
  public:
    /// The following methods should never be called because the
    /// invalidation in \c LoopNestAnalysisManager will be passed to the
    /// internal \c LoopAnalysisManager. The only purpose of these methods is to
    /// satisfy the requirements of being an \c AnalysisManager.
    template <typename PassT>
    bool invalidate(LoopNest &, const PreservedAnalyses &) {
      assert(false && "This method should never be called.");
      return false;
    }

    bool invalidate(AnalysisKey *, LoopNest &, const PreservedAnalyses &) {
      assert(false && "This method should never be called.");
      return false;
    }
  };

  AnalysisManager(LoopAnalysisManager &LAM) : InternalLAM(LAM) {}

  bool empty() const { return InternalLAM.empty(); };

  void clear(LoopNest &LN, llvm::StringRef Name) {
    InternalLAM.clear(LN.getOutermostLoop(), Name);
  }
  void clear(Loop &L, llvm::StringRef Name) { InternalLAM.clear(L, Name); }
  void clear() { InternalLAM.clear(); }

  LoopNest &getLoopNest(Loop &Root, LoopStandardAnalysisResults &LAR) {
    return InternalLAM.getResult<LoopNestAnalysis>(Root, LAR);
  }

  /// Get the result of an analysis pass for a given LoopNest.
  ///
  /// Runs the analysis if a cached result is not available.
  template <typename PassT>
  typename PassT::Result &getResult(LoopNest &LN,
                                    LoopStandardAnalysisResults &LAR) {
    return InternalLAM.getResult<PassT>(LN.getOutermostLoop(), LAR);
  }
  template <typename PassT>
  typename PassT::Result &getResult(Loop &L, LoopStandardAnalysisResults &LAR) {
    return InternalLAM.getResult<PassT>(L, LAR);
  }

  /// Get the cached result of an analysis pass for a given LoopNest.
  ///
  /// This method never runs the analysis.
  ///
  /// \returns null if there is no cached result.
  template <typename PassT>
  typename PassT::Result *getCachedResult(LoopNest &LN) const {
    return InternalLAM.getCachedResult<PassT>(LN.getOutermostLoop());
  }
  template <typename PassT>
  typename PassT::Result *getCachedResult(Loop &L) const {
    return InternalLAM.getCachedResult<PassT>(L);
  }

  template <typename PassT>
  void verifyNotInvalidated(LoopNest &LN,
                            typename PassT::Result *Result) const {
    InternalLAM.verifyNotInvalidated<PassT>(LN.getOutermostLoop(), Result);
  }
  template <typename PassT>
  void verifyNotInvalidated(Loop &L, typename PassT::Result *Result) const {
    InternalLAM.verifyNotInvalidated<PassT>(L, Result);
  }

  template <typename PassBuilderT>
  bool registerPass(PassBuilderT &&PassBuilder) {
    return InternalLAM.registerPass(std::forward<PassBuilderT>(PassBuilder));
  }

  /// Invalidate the analysis results. Aside from the loop nest analyses of the
  /// root loop, we have to invalidate the loop analyses of all the subtree as
  /// well.
  void invalidate(LoopNest &LN, const PreservedAnalyses &PA) {
    invalidateSubLoopAnalyses(LN.getOutermostLoop(), PA);
    InternalLAM.invalidate(LN.getOutermostLoop(), PA);
  }
  void invalidate(Loop &L, const PreservedAnalyses &PA) {
    invalidateSubLoopAnalyses(L, PA);
    InternalLAM.invalidate(L, PA);
  }

  LoopAnalysisManager &getLoopAnalysisManager() { return InternalLAM; }

private:
  LoopAnalysisManager &InternalLAM;
  friend class InnerAnalysisManagerProxy<
      AnalysisManager<LoopNest, LoopStandardAnalysisResults &>, Function>;

  /// Invalidate the loop analyses of loops in the subtree rooted at \p L
  /// (excluding \p L).
  void invalidateSubLoopAnalyses(Loop &L, const PreservedAnalyses &PA);
};

using LoopNestAnalysisManager =
    AnalysisManager<LoopNest, LoopStandardAnalysisResults &>;
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
extern template class OuterAnalysisManagerProxy<FunctionAnalysisManager, Loop,
                                                LoopStandardAnalysisResults &>;
using FunctionAnalysisManagerLoopNestProxy =
    OuterAnalysisManagerProxy<FunctionAnalysisManager, Loop,
                              LoopStandardAnalysisResults &>;

} // namespace llvm

#endif // LLVM_ANALYSIS_LOOPNESTANALYSISMANAGER_H
