;RUN: opt %s -passes='adce,loop(loop-rotate),adce' -S -debug-pass-manager -debug-only=loop-rotate 2>&1 | FileCheck %s
;RUN: opt %s -passes='adce,loop-mssa(loop-rotate),adce' -S -debug-pass-manager -debug-only=loop-rotate -verify-memoryssa 2>&1 | FileCheck %s --check-prefix=MSSA
;RUN: opt %s -passes='adce,loop-nest(loop(loop-rotate)),adce' -S -debug-pass-manager -debug-only=loop-rotate 2>&1 | FileCheck %s --check-prefix=LN
;RUN: opt %s -passes='adce,loop-nest-mssa(loop-mssa(loop-rotate)),adce' -S -debug-pass-manager -debug-only=loop-rotate -verify-memoryssa 2>&1 | FileCheck %s --check-prefix=LNMSSA
;REQUIRES: asserts

; This test is to make sure we invalidate the post dominator pass after loop rotate simplifies the loop latch.
; The adce passes are here to make sure post dominator analysis is required.

; CHECK: Starting llvm::Function pass manager run.
; CHECK-NEXT: Running pass: ADCEPass on f
; CHECK-NEXT: Running analysis: PostDominatorTreeAnalysis on f
; CHECK-NEXT: Starting llvm::Function pass manager run.
; CHECK-NEXT: Running pass: LoopSimplifyPass on f
; CHECK-NEXT: Running analysis: LoopAnalysis on f
; CHECK-NEXT: Running analysis: DominatorTreeAnalysis on f
; CHECK-NEXT: Running analysis: AssumptionAnalysis on f
; CHECK-NEXT: Running pass: LCSSAPass on f
; CHECK-NEXT: Finished llvm::Function pass manager run.
; CHECK-NEXT: Running analysis: AAManager on f
; CHECK-NEXT: Running analysis: TargetLibraryAnalysis on f
; CHECK-NEXT: Running analysis: ScalarEvolutionAnalysis on f
; CHECK-NEXT: Running analysis: TargetIRAnalysis on f
; CHECK-NEXT: Running analysis: InnerAnalysisManagerProxy{{.*}} on f
; CHECK-NEXT: Starting Loop pass manager run.
; CHECK-NEXT: Running pass: LoopRotatePass on Loop at depth 1 containing: %bb<header><exiting>,%bb4<latch>
; CHECK-NEXT: Folding loop latch bb4 into bb
; CHECK-NEXT: Finished Loop pass manager run.
; CHECK-NEXT: Invalidating analysis: PostDominatorTreeAnalysis on f
; CHECK-NEXT: Running pass: ADCEPass on f
; CHECK-NEXT: Running analysis: PostDominatorTreeAnalysis on f
; CHECK-NEXT: Finished llvm::Function pass manager run.

; LN: Starting llvm::Function pass manager run.
; LN-NEXT: Running pass: ADCEPass on f
; LN-NEXT: Running analysis: PostDominatorTreeAnalysis on f
; LN-NEXT: Starting llvm::Function pass manager run.
; LN-NEXT: Running pass: LoopSimplifyPass on f
; LN-NEXT: Running analysis: LoopAnalysis on f
; LN-NEXT: Running analysis: DominatorTreeAnalysis on f
; LN-NEXT: Running analysis: AssumptionAnalysis on f
; LN-NEXT: Running pass: LCSSAPass on f
; LN-NEXT: Finished llvm::Function pass manager run.
; LN-NEXT: Running analysis: AAManager on f
; LN-NEXT: Running analysis: TargetLibraryAnalysis on f
; LN-NEXT: Running analysis: ScalarEvolutionAnalysis on f
; LN-NEXT: Running analysis: TargetIRAnalysis on f
; LN-NEXT: Running analysis: InnerAnalysisManagerProxy{{.*}} on f
; LN-NEXT: Running analysis: LoopNestAnalysis on Loop at depth 1 containing: %bb<header><exiting>,%bb4<latch>
; LN-NEXT: Starting LoopNest pass manager run.
; LN-NEXT: Starting Loop pass manager run.
; LN-NEXT: Running pass: LoopRotatePass on Loop at depth 1 containing: %bb<header><exiting>,%bb4<latch>
; LN-NEXT: Folding loop latch bb4 into bb
; LN-NEXT: Invalidating analysis: LoopNestAnalysis on bb
; LN-NEXT: Finished Loop pass manager run.
; LN-NEXT: Finished LoopNest pass manager run.
; LN-NEXT: Invalidating analysis: PostDominatorTreeAnalysis on f
; LN-NEXT: Running pass: ADCEPass on f
; LN-NEXT: Running analysis: PostDominatorTreeAnalysis on f
; LN-NEXT: Finished llvm::Function pass manager run.

