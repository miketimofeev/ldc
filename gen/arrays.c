#include "gen/llvm.h"

#include "mtype.h"
#include "dsymbol.h"
#include "aggregate.h"
#include "declaration.h"
#include "init.h"

#include "gen/irstate.h"
#include "gen/tollvm.h"
#include "gen/arrays.h"
#include "gen/runtime.h"
#include "gen/logger.h"
#include "gen/elem.h"

//////////////////////////////////////////////////////////////////////////////////////////

const llvm::StructType* DtoArrayType(Type* t)
{
    assert(t->next);
    const llvm::Type* at = DtoType(t->next);
    const llvm::Type* arrty;

    /*if (t->ty == Tsarray) {
        TypeSArray* tsa = (TypeSArray*)t;
        assert(tsa->dim->type->isintegral());
        arrty = llvm::ArrayType::get(at,tsa->dim->toUInteger());
    }
    else {
        arrty = llvm::ArrayType::get(at,0);
    }*/
    if (at == llvm::Type::VoidTy) {
        at = llvm::Type::Int8Ty;
    }
    arrty = llvm::PointerType::get(at);

    std::vector<const llvm::Type*> members;
    if (global.params.is64bit)
        members.push_back(llvm::Type::Int64Ty);
    else
        members.push_back(llvm::Type::Int32Ty);

    members.push_back(arrty);

    return llvm::StructType::get(members);
}

//////////////////////////////////////////////////////////////////////////////////////////

const llvm::ArrayType* DtoStaticArrayType(Type* t)
{
    if (t->llvmType)
        return llvm::cast<llvm::ArrayType>(t->llvmType);

    assert(t->ty == Tsarray);
    assert(t->next);

    const llvm::Type* at = DtoType(t->next);

    TypeSArray* tsa = (TypeSArray*)t;
    assert(tsa->dim->type->isintegral());
    const llvm::ArrayType* arrty = llvm::ArrayType::get(at,tsa->dim->toUInteger());

    tsa->llvmType = arrty;
    return arrty;
}

//////////////////////////////////////////////////////////////////////////////////////////

void DtoNullArray(llvm::Value* v)
{
    assert(gIR);

    llvm::Value* len = DtoGEPi(v,0,0,"tmp",gIR->scopebb());
    llvm::Value* zerolen = llvm::ConstantInt::get(len->getType()->getContainedType(0), 0, false);
    new llvm::StoreInst(zerolen, len, gIR->scopebb());

    llvm::Value* ptr = DtoGEPi(v,0,1,"tmp",gIR->scopebb());
    const llvm::PointerType* pty = llvm::cast<llvm::PointerType>(ptr->getType()->getContainedType(0));
    llvm::Value* nullptr = llvm::ConstantPointerNull::get(pty);
    new llvm::StoreInst(nullptr, ptr, gIR->scopebb());
}

//////////////////////////////////////////////////////////////////////////////////////////

void DtoArrayAssign(llvm::Value* dst, llvm::Value* src)
{
    assert(gIR);
    if (dst->getType() == src->getType())
    {
        llvm::Value* ptr = DtoGEPi(src,0,0,"tmp",gIR->scopebb());
        llvm::Value* val = new llvm::LoadInst(ptr,"tmp",gIR->scopebb());
        ptr = DtoGEPi(dst,0,0,"tmp",gIR->scopebb());
        new llvm::StoreInst(val, ptr, gIR->scopebb());

        ptr = DtoGEPi(src,0,1,"tmp",gIR->scopebb());
        val = new llvm::LoadInst(ptr,"tmp",gIR->scopebb());
        ptr = DtoGEPi(dst,0,1,"tmp",gIR->scopebb());
        new llvm::StoreInst(val, ptr, gIR->scopebb());
    }
    else
    {
        Logger::cout() << "array assignment type dont match: " << *dst->getType() << '\n' << *src->getType() << '\n';
        if (!llvm::isa<llvm::ArrayType>(src->getType()->getContainedType(0)))
        {
            Logger::cout() << "invalid: " << *src << '\n';
            assert(0);
        }
        const llvm::ArrayType* arrty = llvm::cast<llvm::ArrayType>(src->getType()->getContainedType(0));
        llvm::Type* dstty = llvm::PointerType::get(arrty->getElementType());

        llvm::Value* dstlen = DtoGEPi(dst,0,0,"tmp",gIR->scopebb());
        llvm::Value* srclen = DtoConstSize_t(arrty->getNumElements());
        new llvm::StoreInst(srclen, dstlen, gIR->scopebb());

        llvm::Value* dstptr = DtoGEPi(dst,0,1,"tmp",gIR->scopebb());
        llvm::Value* srcptr = new llvm::BitCastInst(src,dstty,"tmp",gIR->scopebb());
        new llvm::StoreInst(srcptr, dstptr, gIR->scopebb());
    }
}

