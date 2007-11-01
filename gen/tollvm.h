// D -> LLVM helpers

struct StructInitializer;

const llvm::Type* DtoType(Type* t);
bool DtoIsPassedByRef(Type* type);
Type* DtoDType(Type* t);

const llvm::Type* DtoStructType(Type* t);
llvm::Value* DtoStructZeroInit(llvm::Value* v);
llvm::Value* DtoStructCopy(llvm::Value* dst, llvm::Value* src);
llvm::Constant* DtoConstStructInitializer(StructInitializer* si);

const llvm::FunctionType* DtoFunctionType(Type* t, const llvm::Type* thistype, bool ismain = false);
const llvm::FunctionType* DtoFunctionType(FuncDeclaration* fdecl);
llvm::Function* DtoDeclareFunction(FuncDeclaration* fdecl);

const llvm::StructType* DtoDelegateType(Type* t);
llvm::Value* DtoNullDelegate(llvm::Value* v);
llvm::Value* DtoDelegateCopy(llvm::Value* dst, llvm::Value* src);
llvm::Value* DtoCompareDelegate(TOK op, llvm::Value* lhs, llvm::Value* rhs);

llvm::GlobalValue::LinkageTypes DtoLinkage(PROT prot, uint stc);
unsigned DtoCallingConv(LINK l);

llvm::Value* DtoPointedType(llvm::Value* ptr, llvm::Value* val);
llvm::Value* DtoBoolean(llvm::Value* val);

const llvm::Type* DtoSize_t();

void DtoMain();

void DtoCallClassDtors(TypeClass* tc, llvm::Value* instance);
void DtoInitClass(TypeClass* tc, llvm::Value* dst);

llvm::Constant* DtoConstInitializer(Type* type, Initializer* init);
void DtoInitializer(Initializer* init);

llvm::Function* LLVM_DeclareMemSet32();
llvm::Function* LLVM_DeclareMemSet64();
llvm::Function* LLVM_DeclareMemCpy32();
llvm::Function* LLVM_DeclareMemCpy64();

llvm::Value* DtoGEP(llvm::Value* ptr, llvm::Value* i0, llvm::Value* i1, const std::string& var, llvm::BasicBlock* bb=NULL);
llvm::Value* DtoGEP(llvm::Value* ptr, const std::vector<unsigned>& src, const std::string& var, llvm::BasicBlock* bb=NULL);
llvm::Value* DtoGEPi(llvm::Value* ptr, unsigned i0, const std::string& var, llvm::BasicBlock* bb=NULL);
llvm::Value* DtoGEPi(llvm::Value* ptr, unsigned i0, unsigned i1, const std::string& var, llvm::BasicBlock* bb=NULL);

void DtoGiveArgumentStorage(elem* e);

llvm::Value* DtoRealloc(llvm::Value* ptr, const llvm::Type* ty);
llvm::Value* DtoRealloc(llvm::Value* ptr, llvm::Value* len);

void DtoAssert(llvm::Value* cond, llvm::Value* loc, llvm::Value* msg);

llvm::Value* DtoArgument(const llvm::Type* paramtype, Argument* fnarg, Expression* argexp);

llvm::Value* DtoNestedVariable(VarDeclaration* vd);

void DtoAssign(Type* lhsType, llvm::Value* lhs, llvm::Value* rhs);

llvm::ConstantInt* DtoConstSize_t(size_t);
llvm::ConstantInt* DtoConstUint(unsigned i);
llvm::ConstantInt* DtoConstInt(int i);
llvm::Constant* DtoConstString(const char*);
llvm::Constant* DtoConstBool(bool);

void DtoMemCpy(llvm::Value* dst, llvm::Value* src, llvm::Value* nbytes);

llvm::Value* DtoIndexStruct(llvm::Value* ptr, StructDeclaration* sd, Type* t, unsigned os, std::vector<unsigned>& idxs);

#include "enums.h"
