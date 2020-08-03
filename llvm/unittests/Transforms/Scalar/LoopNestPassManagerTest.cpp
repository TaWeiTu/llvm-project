//===- unittests/Transforms/Scalar/LoopNestPassManagerTest.cpp - LNPM Test ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/LoopNestPassManager.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopNestAnalysisManager.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Regex.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace llvm;

namespace {

using testing::_;
using testing::DoDefault;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Return;
using testing::WithArgs;

/// A CRTP base for analysis mock handles
///
/// This class reconciles mocking with the value semantics implementation of the
/// AnalysisManager. Analysis mock handles should derive from this class and
/// call \c setDefault() in their constroctur for wiring up the defaults defined
/// by this base with their mock run() and invalidate() implementations.
template <typename DerivedT, typename IRUnitT,
          typename AnalysisManagerT = AnalysisManager<IRUnitT>,
          typename... ExtraArgTs>
class MockAnalysisHandleBase {
public:
  class Analysis : public AnalysisInfoMixin<Analysis> {
    friend AnalysisInfoMixin<Analysis>;
    friend MockAnalysisHandleBase;
    static AnalysisKey Key;

    DerivedT *Handle;

    Analysis(DerivedT &Handle) : Handle(&Handle) {
      static_assert(std::is_base_of<MockAnalysisHandleBase, DerivedT>::value,
                    "Must pass the derived type to this template!");
    }

  public:
    class Result {
      friend MockAnalysisHandleBase;

      DerivedT *Handle;

      Result(DerivedT &Handle) : Handle(&Handle) {}

    public:
      // Forward invalidation events to the mock handle.
      bool invalidate(IRUnitT &IR, const PreservedAnalyses &PA,
                      typename AnalysisManagerT::Invalidator &Inv) {
        return Handle->invalidate(IR, PA, Inv);
      }
    };

    Result run(IRUnitT &IR, AnalysisManagerT &AM, ExtraArgTs... ExtraArgs) {
      return Handle->run(IR, AM, ExtraArgs...);
    }
  };

  Analysis getAnalysis() { return Analysis(static_cast<DerivedT &>(*this)); }
  typename Analysis::Result getResult() {
    return typename Analysis::Result(static_cast<DerivedT &>(*this));
  }
  static StringRef getName() { return llvm::getTypeName<DerivedT>(); }

protected:
  // FIXME: MSVC seems unable to handle a lambda argument to Invoke from within
  // the template, so we use a boring static function.
  static bool invalidateCallback(IRUnitT &IR, const PreservedAnalyses &PA,
                                 typename AnalysisManagerT::Invalidator &Inv) {
    auto PAC = PA.template getChecker<Analysis>();
    return !PAC.preserved() &&
           !PAC.template preservedSet<AllAnalysesOn<IRUnitT>>();
  }

  /// Derived classes should call this in their constructor to set up default
  /// mock actions. (We can't do this in our constructor because this has to
  /// run after the DerivedT is constructed.)
  void setDefaults() {
    ON_CALL(static_cast<DerivedT &>(*this),
            run(_, _, testing::Matcher<ExtraArgTs>(_)...))
        .WillByDefault(Return(this->getResult()));
    ON_CALL(static_cast<DerivedT &>(*this), invalidate(_, _, _))
        .WillByDefault(Invoke(&invalidateCallback));
  }
};

template <typename DerivedT, typename IRUnitT, typename AnalysisManagerT,
          typename... ExtraArgTs>
AnalysisKey MockAnalysisHandleBase<DerivedT, IRUnitT, AnalysisManagerT,
                                   ExtraArgTs...>::Analysis::Key;

/// A CRTP base for pass mock handles
///
/// This class reconciles mocking with the value semantics implementation of the
/// PassManager. Pass mock handles should derive from this class and
/// call \c setDefault() in their constroctur for wiring up the defaults defined
/// by this base with their mock run() and invalidate() implementations.
template <typename DerivedT, typename IRUnitT,
          typename AnalysisManagerT = AnalysisManager<IRUnitT>,
          typename... ExtraArgTs>
class MockPassHandleBase {
public:
  class Pass : public PassInfoMixin<Pass> {
    friend MockPassHandleBase;

    DerivedT *Handle;

    Pass(DerivedT &Handle) : Handle(&Handle) {
      static_assert(std::is_base_of<MockPassHandleBase, DerivedT>::value,
                    "Must pass the derived type to this template!");
    }

  public:
    PreservedAnalyses run(IRUnitT &IR, AnalysisManagerT &AM,
                          ExtraArgTs... ExtraArgs) {
      return Handle->run(IR, AM, ExtraArgs...);
    }
  };

  static StringRef getName() { return llvm::getTypeName<DerivedT>(); }

