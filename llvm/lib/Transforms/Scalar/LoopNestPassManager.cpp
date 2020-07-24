//===- LoopNestPassManager.cpp - Loop nest pass management ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/LoopNestPassManager.h"

namespace llvm {

template class PassManager<LoopNest, LoopNestAnalysisManager,
                           LoopStandardAnalysisResults &, LNPMUpdater &>;

PrintLoopNestPass::PrintLoopNestPass() : OS(dbgs()) {}
PrintLoopNestPass::PrintLoopNestPass(raw_ostream &OS, const std::string &Banner)
    : OS(OS), Banner(Banner) {}

PreservedAnalyses PrintLoopNestPass::run(LoopNest &LN,
                                         LoopNestAnalysisManager &,
                                         LoopStandardAnalysisResults &,
                                         LNPMUpdater &) {
  OS << LN << "\n";
  return PreservedAnalyses::all();
}

} // namespace llvm
