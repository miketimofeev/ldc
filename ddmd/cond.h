
/* Compiler implementation of the D programming language
 * Copyright (c) 1999-2014 by Digital Mars
 * All Rights Reserved
 * written by Walter Bright
 * http://www.digitalmars.com
 * Distributed under the Boost Software License, Version 1.0.
 * http://www.boost.org/LICENSE_1_0.txt
 * https://github.com/D-Programming-Language/dmd/blob/master/src/cond.h
 */

#ifndef DMD_DEBCOND_H
#define DMD_DEBCOND_H

class Expression;
class Identifier;
struct OutBuffer;
class Module;
struct Scope;
class ScopeDsymbol;
class DebugCondition;

int findCondition(Strings *ids, Identifier *ident);

class Condition
{
public:
    Loc loc;
    // 0: not computed yet
    // 1: include
    // 2: do not include
    int inc;

    Condition(Loc loc);

    virtual Condition *syntaxCopy() = 0;
    virtual int include(Scope *sc, ScopeDsymbol *sds) = 0;
    virtual DebugCondition *isDebugCondition() { return NULL; }
    virtual void accept(Visitor *v) { v->visit(this); }
};

class DVCondition : public Condition
{
public:
    unsigned level;
    Identifier *ident;
    Module *mod;

    DVCondition(Module *mod, unsigned level, Identifier *ident);

    Condition *syntaxCopy();
    void accept(Visitor *v) { v->visit(this); }
};

class DebugCondition : public DVCondition
{
public:
    static void setGlobalLevel(unsigned level);
    static void addGlobalIdent(const char *ident);

    DebugCondition(Module *mod, unsigned level, Identifier *ident);

    int include(Scope *sc, ScopeDsymbol *sds);
    DebugCondition *isDebugCondition() { return this; }
    void accept(Visitor *v) { v->visit(this); }
};

class VersionCondition : public DVCondition
{
public:
    static void setGlobalLevel(unsigned level);
    static bool isPredefined(const char *ident);
    static void checkPredefined(Loc loc, const char *ident);
    static void addGlobalIdent(const char *ident);
    static void addPredefinedGlobalIdent(const char *ident);

    VersionCondition(Module *mod, unsigned level, Identifier *ident);

    int include(Scope *sc, ScopeDsymbol *sds);
    void accept(Visitor *v) { v->visit(this); }
};

class StaticIfCondition : public Condition
{
public:
    Expression *exp;
    int nest;         // limit circular dependencies

    StaticIfCondition(Loc loc, Expression *exp);
    Condition *syntaxCopy();
    int include(Scope *sc, ScopeDsymbol *sds);
    void accept(Visitor *v) { v->visit(this); }
};

#endif