; MSSA: Starting llvm::Function pass manager run.
; MSSA-NEXT: Running pass: ADCEPass on f
; MSSA-NEXT: Running analysis: PostDominatorTreeAnalysis on f
; MSSA-NEXT: Starting llvm::Function pass manager run.
; MSSA-NEXT: Running pass: LoopSimplifyPass on f
; MSSA-NEXT: Running analysis: LoopAnalysis on f
; MSSA-NEXT: Running analysis: DominatorTreeAnalysis on f
; MSSA-NEXT: Running analysis: AssumptionAnalysis on f
; MSSA-NEXT: Running pass: LCSSAPass on f
; MSSA-NEXT: Finished llvm::Function pass manager run.
; MSSA-NEXT: Running analysis: MemorySSAAnalysis on f
; MSSA-NEXT: Running analysis: AAManager on f
; MSSA-NEXT: Running analysis: TargetLibraryAnalysis on f
; MSSA-NEXT: Running analysis: ScalarEvolutionAnalysis on f
; MSSA-NEXT: Running analysis: TargetIRAnalysis on f
; MSSA-NEXT: Running analysis: InnerAnalysisManagerProxy{{.*}} on f
; MSSA-NEXT: Starting Loop pass manager run.
; MSSA-NEXT: Running pass: LoopRotatePass on Loop at depth 1 containing: %bb<header><exiting>,%bb4<latch>
; MSSA-NEXT: Folding loop latch bb4 into bb
; MSSA-NEXT: Finished Loop pass manager run.
; MSSA-NEXT: Invalidating analysis: PostDominatorTreeAnalysis on f
; MSSA-NEXT: Running pass: ADCEPass on f
; MSSA-NEXT: Running analysis: PostDominatorTreeAnalysis on f
; MSSA-NEXT: Finished llvm::Function pass manager run.

; LNMSSA: Starting llvm::Function pass manager run.
; LNMSSA-NEXT: Running pass: ADCEPass on f
; LNMSSA-NEXT: Running analysis: PostDominatorTreeAnalysis on f
; LNMSSA-NEXT: Starting llvm::Function pass manager run.
; LNMSSA-NEXT: Running pass: LoopSimplifyPass on f
; LNMSSA-NEXT: Running analysis: LoopAnalysis on f
; LNMSSA-NEXT: Running analysis: DominatorTreeAnalysis on f
; LNMSSA-NEXT: Running analysis: AssumptionAnalysis on f
; LNMSSA-NEXT: Running pass: LCSSAPass on f
; LNMSSA-NEXT: Finished llvm::Function pass manager run.
; LNMSSA-NEXT: Running analysis: MemorySSAAnalysis on f
; LNMSSA-NEXT: Running analysis: AAManager on f
; LNMSSA-NEXT: Running analysis: TargetLibraryAnalysis on f
; LNMSSA-NEXT: Running analysis: ScalarEvolutionAnalysis on f
; LNMSSA-NEXT: Running analysis: TargetIRAnalysis on f
; LNMSSA-NEXT: Running analysis: InnerAnalysisManagerProxy{{.*}} on f
; LNMSSA-NEXT: Running analysis: LoopNestAnalysis on Loop at depth 1 containing: %bb<header><exiting>,%bb4<latch>
; LNMSSA-NEXT: Starting LoopNest pass manager run.
; LNMSSA-NEXT: Starting Loop pass manager run.
; LNMSSA-NEXT: Running pass: LoopRotatePass on Loop at depth 1 containing: %bb<header><exiting>,%bb4<latch>
; LNMSSA-NEXT: Folding loop latch bb4 into bb
; LNMSSA-NEXT: Invalidating analysis: LoopNestAnalysis on bb
; LNMSSA-NEXT: Finished Loop pass manager run.
; LNMSSA-NEXT: Finished LoopNest pass manager run.
; LNMSSA-NEXT: Invalidating analysis: PostDominatorTreeAnalysis on f
; LNMSSA-NEXT: Running pass: ADCEPass on f
; LNMSSA-NEXT: Running analysis: PostDominatorTreeAnalysis on f
; LNMSSA-NEXT: Finished llvm::Function pass manager run.