  Pass getPass() { return Pass(static_cast<DerivedT &>(*this)); }

protected:
  /// Derived classes should call this in their constructor to set up default
  /// mock actions. (We can't do this in our constructor because this has to
  /// run after the DerivedT is constructed.)
  void setDefaults() {
    ON_CALL(static_cast<DerivedT &>(*this),
            run(_, _, testing::Matcher<ExtraArgTs>(_)...))
        .WillByDefault(Return(PreservedAnalyses::all()));
  }
};

struct MockFunctionAnalysisHandle
    : MockAnalysisHandleBase<MockFunctionAnalysisHandle, Function> {
  MOCK_METHOD2(run, Analysis::Result(Function &, FunctionAnalysisManager &));

  MOCK_METHOD3(invalidate, bool(Function &, const PreservedAnalyses &,
                                FunctionAnalysisManager::Invalidator &));

  MockFunctionAnalysisHandle() { setDefaults(); }
};

template <size_t I = static_cast<size_t>(-1)>
struct MockLoopNestAnalysisHandleTemplate
    : MockAnalysisHandleBase<MockLoopNestAnalysisHandleTemplate<I>, Loop,
                             LoopAnalysisManager,
                             LoopStandardAnalysisResults &> {
  using Analysis = typename MockLoopNestAnalysisHandleTemplate::Analysis;
  MOCK_METHOD3_T(run, typename Analysis::Result(Loop &, LoopAnalysisManager &,
                                                LoopStandardAnalysisResults &));

  MOCK_METHOD3_T(invalidate, bool(Loop &, const PreservedAnalyses &,
                                  LoopAnalysisManager::Invalidator &));

  MockLoopNestAnalysisHandleTemplate() { this->setDefaults(); }
};

using MockLoopNestAnalysisHandle = MockLoopNestAnalysisHandleTemplate<>;

template <size_t I = static_cast<size_t>(-1)>
struct MockLoopAnalysisHandleTemplate
    : MockAnalysisHandleBase<MockLoopAnalysisHandleTemplate<I>, Loop,
                             LoopAnalysisManager,
                             LoopStandardAnalysisResults &> {
  using Analysis = typename MockLoopAnalysisHandleTemplate::Analysis;
  MOCK_METHOD3_T(run, typename Analysis::Result(Loop &, LoopAnalysisManager &,
                                                LoopStandardAnalysisResults &));

  MOCK_METHOD3_T(invalidate, bool(Loop &, const PreservedAnalyses &,
                                  LoopAnalysisManager::Invalidator &));

  MockLoopAnalysisHandleTemplate() { this->setDefaults(); }
};

using MockLoopAnalysisHandle = MockLoopAnalysisHandleTemplate<>;

struct MockModulePassHandle : MockPassHandleBase<MockModulePassHandle, Module> {
  MOCK_METHOD2(run, PreservedAnalyses(Module &, ModuleAnalysisManager &));

  MockModulePassHandle() { setDefaults(); }
};

struct MockFunctionPassHandle
    : MockPassHandleBase<MockFunctionPassHandle, Function> {
  MOCK_METHOD2(run, PreservedAnalyses(Function &, FunctionAnalysisManager &));

  MockFunctionPassHandle() { setDefaults(); }
};

struct MockLoopNestPassHandle
    : MockPassHandleBase<MockLoopNestPassHandle, LoopNest,
                         LoopNestAnalysisManager, LoopStandardAnalysisResults &,
                         LNPMUpdater &> {
  MOCK_METHOD4(run,
               PreservedAnalyses(LoopNest &, LoopNestAnalysisManager &,
                                 LoopStandardAnalysisResults &, LNPMUpdater &));

  MockLoopNestPassHandle() { setDefaults(); }
};

struct MockLoopPassHandle
    : MockPassHandleBase<MockLoopPassHandle, Loop, LoopAnalysisManager,
                         LoopStandardAnalysisResults &, LPMUpdater &> {
  MOCK_METHOD4(run,
               PreservedAnalyses(Loop &, LoopAnalysisManager &,
                                 LoopStandardAnalysisResults &, LPMUpdater &));

  MockLoopPassHandle() { setDefaults(); }
};

/// Helper for HasName matcher that returns getName both for IRUnit and
/// for IRUnit pointer wrapper into llvm::Any (wrapped by PassInstrumentation).
template <typename IRUnitT> std::string getName(const IRUnitT &IR) {
  return std::string(IR.getName());
}

/// Define a custom matcher for objects which support a 'getName' method.
///
/// LLVM often has IR objects or analysis objects which expose a name
/// and in tests it is convenient to match these by name for readability.
/// Usually, this name is either a StringRef or a plain std::string. This
/// matcher supports any type exposing a getName() method of this form whose
/// return value is compatible with an std::ostream. For StringRef, this uses
/// the shift operator defined above.
///
/// It should be used as:
///
///   HasName("my_function")
///
/// No namespace or other qualification is required.
MATCHER_P(HasName, Name, "") {
  *result_listener << "has name '" << getName(arg) << "'";
  return Name == getName(arg);
}

MATCHER_P(HasNameRegex, Name, "") {
  *result_listener << "has name '" << getName(arg) << "'";
  llvm::Regex R(Name);
  return R.match(getName(arg));
}

std::unique_ptr<Module> parseIR(LLVMContext &C, const char *IR) {
  SMDiagnostic Err;
  return parseAssemblyString(IR, Err, C);
}

class LoopNestPassManagerTest : public ::testing::Test {
protected:
  LLVMContext Context;
  std::unique_ptr<Module> M;

  LoopAnalysisManager LAM;
  LoopNestAnalysisManager LNAM;
  FunctionAnalysisManager FAM;
  ModuleAnalysisManager MAM;

  MockLoopAnalysisHandle MLAHandle;
  MockLoopNestAnalysisHandle MLNAHandle;

