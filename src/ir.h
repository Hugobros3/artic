#ifndef IR_H
#define IR_H

#include <cstdint>
#include <vector>
#include <string>

#include "cast.h"
#include "types.h"
#include "loc.h"

namespace artic {

class IRBuilder;
class PrettyPrinter;
class CheckSema;
class InferSema;

typedef std::vector<const class Expr*>  ExprVec;

/// Base class for expressions.
class Expr : public Cast<Expr> {
    friend class IRBuilder;
    friend class CheckSema;
    friend class InferSema;
    friend class Parser;

public:
    Expr() : builder_(nullptr), type_(nullptr) {}
    virtual ~Expr() {}

    /// Returns the type of the expression (after type-checking).
    const Type* type() const { return type_; }
    void set_type(const Type* type) const { type_ = type; }

    /// Returns the location of the expression in the file.
    const Loc& loc() const { return loc_; }
    void set_loc(const Loc& loc) { loc_ = loc; }

    /// Returns the builder that was used to create this node.
    IRBuilder* builder() const { return builder_; }

    /// Dumps the expression without any indentation nor coloring.
    void dump() const;

    /// Computes the complexity of the expression (used for pretty printing).
    virtual size_t complexity() const { return 1; }
    /// Prints the expression in a human-readable form.
    virtual void print(PrettyPrinter&) const = 0;
    /// Type checks an expression.
    virtual void check(CheckSema&) const = 0;
    /// Infers the type of the expression.
    virtual const Type* infer(InferSema&) const = 0;

private:
    IRBuilder* builder_;
    mutable const Type* type_;
    mutable Loc loc_;
};

std::ostream& operator << (std::ostream& os, const Expr*);

/// Scalar value or vector that holds elements of the same type.
class Vector : public Expr {
    friend class IRBuilder;

public:
    union Elem {
        Elem() {}
        bool     i1 ; Elem(bool     i1 ) : i1 (i1 ) {};
        int8_t   i8 ; Elem(int8_t   i8 ) : i8 (i8 ) {};
        int16_t  i16; Elem(int16_t  i16) : i16(i16) {};
        int32_t  i32; Elem(int32_t  i32) : i32(i32) {};
        int64_t  i64; Elem(int64_t  i64) : i64(i64) {};
        uint8_t  u8 ; Elem(uint8_t  u8 ) : u8 (u8 ) {};
        uint16_t u16; Elem(uint16_t u16) : u16(u16) {};
        uint32_t u32; Elem(uint32_t u32) : u32(u32) {};
        uint64_t u64; Elem(uint64_t u64) : u64(u64) {};
        float    f32; Elem(float    f32) : f32(f32) {};
        double   f64; Elem(double   f64) : f64(f64) {};
    };

    typedef std::vector<Elem> ElemVec;

private:
    Vector() {}
    Vector(Prim p, ElemVec&& e) : prim_(p), elems_(e) {}
    Vector(Prim p, const ElemVec& e) : prim_(p), elems_(e) {}
    template <typename... Args>
    Vector(Args... args) { set(args...); }

public:
    const ElemVec& elems() const { return elems_; }

    Elem elem(int i) const { return elems_[i]; }
    Elem value() const { return elems_[0]; }

    Prim prim() const { return prim_; }
    size_t size() const { return elems_.size(); }
    void resize(size_t s) { elems_.resize(s); }

    bool is_integer() const { return artic::is_integer(prim()); }
    int bit_count() const { return artic::bitcount(prim()) * size(); }

    void print(PrettyPrinter&) const override;
    void check(CheckSema&) const override;
    const Type* infer(InferSema&) const override;

private:
    template <int K, typename T, typename... Args>
    void set_(T t, Args... args) {
        elems_[K] = Elem(t);
        set_<K + 1>(args...);
    }

    template <int K>
    void set_() {}

    template <typename T, typename... Args>
    void set(T t, Args... args) {
        // Infer the type based on the first parameter
        prim_ = RepToPrim<T>::prim();
        elems_.resize(sizeof...(args) + 1);
        set_<0>(t, args...);
    }

    Prim prim_;
    ElemVec elems_;
};

/// Tuple value that holds several values of (possibly) different types.
class Tuple : public Expr {
    friend class IRBuilder;

    Tuple(ExprVec&& v) : elems_(v) {}
    Tuple(const ExprVec& v = ExprVec()) : elems_(v) {}

public:
    const ExprVec& elems() const { return elems_; }
    const Expr* elem(int i) const { return elems_[i]; }
    size_t size() const { return elems_.size(); }

    size_t complexity() const override {
        size_t c = 1;
        for (auto e : elems()) c += e->complexity();
        return c;
    }

    void print(PrettyPrinter&) const override;
    void check(CheckSema&) const override;
    const Type* infer(InferSema&) const override;

private:
    ExprVec elems_;
};

/// Variable binding coming from a let expression.
class Var : public Expr {
    friend class IRBuilder;

    Var(const std::string& n, const Expr* b)
        : name_(n), binding_(b)
    {}

public:
    const Expr* binding() const { return binding_; }
    const std::string& name() const { return name_; }

    void print(PrettyPrinter&) const override;
    void check(CheckSema&) const override;
    const Type* infer(InferSema&) const override;

private:
    std::string name_;
    const Expr* binding_;
};

/// Parameter coming from a lambda expression.
class Param : public Expr {
    friend class IRBuilder;

    Param(const std::string& n)
        : name_(n)
    {}

public:
    const std::string& name() const { return name_; }

