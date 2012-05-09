#define DEBUG_TYPE "overflow"
#include <llvm/Instructions.h>
#include <llvm/Intrinsics.h>
#include <llvm/Module.h>
#include <llvm/Pass.h>
#include <llvm/ADT/OwningPtr.h>
#include <llvm/Support/IRBuilder.h>
#include <llvm/Support/InstIterator.h>
#include <llvm/Support/PatternMatch.h>
#include <llvm/Transforms/Utils/Local.h>

using namespace llvm;
using namespace llvm::PatternMatch;

namespace {

struct Overflow : FunctionPass {
	static char ID;
	Overflow() : FunctionPass(ID) { }

	virtual void getAnalysisUsage(AnalysisUsage &AU) const {
		AU.setPreservesCFG();
	}

	virtual bool doInitialization(Module &M) {
		Builder.reset(new IRBuilder<>(M.getContext()));
		this->M = &M;
		return false;
	}

	virtual bool runOnFunction(Function &);

private:
	OwningPtr<IRBuilder<> > Builder;
	Module *M;

	Value *tryIntrinsicWithInverse(CmpInst::Predicate Pred, Value *L, Value *R) {
		Value *V = tryIntrinsic(Pred, L, R);
		if (V)
			return V;
		V = tryIntrinsic(CmpInst::getInversePredicate(Pred), L, R);
		if (V)
			return Builder->CreateXor(V, 1);
		return 0;
	}

	Value *createOverflowBit(Intrinsic::ID ID, Value *V0, Value *V1) {
		Function *F = Intrinsic::getDeclaration(M, ID, V0->getType());
		CallInst *CI = Builder->CreateCall2(F, V0, V1);
		return Builder->CreateExtractValue(CI, 1);
	}

	Value *tryIntrinsic(CmpInst::Predicate, Value *, Value *);
};

} // anonymous namespace

bool Overflow::runOnFunction(Function &F) {
	bool Changed = false;
	inst_iterator i = inst_begin(F), e = inst_end(F);
	for (; i != e; ) {
		ICmpInst *I = dyn_cast<ICmpInst>(&*i);
		++i;
		if (!I)
			continue;
		Builder->SetInsertPoint(I);
		Value *L = I->getOperand(0), *R = I->getOperand(1);
		Value *V = tryIntrinsicWithInverse(I->getPredicate(), L, R);
		if (!V && !I->isEquality())
			V = tryIntrinsicWithInverse(I->getSwappedPredicate(), R, L);
		if (V) {
			if (Instruction *NewInst = dyn_cast<Instruction>(V))
				NewInst->setDebugLoc(I->getDebugLoc());
			I->replaceAllUsesWith(V);
			RecursivelyDeleteTriviallyDeadInstructions(I);
			Changed = true;
		}
	}
	return Changed;
}

Value *Overflow::tryIntrinsic(CmpInst::Predicate Pred, Value *L, Value *R) {
	llvm::Value *X, *Y;

	// x > UMAX / y
	if (Pred == llvm::CmpInst::ICMP_UGT
		&& match(L, m_Value(X))
		&& match(R, m_UDiv(m_AllOnes(), m_Value(Y)))) {
			return createOverflowBit(Intrinsic::umul_with_overflow, X, Y);
        }

	return 0;
}

char Overflow::ID;

static RegisterPass<Overflow>
X("overflow", "Rewrite overflow checking idioms using intrinsics");