//////////////////////////////////////////////////////////////////////////////////////////

void DtoArrayInit(llvm::Value* l, llvm::Value* r)
{
    const llvm::PointerType* ptrty = llvm::cast<llvm::PointerType>(l->getType());
    const llvm::Type* t = ptrty->getContainedType(0);
    const llvm::ArrayType* arrty = llvm::cast_or_null<llvm::ArrayType>(t);
    if (arrty)
    {
        llvm::Value* ptr = DtoGEPi(l,0,0,"tmp",gIR->scopebb());
        llvm::Value* dim = llvm::ConstantInt::get(DtoSize_t(), arrty->getNumElements(), false);
        llvm::Value* val = r;
        DtoArrayInit(ptr, dim, val);
    }
    else if (llvm::isa<llvm::StructType>(t))
    {
        assert(0 && "Only static arrays support initialisers atm");
    }
    else
    assert(0);
}

//////////////////////////////////////////////////////////////////////////////////////////

typedef const llvm::Type* constLLVMTypeP;

static size_t checkRectArrayInit(const llvm::Type* pt, constLLVMTypeP& finalty)
{
    if (llvm::isa<llvm::ArrayType>(pt)) {
        size_t n = checkRectArrayInit(pt->getContainedType(0), finalty);
        size_t ne = llvm::cast<llvm::ArrayType>(pt)->getNumElements();
        if (n) return n * ne;
        return ne;
    }
    finalty = pt;
    return 0;
}

void DtoArrayInit(llvm::Value* ptr, llvm::Value* dim, llvm::Value* val)
{
    const llvm::Type* pt = ptr->getType()->getContainedType(0);
    const llvm::Type* t = val->getType();
    const llvm::Type* finalTy;
    if (size_t arrsz = checkRectArrayInit(pt, finalTy)) {
        assert(finalTy == t);
        llvm::Constant* c = llvm::cast_or_null<llvm::Constant>(dim);
        assert(c);
        dim = llvm::ConstantExpr::getMul(c, DtoConstSize_t(arrsz));
        ptr = gIR->ir->CreateBitCast(ptr, llvm::PointerType::get(finalTy), "tmp");
    }
    else if (llvm::isa<llvm::StructType>(t)) {
        assert(0);
    }
    else {
        assert(t == pt);
    }

    std::vector<llvm::Value*> args;
    args.push_back(ptr);
    args.push_back(dim);
    args.push_back(val);

    const char* funcname = NULL;

    if (llvm::isa<llvm::PointerType>(t)) {
        funcname = "_d_array_init_pointer";

        const llvm::Type* dstty = llvm::PointerType::get(llvm::PointerType::get(llvm::Type::Int8Ty));
        if (args[0]->getType() != dstty)
            args[0] = new llvm::BitCastInst(args[0],dstty,"tmp",gIR->scopebb());

        const llvm::Type* valty = llvm::PointerType::get(llvm::Type::Int8Ty);
        if (args[2]->getType() != valty)
            args[2] = new llvm::BitCastInst(args[2],valty,"tmp",gIR->scopebb());
    }
    else if (t == llvm::Type::Int1Ty) {
        funcname = "_d_array_init_i1";
    }
    else if (t == llvm::Type::Int8Ty) {
        funcname = "_d_array_init_i8";
    }
    else if (t == llvm::Type::Int16Ty) {
        funcname = "_d_array_init_i16";
    }
    else if (t == llvm::Type::Int32Ty) {
        funcname = "_d_array_init_i32";
    }
    else if (t == llvm::Type::Int64Ty) {
        funcname = "_d_array_init_i64";
    }
    else if (t == llvm::Type::FloatTy) {
        funcname = "_d_array_init_float";
    }
    else if (t == llvm::Type::DoubleTy) {
        funcname = "_d_array_init_double";
    }
    else {
        Logger::cout() << *ptr->getType() << " = " << *val->getType() << '\n';
        assert(0);
    }

    llvm::Function* fn = LLVM_D_GetRuntimeFunction(gIR->module, funcname);
    assert(fn);
    Logger::cout() << "calling array init function: " << *fn <<'\n';
    llvm::CallInst* call = new llvm::CallInst(fn, args.begin(), args.end(), "", gIR->scopebb());
    call->setCallingConv(llvm::CallingConv::C);
}