  MockLoopPassHandle MLPHandle;
  MockLoopNestPassHandle MLNPHandle;
  MockFunctionPassHandle MFPHandle;
  MockModulePassHandle MMPHandle;

  static PreservedAnalyses
  getLoopNestAnalysisResult(LoopNest &LN, LoopNestAnalysisManager &AM,
                            LoopStandardAnalysisResults &AR, LNPMUpdater &) {
    (void)AM.getResult<MockLoopNestAnalysisHandle::Analysis>(LN, AR);
    return PreservedAnalyses::all();
  }

  static PreservedAnalyses
  getLoopAnalysisResult(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &) {
    (void)AM.getResult<MockLoopAnalysisHandle::Analysis>(L, AR);
    return PreservedAnalyses::all();
  }

public:
  LoopNestPassManagerTest()
      : M(parseIR(
            Context,
            "define void @f(i1* %ptr) {\n"
            "entry:\n"
            "  br label %loop.f.0\n"
            "loop.f.0:\n"
            "  %cond.0 = load volatile i1, i1* %ptr\n"
            "  br i1 %cond.0, label %loop.f.0.0.ph, label %end\n"
            "loop.f.0.0.ph:\n"
            "  br label %loop.f.0.0\n"
            "loop.f.0.0:\n"
            "  %cond.0.0 = load volatile i1, i1* %ptr\n"
            "  br i1 %cond.0.0, label %loop.f.0.0, label %loop.f.0.1.ph\n"
            "loop.f.0.1.ph:\n"
            "  br label %loop.f.0.1\n"
            "loop.f.0.1:\n"
            "  %cond.0.1 = load volatile i1, i1* %ptr\n"
            "  br i1 %cond.0.1, label %loop.f.0.1, label %loop.f.0.latch\n"
            "loop.f.0.latch:\n"
            "  br label %loop.f.0\n"
            "end:\n"
            "  ret void\n"
            "}\n"
            "\n"
            "define void @g(i1* %ptr) {\n"
            "entry:\n"
            "  br label %loop.g.0\n"
            "loop.g.0:\n"
            "  %cond.0 = load volatile i1, i1* %ptr\n"
            "  br i1 %cond.0, label %loop.g.0, label %loop.g.1.ph\n"
            "loop.g.1.ph:\n"
            "  br label %loop.g.1\n"
            "loop.g.1:\n"
            "  %cond.1 = load volatile i1, i1* %ptr\n"
            "  br i1 %cond.1, label %loop.g.1.0.ph, label %end\n"
            "loop.g.1.0.ph:\n"
            "  br label %loop.g.1.0\n"
            "loop.g.1.0:\n"
            "  %cond.1.0 = load volatile i1, i1* %ptr\n"
            "  br i1 %cond.1.0, label %loop.g.1.0, label %loop.g.1.latch\n"
            "loop.g.1.latch:\n"
            "  br label %loop.g.1\n"
            "end:\n"
            "  ret void\n"
            "}\n")),
        LAM(true), LNAM(LAM), FAM(true), MAM(true) {
    // Register mock analysis.
    LNAM.registerPass([&] { return MLNAHandle.getAnalysis(); });
    LAM.registerPass([&] { return MLAHandle.getAnalysis(); });

    // Register loop standard analyses.
    FAM.registerPass([&] { return DominatorTreeAnalysis(); });
    FAM.registerPass([&] { return LoopAnalysis(); });
    FAM.registerPass([&] { return AAManager(); });
    FAM.registerPass([&] { return AssumptionAnalysis(); });
    FAM.registerPass([&] { return ScalarEvolutionAnalysis(); });
    FAM.registerPass([&] { return TargetLibraryAnalysis(); });
    FAM.registerPass([&] { return TargetIRAnalysis(); });
    FAM.registerPass([&] { return MemorySSAAnalysis(); });

    // Register loop nest analysis.
    LNAM.registerPass([&] { return LoopNestAnalysis(); });

    // Register pass instrumentation analysis.
    LAM.registerPass([&] { return PassInstrumentationAnalysis(); });
    LNAM.registerPass([&] { return PassInstrumentationAnalysis(); });
    FAM.registerPass([&] { return PassInstrumentationAnalysis(); });
    MAM.registerPass([&] { return PassInstrumentationAnalysis(); });

    // Cross register analysis manager proxies.
    MAM.registerPass([&] { return FunctionAnalysisManagerModuleProxy(FAM); });
    FAM.registerPass([&] { return ModuleAnalysisManagerFunctionProxy(MAM); });
    FAM.registerPass(
        [&] { return LoopNestAnalysisManagerFunctionProxy(LNAM); });
    FAM.registerPass([&] { return LoopAnalysisManagerFunctionProxy(LAM); });
    LNAM.registerPass(
        [&] { return FunctionAnalysisManagerLoopNestProxy(FAM); });
    LAM.registerPass([&] { return FunctionAnalysisManagerLoopProxy(FAM); });
  }
};

TEST_F(LoopNestPassManagerTest, Basic) {
  ModulePassManager MPM(true);
  ::testing::InSequence MakeExpectationsSequenced;

  // First we first visit all the top-level loop nests in both functions, then
  // the subloops are visited by the loop passes and loop analyses. By
  // definition, the top-level loops will be visited by both kinds of passes and
  // analyses.
  EXPECT_CALL(MLNPHandle, run(HasName("loop.f.0"), _, _, _))
      .WillOnce(Invoke(getLoopNestAnalysisResult));
  EXPECT_CALL(MLNAHandle, run(HasName("loop.f.0"), _, _));
  EXPECT_CALL(MLPHandle, run(HasName("loop.f.0.0"), _, _, _))
      .WillOnce(Invoke(getLoopAnalysisResult));
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0.0"), _, _));
  EXPECT_CALL(MLPHandle, run(HasName("loop.f.0.1"), _, _, _))
      .WillOnce(Invoke(getLoopAnalysisResult));
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0.1"), _, _));
  EXPECT_CALL(MLPHandle, run(HasName("loop.f.0"), _, _, _))
      .WillOnce(Invoke(getLoopAnalysisResult));
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0"), _, _));

  EXPECT_CALL(MLNPHandle, run(HasName("loop.g.0"), _, _, _))
      .WillOnce(Invoke(getLoopNestAnalysisResult));
  EXPECT_CALL(MLNAHandle, run(HasName("loop.g.0"), _, _));
  EXPECT_CALL(MLPHandle, run(HasName("loop.g.0"), _, _, _))
      .WillOnce(Invoke(getLoopAnalysisResult));
  EXPECT_CALL(MLAHandle, run(HasName("loop.g.0"), _, _));

  EXPECT_CALL(MLNPHandle, run(HasName("loop.g.1"), _, _, _))
      .WillOnce(Invoke(getLoopNestAnalysisResult));
  EXPECT_CALL(MLNAHandle, run(HasName("loop.g.1"), _, _));
  EXPECT_CALL(MLPHandle, run(HasName("loop.g.1.0"), _, _, _))
      .WillOnce(Invoke(getLoopAnalysisResult));
  EXPECT_CALL(MLAHandle, run(HasName("loop.g.1.0"), _, _));
  EXPECT_CALL(MLPHandle, run(HasName("loop.g.1"), _, _, _))
      .WillOnce(Invoke(getLoopAnalysisResult));
  EXPECT_CALL(MLAHandle, run(HasName("loop.g.1"), _, _));

  {
    LoopPassManager LPM(true);
    LPM.addPass(MLPHandle.getPass());
    LoopNestPassManager LNPM(true);
    LNPM.addPass(MLNPHandle.getPass());
    LNPM.addPass(createLoopNestToLoopPassAdaptor(std::move(LPM)));
    FunctionPassManager FPM(true);
    FPM.addPass(createFunctionToLoopNestPassAdaptor(std::move(LNPM)));
    MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
  }

  // Next we reverse the order of loop pass and loop nest pass. The analyses are
  // perserved and hence never run.
  EXPECT_CALL(MLPHandle, run(HasName("loop.f.0.0"), _, _, _))
      .WillOnce(Invoke(getLoopAnalysisResult));
  EXPECT_CALL(MLPHandle, run(HasName("loop.f.0.1"), _, _, _))
      .WillOnce(Invoke(getLoopAnalysisResult));
  EXPECT_CALL(MLPHandle, run(HasName("loop.f.0"), _, _, _))
      .WillOnce(Invoke(getLoopAnalysisResult));
  EXPECT_CALL(MLNPHandle, run(HasName("loop.f.0"), _, _, _))
      .WillOnce(Invoke(getLoopNestAnalysisResult));

  EXPECT_CALL(MLPHandle, run(HasName("loop.g.0"), _, _, _))
      .WillOnce(Invoke(getLoopAnalysisResult));
  EXPECT_CALL(MLNPHandle, run(HasName("loop.g.0"), _, _, _))
      .WillOnce(Invoke(getLoopNestAnalysisResult));

  EXPECT_CALL(MLPHandle, run(HasName("loop.g.1.0"), _, _, _))
      .WillOnce(Invoke(getLoopAnalysisResult));
  EXPECT_CALL(MLPHandle, run(HasName("loop.g.1"), _, _, _))
      .WillOnce(Invoke(getLoopAnalysisResult));
  EXPECT_CALL(MLNPHandle, run(HasName("loop.g.1"), _, _, _))
      .WillOnce(Invoke(getLoopNestAnalysisResult));

  {
    LoopPassManager LPM(true);
    LPM.addPass(MLPHandle.getPass());
    LoopNestPassManager LNPM(true);
    LNPM.addPass(createLoopNestToLoopPassAdaptor(std::move(LPM)));
    LNPM.addPass(MLNPHandle.getPass());
    FunctionPassManager FPM(true);
    FPM.addPass(createFunctionToLoopNestPassAdaptor(std::move(LNPM)));
    MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
  }

  EXPECT_CALL(MLNPHandle, run(HasName("loop.f.0"), _, _, _))
      .WillOnce(Return(PreservedAnalyses::none()))
      .WillOnce(Invoke(getLoopNestAnalysisResult));
  EXPECT_CALL(MLNAHandle, run(HasName("loop.f.0"), _, _));

  EXPECT_CALL(MLNPHandle, run(HasName("loop.g.0"), _, _, _))
      .WillOnce(DoDefault())
      .WillOnce(Invoke(getLoopNestAnalysisResult));
  EXPECT_CALL(MLNPHandle, run(HasName("loop.g.1"), _, _, _))
      .WillOnce(Invoke(getLoopNestAnalysisResult))
      .WillOnce(Return(PreservedAnalyses::none()));

  {
    LoopNestPassManager LNPM(true);
    LNPM.addPass(MLNPHandle.getPass());
    LNPM.addPass(MLNPHandle.getPass());
    FunctionPassManager FPM(true);
    FPM.addPass(createFunctionToLoopNestPassAdaptor(std::move(LNPM)));
    MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
  }

  // Inner loops should not be visited by loop nest passes and analyses at all.
  EXPECT_CALL(MLNPHandle, run(HasName("loop.f.0.0"), _, _, _)).Times(0);
  EXPECT_CALL(MLNAHandle, run(HasName("loop.f.0.0"), _, _)).Times(0);
  EXPECT_CALL(MLNPHandle, run(HasName("loop.f.0.1"), _, _, _)).Times(0);
  EXPECT_CALL(MLNAHandle, run(HasName("loop.f.0.1"), _, _)).Times(0);
  EXPECT_CALL(MLNPHandle, run(HasName("loop.g.1.0"), _, _, _)).Times(0);
  EXPECT_CALL(MLNAHandle, run(HasName("loop.g.1.0"), _, _)).Times(0);

  MPM.run(*M, MAM);
}

TEST_F(LoopNestPassManagerTest, DeleteTopLevelLoops) {
  ::testing::InSequence MakeExpectationsSequenced;

  EXPECT_CALL(MLPHandle, run(HasName("loop.f.0.0"), _, _, _));
  EXPECT_CALL(MLPHandle, run(HasName("loop.f.0.1"), _, _, _));
  // We mark the top-level loop as deleted in the loop pass, so the loop nest
  // pass manager should skip the loop nest.
  EXPECT_CALL(MLPHandle, run(HasName("loop.f.0"), _, _, _))
      .WillOnce(WithArgs<0, 3>(Invoke([&](Loop &L, LPMUpdater &U) {
        U.markLoopAsDeleted(L, L.getName());
        return PreservedAnalyses::all();
      })));

  EXPECT_CALL(MLNPHandle, run(HasName("loop.f.0"), _, _, _)).Times(0);

  EXPECT_CALL(MLPHandle, run(HasName("loop.g.0"), _, _, _));
  EXPECT_CALL(MLNPHandle, run(HasName("loop.g.0"), _, _, _));
  // The inner loop is mark as deleted, but it does not affect whether the loop
  // nest pass should be run.
  EXPECT_CALL(MLPHandle, run(HasName("loop.g.1.0"), _, _, _))
      .WillOnce(WithArgs<0, 3>(Invoke([&](Loop &L, LPMUpdater &U) {
        U.markLoopAsDeleted(L, L.getName());
        return PreservedAnalyses::all();
      })));
  EXPECT_CALL(MLPHandle, run(HasName("loop.g.1"), _, _, _));
  EXPECT_CALL(MLNPHandle, run(HasName("loop.g.1"), _, _, _));

  ModulePassManager MPM(true);
  LoopPassManager LPM(true);
  LPM.addPass(MLPHandle.getPass());
  LoopNestPassManager LNPM(true);
  LNPM.addPass(createLoopNestToLoopPassAdaptor(std::move(LPM)));
  LNPM.addPass(MLNPHandle.getPass());
  FunctionPassManager FPM(true);
  FPM.addPass(createFunctionToLoopNestPassAdaptor(std::move(LNPM)));
  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));

  MPM.run(*M, MAM);
}