; CHECK-LABEL: define i8 @f() {
; CHECK-NEXT:  entry:
; CHECK-NEXT:    br label %bb
; CHECK:       bb:                                               ; preds = %bb, %entry
; CHECK-NEXT:    %mode.0 = phi i8 [ 0, %entry ], [ %indvar.next, %bb ]
; CHECK-NEXT:    %tmp5 = icmp eq i8 %mode.0, 1
; CHECK-NEXT:    %indvar.next = add i8 %mode.0, 1
; CHECK-NEXT:    br i1 %tmp5, label %bb5, label %bb
; CHECK:       bb5:                                              ; preds = %bb
; CHECK-NEXT:    tail call void @raise_exception() #0
; CHECK-NEXT:    unreachable
; CHECK-NEXT:  }
; CHECK:       ; Function Attrs: noreturn
; CHECK:       declare void @raise_exception() #0
; CHECK:       attributes #0 = { noreturn }

; LN-LABEL: define i8 @f() {
; LN-NEXT:  entry:
; LN-NEXT:    br label %bb
; LN:       bb:                                               ; preds = %bb, %entry
; LN-NEXT:    %mode.0 = phi i8 [ 0, %entry ], [ %indvar.next, %bb ]
; LN-NEXT:    %tmp5 = icmp eq i8 %mode.0, 1
; LN-NEXT:    %indvar.next = add i8 %mode.0, 1
; LN-NEXT:    br i1 %tmp5, label %bb5, label %bb
; LN:       bb5:                                              ; preds = %bb
; LN-NEXT:    tail call void @raise_exception() #0
; LN-NEXT:    unreachable
; LN-NEXT:  }
; LN:       ; Function Attrs: noreturn
; LN:       declare void @raise_exception() #0
; LN:       attributes #0 = { noreturn }

; MSSA-LABEL: define i8 @f() {
; MSSA-NEXT:  entry:
; MSSA-NEXT:    br label %bb
; MSSA:       bb:                                               ; preds = %bb, %entry
; MSSA-NEXT:    %mode.0 = phi i8 [ 0, %entry ], [ %indvar.next, %bb ]
; MSSA-NEXT:    %tmp5 = icmp eq i8 %mode.0, 1
; MSSA-NEXT:    %indvar.next = add i8 %mode.0, 1
; MSSA-NEXT:    br i1 %tmp5, label %bb5, label %bb
; MSSA:       bb5:                                              ; preds = %bb
; MSSA-NEXT:    tail call void @raise_exception() #0
; MSSA-NEXT:    unreachable
; MSSA-NEXT:  }
; MSSA:       ; Function Attrs: noreturn
; MSSA:       declare void @raise_exception() #0
; MSSA:       attributes #0 = { noreturn }

; LNMSSA-LABEL: define i8 @f() {
; LNMSSA-NEXT:  entry:
; LNMSSA-NEXT:    br label %bb
; LNMSSA:       bb:                                               ; preds = %bb, %entry
; LNMSSA-NEXT:    %mode.0 = phi i8 [ 0, %entry ], [ %indvar.next, %bb ]
; LNMSSA-NEXT:    %tmp5 = icmp eq i8 %mode.0, 1
; LNMSSA-NEXT:    %indvar.next = add i8 %mode.0, 1
; LNMSSA-NEXT:    br i1 %tmp5, label %bb5, label %bb
; LNMSSA:       bb5:                                              ; preds = %bb
; LNMSSA-NEXT:    tail call void @raise_exception() #0
; LNMSSA-NEXT:    unreachable
; LNMSSA-NEXT:  }
; LNMSSA:       ; Function Attrs: noreturn
; LNMSSA:       declare void @raise_exception() #0
; LNMSSA:       attributes #0 = { noreturn }

define i8 @f() {
entry:
  br label %bb

bb:                                               ; preds = %bb4, %entry
  %mode.0 = phi i8 [ 0, %entry ], [ %indvar.next, %bb4 ]
  %tmp5 = icmp eq i8 %mode.0, 1
  br i1 %tmp5, label %bb5, label %bb4

bb4:                                              ; preds = %bb2
  %indvar.next = add i8 %mode.0, 1
  br label %bb

bb5:                                              ; preds = %bb2
  tail call void @raise_exception() #0
  unreachable
}

; Function Attrs: noreturn
declare void @raise_exception() #0

attributes #0 = { noreturn }
