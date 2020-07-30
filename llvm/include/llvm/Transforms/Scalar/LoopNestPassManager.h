//===- LoopNestPassManager.h - Loop nest pass management -----------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOOPNESTPASSMANAGER_H
#define LLVM_TRANSFORMS_SCALAR_LOOPNESTPASSMANAGER_H

#include "llvm/ADT/PriorityWorklist.h"
#include "llvm/Analysis/LoopNestAnalysis.h"
#include "llvm/Analysis/LoopNestAnalysisManager.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"

namespace llvm {

class LNPMUpdater;

template <>
PreservedAnalyses
PassManager<LoopNest, LoopNestAnalysisManager, LoopStandardAnalysisResults &,
            LNPMUpdater &>::run(LoopNest &LN, LoopNestAnalysisManager &AM,
                                LoopStandardAnalysisResults &AR,
                                LNPMUpdater &U);

extern template class PassManager<LoopNest, LoopNestAnalysisManager,
                                  LoopStandardAnalysisResults &, LNPMUpdater &>;

using LoopNestPassManager =
    PassManager<LoopNest, LoopNestAnalysisManager,
                LoopStandardAnalysisResults &, LNPMUpdater &>;

/// A partial specialization of the require analysis template pass to forward
/// the extra parameters from a transformation's run method to the
/// AnalysisManager's getResult.
template <typename AnalysisT>
struct RequireAnalysisPass<AnalysisT, LoopNest, LoopNestAnalysisManager,
                           LoopStandardAnalysisResults &, LNPMUpdater &>
    : PassInfoMixin<
          RequireAnalysisPass<AnalysisT, LoopNest, LoopNestAnalysisManager,
                              LoopStandardAnalysisResults &, LNPMUpdater &>> {
  PreservedAnalyses run(LoopNest &LN, LoopNestAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LNPMUpdater &) {
    (void)AM.template getResult<AnalysisT>(LN, AR);
    return PreservedAnalyses::all();
  }
};

/// This class provides an interface for updating the loop nest pass manager
/// based on mutations to the loop nest.
///
/// A reference to an instance of this class is passed as an argument to each
/// LoopNest pass, and LoopNest passes should use it to update LNPM
/// infrastructure if they modify the loop nest structure.
class LNPMUpdater {
public:
  /// This can be queried by loop nest passes which run other loop nest passes
  /// (like pass managers) to know whether the loop nest needs to be skipped due
  /// to updates to the loop nest.
  ///
  /// If this returns true, the loop nest object may have been deleted, so
  /// passes should take care not to touch the object.
  bool skipCurrentLoopNest() const { return SkipCurrentLoopNest; }

  void markLoopNestAsDeleted(LoopNest &LN, llvm::StringRef Name) {
    LNAM.clear(LN, Name);
    assert(&LN.getOutermostLoop() == CurrentLoopNest &&
           "Cannot delete loop nests other than the current one");
    SkipCurrentLoopNest = true;
  }

  /// Loop nest passes should use this method to indicate they have added new
  /// loop nests to the current function.
  ///
  /// \p NewLoopNests must only contain top-level loops.
  void addNewLoopNests(ArrayRef<Loop *> NewLoopNests) {
    for (Loop *NewL : NewLoopNests) {
#ifndef NDEBUG
      assert(!NewL->getParentLoop() &&
             "All of the new loops must be top-level!");
#endif
      Worklist.insert(NewL);
    }
  }

  void revisitCurrentLoopNest() {
    SkipCurrentLoopNest = true;
    Worklist.insert(CurrentLoopNest);
  }

private:
  template <typename LoopNestPassT> friend class FunctionToLoopNestPassAdaptor;

  LNPMUpdater(SmallPriorityWorklist<Loop *, 4> &Worklist,
              LoopNestAnalysisManager &LNAM)
      : Worklist(Worklist), LNAM(LNAM) {}

  /// The \c FunctionToLoopNestPassAdaptor's worklist of loops to process.
  SmallPriorityWorklist<Loop *, 4> &Worklist;

  /// The analysis manager for use in the current loop nest;
  LoopNestAnalysisManager &LNAM;