    void print(PrettyPrinter&) const override;
    void check(CheckSema&) const override;
    const Type* infer(InferSema&) const override;

private:
    std::string name_;
};

/// Lambda function abstraction, composed of the argument with its type, and the corresponding body.
class Lambda : public Expr {
    friend class IRBuilder;

    Lambda(const Param* p, const Expr* b = nullptr)
        : param_(p), body_(b)
    {}

public:
    const Param* param() const { return param_; }
    const Expr* body() const { return body_; }

    size_t complexity() const override { return 1 + body_->complexity(); }

    void print(PrettyPrinter&) const override;
    void check(CheckSema&) const override;
    const Type* infer(InferSema&) const override;

private:
    const Param* param_;
    const Expr* body_;
};

/// Primitive operation on values.
class PrimOp : public Expr {
    friend class IRBuilder;

public:
    enum Op {
        ADD, SUB, MUL, DIV, MOD,                    // Arithmetic
        RSHFT, LSHFT, AND, OR, XOR,                 // Bitwise
        CMP_GE, CMP_LE, CMP_GT, CMP_LT, CMP_EQ,     // Comparison
        SELECT, BITCAST, EXTRACT, INSERT            // Misc.
    };

    static int precedence(Op op) {
        switch (op) {
            case ADD:
            case SUB:
                return 4;
            case MUL:
            case DIV:
            case MOD:
                return 3;
            case RSHFT:
            case LSHFT:
                return 5;
            case AND: return 8;
            case OR:  return 10;
            case XOR: return 9;
            case CMP_GE:
            case CMP_LE:
            case CMP_GT:
            case CMP_LT:
            case CMP_EQ:
                return 14;
            default: assert(false);
        }
        return 0;
    }

    static constexpr int max_precedence() { return 14; }

private:
    PrimOp(Op op, const Type* t, const Expr* a)
        : op_(op) {
        assert(op == BITCAST);
        args_.push_back(a);
        type_args_.push_back(t);
    }

    PrimOp(Op op, const Expr* a, const Expr* b)
        : op_(op) {
        args_.push_back(a);
        args_.push_back(b);
    }

    PrimOp(Op op, const Expr* a, const Expr* b, const Expr* c)
        : op_(op) {
        assert(op == SELECT || op == INSERT);
        args_.push_back(a);
        args_.push_back(b);
        args_.push_back(c);
    }

public:
    /// Evaluates the result of the operation.
    //const Expr* eval() const;

    Op op() const { return op_; }

    const TypeVec& type_args() const { return type_args_; }
    const Type* type_arg(int i = 0) const { return type_args_[i]; }
    size_t num_type_args() const { return type_args_.size(); }

    const ExprVec& args() const { return args_; }
    const Expr* arg(int i = 0) const { return args_[i]; }
    size_t num_args() const { return args_.size(); }

    bool binary() const { return op() <= CMP_EQ; }
    int precedence() const { return precedence(op()); }

    size_t complexity() const override {
        size_t c = 1 + num_type_args();
        for (auto a : args()) c += a->complexity();
        return c;
    }

    void print(PrettyPrinter&) const override;
    void check(CheckSema&) const override;
    const Type* infer(InferSema&) const override;

private:
    void check_select(CheckSema&) const;
    void check_bitcast(CheckSema&) const;
    void check_extract_or_insert(CheckSema&, bool) const;

    Op op_;
    ExprVec args_;
    TypeVec type_args_;
};

/// If-expression, which evaluates either one of its branches based on some condition.
class IfExpr : public Expr {
    friend class IRBuilder;

    IfExpr(const Expr* cond, const Expr* if_true, const Expr* if_false)
        : cond_(cond), if_true_(if_true), if_false_(if_false)
    {}

public:
    const Expr* cond() const { return cond_; }
    const Expr* if_true() const { return if_true_; }
    const Expr* if_false() const { return if_false_; }

    size_t complexity() const override {
        return 1 +
               cond()->complexity() +
               if_true()->complexity() +
               if_false()->complexity();
    }

    void print(PrettyPrinter&) const override;
    void check(CheckSema&) const override;
    const Type* infer(InferSema&) const override;

private:
    const Expr* cond_;
    const Expr* if_true_;
    const Expr* if_false_;
};

/// Lambda application expression.
class AppExpr : public Expr {
    friend class IRBuilder;

    AppExpr(const ExprVec& args)
        : args_(args), lambda_type_(nullptr)
    {}

public:
    const Type* lambda_type() const { return lambda_type_; }
    void set_lambda_type(const Type* t) const { lambda_type_ = t; }

    const ExprVec& args() const { return args_; }
    const Expr* arg(int i) const { return args_[i]; }
    size_t num_args() const { return args_.size(); }

    size_t complexity() const override {
        size_t c = 0;
        for (int i = 0, n = num_args(); i < n; i++) c += arg(i)->complexity();
        return c;
    }

    void print(PrettyPrinter&) const override;
    void check(CheckSema&) const override;
    const Type* infer(InferSema&) const override;

private:
    mutable const Type* lambda_type_;
    ExprVec args_;
};

/// Let-expression, introducing a new variable in the scope of an expression.
class LetExpr : public Expr {
    friend class IRBuilder;

    LetExpr(const Var* var, const Expr* e = nullptr)
        : var_(var), body_(e)
    {}

public:
    const Var* var() const { return var_; }
    const Expr* body() const { return body_; }

    size_t complexity() const override {
        return 1 + var()->binding()->complexity() + body()->complexity();
    }

    void print(PrettyPrinter&) const override;
    void check(CheckSema&) const override;
    const Type* infer(InferSema&) const override;

private:
    const Var* var_;
    const Expr* body_;
};

} // namespace artic

#endif // IR_H