TEST_F(LoopNestPassManagerTest, FunctionPassInvalidationOfLoopNestAnalyses) {
  ::testing::Sequence FSequence, GSequence;

  // First, force the analysis result to be computed for each loop nest.
  EXPECT_CALL(MLNAHandle, run(HasName("loop.f.0"), _, _)).InSequence(FSequence);
  EXPECT_CALL(MLNAHandle, run(HasName("loop.g.0"), _, _)).InSequence(GSequence);
  EXPECT_CALL(MLNAHandle, run(HasName("loop.g.1"), _, _)).InSequence(GSequence);

  FunctionPassManager FPM(true);
  FPM.addPass(createFunctionToLoopNestPassAdaptor(
      RequireAnalysisLoopNestPass<MockLoopNestAnalysisHandle::Analysis>()));

  // No need to re-run if we require again from a fresh loop nest pass manager.
  FPM.addPass(createFunctionToLoopNestPassAdaptor(
      RequireAnalysisLoopNestPass<MockLoopNestAnalysisHandle::Analysis>()));

  // All analyses are invalidated (the proxy in particular). In this case the
  // LoopAnalysisManager (LoopNestAnalysisManager) will be cleared, so the
  // invalidation will not happen.
  EXPECT_CALL(MFPHandle, run(HasName("f"), _))
      .InSequence(FSequence)
      .WillOnce(Return(PreservedAnalyses::none()));

  // Only the MockLoopNestAnalysisHandle::Analysis is invalidated.
  PreservedAnalyses PA = getLoopPassPreservedAnalyses();
  if (EnableMSSALoopDependency)
    PA.preserve<MemorySSAAnalysis>();

  EXPECT_CALL(MFPHandle, run(HasName("g"), _))
      .InSequence(GSequence)
      .WillOnce(Return(PA));
  // The analysis result is not invalidated on loop.g.0, so no need to re-run.
  EXPECT_CALL(MLNAHandle, invalidate(HasName("loop.g.0"), _, _))
      .InSequence(GSequence)
      .WillOnce(Return(false));
  EXPECT_CALL(MLNAHandle, invalidate(HasName("loop.g.1"), _, _))
      .InSequence(GSequence);

  EXPECT_CALL(MLNAHandle, run(HasName("loop.f.0"), _, _)).InSequence(FSequence);
  EXPECT_CALL(MLNAHandle, run(HasName("loop.g.1"), _, _)).InSequence(GSequence);

  FPM.addPass(MFPHandle.getPass());
  FPM.addPass(createFunctionToLoopNestPassAdaptor(
      RequireAnalysisLoopNestPass<MockLoopNestAnalysisHandle::Analysis>()));

  ModulePassManager MPM(true);
  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));

  MPM.addPass(
      createModuleToFunctionPassAdaptor(createFunctionToLoopNestPassAdaptor(
          RequireAnalysisLoopNestPass<
              MockLoopNestAnalysisHandle::Analysis>())));

  MPM.run(*M, MAM);
}