//////////////////////////////////////////////////////////////////////////////////////////

void DtoSetArray(llvm::Value* arr, llvm::Value* dim, llvm::Value* ptr)
{
    Logger::cout() << "DtoSetArray(" << *arr << ", " << *dim << ", " << *ptr << ")\n";
    const llvm::StructType* st = llvm::cast<llvm::StructType>(arr->getType()->getContainedType(0));
    //const llvm::PointerType* pt = llvm::cast<llvm::PointerType>(r->getType());
    
    llvm::Value* zero = llvm::ConstantInt::get(llvm::Type::Int32Ty, 0, false);
    llvm::Value* one = llvm::ConstantInt::get(llvm::Type::Int32Ty, 1, false);

    llvm::Value* arrdim = DtoGEP(arr,zero,zero,"tmp",gIR->scopebb());
    new llvm::StoreInst(dim, arrdim, gIR->scopebb());
    
    llvm::Value* arrptr = DtoGEP(arr,zero,one,"tmp",gIR->scopebb());
    new llvm::StoreInst(ptr, arrptr, gIR->scopebb());
}

//////////////////////////////////////////////////////////////////////////////////////////
llvm::Constant* DtoConstArrayInitializer(ArrayInitializer* arrinit)
{
    Logger::println("arr init begin");
    Type* arrinittype = DtoDType(arrinit->type);
    assert(arrinittype->ty == Tsarray);
    TypeSArray* t = (TypeSArray*)arrinittype;
    integer_t tdim = t->dim->toInteger();

    std::vector<llvm::Constant*> inits(tdim, 0);

    const llvm::Type* elemty = DtoType(arrinittype->next);

    assert(arrinit->index.dim == arrinit->value.dim);
    for (int i=0,j=0; i < tdim; ++i)
    {
        Initializer* init = 0;
        Expression* idx = (Expression*)arrinit->index.data[j];

        if (idx)
        {
            integer_t k = idx->toInteger();
            if (i == k)
            {
                init = (Initializer*)arrinit->value.data[j];
                assert(init);
                ++j;
            }
        }
        else
        {
            init = (Initializer*)arrinit->value.data[j];
            ++j;
        }

        llvm::Constant* v = 0;

        if (!init)
        {
            v = t->next->defaultInit()->toConstElem(gIR);
        }
        else if (ExpInitializer* ex = init->isExpInitializer())
        {
            v = ex->exp->toConstElem(gIR);
        }
        else if (StructInitializer* si = init->isStructInitializer())
        {
            v = DtoConstStructInitializer(si);
        }
        else if (ArrayInitializer* ai = init->isArrayInitializer())
        {
            v = DtoConstArrayInitializer(ai);
        }
        else if (init->isVoidInitializer())
        {
            v = llvm::UndefValue::get(elemty);
        }
        else
        assert(v);

        inits[i] = v;
    }

    const llvm::ArrayType* arrty = DtoStaticArrayType(t);
    return llvm::ConstantArray::get(arrty, inits);
}

