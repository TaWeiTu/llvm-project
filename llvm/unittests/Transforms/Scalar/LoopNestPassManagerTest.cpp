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
        LAM(true), LNAM(LAM, true), FAM(true), MAM(true) {
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

// Make sure that the IR is parsed correctly.
TEST_F(LoopNestPassManagerTest, ParseIR) {
  Function &F = *M->begin();
  ASSERT_THAT(F, HasName("f"));
  auto FBBI = F.begin();
  BasicBlock &FEntry = *FBBI++;
  ASSERT_THAT(FEntry, HasName("entry"));
  BasicBlock &LoopF0BB = *FBBI++;
  ASSERT_THAT(LoopF0BB, HasName("loop.f.0"));
  BasicBlock &LoopF00PHBB = *FBBI++;
  ASSERT_THAT(LoopF00PHBB, HasName("loop.f.0.0.ph"));
  BasicBlock &LoopF00BB = *FBBI++;
  ASSERT_THAT(LoopF00BB, HasName("loop.f.0.0"));
  BasicBlock &LoopF01PHBB = *FBBI++;
  ASSERT_THAT(LoopF01PHBB, HasName("loop.f.0.1.ph"));
  BasicBlock &LoopF01BB = *FBBI++;
  ASSERT_THAT(LoopF01BB, HasName("loop.f.0.1"));
  BasicBlock &LoopF0LatchBB = *FBBI++;
  ASSERT_THAT(LoopF0LatchBB, HasName("loop.f.0.latch"));
  BasicBlock &FEnd = *FBBI++;
  ASSERT_THAT(FEnd, HasName("end"));
  ASSERT_THAT(FBBI, F.end());

  Function &G = *std::next(M->begin());
  ASSERT_THAT(G, HasName("g"));
  auto GBBI = G.begin();
  BasicBlock &GEntry = *GBBI++;
  ASSERT_THAT(GEntry, HasName("entry"));
  BasicBlock &LoopG0BB = *GBBI++;
  ASSERT_THAT(LoopG0BB, HasName("loop.g.0"));
  BasicBlock &LoopG1PHBB = *GBBI++;
  ASSERT_THAT(LoopG1PHBB, HasName("loop.g.1.ph"));
  BasicBlock &LoopG1BB = *GBBI++;
  ASSERT_THAT(LoopG1BB, HasName("loop.g.1"));
  BasicBlock &LoopG10PHBB = *GBBI++;
  ASSERT_THAT(LoopG10PHBB, HasName("loop.g.1.0.ph"));
  BasicBlock &LoopG10BB = *GBBI++;
  ASSERT_THAT(LoopG10BB, HasName("loop.g.1.0"));
  BasicBlock &LoopG1Latch = *GBBI++;
  ASSERT_THAT(LoopG1Latch, HasName("loop.g.1.latch"));
  BasicBlock &GEnd = *GBBI++;
  ASSERT_THAT(GEnd, HasName("end"));
  ASSERT_THAT(GBBI, G.end());
}

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

// Test that if the top-level loop is marked as deleted by the loop pass, the
// adaptor should mark the loop nest as deleted as well.
TEST_F(LoopNestPassManagerTest, DeletionOfTopLevelLoops) {
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
  PA.preserve<LoopNestAnalysisManagerFunctionProxy>();
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
    PA.preserve<LoopNestAnalysisManagerFunctionProxy>();
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

// Test that if any of the bundled analyses provided in the LNPM's signature
// become invalid, the analysis proxy itself becomes invalid and we clear all
// loop nest analysis and loop analysis results.
TEST_F(LoopNestPassManagerTest, InvalidationOfBoundedAnalyses) {
  ModulePassManager MPM(true);
  FunctionPassManager FPM(true);
  ::testing::InSequence MakeExpectationsSequenced;

  // First, force the analysis result to be computed for each loop nest.
  EXPECT_CALL(MLNAHandle, run(HasName("loop.f.0"), _, _));
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0.0"), _, _));
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0.1"), _, _));
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0"), _, _));
  FPM.addPass(createFunctionToLoopNestPassAdaptor(
      RequireAnalysisLoopNestPass<MockLoopNestAnalysisHandle::Analysis>()));
  FPM.addPass(
      createFunctionToLoopNestPassAdaptor(createLoopNestToLoopPassAdaptor(
          RequireAnalysisLoopPass<MockLoopAnalysisHandle::Analysis>())));

  // No need to re-run if we require again from a fresh loop nest pass manager.
  FPM.addPass(createFunctionToLoopNestPassAdaptor(
      RequireAnalysisLoopNestPass<MockLoopNestAnalysisHandle::Analysis>()));
  FPM.addPass(
      createFunctionToLoopNestPassAdaptor(createLoopNestToLoopPassAdaptor(
          RequireAnalysisLoopPass<MockLoopAnalysisHandle::Analysis>())));

  // Preserving everything but the loop analyses themselves results in
  // invalidation and running.
  EXPECT_CALL(MFPHandle, run(HasName("f"), _)).WillOnce(InvokeWithoutArgs([] {
    PreservedAnalyses PA = getLoopPassPreservedAnalyses();
    PA.preserve<LoopNestAnalysisManagerFunctionProxy>();
    return PA;
  }));
  EXPECT_CALL(MLNAHandle, run(HasName("loop.f.0"), _, _));
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0.0"), _, _));
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0.1"), _, _));
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0"), _, _));
  FPM.addPass(MFPHandle.getPass());
  FPM.addPass(createFunctionToLoopNestPassAdaptor(
      RequireAnalysisLoopNestPass<MockLoopNestAnalysisHandle::Analysis>()));
  FPM.addPass(
      createFunctionToLoopNestPassAdaptor(createLoopNestToLoopPassAdaptor(
          RequireAnalysisLoopPass<MockLoopAnalysisHandle::Analysis>())));

  EXPECT_CALL(MFPHandle, run(HasName("f"), _)).WillOnce(InvokeWithoutArgs([] {
    PreservedAnalyses PA = getLoopPassPreservedAnalyses();
    PA.preserve<LoopNestAnalysisManagerFunctionProxy>();
    PA.preserve<MockLoopNestAnalysisHandle::Analysis>();
    return PA;
  }));
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0.0"), _, _));
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0.1"), _, _));
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0"), _, _));
  FPM.addPass(MFPHandle.getPass());
  FPM.addPass(createFunctionToLoopNestPassAdaptor(
      RequireAnalysisLoopNestPass<MockLoopNestAnalysisHandle::Analysis>()));
  FPM.addPass(
      createFunctionToLoopNestPassAdaptor(createLoopNestToLoopPassAdaptor(
          RequireAnalysisLoopPass<MockLoopAnalysisHandle::Analysis>())));

  EXPECT_CALL(MFPHandle, run(HasName("f"), _)).WillOnce(InvokeWithoutArgs([] {
    PreservedAnalyses PA = getLoopPassPreservedAnalyses();
    PA.preserve<LoopNestAnalysisManagerFunctionProxy>();
    PA.preserve<MockLoopAnalysisHandle::Analysis>();
    return PA;
  }));
  EXPECT_CALL(MLNAHandle, run(HasName("loop.f.0"), _, _));
  FPM.addPass(MFPHandle.getPass());
  FPM.addPass(createFunctionToLoopNestPassAdaptor(
      RequireAnalysisLoopNestPass<MockLoopNestAnalysisHandle::Analysis>()));
  FPM.addPass(
      createFunctionToLoopNestPassAdaptor(createLoopNestToLoopPassAdaptor(
          RequireAnalysisLoopPass<MockLoopAnalysisHandle::Analysis>())));

  // The rest don't invalidate analyses, they only trigger re-runs because we
  // clear the cache completely.
  EXPECT_CALL(MFPHandle, run(HasName("f"), _)).WillOnce(InvokeWithoutArgs([] {
    auto PA = PreservedAnalyses::none();
    PA.preserve<LoopNestAnalysisManagerFunctionProxy>();
    // Abandon `AAManager`.
    PA.abandon<AAManager>();
    PA.preserve<AssumptionAnalysis>();
    PA.preserve<DominatorTreeAnalysis>();
    PA.preserve<LoopAnalysis>();
    PA.preserve<LoopAnalysisManagerFunctionProxy>();
    PA.preserve<ScalarEvolutionAnalysis>();
    PA.preserve<MockLoopAnalysisHandle::Analysis>();
    PA.preserve<MockLoopNestAnalysisHandle::Analysis>();
    return PA;
  }));
  EXPECT_CALL(MLNAHandle, run(HasName("loop.f.0"), _, _));
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0.0"), _, _));
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0.1"), _, _));
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0"), _, _));
  FPM.addPass(MFPHandle.getPass());
  FPM.addPass(createFunctionToLoopNestPassAdaptor(
      RequireAnalysisLoopNestPass<MockLoopNestAnalysisHandle::Analysis>()));
  FPM.addPass(
      createFunctionToLoopNestPassAdaptor(createLoopNestToLoopPassAdaptor(
          RequireAnalysisLoopPass<MockLoopAnalysisHandle::Analysis>())));

  EXPECT_CALL(MFPHandle, run(HasName("f"), _)).WillOnce(InvokeWithoutArgs([] {
    auto PA = PreservedAnalyses::none();
    PA.preserve<LoopNestAnalysisManagerFunctionProxy>();
    PA.preserve<AAManager>();
    // Not preserving `AssumptionAnalysis`.
    PA.preserve<DominatorTreeAnalysis>();
    PA.preserve<LoopAnalysis>();
    PA.preserve<LoopAnalysisManagerFunctionProxy>();
    PA.preserve<ScalarEvolutionAnalysis>();
    PA.preserve<ScalarEvolutionAnalysis>();
    PA.preserve<MockLoopAnalysisHandle::Analysis>();
    PA.preserve<MockLoopNestAnalysisHandle::Analysis>();
    return PA;
  }));
  // Special case: AssumptionAnalysis will never be invalidated.
  FPM.addPass(MFPHandle.getPass());
  FPM.addPass(createFunctionToLoopNestPassAdaptor(
      RequireAnalysisLoopNestPass<MockLoopNestAnalysisHandle::Analysis>()));
  FPM.addPass(
      createFunctionToLoopNestPassAdaptor(createLoopNestToLoopPassAdaptor(
          RequireAnalysisLoopPass<MockLoopAnalysisHandle::Analysis>())));

  EXPECT_CALL(MFPHandle, run(HasName("f"), _)).WillOnce(InvokeWithoutArgs([] {
    auto PA = PreservedAnalyses::none();
    PA.preserve<LoopNestAnalysisManagerFunctionProxy>();
    PA.preserve<AAManager>();
    PA.preserve<AssumptionAnalysis>();
    // Abandon `DominatorTreeAnalysis`.
    PA.abandon<DominatorTreeAnalysis>();
    PA.preserve<LoopAnalysis>();
    PA.preserve<LoopAnalysisManagerFunctionProxy>();
    PA.preserve<ScalarEvolutionAnalysis>();
    PA.preserve<MockLoopAnalysisHandle::Analysis>();
    PA.preserve<MockLoopNestAnalysisHandle::Analysis>();
    return PA;
  }));
  EXPECT_CALL(MLNAHandle, run(HasName("loop.f.0"), _, _));
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0.0"), _, _));
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0.1"), _, _));
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0"), _, _));
  FPM.addPass(MFPHandle.getPass());
  FPM.addPass(createFunctionToLoopNestPassAdaptor(
      RequireAnalysisLoopNestPass<MockLoopNestAnalysisHandle::Analysis>()));
  FPM.addPass(
      createFunctionToLoopNestPassAdaptor(createLoopNestToLoopPassAdaptor(
          RequireAnalysisLoopPass<MockLoopAnalysisHandle::Analysis>())));

  EXPECT_CALL(MFPHandle, run(HasName("f"), _)).WillOnce(InvokeWithoutArgs([] {
    auto PA = PreservedAnalyses::none();
    PA.preserve<LoopNestAnalysisManagerFunctionProxy>();
    PA.preserve<AAManager>();
    PA.preserve<AssumptionAnalysis>();
    PA.preserve<DominatorTreeAnalysis>();
    // Abandon the `LoopAnalysis`.
    PA.abandon<LoopAnalysis>();
    PA.preserve<LoopAnalysisManagerFunctionProxy>();
    PA.preserve<ScalarEvolutionAnalysis>();
    PA.preserve<MockLoopAnalysisHandle::Analysis>();
    PA.preserve<MockLoopNestAnalysisHandle::Analysis>();
    return PA;
  }));
  EXPECT_CALL(MLNAHandle, run(HasName("loop.f.0"), _, _));
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0.0"), _, _));
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0.1"), _, _));
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0"), _, _));
  FPM.addPass(MFPHandle.getPass());
  FPM.addPass(createFunctionToLoopNestPassAdaptor(
      RequireAnalysisLoopNestPass<MockLoopNestAnalysisHandle::Analysis>()));
  FPM.addPass(
      createFunctionToLoopNestPassAdaptor(createLoopNestToLoopPassAdaptor(
          RequireAnalysisLoopPass<MockLoopAnalysisHandle::Analysis>())));

  EXPECT_CALL(MFPHandle, run(HasName("f"), _)).WillOnce(InvokeWithoutArgs([] {
    auto PA = PreservedAnalyses::none();
    PA.preserve<AAManager>();
    PA.preserve<AssumptionAnalysis>();
    PA.preserve<DominatorTreeAnalysis>();
    PA.preserve<LoopAnalysis>();
    // Abandon the `LoopNestAnalysisManagerFunctionProxy`.
    PA.abandon<LoopAnalysisManagerFunctionProxy>();
    PA.preserve<ScalarEvolutionAnalysis>();
    PA.preserve<MockLoopAnalysisHandle::Analysis>();
    PA.preserve<MockLoopNestAnalysisHandle::Analysis>();
    return PA;
  }));
  EXPECT_CALL(MLNAHandle, run(HasName("loop.f.0"), _, _));
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0.0"), _, _));
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0.1"), _, _));
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0"), _, _));
  FPM.addPass(MFPHandle.getPass());
  FPM.addPass(createFunctionToLoopNestPassAdaptor(
      RequireAnalysisLoopNestPass<MockLoopNestAnalysisHandle::Analysis>()));
  FPM.addPass(
      createFunctionToLoopNestPassAdaptor(createLoopNestToLoopPassAdaptor(
          RequireAnalysisLoopPass<MockLoopAnalysisHandle::Analysis>())));

  EXPECT_CALL(MFPHandle, run(HasName("f"), _)).WillOnce(InvokeWithoutArgs([] {
    auto PA = PreservedAnalyses::none();
    PA.preserve<LoopNestAnalysisManagerFunctionProxy>();
    PA.preserve<AAManager>();
    PA.preserve<AssumptionAnalysis>();
    PA.preserve<DominatorTreeAnalysis>();
    PA.preserve<LoopAnalysis>();
    PA.preserve<LoopAnalysisManagerFunctionProxy>();
    // Abandon `ScalarEvolutionAnalysis`.
    PA.abandon<ScalarEvolutionAnalysis>();
    PA.preserve<MockLoopAnalysisHandle::Analysis>();
    PA.preserve<MockLoopNestAnalysisHandle::Analysis>();
    return PA;
  }));
  EXPECT_CALL(MLNAHandle, run(HasName("loop.f.0"), _, _));
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0.0"), _, _));
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0.1"), _, _));
  EXPECT_CALL(MLAHandle, run(HasName("loop.f.0"), _, _));
  FPM.addPass(MFPHandle.getPass());
  FPM.addPass(createFunctionToLoopNestPassAdaptor(
      RequireAnalysisLoopNestPass<MockLoopNestAnalysisHandle::Analysis>()));
  FPM.addPass(
      createFunctionToLoopNestPassAdaptor(createLoopNestToLoopPassAdaptor(
          RequireAnalysisLoopPass<MockLoopAnalysisHandle::Analysis>())));

  // The loop analyses and loop nest analyses will be run only on the first
  // time. The results are cached in the remaining passes.
  EXPECT_CALL(MLNAHandle, run(HasName("loop.g.0"), _, _));
  EXPECT_CALL(MLNAHandle, run(HasName("loop.g.1"), _, _));
  EXPECT_CALL(MLAHandle, run(HasName("loop.g.0"), _, _));
  EXPECT_CALL(MLAHandle, run(HasName("loop.g.1.0"), _, _));
  EXPECT_CALL(MLAHandle, run(HasName("loop.g.1"), _, _));
  EXPECT_CALL(MFPHandle, run(HasName("g"), _)).Times(9);

  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
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

  // Invalidating loop analysis results in subloops will not affect each other.
  // In other words, the invalidation will not be propagate to the loop nest
  // pass manager since AllAnalysesOn<Loop> is preserved in the adaptor.
  EXPECT_CALL(MLPHandle, run(HasName("loop.f.0.0"), _, _, _))
      .InSequence(F0Sequence)
      .WillOnce(InvokeWithoutArgs([] {
        PreservedAnalyses PA;
        PA.preserve<MockLoopAnalysisHandleTemplate<A>::Analysis>();
        return PA;
      }));
  EXPECT_CALL(MLAHandleA, invalidate(HasName("loop.f.0.0"), _, _))
      .InSequence(F0Sequence);
  EXPECT_CALL(MLAHandleB, invalidate(HasName("loop.f.0.0"), _, _))
      .InSequence(F0Sequence);

  EXPECT_CALL(MLPHandle, run(HasName("loop.f.0.1"), _, _, _))
      .InSequence(F0Sequence)
      .WillOnce(InvokeWithoutArgs([] {
        PreservedAnalyses PA;
        PA.preserve<MockLoopAnalysisHandleTemplate<B>::Analysis>();
        return PA;
      }));
  EXPECT_CALL(MLAHandleA, invalidate(HasName("loop.f.0.1"), _, _))
      .InSequence(F0Sequence);
  EXPECT_CALL(MLAHandleB, invalidate(HasName("loop.f.0.1"), _, _))
      .InSequence(F0Sequence);

  EXPECT_CALL(MLPHandle, run(HasName("loop.f.0"), _, _, _))
      .InSequence(F0Sequence);

  EXPECT_CALL(MLPHandle, run(HasName("loop.g.0"), _, _, _))
      .InSequence(G0Sequence);
  EXPECT_CALL(MLPHandle, run(HasName("loop.g.1.0"), _, _, _))
      .InSequence(G1Sequence);
  EXPECT_CALL(MLPHandle, run(HasName("loop.g.1"), _, _, _))
      .InSequence(G1Sequence);

  EXPECT_CALL(MLAHandleA, run(HasName("loop.f.0.1"), _, _))
      .InSequence(F0Sequence);
  EXPECT_CALL(MLAHandleB, run(HasName("loop.f.0.0"), _, _))
      .InSequence(F0Sequence);

  LNPM.addPass(createLoopNestToLoopPassAdaptor(MLPHandle.getPass()));
  LNPM.addPass(createLoopNestToLoopPassAdaptor(
      RequireAnalysisLoopPass<MockLoopAnalysisHandleTemplate<A>::Analysis>()));
  LNPM.addPass(createLoopNestToLoopPassAdaptor(
      RequireAnalysisLoopPass<MockLoopAnalysisHandleTemplate<B>::Analysis>()));

  ModulePassManager MPM(true);
  MPM.addPass(createModuleToFunctionPassAdaptor(
      createFunctionToLoopNestPassAdaptor(std::move(LNPM))));

  MPM.run(*M, MAM);
}

TEST_F(LoopNestPassManagerTest, RevisitCurrentLoopNest) {
  M = parseIR(Context, "define void @f(i1* %ptr) {\n"
                       "entry:\n"
                       "  br label %loop.0\n"
                       "loop.0:\n"
                       "  %cond.0 = load volatile i1, i1* %ptr\n"
                       "  br i1 %cond.0, label %loop.0, label %loop.1.ph\n"
                       "loop.1.ph:\n"
                       "  br label %loop.1\n"
                       "loop.1:\n"
                       "  %cond.1 = load volatile i1, i1* %ptr\n"
                       "  br i1 %cond.1, label %loop.1, label %end\n"
                       "end:\n"
                       "  ret void\n"
                       "}\n");
  Function &F = *M->begin();
  ASSERT_THAT(F, HasName("f"));
  auto BBI = F.begin();
  BasicBlock &EntryBB = *BBI++;
  ASSERT_THAT(EntryBB, HasName("entry"));
  BasicBlock &Loop0BB = *BBI++;
  ASSERT_THAT(Loop0BB, HasName("loop.0"));
  BasicBlock &Loop1PHBB = *BBI++;
  ASSERT_THAT(Loop1PHBB, HasName("loop.1.ph"));
  BasicBlock &Loop1BB = *BBI++;
  ASSERT_THAT(Loop1BB, HasName("loop.1"));
  BasicBlock &EndBB = *BBI++;
  ASSERT_THAT(EndBB, HasName("end"));
  ASSERT_THAT(BBI, F.end());

  ModulePassManager MPM(true);
  LoopNestPassManager LNPM(true);

  ::testing::InSequence MakeExpectationsSequenced;

  EXPECT_CALL(MLNPHandle, run(HasName("loop.0"), _, _, _))
      .WillOnce(Invoke(getLoopNestAnalysisResult));
  EXPECT_CALL(MLNAHandle, run(HasName("loop.0"), _, _));
  EXPECT_CALL(MLNPHandle, run(HasName("loop.0"), _, _, _))
      .WillOnce(WithArgs<3>(Invoke([&](LNPMUpdater &U) {
        U.revisitCurrentLoopNest();
        return PreservedAnalyses::all();
      })));

  EXPECT_CALL(MLNPHandle, run(HasName("loop.0"), _, _, _))
      .Times(3)
      .WillRepeatedly(Invoke(getLoopNestAnalysisResult));

  EXPECT_CALL(MLNPHandle, run(HasName("loop.1"), _, _, _))
      .WillOnce(Invoke(getLoopNestAnalysisResult));
  EXPECT_CALL(MLNAHandle, run(HasName("loop.1"), _, _));
  EXPECT_CALL(MLNPHandle, run(HasName("loop.1"), _, _, _))
      .WillOnce(Invoke(getLoopNestAnalysisResult));
  EXPECT_CALL(MLNPHandle, run(HasName("loop.1"), _, _, _))
      .WillOnce(WithArgs<3>(Invoke([&](LNPMUpdater &U) {
        U.revisitCurrentLoopNest();
        return PreservedAnalyses::all();
      })));
  EXPECT_CALL(MLNPHandle, run(HasName("loop.1"), _, _, _))
      .Times(3)
      .WillRepeatedly(Invoke(getLoopNestAnalysisResult));

  LNPM.addPass(MLNPHandle.getPass());
  LNPM.addPass(MLNPHandle.getPass());
  LNPM.addPass(MLNPHandle.getPass());

  MPM.addPass(createModuleToFunctionPassAdaptor(
      createFunctionToLoopNestPassAdaptor(std::move(LNPM))));
  MPM.run(*M, MAM);
}

// Test the functionality of `addNewLoopNests()`. This includes adding loop
// nests directly in loop nest passes, or indirectly via addition of top-level
// loops in loop passes.
TEST_F(LoopNestPassManagerTest, TopLevelLoopInsertion) {
  M = parseIR(Context,
              "define void @f(i1* %ptr) {\n"
              "entry:\n"
              "  br label %loop.0\n"
              "loop.0:\n"
              "  %cond.0 = load volatile i1, i1* %ptr\n"
              "  br i1 %cond.0, label %loop.0.0.ph, label %loop.2.ph\n"
              "loop.0.0.ph:\n"
              "  br label %loop.0.0\n"
              "loop.0.0:\n"
              "  %cond.0.0 = load volatile i1, i1* %ptr\n"
              "  br i1 %cond.0.0, label %loop.0.0, label %loop.0.2.ph\n"
              "loop.0.2.ph:\n"
              "  br label %loop.0.2\n"
              "loop.0.2:\n"
              "  %cond.0.2 = load volatile i1, i1* %ptr\n"
              "  br i1 %cond.0.2, label %loop.0.2, label %loop.0.latch\n"
              "loop.0.latch:\n"
              "  br label %loop.0\n"
              "loop.2.ph:\n"
              "  br label %loop.2\n"
              "loop.2:\n"
              "  %cond.2 = load volatile i1, i1* %ptr\n"
              "  br i1 %cond.2, label %loop.2, label %end\n"
              "end:\n"
              "  ret void\n"
              "}\n");
  // Build up variables referring into the IR so we can rewrite it below
  // easily.
  Function &F = *M->begin();
  ASSERT_THAT(F, HasName("f"));
  Argument &Ptr = *F.arg_begin();
  auto BBI = F.begin();
  BasicBlock &EntryBB = *BBI++;
  ASSERT_THAT(EntryBB, HasName("entry"));
  BasicBlock &Loop0BB = *BBI++;
  ASSERT_THAT(Loop0BB, HasName("loop.0"));
  BasicBlock &Loop00PHBB = *BBI++;
  ASSERT_THAT(Loop00PHBB, HasName("loop.0.0.ph"));
  BasicBlock &Loop00BB = *BBI++;
  ASSERT_THAT(Loop00BB, HasName("loop.0.0"));
  BasicBlock &Loop02PHBB = *BBI++;
  ASSERT_THAT(Loop02PHBB, HasName("loop.0.2.ph"));
  BasicBlock &Loop02BB = *BBI++;
  ASSERT_THAT(Loop02BB, HasName("loop.0.2"));
  BasicBlock &Loop0LatchBB = *BBI++;
  ASSERT_THAT(Loop0LatchBB, HasName("loop.0.latch"));
  BasicBlock &Loop2PHBB = *BBI++;
  ASSERT_THAT(Loop2PHBB, HasName("loop.2.ph"));
  BasicBlock &Loop2BB = *BBI++;
  ASSERT_THAT(Loop2BB, HasName("loop.2"));
  BasicBlock &EndBB = *BBI++;
  ASSERT_THAT(EndBB, HasName("end"));
  ASSERT_THAT(BBI, F.end());

  ::testing::InSequence MakeExpectationsSequenced;
  FunctionPassManager FPM(true);

  // First we add loop.0.1 between loop.0.0 and loop.0.2. This should not
  // trigger the addition of top-level loop.
  EXPECT_CALL(MLPHandle, run(HasName("loop.0.0"), _, _, _))
      .WillOnce(Invoke([&](Loop &L, LoopAnalysisManager &AM,
                           LoopStandardAnalysisResults &AR, LPMUpdater &U) {
        auto *NewLoop01 = AR.LI.AllocateLoop();
        L.getParentLoop()->addChildLoop(NewLoop01);
        auto *NewLoop01PHBB =
            BasicBlock::Create(Context, "loop.0.1.ph", &F, &Loop02PHBB);
        auto *NewLoop01BB =
            BasicBlock::Create(Context, "loop.0.1", &F, &Loop02PHBB);
        BranchInst::Create(NewLoop01BB, NewLoop01PHBB);
        auto *NewCond01 = new LoadInst(Type::getInt1Ty(Context), &Ptr,
                                       "cond.0.1", true, NewLoop01BB);
        BranchInst::Create(&Loop02PHBB, NewLoop01BB, NewCond01, NewLoop01BB);
        Loop00BB.getTerminator()->replaceUsesOfWith(&Loop02PHBB, NewLoop01PHBB);
        AR.DT.addNewBlock(NewLoop01PHBB, &Loop00BB);
        auto *NewDTNode = AR.DT.addNewBlock(NewLoop01BB, NewLoop01PHBB);
        AR.DT.changeImmediateDominator(AR.DT[&Loop02PHBB], NewDTNode);
        EXPECT_TRUE(AR.DT.verify());
        L.getParentLoop()->addBasicBlockToLoop(NewLoop01PHBB, AR.LI);
        NewLoop01->addBasicBlockToLoop(NewLoop01BB, AR.LI);
        L.getParentLoop()->verifyLoop();
        U.addSiblingLoops({NewLoop01});
        return getLoopPassPreservedAnalyses();
      }));

  EXPECT_CALL(MLPHandle, run(HasName("loop.0.1"), _, _, _));
  EXPECT_CALL(MLPHandle, run(HasName("loop.0.2"), _, _, _));
  EXPECT_CALL(MLPHandle, run(HasName("loop.0"), _, _, _));
  EXPECT_CALL(MLPHandle, run(HasName("loop.2"), _, _, _));

  FPM.addPass(createFunctionToLoopNestPassAdaptor(
      createLoopNestToLoopPassAdaptor(MLPHandle.getPass())));

  EXPECT_CALL(MLNPHandle, run(HasName("loop.0"), _, _, _));
  EXPECT_CALL(MLPHandle, run(HasName("loop.0.0"), _, _, _));
  EXPECT_CALL(MLPHandle, run(HasName("loop.0.2"), _, _, _));
  // loop.0.1 is added later
  EXPECT_CALL(MLPHandle, run(HasName("loop.0.1"), _, _, _));

  EXPECT_CALL(MLPHandle, run(HasName("loop.0"), _, _, _))
      .WillOnce(Invoke([&](Loop &L, LoopAnalysisManager &AM,
                           LoopStandardAnalysisResults &AR, LPMUpdater &U) {
        auto *NewLoop1 = AR.LI.AllocateLoop();
        AR.LI.addTopLevelLoop(NewLoop1);
        auto *NewLoop10 = AR.LI.AllocateLoop();
        NewLoop1->addChildLoop(NewLoop10);
        auto *NewLoop1PHBB =
            BasicBlock::Create(Context, "loop.1.ph", &F, &Loop2PHBB);
        auto *NewLoop1BB =
            BasicBlock::Create(Context, "loop.1", &F, &Loop2PHBB);
        auto *NewLoop10PHBB =
            BasicBlock::Create(Context, "loop.1.0.ph", &F, &Loop2PHBB);
        auto *NewLoop10BB =
            BasicBlock::Create(Context, "loop.1.0", &F, &Loop2PHBB);
        auto *NewLoop1LatchBB =
            BasicBlock::Create(Context, "loop.1.latch", &F, &Loop2PHBB);
        BranchInst::Create(NewLoop1BB, NewLoop1PHBB);
        BranchInst::Create(NewLoop10BB, NewLoop10PHBB);
        auto *NewCond1 = new LoadInst(Type::getInt1Ty(Context), &Ptr, "cond.1",
                                      true, NewLoop1BB);
        BranchInst::Create(NewLoop10PHBB, &Loop2PHBB, NewCond1, NewLoop1BB);
        auto *NewCond10 = new LoadInst(Type::getInt1Ty(Context), &Ptr,
                                       "cond.1.0", true, NewLoop10BB);
        BranchInst::Create(NewLoop10BB, NewLoop1LatchBB, NewCond10,
                           NewLoop10BB);
        BranchInst::Create(NewLoop1BB, NewLoop1LatchBB);
        Loop0BB.getTerminator()->replaceUsesOfWith(&Loop2PHBB, NewLoop1PHBB);

        AR.DT.addNewBlock(NewLoop1PHBB, &Loop0BB);
        AR.DT.addNewBlock(NewLoop1BB, NewLoop1PHBB);
        AR.DT.addNewBlock(NewLoop10PHBB, NewLoop1BB);
        AR.DT.addNewBlock(NewLoop10BB, NewLoop10PHBB);
        AR.DT.addNewBlock(NewLoop1LatchBB, NewLoop10BB);
        AR.DT.changeImmediateDominator(AR.DT[&Loop2PHBB], AR.DT[NewLoop1BB]);
        EXPECT_TRUE(AR.DT.verify());
        NewLoop1->addBasicBlockToLoop(NewLoop1BB, AR.LI);
        NewLoop1->addBasicBlockToLoop(NewLoop10PHBB, AR.LI);
        NewLoop10->addBasicBlockToLoop(NewLoop10BB, AR.LI);
        NewLoop1->addBasicBlockToLoop(NewLoop1LatchBB, AR.LI);
        NewLoop1->verifyLoop();
        U.addSiblingLoops({NewLoop1});
        return getLoopPassPreservedAnalyses();
      }));

  EXPECT_CALL(MLNPHandle, run(HasName("loop.1"), _, _, _));
  EXPECT_CALL(MLPHandle, run(HasName("loop.1.0"), _, _, _));
  EXPECT_CALL(MLPHandle, run(HasName("loop.1"), _, _, _));

  EXPECT_CALL(MLNPHandle, run(HasName("loop.2"), _, _, _));
  EXPECT_CALL(MLPHandle, run(HasName("loop.2"), _, _, _));

  LoopNestPassManager LNPM(true);
  LNPM.addPass(MLNPHandle.getPass());
  LNPM.addPass(createLoopNestToLoopPassAdaptor(MLPHandle.getPass()));
  FPM.addPass(createFunctionToLoopNestPassAdaptor(std::move(LNPM)));

  EXPECT_CALL(MLNPHandle, run(HasName("loop.1"), _, _, _));
  EXPECT_CALL(MLNPHandle, run(HasName("loop.0"), _, _, _));
  EXPECT_CALL(MLNPHandle, run(HasName("loop.2"), _, _, _));

  FPM.addPass(createFunctionToLoopNestPassAdaptor(MLNPHandle.getPass()));
  FPM.addPass(DominatorTreeVerifierPass());
  FPM.addPass(LoopVerifierPass());
  ModulePassManager MPM(true);
  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));

  MPM.run(*M, MAM);
}

} // namespace