TEST_F(LoopNestPassManagerTest, LoopNestPassInvalidationOfLoopAnalyses) {
  ::testing::Sequence FSequence, G0Sequence, G1Sequence;

  // First, force the analysis result to be computed for each loop.
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0.0"), _, _))
      .InSequence(FSequence);
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0.1"), _, _))
      .InSequence(FSequence);
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0"), _, _)).InSequence(FSequence);

  EXPECT_CALL(MLAHandle, run(HasName("loop.g.0"), _, _)).InSequence(G0Sequence);
  EXPECT_CALL(MLAHandle, run(HasName("loop.g.1.0"), _, _))
      .InSequence(G1Sequence);
  EXPECT_CALL(MLAHandle, run(HasName("loop.g.1"), _, _)).InSequence(G1Sequence);

  LoopNestPassManager LNPM(true);
  LNPM.addPass(createLoopNestToLoopPassAdaptor(
      RequireAnalysisLoopPass<MockLoopAnalysisHandle::Analysis>()));

  // No need to re-run if we require again from a fresh loop pass manager.
  LNPM.addPass(createLoopNestToLoopPassAdaptor(
      RequireAnalysisLoopPass<MockLoopAnalysisHandle::Analysis>()));

  EXPECT_CALL(MLNPHandle, run(HasName("loop.f.0"), _, _, _))
      .InSequence(FSequence)
      .WillOnce(Return(PreservedAnalyses::none()));
  EXPECT_CALL(MLAHandle, invalidate(HasName("loop.f.0.0"), _, _))
      .InSequence(FSequence);
  EXPECT_CALL(MLAHandle, invalidate(HasName("loop.f.0.1"), _, _))
      .InSequence(FSequence)
      .WillOnce(Return(false));
  EXPECT_CALL(MLAHandle, invalidate(HasName("loop.f.0"), _, _))
      .InSequence(FSequence);

  // Only the MockLoopAnalysisHandle::Analysis is invalidated.
  PreservedAnalyses PA = getLoopPassPreservedAnalyses();
  if (EnableMSSALoopDependency)
    PA.preserve<MemorySSAAnalysis>();

  EXPECT_CALL(MLNPHandle, run(HasName("loop.g.0"), _, _, _))
      .InSequence(G0Sequence)
      .WillOnce(Return(PA));
  EXPECT_CALL(MLAHandle, invalidate(HasName("loop.g.0"), _, _))
      .InSequence(G0Sequence);
  EXPECT_CALL(MLNPHandle, run(HasName("loop.g.1"), _, _, _))
      .InSequence(G1Sequence);
  LNPM.addPass(MLNPHandle.getPass());

  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0.0"), _, _))
      .InSequence(FSequence);
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0"), _, _)).InSequence(FSequence);
  EXPECT_CALL(MLAHandle, run(HasName("loop.g.0"), _, _)).InSequence(G0Sequence);

  LNPM.addPass(createLoopNestToLoopPassAdaptor(
      RequireAnalysisLoopPass<MockLoopAnalysisHandle::Analysis>()));

  ModulePassManager MPM(true);
  MPM.addPass(createModuleToFunctionPassAdaptor(
      createFunctionToLoopNestPassAdaptor(std::move(LNPM))));
  MPM.addPass(createModuleToFunctionPassAdaptor(createFunctionToLoopPassAdaptor(
      RequireAnalysisLoopPass<MockLoopAnalysisHandle::Analysis>())));
  MPM.run(*M, MAM);
}