//////////////////////////////////////////////////////////////////////////////////////////
static llvm::Value* get_slice_ptr(elem* e, llvm::Value*& sz)
{
    assert(e->mem);
    const llvm::Type* t = e->mem->getType()->getContainedType(0);
    llvm::Value* ret = 0;
    if (e->arg != 0) {
        // this means it's a real slice
        ret = e->mem;

        size_t elembsz = gTargetData->getTypeSize(ret->getType());
        llvm::ConstantInt* elemsz = llvm::ConstantInt::get(DtoSize_t(), elembsz, false);

        if (llvm::isa<llvm::ConstantInt>(e->arg)) {
            sz = llvm::ConstantExpr::getMul(elemsz, llvm::cast<llvm::Constant>(e->arg));
        }
        else {
            sz = llvm::BinaryOperator::createMul(elemsz,e->arg,"tmp",gIR->scopebb());
        }
    }
    else if (llvm::isa<llvm::ArrayType>(t)) {
        ret = DtoGEPi(e->mem, 0, 0, "tmp", gIR->scopebb());

        size_t elembsz = gTargetData->getTypeSize(ret->getType()->getContainedType(0));
        llvm::ConstantInt* elemsz = llvm::ConstantInt::get(DtoSize_t(), elembsz, false);

        size_t numelements = llvm::cast<llvm::ArrayType>(t)->getNumElements();
        llvm::ConstantInt* nelems = llvm::ConstantInt::get(DtoSize_t(), numelements, false);

        sz = llvm::ConstantExpr::getMul(elemsz, nelems);
    }
    else if (llvm::isa<llvm::StructType>(t)) {
        ret = DtoGEPi(e->mem, 0, 1, "tmp", gIR->scopebb());
        ret = new llvm::LoadInst(ret, "tmp", gIR->scopebb());

        size_t elembsz = gTargetData->getTypeSize(ret->getType()->getContainedType(0));
        llvm::ConstantInt* elemsz = llvm::ConstantInt::get(DtoSize_t(), elembsz, false);

        llvm::Value* len = DtoGEPi(e->mem, 0, 0, "tmp", gIR->scopebb());
        len = new llvm::LoadInst(len, "tmp", gIR->scopebb());
        sz = llvm::BinaryOperator::createMul(len,elemsz,"tmp",gIR->scopebb());
    }
    else {
        assert(0);
    }
    return ret;
}

void DtoArrayCopy(elem* dst, elem* src)
{
    Logger::cout() << "Array copy ((((" << *src->mem << ")))) into ((((" << *dst->mem << "))))\n";

    assert(dst->type == elem::SLICE);
    assert(src->type == elem::SLICE);

    llvm::Type* arrty = llvm::PointerType::get(llvm::Type::Int8Ty);

    llvm::Value* sz1;
    llvm::Value* dstarr = new llvm::BitCastInst(get_slice_ptr(dst,sz1),arrty,"tmp",gIR->scopebb());

    llvm::Value* sz2;
    llvm::Value* srcarr = new llvm::BitCastInst(get_slice_ptr(src,sz2),arrty,"tmp",gIR->scopebb());

    llvm::Function* fn = (global.params.is64bit) ? LLVM_DeclareMemCpy64() : LLVM_DeclareMemCpy32();
    std::vector<llvm::Value*> llargs;
    llargs.resize(4);
    llargs[0] = dstarr;
    llargs[1] = srcarr;
    llargs[2] = sz1;
    llargs[3] = llvm::ConstantInt::get(llvm::Type::Int32Ty, 0, false);

    new llvm::CallInst(fn, llargs.begin(), llargs.end(), "", gIR->scopebb());
}

//////////////////////////////////////////////////////////////////////////////////////////
void DtoStaticArrayCopy(llvm::Value* dst, llvm::Value* src)
{
    assert(dst->getType() == src->getType());
    size_t arrsz = gTargetData->getTypeSize(dst->getType()->getContainedType(0));
    llvm::Value* n = llvm::ConstantInt::get(DtoSize_t(), arrsz, false);

    llvm::Type* arrty = llvm::PointerType::get(llvm::Type::Int8Ty);
    llvm::Value* dstarr = new llvm::BitCastInst(dst,arrty,"tmp",gIR->scopebb());
    llvm::Value* srcarr = new llvm::BitCastInst(src,arrty,"tmp",gIR->scopebb());

    llvm::Function* fn = (global.params.is64bit) ? LLVM_DeclareMemCpy64() : LLVM_DeclareMemCpy32();
    std::vector<llvm::Value*> llargs;
    llargs.resize(4);
    llargs[0] = dstarr;
    llargs[1] = srcarr;
    llargs[2] = n;
    llargs[3] = llvm::ConstantInt::get(llvm::Type::Int32Ty, 0, false);

    new llvm::CallInst(fn, llargs.begin(), llargs.end(), "", gIR->scopebb());
}

//////////////////////////////////////////////////////////////////////////////////////////
llvm::Constant* DtoConstSlice(llvm::Constant* dim, llvm::Constant* ptr)
{
    std::vector<const llvm::Type*> types;
    types.push_back(dim->getType());
    types.push_back(ptr->getType());
    const llvm::StructType* type = llvm::StructType::get(types);
    std::vector<llvm::Constant*> values;
    values.push_back(dim);
    values.push_back(ptr);
    return llvm::ConstantStruct::get(type,values);
}