  Loop *CurrentLoopNest;
  bool SkipCurrentLoopNest;
};

/// Adaptor that maps from a function to its loop nests.
///
/// Designed to allow composition of a LoopNestPass(Manager) and a
/// FunctionPassManager. Note that if this pass is constructed with a \c
/// FunctionAnalysisManager it will run the \c
/// LoopNestAnalysisManagerFunctionProxy analysis prior to running the loop
/// passes over the function to enable a \c LoopNestAnalysisManager to be used
/// within this run safely.
template <typename LoopNestPassT>
class FunctionToLoopNestPassAdaptor
    : public PassInfoMixin<FunctionToLoopNestPassAdaptor<LoopNestPassT>> {
public:
  explicit FunctionToLoopNestPassAdaptor(LoopNestPassT Pass,
                                         bool UseMemorySSA = false,
                                         bool DebugLogging = false)
      : Pass(std::move(Pass)), UseMemorySSA(UseMemorySSA),
        LoopCanonicalizationFPM(DebugLogging) {
    LoopCanonicalizationFPM.addPass(LoopSimplifyPass());
    LoopCanonicalizationFPM.addPass(LCSSAPass());
  }

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
    // Before we even compute any loop nest analyses, first run a miniature
    // function pass pipeline to put loops into their canonical form. Note that
    // we can directly build up function analyses after this as the function
    // pass manager handles all the invalidation at that layer.
    PassInstrumentation PI = AM.getResult<PassInstrumentationAnalysis>(F);

    PreservedAnalyses PA = PreservedAnalyses::all();
    if (PI.runBeforePass<Function>(LoopCanonicalizationFPM, F)) {
      PA = LoopCanonicalizationFPM.run(F, AM);
      PI.runAfterPass<Function>(LoopCanonicalizationFPM, F);
    }

    // Get the loop structure for this function
    LoopInfo &LI = AM.getResult<LoopAnalysis>(F);

    // If there are no loops, there is nothing to do here.
    if (LI.empty())
      return PA;

    // Get the analysis results needed by loop nest passes.
    MemorySSA *MSSA = UseMemorySSA
                          ? (&AM.getResult<MemorySSAAnalysis>(F).getMSSA())
                          : nullptr;
    LoopStandardAnalysisResults LAR = {AM.getResult<AAManager>(F),
                                       AM.getResult<AssumptionAnalysis>(F),
                                       AM.getResult<DominatorTreeAnalysis>(F),
                                       AM.getResult<LoopAnalysis>(F),
                                       AM.getResult<ScalarEvolutionAnalysis>(F),
                                       AM.getResult<TargetLibraryAnalysis>(F),
                                       AM.getResult<TargetIRAnalysis>(F),
                                       MSSA};

    // Setup the loop nest analysis manager from its proxy. It is important that
    // this is only done when there are loops to process and we have built the
    // LoopStandardAnalysisResults object. The loop nest analyses cached in this
    // manager have access to those analysis results and so it must invalidate
    // itself when they go away.
    auto &LNAMFP = AM.getResult<LoopNestAnalysisManagerFunctionProxy>(F);
    if (UseMemorySSA)
      LNAMFP.markMSSAUsed();
    LoopNestAnalysisManager &LNAM = LNAMFP.getManager();

    // The worklist of loop nests in the function. The loop nests are
    // represented by their root loops and the actual LoopNest object will be
    // constructed lazily when needed.
    SmallPriorityWorklist<Loop *, 4> Worklist;
    LNPMUpdater Updater(Worklist, LNAM);

    // Append all outer-most loops in the function into the worklist.
    for (Loop *L : LI.getTopLevelLoops())
      Worklist.insert(L);

    do {
      Loop *L = Worklist.pop_back_val();

      // Reset the update structure for this loop nest.
      Updater.CurrentLoopNest = L;
      Updater.SkipCurrentLoopNest = false;

      LoopNest &LN = LNAM.getLoopNest(*L, LAR);
      // Check the PassInstrumentation's BeforePass callbacks before running the
      // pass, skip its execution completely if asked to (callback returns
      // false).
      if (!PI.runBeforePass<LoopNest>(Pass, LN))
        continue;

      PreservedAnalyses PassPA;
      {
        TimeTraceScope TimeScope(Pass.name());
        PassPA = Pass.run(LN, LNAM, LAR, Updater);
      }

      // Do not pass deleted LoopNest into the instrumentation.
      if (Updater.skipCurrentLoopNest())
        PI.runAfterPassInvalidated<LoopNest>(Pass);
      else
        PI.runAfterPass<LoopNest>(Pass, LN);

      if (!Updater.skipCurrentLoopNest())
        // We know that the loop nest pass couldn't have invalidated any other
        // loop nest's analyses (that's the contract of a loop nest pass), so
        // directly handle the loop nest analysis manager's invalidation here.
        LNAM.invalidate(LN, PassPA);

      // Then intersect the preserved set so that invalidation of loop nest
      // analyses will eventually occur when the loop nest pass completes.
      PA.intersect(std::move(PassPA));
    } while (!Worklist.empty());

    // By definition we preserve the proxy. We also preserve all analyses on
    // LoopNests. This precludes *any* invalidation of loop nest analyses by the
    // proxy, but that's OK because we've taken care to invalidate analyses in
    // the loop nest analysis manager incrementally above.
    PA.preserveSet<AllAnalysesOn<LoopNest>>();
    PA.preserve<LoopNestAnalysisManagerFunctionProxy>();
    // We also preserve the set of standard analyses.
    detail::preserveLoopStandardAnalysisResults(PA, UseMemorySSA);
    detail::preserveAACategory(PA);
    return PA;
  }

private:
  LoopNestPassT Pass;
  bool UseMemorySSA;
  FunctionPassManager LoopCanonicalizationFPM;
};