TEST_F(LoopNestPassManagerTest, ModulePassInvalidationOfLoopNestAnalyses) {
  ModulePassManager MPM(true);
  ::testing::InSequence MakeExpectationsSequenced;

  EXPECT_CALL(MLNAHandle, run(HasName("loop.f.0"), _, _));
  EXPECT_CALL(MLNAHandle, run(HasName("loop.g.0"), _, _));
  EXPECT_CALL(MLNAHandle, run(HasName("loop.g.1"), _, _));
  MPM.addPass(
      createModuleToFunctionPassAdaptor(createFunctionToLoopNestPassAdaptor(
          RequireAnalysisLoopNestPass<
              MockLoopNestAnalysisHandle::Analysis>())));
  MPM.addPass(
      createModuleToFunctionPassAdaptor(createFunctionToLoopNestPassAdaptor(
          RequireAnalysisLoopNestPass<
              MockLoopNestAnalysisHandle::Analysis>())));

  EXPECT_CALL(MMPHandle, run(_, _)).WillOnce(InvokeWithoutArgs([] {
    auto PA = getLoopPassPreservedAnalyses();
    PA.preserve<FunctionAnalysisManagerModuleProxy>();
    if (EnableMSSALoopDependency)
      PA.preserve<MemorySSAAnalysis>();
    return PA;
  }));

  EXPECT_CALL(MLNAHandle, invalidate(HasName("loop.f.0"), _, _));
  EXPECT_CALL(MLNAHandle, invalidate(HasName("loop.g.0"), _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(MLNAHandle, invalidate(HasName("loop.g.1"), _, _));

  EXPECT_CALL(MLNAHandle, run(HasName("loop.f.0"), _, _));
  EXPECT_CALL(MLNAHandle, run(HasName("loop.g.1"), _, _));

  MPM.addPass(MMPHandle.getPass());
  MPM.addPass(
      createModuleToFunctionPassAdaptor(createFunctionToLoopNestPassAdaptor(
          RequireAnalysisLoopNestPass<
              MockLoopNestAnalysisHandle::Analysis>())));
  MPM.addPass(
      createModuleToFunctionPassAdaptor(createFunctionToLoopNestPassAdaptor(
          RequireAnalysisLoopNestPass<
              MockLoopNestAnalysisHandle::Analysis>())));
  EXPECT_CALL(MMPHandle, run(_, _)).WillOnce(InvokeWithoutArgs([] {
    auto PA = PreservedAnalyses::none();
    PA.preserveSet<AllAnalysesOn<Function>>();
    PA.preserveSet<AllAnalysesOn<LoopNest>>();
    return PA;
  }));

  EXPECT_CALL(MLNAHandle, run(HasName("loop.f.0"), _, _));
  EXPECT_CALL(MLNAHandle, run(HasName("loop.g.0"), _, _));
  EXPECT_CALL(MLNAHandle, run(HasName("loop.g.1"), _, _));
  MPM.addPass(MMPHandle.getPass());
  MPM.addPass(
      createModuleToFunctionPassAdaptor(createFunctionToLoopNestPassAdaptor(
          RequireAnalysisLoopNestPass<
              MockLoopNestAnalysisHandle::Analysis>())));

  MPM.run(*M, MAM);
}

// Test that if a loop analysis is not preserved in the LoopNestPass, the
// analysis results on the corresponding subtree will all be invalidated.
TEST_F(LoopNestPassManagerTest, InvalidationOfLoopAnalysesInSubtree) {
  ::testing::Sequence F0Sequence, G0Sequence, G1Sequence;

  // Register two kinds of loop analyses.
  enum : size_t { A, B };
  MockLoopAnalysisHandleTemplate<A> MLAHandleA;
  MockLoopAnalysisHandleTemplate<B> MLAHandleB;

  LAM.registerPass([&] { return MLAHandleA.getAnalysis(); });
  LAM.registerPass([&] { return MLAHandleB.getAnalysis(); });

  // Force both kinds of analyses to be cached on all loops.
  EXPECT_CALL(MLAHandleA, run(HasName("loop.f.0.0"), _, _))
      .InSequence(F0Sequence);
  EXPECT_CALL(MLAHandleA, run(HasName("loop.f.0.1"), _, _))
      .InSequence(F0Sequence);
  EXPECT_CALL(MLAHandleA, run(HasName("loop.f.0"), _, _))
      .InSequence(F0Sequence);

  EXPECT_CALL(MLAHandleB, run(HasName("loop.f.0.0"), _, _))
      .InSequence(F0Sequence);
  EXPECT_CALL(MLAHandleB, run(HasName("loop.f.0.1"), _, _))
      .InSequence(F0Sequence);
  EXPECT_CALL(MLAHandleB, run(HasName("loop.f.0"), _, _))
      .InSequence(F0Sequence);

  EXPECT_CALL(MLAHandleA, run(HasName("loop.g.0"), _, _))
      .InSequence(G0Sequence);
  EXPECT_CALL(MLAHandleA, run(HasName("loop.g.1.0"), _, _))
      .InSequence(G1Sequence);
  EXPECT_CALL(MLAHandleA, run(HasName("loop.g.1"), _, _))
      .InSequence(G1Sequence);

  EXPECT_CALL(MLAHandleB, run(HasName("loop.g.0"), _, _))
      .InSequence(G0Sequence);
  EXPECT_CALL(MLAHandleB, run(HasName("loop.g.1.0"), _, _))
      .InSequence(G1Sequence);
  EXPECT_CALL(MLAHandleB, run(HasName("loop.g.1"), _, _))
      .InSequence(G1Sequence);

  LoopNestPassManager LNPM(true);
  LNPM.addPass(createLoopNestToLoopPassAdaptor(
      RequireAnalysisLoopPass<MockLoopAnalysisHandleTemplate<A>::Analysis>()));
  LNPM.addPass(createLoopNestToLoopPassAdaptor(
      RequireAnalysisLoopPass<MockLoopAnalysisHandleTemplate<B>::Analysis>()));

  EXPECT_CALL(MLNPHandle, run(HasName("loop.f.0"), _, _, _))
      .InSequence(F0Sequence);
  EXPECT_CALL(MLNPHandle, run(HasName("loop.g.0"), _, _, _))
      .InSequence(G0Sequence);
  EXPECT_CALL(MLNPHandle, run(HasName("loop.g.1"), _, _, _))
      .InSequence(G1Sequence);

  LNPM.addPass(MLNPHandle.getPass());

  // The analyses results should be cached and the analysis passes don't have to
  // be executed again after the loop nest pass.
  LNPM.addPass(createLoopNestToLoopPassAdaptor(
      RequireAnalysisLoopPass<MockLoopAnalysisHandleTemplate<A>::Analysis>()));
  LNPM.addPass(createLoopNestToLoopPassAdaptor(
      RequireAnalysisLoopPass<MockLoopAnalysisHandleTemplate<B>::Analysis>()));

  // The loop nest pass does not preserve loop analysis B on loop nest f.0,
  // which means that analysis B will indeed be invalidated on loops f.0.0,
  // f.0.1, and f.0.
  EXPECT_CALL(MLNPHandle, run(HasName("loop.f.0"), _, _, _))
      .InSequence(F0Sequence)
      .WillOnce(InvokeWithoutArgs([] {
        PreservedAnalyses PA;
        PA.preserveSet<AllAnalysesOn<Function>>();
        PA.preserve<MockLoopAnalysisHandleTemplate<A>::Analysis>();
        return PA;
      }));
  EXPECT_CALL(MLAHandleB, invalidate(HasName("loop.f.0.0"), _, _))
      .InSequence(F0Sequence);
  // Returns false on purpose: the analysis pass should be skipped.
  EXPECT_CALL(MLAHandleB, invalidate(HasName("loop.f.0.1"), _, _))
      .InSequence(F0Sequence)
      .WillOnce(Return(false));
  EXPECT_CALL(MLAHandleB, invalidate(HasName("loop.f.0"), _, _))
      .InSequence(F0Sequence);

  EXPECT_CALL(MLAHandleA, invalidate(HasName("loop.f.0.0"), _, _));
  EXPECT_CALL(MLAHandleA, invalidate(HasName("loop.f.0.1"), _, _));
  EXPECT_CALL(MLAHandleA, invalidate(HasName("loop.f.0"), _, _));

  // On loop nest g.0, although both analysis A and B are preserved, the
  // `invalidation` method will still be invoked since `AllAnalysesOn<Loop>` is
  // not preserved. However, the analysis results are still valid so no need to
  // re-run analysis passes in this case.
  EXPECT_CALL(MLNPHandle, run(HasName("loop.g.0"), _, _, _))
      .InSequence(G0Sequence)
      .WillOnce(InvokeWithoutArgs([] {
        PreservedAnalyses PA;
        PA.preserveSet<AllAnalysesOn<Function>>();
        PA.preserve<MockLoopAnalysisHandleTemplate<A>::Analysis>();
        PA.preserve<MockLoopAnalysisHandleTemplate<B>::Analysis>();
        return PA;
      }));

  EXPECT_CALL(MLAHandleA, invalidate(HasName("loop.g.0"), _, _));
  EXPECT_CALL(MLAHandleB, invalidate(HasName("loop.g.0"), _, _));

  // On loop nest g.1, all loop analyses are marked as preserved. In this case,
  // the `invalidation` method of the subloops will not be called.
  EXPECT_CALL(MLNPHandle, run(HasName("loop.g.1"), _, _, _))
      .InSequence(G1Sequence)
      .WillOnce(InvokeWithoutArgs([] {
        PreservedAnalyses PA;
        PA.preserveSet<AllAnalysesOn<Function>>();
        PA.preserveSet<AllAnalysesOn<Loop>>();
        return PA;
      }));

  EXPECT_CALL(MLAHandleA, invalidate(HasName("loop.g.1"), _, _)).Times(0);
  EXPECT_CALL(MLAHandleA, invalidate(HasName("loop.g.1.0"), _, _)).Times(0);
  EXPECT_CALL(MLAHandleB, invalidate(HasName("loop.g.1"), _, _)).Times(0);
  EXPECT_CALL(MLAHandleB, invalidate(HasName("loop.g.1.0"), _, _)).Times(0);

  LNPM.addPass(MLNPHandle.getPass());

  // On loop nest f.0, only analysis pass B will be re-run.
  EXPECT_CALL(MLAHandleB, run(HasName("loop.f.0.0"), _, _))
      .InSequence(F0Sequence);
  EXPECT_CALL(MLAHandleB, run(HasName("loop.f.0"), _, _))
      .InSequence(F0Sequence);

  LNPM.addPass(createLoopNestToLoopPassAdaptor(
      RequireAnalysisLoopPass<MockLoopAnalysisHandleTemplate<A>::Analysis>()));
  LNPM.addPass(createLoopNestToLoopPassAdaptor(
      RequireAnalysisLoopPass<MockLoopAnalysisHandleTemplate<B>::Analysis>()));

  ModulePassManager MPM(true);
  MPM.addPass(createModuleToFunctionPassAdaptor(
      createFunctionToLoopNestPassAdaptor(std::move(LNPM))));

  MPM.run(*M, MAM);
}

} // namespace