//////////////////////////////////////////////////////////////////////////////////////////
llvm::Value* DtoNewDynArray(llvm::Value* dst, llvm::Value* dim, Type* dty, bool doinit)
{
    const llvm::Type* ty = DtoType(dty);
    assert(ty != llvm::Type::VoidTy);
    size_t sz = gTargetData->getTypeSize(ty);
    llvm::ConstantInt* n = llvm::ConstantInt::get(DtoSize_t(), sz, false);
    llvm::Value* bytesize = (sz == 1) ? dim : llvm::BinaryOperator::createMul(n,dim,"tmp",gIR->scopebb());

    llvm::Value* nullptr = llvm::ConstantPointerNull::get(llvm::PointerType::get(ty));

    llvm::Value* newptr = DtoRealloc(nullptr, bytesize);

    if (doinit) {
        elem* e = dty->defaultInit()->toElem(gIR);
        DtoArrayInit(newptr,dim,e->getValue());
        delete e;
    }

    llvm::Value* lenptr = DtoGEPi(dst,0,0,"tmp",gIR->scopebb());
    new llvm::StoreInst(dim,lenptr,gIR->scopebb());
    llvm::Value* ptrptr = DtoGEPi(dst,0,1,"tmp",gIR->scopebb());
    new llvm::StoreInst(newptr,ptrptr,gIR->scopebb());

    return newptr;
}

//////////////////////////////////////////////////////////////////////////////////////////
void DtoResizeDynArray(llvm::Value* arr, llvm::Value* sz)
{
    llvm::Value* ptr = DtoGEPi(arr, 0, 1, "tmp", gIR->scopebb());
    llvm::Value* ptrld = new llvm::LoadInst(ptr,"tmp",gIR->scopebb());

    size_t isz = gTargetData->getTypeSize(ptrld->getType()->getContainedType(0));
    llvm::ConstantInt* n = llvm::ConstantInt::get(DtoSize_t(), isz, false);
    llvm::Value* bytesz = (isz == 1) ? sz : llvm::BinaryOperator::createMul(n,sz,"tmp",gIR->scopebb());

    llvm::Value* newptr = DtoRealloc(ptrld, bytesz);
    new llvm::StoreInst(newptr,ptr,gIR->scopebb());

    llvm::Value* len = DtoGEPi(arr, 0, 0, "tmp", gIR->scopebb());
    new llvm::StoreInst(sz,len,gIR->scopebb());
}

//////////////////////////////////////////////////////////////////////////////////////////
void DtoCatAssignElement(llvm::Value* arr, Expression* exp)
{
    llvm::Value* ptr = DtoGEPi(arr, 0, 0, "tmp", gIR->scopebb());
    llvm::Value* idx = new llvm::LoadInst(ptr, "tmp", gIR->scopebb());
    llvm::Value* one = llvm::ConstantInt::get(idx->getType(),1,false);
    llvm::Value* len = llvm::BinaryOperator::createAdd(idx, one, "tmp", gIR->scopebb());
    DtoResizeDynArray(arr,len);

    ptr = DtoGEPi(arr, 0, 1, "tmp", gIR->scopebb());
    ptr = new llvm::LoadInst(ptr, "tmp", gIR->scopebb());
    ptr = new llvm::GetElementPtrInst(ptr, idx, "tmp", gIR->scopebb());

    elem* e = exp->toElem(gIR);
    Type* et = DtoDType(exp->type);
    DtoAssign(et, ptr, e->getValue());
    delete e;
}

//////////////////////////////////////////////////////////////////////////////////////////
void DtoCatArrays(llvm::Value* arr, Expression* exp1, Expression* exp2)
{
    Type* t1 = DtoDType(exp1->type);
    Type* t2 = DtoDType(exp2->type);

    assert(t1->ty == Tarray);
    assert(t1->ty == t2->ty);

    elem* e1 = exp1->toElem(gIR);
    llvm::Value* a = e1->getValue();
    delete e1;

    elem* e2 = exp2->toElem(gIR);
    llvm::Value* b = e2->getValue();
    delete e2;

    llvm::Value *len1, *len2, *src1, *src2, *res;
    len1 = gIR->ir->CreateLoad(DtoGEPi(a,0,0,"tmp"),"tmp");
    len2 = gIR->ir->CreateLoad(DtoGEPi(b,0,0,"tmp"),"tmp");
    res = gIR->ir->CreateAdd(len1,len2,"tmp");

    llvm::Value* mem = DtoNewDynArray(arr, res, DtoDType(t1->next), false);

    src1 = gIR->ir->CreateLoad(DtoGEPi(a,0,1,"tmp"),"tmp");
    src2 = gIR->ir->CreateLoad(DtoGEPi(b,0,1,"tmp"),"tmp");

    DtoMemCpy(mem,src1,len1);
    mem = gIR->ir->CreateGEP(mem,len1,"tmp");
    DtoMemCpy(mem,src2,len2);
}