/// A function to deduce a loop nest pass type and wrap it in the templated
/// adaptor.
template <typename LoopNestPassT>
FunctionToLoopNestPassAdaptor<LoopNestPassT>
createFunctionToLoopNestPassAdaptor(LoopNestPassT Pass,
                                    bool UseMemorySSA = false,
                                    bool DebugLogging = false) {
  return FunctionToLoopNestPassAdaptor<LoopNestPassT>(
      std::move(Pass), UseMemorySSA, DebugLogging);
}

/// Pass for printing a loop nest's property. This is similar to
/// \c LoopNestPrinterPass in \file LoopNestAnalysis.h but implemented as a
/// LoopNestPass.
class PrintLoopNestPass : public PassInfoMixin<PrintLoopNestPass> {
  raw_ostream &OS;
  std::string Banner;

public:
  PrintLoopNestPass();
  explicit PrintLoopNestPass(raw_ostream &OS, const std::string &Banner = "");

  PreservedAnalyses run(LoopNest &LN, LoopNestAnalysisManager &,
                        LoopStandardAnalysisResults &, LNPMUpdater &U);
};

/// Adaptor that maps from a loop nest to its loops.
template <typename LoopPassT>
class LoopNestToLoopPassAdaptor
    : public PassInfoMixin<LoopNestToLoopPassAdaptor<LoopPassT>> {
public:
  explicit LoopNestToLoopPassAdaptor(LoopPassT Pass) : Pass(std::move(Pass)) {}

  PreservedAnalyses run(LoopNest &LN, LoopNestAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LNPMUpdater &U) {
    PassInstrumentation PI = AM.getResult<PassInstrumentationAnalysis>(LN, AR);
    PreservedAnalyses PA = PreservedAnalyses::all();

    // Get the loop analysis manager from the loop nest analysis manager. No
    // need to set up proxy here since currently the latter is simply a wrapper
    // around the former.
    LoopAnalysisManager &LAM = AM.getLoopAnalysisManager();

    SmallPriorityWorklist<Loop *, 4> Worklist;
    LPMUpdater Updater(Worklist, LAM);
    appendLoopNestToWorklist(LN.getOutermostLoop(), Worklist);

    assert(!Worklist.empty() &&
           "Worklist should be non-empty since we're running on a LoopNest");
    do {
      Loop *L = Worklist.pop_back_val();
      Updater.CurrentL = L;
      Updater.SkipCurrentLoop = false;

#ifndef NDEBUG
      // Save a parent loop pointer for asserts.
      Updater.ParentL = L->getParentLoop();

      // Verify the loop structure and LCSSA form.
      L->verifyLoop();
      assert(L->isRecursivelyLCSSAForm(AR.DT, AR.LI) &&
             "Loops must remain in LCSSA form!");
#endif

      // Check the PassInstrumentation's BeforePass callbacks.
      if (!PI.runBeforePass<Loop>(Pass, *L))
        continue;

      PreservedAnalyses PassPA;
      {
        TimeTraceScope TimeScope(Pass.name());
        PassPA = Pass.run(*L, LAM, AR, Updater);
      }

      // Do not pass deleted Loop into the instrumentation.
      if (Updater.skipCurrentLoop())
        PI.runAfterPassInvalidated<Loop>(Pass);
      else
        PI.runAfterPass<Loop>(Pass, *L);

      if (!Updater.SkipCurrentLoop)
        // Invalidate the loop analysis results here.
        LAM.invalidate(*L, PassPA);

      PA.intersect(std::move(PassPA));
    } while (!Worklist.empty());

    // We don't have to explicitly mark the loop standard analysis results as
    // preserved here since this will eventually be handled by the \c
    // FunctionToLoopNestPassAdaptor.
    PA.preserveSet<AllAnalysesOn<Loop>>();
    // FIXME: We should check whether the loop nest structure is preserved or
    // not.
    return PA;
  }

private:
  LoopPassT Pass;
};

/// A function to deduce a loop pass type and wrap it in the templated
/// adaptor.
template <typename LoopPassT>
LoopNestToLoopPassAdaptor<LoopPassT>
createLoopNestToLoopPassAdaptor(LoopPassT Pass) {
  return LoopNestToLoopPassAdaptor<LoopPassT>(std::move(Pass));
}

} // namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_LOOPNESTPASSMANAGER_H