//////////////////////////////////////////////////////////////////////////////////////////
llvm::Value* DtoStaticArrayCompare(TOK op, llvm::Value* l, llvm::Value* r)
{
    const char* fname;
    if (op == TOKequal)
        fname = "_d_static_array_eq";
    else if (op == TOKnotequal)
        fname = "_d_static_array_neq";
    else
        assert(0);
    llvm::Function* fn = LLVM_D_GetRuntimeFunction(gIR->module, fname);
    assert(fn);

    assert(l->getType() == r->getType());
    assert(llvm::isa<llvm::PointerType>(l->getType()));
    const llvm::Type* arrty = l->getType()->getContainedType(0);
    assert(llvm::isa<llvm::ArrayType>(arrty));
    
    llvm::Value* ll = new llvm::BitCastInst(l, llvm::PointerType::get(llvm::Type::Int8Ty), "tmp", gIR->scopebb());
    llvm::Value* rr = new llvm::BitCastInst(r, llvm::PointerType::get(llvm::Type::Int8Ty), "tmp", gIR->scopebb());
    llvm::Value* n = llvm::ConstantInt::get(DtoSize_t(),gTargetData->getTypeSize(arrty),false);

    std::vector<llvm::Value*> args;
    args.push_back(ll);
    args.push_back(rr);
    args.push_back(n);
    return new llvm::CallInst(fn, args.begin(), args.end(), "tmp", gIR->scopebb());
}

//////////////////////////////////////////////////////////////////////////////////////////

llvm::Value* DtoDynArrayCompare(TOK op, llvm::Value* l, llvm::Value* r)
{
    const char* fname;
    if (op == TOKequal)
        fname = "_d_dyn_array_eq";
    else if (op == TOKnotequal)
        fname = "_d_dyn_array_neq";
    else
        assert(0);
    llvm::Function* fn = LLVM_D_GetRuntimeFunction(gIR->module, fname);
    assert(fn);

    Logger::cout() << "lhsType:" << *l->getType() << "\nrhsType:" << *r->getType() << '\n';
    assert(l->getType() == r->getType());
    assert(llvm::isa<llvm::PointerType>(l->getType()));
    const llvm::Type* arrty = l->getType()->getContainedType(0);
    assert(llvm::isa<llvm::StructType>(arrty));
    const llvm::StructType* structType = llvm::cast<llvm::StructType>(arrty);
    const llvm::Type* elemType = structType->getElementType(1)->getContainedType(0);

    std::vector<const llvm::Type*> arrTypes;
    arrTypes.push_back(DtoSize_t());
    arrTypes.push_back(llvm::PointerType::get(llvm::Type::Int8Ty));
    const llvm::StructType* arrType = llvm::StructType::get(arrTypes);

    llvm::Value* llmem = l;
    llvm::Value* rrmem = r;

    if (arrty != arrType) {
        llmem= new llvm::AllocaInst(arrType,"tmparr",gIR->topallocapoint());

        llvm::Value* ll = gIR->ir->CreateLoad(DtoGEPi(l, 0,0, "tmp"),"tmp");
        ll = DtoArrayCastLength(ll, elemType, llvm::Type::Int8Ty);
        llvm::Value* lllen = DtoGEPi(llmem, 0,0, "tmp");
        gIR->ir->CreateStore(ll,lllen);

        ll = gIR->ir->CreateLoad(DtoGEPi(l, 0,1, "tmp"),"tmp");
        ll = new llvm::BitCastInst(ll, llvm::PointerType::get(llvm::Type::Int8Ty), "tmp", gIR->scopebb());
        llvm::Value* llptr = DtoGEPi(llmem, 0,1, "tmp");
        gIR->ir->CreateStore(ll,llptr);

        rrmem = new llvm::AllocaInst(arrType,"tmparr",gIR->topallocapoint());

        llvm::Value* rr = gIR->ir->CreateLoad(DtoGEPi(r, 0,0, "tmp"),"tmp");
        rr = DtoArrayCastLength(rr, elemType, llvm::Type::Int8Ty);
        llvm::Value* rrlen = DtoGEPi(rrmem, 0,0, "tmp");
        gIR->ir->CreateStore(rr,rrlen);

        rr = gIR->ir->CreateLoad(DtoGEPi(r, 0,1, "tmp"),"tmp");
        rr = new llvm::BitCastInst(rr, llvm::PointerType::get(llvm::Type::Int8Ty), "tmp", gIR->scopebb());
        llvm::Value* rrptr = DtoGEPi(rrmem, 0,1, "tmp");
        gIR->ir->CreateStore(rr,rrptr);
    }

    std::vector<llvm::Value*> args;
    args.push_back(llmem);
    args.push_back(rrmem);
    return new llvm::CallInst(fn, args.begin(), args.end(), "tmp", gIR->scopebb());
}

//////////////////////////////////////////////////////////////////////////////////////////
llvm::Value* DtoArrayCastLength(llvm::Value* len, const llvm::Type* elemty, const llvm::Type* newelemty)
{
    llvm::Function* fn = LLVM_D_GetRuntimeFunction(gIR->module, "_d_array_cast_len");
    assert(fn);
    std::vector<llvm::Value*> args;
    args.push_back(len);
    args.push_back(llvm::ConstantInt::get(DtoSize_t(), gTargetData->getTypeSize(elemty), false));
    args.push_back(llvm::ConstantInt::get(DtoSize_t(), gTargetData->getTypeSize(newelemty), false));
    return new llvm::CallInst(fn, args.begin(), args.end(), "tmp", gIR->scopebb());
}

//////////////////////////////////////////////////////////////////////////////////////////
llvm::Value* DtoDynArrayIs(TOK op, llvm::Value* l, llvm::Value* r)
{
    llvm::ICmpInst::Predicate pred = (op == TOKidentity) ? llvm::ICmpInst::ICMP_EQ : llvm::ICmpInst::ICMP_NE;

    if (r == NULL) {
        llvm::Value* ll = gIR->ir->CreateLoad(DtoGEPi(l, 0,0, "tmp"),"tmp");
        llvm::Value* rl = DtoConstSize_t(0);
        llvm::Value* b1 = gIR->ir->CreateICmp(pred,ll,rl,"tmp");

        llvm::Value* lp = gIR->ir->CreateLoad(DtoGEPi(l, 0,1, "tmp"),"tmp");
        const llvm::PointerType* pty = llvm::cast<llvm::PointerType>(lp->getType());
        llvm::Value* rp = llvm::ConstantPointerNull::get(pty);
        llvm::Value* b2 = gIR->ir->CreateICmp(pred,lp,rp,"tmp");

        llvm::Value* b = gIR->ir->CreateAnd(b1,b2,"tmp");
        return b;
    }
    else {
        assert(l->getType() == r->getType());

        llvm::Value* ll = gIR->ir->CreateLoad(DtoGEPi(l, 0,0, "tmp"),"tmp");
        llvm::Value* rl = gIR->ir->CreateLoad(DtoGEPi(r, 0,0, "tmp"),"tmp");
        llvm::Value* b1 = gIR->ir->CreateICmp(pred,ll,rl,"tmp");

        llvm::Value* lp = gIR->ir->CreateLoad(DtoGEPi(l, 0,1, "tmp"),"tmp");
        llvm::Value* rp = gIR->ir->CreateLoad(DtoGEPi(r, 0,1, "tmp"),"tmp");
        llvm::Value* b2 = gIR->ir->CreateICmp(pred,lp,rp,"tmp");

        llvm::Value* b = gIR->ir->CreateAnd(b1,b2,"tmp");
        return b;
    }
}

//////////////////////////////////////////////////////////////////////////////////////////
llvm::Constant* DtoConstStaticArray(const llvm::Type* t, llvm::Constant* c)
{
    assert(llvm::isa<llvm::ArrayType>(t));
    const llvm::ArrayType* at = llvm::cast<llvm::ArrayType>(t);

    if (llvm::isa<llvm::ArrayType>(at->getElementType()))
    {
        c = DtoConstStaticArray(at->getElementType(), c);
    }
    else {
        assert(at->getElementType() == c->getType());
    }
    std::vector<llvm::Constant*> initvals;
    initvals.resize(at->getNumElements(), c);
    return llvm::ConstantArray::get(at, initvals);
}
