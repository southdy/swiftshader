//===- subzero/src/IceInstARM32.h - ARM32 machine instructions --*- C++ -*-===//
//
//                        The Subzero Code Generator
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the InstARM32 and OperandARM32 classes and their
/// subclasses. This represents the machine instructions and operands used for
/// ARM32 code selection.
///
//===----------------------------------------------------------------------===//

#ifndef SUBZERO_SRC_ICEINSTARM32_H
#define SUBZERO_SRC_ICEINSTARM32_H

#include "IceConditionCodesARM32.h"
#include "IceDefs.h"
#include "IceInst.h"
#include "IceInstARM32.def"
#include "IceOperand.h"

namespace Ice {

class TargetARM32;

/// OperandARM32 extends the Operand hierarchy. Its subclasses are
/// OperandARM32Mem and OperandARM32Flex.
class OperandARM32 : public Operand {
  OperandARM32() = delete;
  OperandARM32(const OperandARM32 &) = delete;
  OperandARM32 &operator=(const OperandARM32 &) = delete;

public:
  enum OperandKindARM32 {
    k__Start = Operand::kTarget,
    kMem,
    kFlexStart,
    kFlexImm = kFlexStart,
    kFlexReg,
    kFlexEnd = kFlexReg
  };

  enum ShiftKind {
    kNoShift = -1,
#define X(enum, emit) enum,
    ICEINSTARM32SHIFT_TABLE
#undef X
  };

  using Operand::dump;
  void dump(const Cfg *, Ostream &Str) const override {
    if (BuildDefs::dump())
      Str << "<OperandARM32>";
  }

protected:
  OperandARM32(OperandKindARM32 Kind, Type Ty)
      : Operand(static_cast<OperandKind>(Kind), Ty) {}
};

/// OperandARM32Mem represents a memory operand in any of the various ARM32
/// addressing modes.
class OperandARM32Mem : public OperandARM32 {
  OperandARM32Mem() = delete;
  OperandARM32Mem(const OperandARM32Mem &) = delete;
  OperandARM32Mem &operator=(const OperandARM32Mem &) = delete;

public:
  /// Memory operand addressing mode.
  /// The enum value also carries the encoding.
  // TODO(jvoung): unify with the assembler.
  enum AddrMode {
    // bit encoding P U W
    Offset = (8 | 4 | 0) << 21,      // offset (w/o writeback to base)
    PreIndex = (8 | 4 | 1) << 21,    // pre-indexed addressing with writeback
    PostIndex = (0 | 4 | 0) << 21,   // post-indexed addressing with writeback
    NegOffset = (8 | 0 | 0) << 21,   // negative offset (w/o writeback to base)
    NegPreIndex = (8 | 0 | 1) << 21, // negative pre-indexed with writeback
    NegPostIndex = (0 | 0 | 0) << 21 // negative post-indexed with writeback
  };

  /// Provide two constructors.
  /// NOTE: The Variable-typed operands have to be registers.
  ///
  /// (1) Reg + Imm. The Immediate actually has a limited number of bits
  /// for encoding, so check canHoldOffset first. It cannot handle general
  /// Constant operands like ConstantRelocatable, since a relocatable can
  /// potentially take up too many bits.
  static OperandARM32Mem *create(Cfg *Func, Type Ty, Variable *Base,
                                 ConstantInteger32 *ImmOffset,
                                 AddrMode Mode = Offset) {
    return new (Func->allocate<OperandARM32Mem>())
        OperandARM32Mem(Func, Ty, Base, ImmOffset, Mode);
  }
  /// (2) Reg +/- Reg with an optional shift of some kind and amount. Note that
  /// this mode is disallowed in the NaCl sandbox.
  static OperandARM32Mem *create(Cfg *Func, Type Ty, Variable *Base,
                                 Variable *Index, ShiftKind ShiftOp = kNoShift,
                                 uint16_t ShiftAmt = 0,
                                 AddrMode Mode = Offset) {
    return new (Func->allocate<OperandARM32Mem>())
        OperandARM32Mem(Func, Ty, Base, Index, ShiftOp, ShiftAmt, Mode);
  }
  Variable *getBase() const { return Base; }
  ConstantInteger32 *getOffset() const { return ImmOffset; }
  Variable *getIndex() const { return Index; }
  ShiftKind getShiftOp() const { return ShiftOp; }
  uint16_t getShiftAmt() const { return ShiftAmt; }
  AddrMode getAddrMode() const { return Mode; }

  bool isRegReg() const { return Index != nullptr; }
  bool isNegAddrMode() const {
    // Positive address modes have the "U" bit set, and negative modes don't.
    static_assert((PreIndex & (4 << 21)) != 0,
                  "Positive addr modes should have U bit set.");
    static_assert((NegPreIndex & (4 << 21)) == 0,
                  "Negative addr modes should have U bit clear.");
    return (Mode & (4 << 21)) == 0;
  }

  void emit(const Cfg *Func) const override;
  using OperandARM32::dump;
  void dump(const Cfg *Func, Ostream &Str) const override;

  static bool classof(const Operand *Operand) {
    return Operand->getKind() == static_cast<OperandKind>(kMem);
  }

  /// Return true if a load/store instruction for an element of type Ty can
  /// encode the Offset directly in the immediate field of the 32-bit ARM
  /// instruction. For some types, if the load is Sign extending, then the range
  /// is reduced.
  static bool canHoldOffset(Type Ty, bool SignExt, int32_t Offset);

private:
  OperandARM32Mem(Cfg *Func, Type Ty, Variable *Base,
                  ConstantInteger32 *ImmOffset, AddrMode Mode);
  OperandARM32Mem(Cfg *Func, Type Ty, Variable *Base, Variable *Index,
                  ShiftKind ShiftOp, uint16_t ShiftAmt, AddrMode Mode);

  Variable *Base;
  ConstantInteger32 *ImmOffset;
  Variable *Index;
  ShiftKind ShiftOp;
  uint16_t ShiftAmt;
  AddrMode Mode;
};

/// OperandARM32Flex represent the "flexible second operand" for data-processing
/// instructions. It can be a rotatable 8-bit constant, or a register with an
/// optional shift operand. The shift amount can even be a third register.
class OperandARM32Flex : public OperandARM32 {
  OperandARM32Flex() = delete;
  OperandARM32Flex(const OperandARM32Flex &) = delete;
  OperandARM32Flex &operator=(const OperandARM32Flex &) = delete;

public:
  static bool classof(const Operand *Operand) {
    return static_cast<OperandKind>(kFlexStart) <= Operand->getKind() &&
           Operand->getKind() <= static_cast<OperandKind>(kFlexEnd);
  }

protected:
  OperandARM32Flex(OperandKindARM32 Kind, Type Ty) : OperandARM32(Kind, Ty) {}
};

/// Rotated immediate variant.
class OperandARM32FlexImm : public OperandARM32Flex {
  OperandARM32FlexImm() = delete;
  OperandARM32FlexImm(const OperandARM32FlexImm &) = delete;
  OperandARM32FlexImm &operator=(const OperandARM32FlexImm &) = delete;

public:
  /// Immed_8 rotated by an even number of bits (2 * RotateAmt).
  static OperandARM32FlexImm *create(Cfg *Func, Type Ty, uint32_t Imm,
                                     uint32_t RotateAmt) {
    return new (Func->allocate<OperandARM32FlexImm>())
        OperandARM32FlexImm(Func, Ty, Imm, RotateAmt);
  }

  void emit(const Cfg *Func) const override;
  using OperandARM32::dump;
  void dump(const Cfg *Func, Ostream &Str) const override;

  static bool classof(const Operand *Operand) {
    return Operand->getKind() == static_cast<OperandKind>(kFlexImm);
  }

  /// Return true if the Immediate can fit in the ARM flexible operand. Fills in
  /// the out-params RotateAmt and Immed_8 if Immediate fits.
  static bool canHoldImm(uint32_t Immediate, uint32_t *RotateAmt,
                         uint32_t *Immed_8);

  uint32_t getImm() const { return Imm; }
  uint32_t getRotateAmt() const { return RotateAmt; }

private:
  OperandARM32FlexImm(Cfg *Func, Type Ty, uint32_t Imm, uint32_t RotateAmt);

  uint32_t Imm;
  uint32_t RotateAmt;
};

/// Shifted register variant.
class OperandARM32FlexReg : public OperandARM32Flex {
  OperandARM32FlexReg() = delete;
  OperandARM32FlexReg(const OperandARM32FlexReg &) = delete;
  OperandARM32FlexReg &operator=(const OperandARM32FlexReg &) = delete;

public:
  /// Register with immediate/reg shift amount and shift operation.
  static OperandARM32FlexReg *create(Cfg *Func, Type Ty, Variable *Reg,
                                     ShiftKind ShiftOp, Operand *ShiftAmt) {
    return new (Func->allocate<OperandARM32FlexReg>())
        OperandARM32FlexReg(Func, Ty, Reg, ShiftOp, ShiftAmt);
  }

  void emit(const Cfg *Func) const override;
  using OperandARM32::dump;
  void dump(const Cfg *Func, Ostream &Str) const override;

  static bool classof(const Operand *Operand) {
    return Operand->getKind() == static_cast<OperandKind>(kFlexReg);
  }

  Variable *getReg() const { return Reg; }
  ShiftKind getShiftOp() const { return ShiftOp; }
  /// ShiftAmt can represent an immediate or a register.
  Operand *getShiftAmt() const { return ShiftAmt; }

private:
  OperandARM32FlexReg(Cfg *Func, Type Ty, Variable *Reg, ShiftKind ShiftOp,
                      Operand *ShiftAmt);

  Variable *Reg;
  ShiftKind ShiftOp;
  Operand *ShiftAmt;
};

/// StackVariable represents a Var that isn't assigned a register (stack-only).
/// It is assigned a stack slot, but the slot's offset may be too large to
/// represent in the native addressing mode, and so it has a separate base
/// register from SP/FP, where the offset from that base register is then in
/// range.
class StackVariable final : public Variable {
  StackVariable() = delete;
  StackVariable(const StackVariable &) = delete;
  StackVariable &operator=(const StackVariable &) = delete;

public:
  static StackVariable *create(Cfg *Func, Type Ty, SizeT Index) {
    return new (Func->allocate<StackVariable>()) StackVariable(Ty, Index);
  }
  const static OperandKind StackVariableKind =
      static_cast<OperandKind>(kVariable_Target);
  static bool classof(const Operand *Operand) {
    return Operand->getKind() == StackVariableKind;
  }
  void setBaseRegNum(int32_t RegNum) { BaseRegNum = RegNum; }
  int32_t getBaseRegNum() const override { return BaseRegNum; }
  // Inherit dump() and emit() from Variable.

private:
  StackVariable(Type Ty, SizeT Index)
      : Variable(StackVariableKind, Ty, Index) {}
  int32_t BaseRegNum = Variable::NoRegister;
};

/// Base class for ARM instructions. While most ARM instructions can be
/// conditionally executed, a few of them are not predicable (halt, memory
/// barriers, etc.).
class InstARM32 : public InstTarget {
  InstARM32() = delete;
  InstARM32(const InstARM32 &) = delete;
  InstARM32 &operator=(const InstARM32 &) = delete;

public:
  enum InstKindARM32 {
    k__Start = Inst::Target,
    Adc,
    Add,
    Adjuststack,
    And,
    Asr,
    Bic,
    Br,
    Call,
    Cmp,
    Clz,
    Dmb,
    Eor,
    Label,
    Ldr,
    Ldrex,
    Lsl,
    Lsr,
    Mla,
    Mls,
    Mov,
    Movt,
    Movw,
    Mul,
    Mvn,
    Orr,
    Pop,
    Push,
    Rbit,
    Ret,
    Rev,
    Rsb,
    Sbc,
    Sdiv,
    Str,
    Strex,
    Sub,
    Sxt,
    Trap,
    Tst,
    Udiv,
    Umull,
    Uxt,
    Vabs,
    Vadd,
    Vcmp,
    Vcvt,
    Vdiv,
    Vmrs,
    Vmul,
    Vsqrt,
    Vsub
  };

  static constexpr size_t InstSize = sizeof(uint32_t);

  static const char *getWidthString(Type Ty);
  static const char *getVecWidthString(Type Ty);
  static CondARM32::Cond getOppositeCondition(CondARM32::Cond Cond);

  /// Called inside derived methods emit() to communicate that multiple
  /// instructions are being generated. Used by emitIAS() methods to
  /// generate textual fixups for instructions that are not yet
  /// implemented.
  void startNextInst(const Cfg *Func) const;

  /// Shared emit routines for common forms of instructions.
  static void emitThreeAddrFP(const char *Opcode, const InstARM32 *Inst,
                              const Cfg *Func);

  void dump(const Cfg *Func) const override;

  void emitIAS(const Cfg *Func) const override;

protected:
  InstARM32(Cfg *Func, InstKindARM32 Kind, SizeT Maxsrcs, Variable *Dest)
      : InstTarget(Func, static_cast<InstKind>(Kind), Maxsrcs, Dest) {}

  static bool isClassof(const Inst *Inst, InstKindARM32 MyKind) {
    return Inst->getKind() == static_cast<InstKind>(MyKind);
  }

  // Generates text of assembly instruction using method emit(), and then adds
  // to the assembly buffer as a Fixup.
  void emitUsingTextFixup(const Cfg *Func) const;
};

/// A predicable ARM instruction.
class InstARM32Pred : public InstARM32 {
  InstARM32Pred() = delete;
  InstARM32Pred(const InstARM32Pred &) = delete;
  InstARM32Pred &operator=(const InstARM32Pred &) = delete;

public:
  InstARM32Pred(Cfg *Func, InstKindARM32 Kind, SizeT Maxsrcs, Variable *Dest,
                CondARM32::Cond Predicate)
      : InstARM32(Func, Kind, Maxsrcs, Dest), Predicate(Predicate) {}

  CondARM32::Cond getPredicate() const { return Predicate; }
  void setPredicate(CondARM32::Cond Pred) { Predicate = Pred; }

  static const char *predString(CondARM32::Cond Predicate);
  void dumpOpcodePred(Ostream &Str, const char *Opcode, Type Ty) const;

  /// Shared emit routines for common forms of instructions.
  static void emitUnaryopGPR(const char *Opcode, const InstARM32Pred *Inst,
                             const Cfg *Func, bool NeedsWidthSuffix);
  static void emitUnaryopFP(const char *Opcode, const InstARM32Pred *Inst,
                            const Cfg *Func);
  static void emitTwoAddr(const char *Opcode, const InstARM32Pred *Inst,
                          const Cfg *Func);
  static void emitThreeAddr(const char *Opcode, const InstARM32Pred *Inst,
                            const Cfg *Func, bool SetFlags);
  static void emitFourAddr(const char *Opcode, const InstARM32Pred *Inst,
                           const Cfg *Func);
  static void emitCmpLike(const char *Opcode, const InstARM32Pred *Inst,
                          const Cfg *Func);

protected:
  CondARM32::Cond Predicate;
};

template <typename StreamType>
inline StreamType &operator<<(StreamType &Stream, CondARM32::Cond Predicate) {
  Stream << InstARM32Pred::predString(Predicate);
  return Stream;
}

/// Instructions of the form x := op(y).
template <InstARM32::InstKindARM32 K, bool NeedsWidthSuffix>
class InstARM32UnaryopGPR : public InstARM32Pred {
  InstARM32UnaryopGPR() = delete;
  InstARM32UnaryopGPR(const InstARM32UnaryopGPR &) = delete;
  InstARM32UnaryopGPR &operator=(const InstARM32UnaryopGPR &) = delete;

public:
  static InstARM32UnaryopGPR *create(Cfg *Func, Variable *Dest, Operand *Src,
                                     CondARM32::Cond Predicate) {
    return new (Func->allocate<InstARM32UnaryopGPR>())
        InstARM32UnaryopGPR(Func, Dest, Src, Predicate);
  }
  void emit(const Cfg *Func) const override {
    if (!BuildDefs::dump())
      return;
    emitUnaryopGPR(Opcode, this, Func, NeedsWidthSuffix);
  }
  void emitIAS(const Cfg *Func) const override;
  void dump(const Cfg *Func) const override {
    if (!BuildDefs::dump())
      return;
    Ostream &Str = Func->getContext()->getStrDump();
    dumpDest(Func);
    Str << " = ";
    dumpOpcodePred(Str, Opcode, getDest()->getType());
    Str << " ";
    dumpSources(Func);
  }
  static bool classof(const Inst *Inst) { return isClassof(Inst, K); }

private:
  InstARM32UnaryopGPR(Cfg *Func, Variable *Dest, Operand *Src,
                      CondARM32::Cond Predicate)
      : InstARM32Pred(Func, K, 1, Dest, Predicate) {
    addSource(Src);
  }

  static const char *Opcode;
};

/// Instructions of the form x := op(y), for vector/FP.
template <InstARM32::InstKindARM32 K>
class InstARM32UnaryopFP : public InstARM32Pred {
  InstARM32UnaryopFP() = delete;
  InstARM32UnaryopFP(const InstARM32UnaryopFP &) = delete;
  InstARM32UnaryopFP &operator=(const InstARM32UnaryopFP &) = delete;

public:
  static InstARM32UnaryopFP *create(Cfg *Func, Variable *Dest, Variable *Src,
                                    CondARM32::Cond Predicate) {
    return new (Func->allocate<InstARM32UnaryopFP>())
        InstARM32UnaryopFP(Func, Dest, Src, Predicate);
  }
  void emit(const Cfg *Func) const override {
    if (!BuildDefs::dump())
      return;
    emitUnaryopFP(Opcode, this, Func);
  }
  void dump(const Cfg *Func) const override {
    if (!BuildDefs::dump())
      return;
    Ostream &Str = Func->getContext()->getStrDump();
    dumpDest(Func);
    Str << " = ";
    dumpOpcodePred(Str, Opcode, getDest()->getType());
    Str << " ";
    dumpSources(Func);
  }
  static bool classof(const Inst *Inst) { return isClassof(Inst, K); }

private:
  InstARM32UnaryopFP(Cfg *Func, Variable *Dest, Operand *Src,
                     CondARM32::Cond Predicate)
      : InstARM32Pred(Func, K, 1, Dest, Predicate) {
    addSource(Src);
  }

  static const char *Opcode;
};

/// Instructions of the form x := x op y.
template <InstARM32::InstKindARM32 K>
class InstARM32TwoAddrGPR : public InstARM32Pred {
  InstARM32TwoAddrGPR() = delete;
  InstARM32TwoAddrGPR(const InstARM32TwoAddrGPR &) = delete;
  InstARM32TwoAddrGPR &operator=(const InstARM32TwoAddrGPR &) = delete;

public:
  /// Dest must be a register.
  static InstARM32TwoAddrGPR *create(Cfg *Func, Variable *Dest, Operand *Src,
                                     CondARM32::Cond Predicate) {
    return new (Func->allocate<InstARM32TwoAddrGPR>())
        InstARM32TwoAddrGPR(Func, Dest, Src, Predicate);
  }
  void emit(const Cfg *Func) const override {
    if (!BuildDefs::dump())
      return;
    emitTwoAddr(Opcode, this, Func);
  }
  void emitIAS(const Cfg *Func) const override;
  void dump(const Cfg *Func) const override {
    if (!BuildDefs::dump())
      return;
    Ostream &Str = Func->getContext()->getStrDump();
    dumpDest(Func);
    Str << " = ";
    dumpOpcodePred(Str, Opcode, getDest()->getType());
    Str << " ";
    dumpSources(Func);
  }
  static bool classof(const Inst *Inst) { return isClassof(Inst, K); }

private:
  InstARM32TwoAddrGPR(Cfg *Func, Variable *Dest, Operand *Src,
                      CondARM32::Cond Predicate)
      : InstARM32Pred(Func, K, 2, Dest, Predicate) {
    addSource(Dest);
    addSource(Src);
  }

  static const char *Opcode;
};

/// Base class for load instructions.
template <InstARM32::InstKindARM32 K>
class InstARM32LoadBase : public InstARM32Pred {
  InstARM32LoadBase() = delete;
  InstARM32LoadBase(const InstARM32LoadBase &) = delete;
  InstARM32LoadBase &operator=(const InstARM32LoadBase &) = delete;

public:
  static InstARM32LoadBase *create(Cfg *Func, Variable *Dest, Operand *Source,
                                   CondARM32::Cond Predicate) {
    return new (Func->allocate<InstARM32LoadBase>())
        InstARM32LoadBase(Func, Dest, Source, Predicate);
  }
  void emit(const Cfg *Func) const override;
  void emitIAS(const Cfg *Func) const override;
  void dump(const Cfg *Func) const override {
    if (!BuildDefs::dump())
      return;
    Ostream &Str = Func->getContext()->getStrDump();
    dumpOpcodePred(Str, Opcode, getDest()->getType());
    Str << " ";
    dumpDest(Func);
    Str << ", ";
    dumpSources(Func);
  }
  static bool classof(const Inst *Inst) { return isClassof(Inst, K); }

private:
  InstARM32LoadBase(Cfg *Func, Variable *Dest, Operand *Source,
                    CondARM32::Cond Predicate)
      : InstARM32Pred(Func, K, 1, Dest, Predicate) {
    addSource(Source);
  }

  static const char *Opcode;
};

/// Instructions of the form x := y op z. May have the side-effect of setting
/// status flags.
template <InstARM32::InstKindARM32 K>
class InstARM32ThreeAddrGPR : public InstARM32Pred {
  InstARM32ThreeAddrGPR() = delete;
  InstARM32ThreeAddrGPR(const InstARM32ThreeAddrGPR &) = delete;
  InstARM32ThreeAddrGPR &operator=(const InstARM32ThreeAddrGPR &) = delete;

public:
  /// Create an ordinary binary-op instruction like add, and sub. Dest and Src1
  /// must be registers.
  static InstARM32ThreeAddrGPR *create(Cfg *Func, Variable *Dest,
                                       Variable *Src0, Operand *Src1,
                                       CondARM32::Cond Predicate,
                                       bool SetFlags = false) {
    return new (Func->allocate<InstARM32ThreeAddrGPR>())
        InstARM32ThreeAddrGPR(Func, Dest, Src0, Src1, Predicate, SetFlags);
  }
  void emit(const Cfg *Func) const override {
    if (!BuildDefs::dump())
      return;
    emitThreeAddr(Opcode, this, Func, SetFlags);
  }
  void emitIAS(const Cfg *Func) const override;
  void dump(const Cfg *Func) const override {
    if (!BuildDefs::dump())
      return;
    Ostream &Str = Func->getContext()->getStrDump();
    dumpDest(Func);
    Str << " = ";
    dumpOpcodePred(Str, Opcode, getDest()->getType());
    Str << (SetFlags ? ".s " : " ");
    dumpSources(Func);
  }
  static bool classof(const Inst *Inst) { return isClassof(Inst, K); }

private:
  InstARM32ThreeAddrGPR(Cfg *Func, Variable *Dest, Variable *Src0,
                        Operand *Src1, CondARM32::Cond Predicate, bool SetFlags)
      : InstARM32Pred(Func, K, 2, Dest, Predicate), SetFlags(SetFlags) {
    addSource(Src0);
    addSource(Src1);
  }

  static const char *Opcode;
  bool SetFlags;
};

/// Instructions of the form x := y op z, for vector/FP. We leave these as
/// unconditional: "ARM deprecates the conditional execution of any instruction
/// encoding provided by the Advanced SIMD Extension that is not also provided
/// by the Floating-point (VFP) extension". They do not set flags.
template <InstARM32::InstKindARM32 K>
class InstARM32ThreeAddrFP : public InstARM32 {
  InstARM32ThreeAddrFP() = delete;
  InstARM32ThreeAddrFP(const InstARM32ThreeAddrFP &) = delete;
  InstARM32ThreeAddrFP &operator=(const InstARM32ThreeAddrFP &) = delete;

public:
  /// Create a vector/FP binary-op instruction like vadd, and vsub. Everything
  /// must be a register.
  static InstARM32ThreeAddrFP *create(Cfg *Func, Variable *Dest, Variable *Src0,
                                      Variable *Src1) {
    return new (Func->allocate<InstARM32ThreeAddrFP>())
        InstARM32ThreeAddrFP(Func, Dest, Src0, Src1);
  }
  void emit(const Cfg *Func) const override {
    if (!BuildDefs::dump())
      return;
    emitThreeAddrFP(Opcode, this, Func);
  }
  void dump(const Cfg *Func) const override {
    if (!BuildDefs::dump())
      return;
    Ostream &Str = Func->getContext()->getStrDump();
    dumpDest(Func);
    Str << " = ";
    Str << Opcode << "." << getDest()->getType() << " ";
    dumpSources(Func);
  }
  static bool classof(const Inst *Inst) { return isClassof(Inst, K); }

private:
  InstARM32ThreeAddrFP(Cfg *Func, Variable *Dest, Variable *Src0,
                       Variable *Src1)
      : InstARM32(Func, K, 2, Dest) {
    addSource(Src0);
    addSource(Src1);
  }

  static const char *Opcode;
};

/// Instructions of the form x := a op1 (y op2 z). E.g., multiply accumulate.
template <InstARM32::InstKindARM32 K>
class InstARM32FourAddrGPR : public InstARM32Pred {
  InstARM32FourAddrGPR() = delete;
  InstARM32FourAddrGPR(const InstARM32FourAddrGPR &) = delete;
  InstARM32FourAddrGPR &operator=(const InstARM32FourAddrGPR &) = delete;

public:
  // Every operand must be a register.
  static InstARM32FourAddrGPR *create(Cfg *Func, Variable *Dest, Variable *Src0,
                                      Variable *Src1, Variable *Src2,
                                      CondARM32::Cond Predicate) {
    return new (Func->allocate<InstARM32FourAddrGPR>())
        InstARM32FourAddrGPR(Func, Dest, Src0, Src1, Src2, Predicate);
  }
  void emit(const Cfg *Func) const override {
    if (!BuildDefs::dump())
      return;
    emitFourAddr(Opcode, this, Func);
  }
  void emitIAS(const Cfg *Func) const override;
  void dump(const Cfg *Func) const override {
    if (!BuildDefs::dump())
      return;
    Ostream &Str = Func->getContext()->getStrDump();
    dumpDest(Func);
    Str << " = ";
    dumpOpcodePred(Str, Opcode, getDest()->getType());
    Str << " ";
    dumpSources(Func);
  }
  static bool classof(const Inst *Inst) { return isClassof(Inst, K); }

private:
  InstARM32FourAddrGPR(Cfg *Func, Variable *Dest, Variable *Src0,
                       Variable *Src1, Variable *Src2,
                       CondARM32::Cond Predicate)
      : InstARM32Pred(Func, K, 3, Dest, Predicate) {
    addSource(Src0);
    addSource(Src1);
    addSource(Src2);
  }

  static const char *Opcode;
};

/// Instructions of the form x cmpop y (setting flags).
template <InstARM32::InstKindARM32 K>
class InstARM32CmpLike : public InstARM32Pred {
  InstARM32CmpLike() = delete;
  InstARM32CmpLike(const InstARM32CmpLike &) = delete;
  InstARM32CmpLike &operator=(const InstARM32CmpLike &) = delete;

public:
  static InstARM32CmpLike *create(Cfg *Func, Variable *Src0, Operand *Src1,
                                  CondARM32::Cond Predicate) {
    return new (Func->allocate<InstARM32CmpLike>())
        InstARM32CmpLike(Func, Src0, Src1, Predicate);
  }
  void emit(const Cfg *Func) const override {
    if (!BuildDefs::dump())
      return;
    emitCmpLike(Opcode, this, Func);
  }
  void emitIAS(const Cfg *Func) const override;
  void dump(const Cfg *Func) const override {
    if (!BuildDefs::dump())
      return;
    Ostream &Str = Func->getContext()->getStrDump();
    dumpOpcodePred(Str, Opcode, getSrc(0)->getType());
    Str << " ";
    dumpSources(Func);
  }
  static bool classof(const Inst *Inst) { return isClassof(Inst, K); }

private:
  InstARM32CmpLike(Cfg *Func, Variable *Src0, Operand *Src1,
                   CondARM32::Cond Predicate)
      : InstARM32Pred(Func, K, 2, nullptr, Predicate) {
    addSource(Src0);
    addSource(Src1);
  }

  static const char *Opcode;
};

using InstARM32Adc = InstARM32ThreeAddrGPR<InstARM32::Adc>;
using InstARM32Add = InstARM32ThreeAddrGPR<InstARM32::Add>;
using InstARM32And = InstARM32ThreeAddrGPR<InstARM32::And>;
using InstARM32Asr = InstARM32ThreeAddrGPR<InstARM32::Asr>;
using InstARM32Bic = InstARM32ThreeAddrGPR<InstARM32::Bic>;
using InstARM32Eor = InstARM32ThreeAddrGPR<InstARM32::Eor>;
using InstARM32Lsl = InstARM32ThreeAddrGPR<InstARM32::Lsl>;
using InstARM32Lsr = InstARM32ThreeAddrGPR<InstARM32::Lsr>;
using InstARM32Mul = InstARM32ThreeAddrGPR<InstARM32::Mul>;
using InstARM32Orr = InstARM32ThreeAddrGPR<InstARM32::Orr>;
using InstARM32Rsb = InstARM32ThreeAddrGPR<InstARM32::Rsb>;
using InstARM32Sbc = InstARM32ThreeAddrGPR<InstARM32::Sbc>;
using InstARM32Sdiv = InstARM32ThreeAddrGPR<InstARM32::Sdiv>;
using InstARM32Sub = InstARM32ThreeAddrGPR<InstARM32::Sub>;
using InstARM32Udiv = InstARM32ThreeAddrGPR<InstARM32::Udiv>;
using InstARM32Vadd = InstARM32ThreeAddrFP<InstARM32::Vadd>;
using InstARM32Vdiv = InstARM32ThreeAddrFP<InstARM32::Vdiv>;
using InstARM32Vmul = InstARM32ThreeAddrFP<InstARM32::Vmul>;
using InstARM32Vsub = InstARM32ThreeAddrFP<InstARM32::Vsub>;
using InstARM32Ldr = InstARM32LoadBase<InstARM32::Ldr>;
using InstARM32Ldrex = InstARM32LoadBase<InstARM32::Ldrex>;
/// MovT leaves the bottom bits alone so dest is also a source. This helps
/// indicate that a previous MovW setting dest is not dead code.
using InstARM32Movt = InstARM32TwoAddrGPR<InstARM32::Movt>;
using InstARM32Movw = InstARM32UnaryopGPR<InstARM32::Movw, false>;
using InstARM32Clz = InstARM32UnaryopGPR<InstARM32::Clz, false>;
using InstARM32Mvn = InstARM32UnaryopGPR<InstARM32::Mvn, false>;
using InstARM32Rbit = InstARM32UnaryopGPR<InstARM32::Rbit, false>;
using InstARM32Rev = InstARM32UnaryopGPR<InstARM32::Rev, false>;
// Technically, the uxt{b,h} and sxt{b,h} instructions have a rotation operand
// as well (rotate source by 8, 16, 24 bits prior to extending), but we aren't
// using that for now, so just model as a Unaryop.
using InstARM32Sxt = InstARM32UnaryopGPR<InstARM32::Sxt, true>;
using InstARM32Uxt = InstARM32UnaryopGPR<InstARM32::Uxt, true>;
using InstARM32Vsqrt = InstARM32UnaryopFP<InstARM32::Vsqrt>;
using InstARM32Mla = InstARM32FourAddrGPR<InstARM32::Mla>;
using InstARM32Mls = InstARM32FourAddrGPR<InstARM32::Mls>;
using InstARM32Cmp = InstARM32CmpLike<InstARM32::Cmp>;
using InstARM32Tst = InstARM32CmpLike<InstARM32::Tst>;

// InstARM32Label represents an intra-block label that is the target of an
// intra-block branch. The offset between the label and the branch must be fit
// in the instruction immediate (considered "near").
class InstARM32Label : public InstARM32 {
  InstARM32Label() = delete;
  InstARM32Label(const InstARM32Label &) = delete;
  InstARM32Label &operator=(const InstARM32Label &) = delete;

public:
  static InstARM32Label *create(Cfg *Func, TargetARM32 *Target) {
    return new (Func->allocate<InstARM32Label>()) InstARM32Label(Func, Target);
  }
  uint32_t getEmitInstCount() const override { return 0; }
  IceString getName(const Cfg *Func) const;
  SizeT getNumber() const { return Number; }
  void emit(const Cfg *Func) const override;
  void emitIAS(const Cfg *Func) const override;
  void dump(const Cfg *Func) const override;

private:
  InstARM32Label(Cfg *Func, TargetARM32 *Target);

  SizeT Number; // used for unique label generation.
};

/// Direct branch instruction.
class InstARM32Br : public InstARM32Pred {
  InstARM32Br() = delete;
  InstARM32Br(const InstARM32Br &) = delete;
  InstARM32Br &operator=(const InstARM32Br &) = delete;

public:
  /// Create a conditional branch to one of two nodes.
  static InstARM32Br *create(Cfg *Func, CfgNode *TargetTrue,
                             CfgNode *TargetFalse, CondARM32::Cond Predicate) {
    assert(Predicate != CondARM32::AL);
    constexpr InstARM32Label *NoLabel = nullptr;
    return new (Func->allocate<InstARM32Br>())
        InstARM32Br(Func, TargetTrue, TargetFalse, NoLabel, Predicate);
  }
  /// Create an unconditional branch to a node.
  static InstARM32Br *create(Cfg *Func, CfgNode *Target) {
    constexpr CfgNode *NoCondTarget = nullptr;
    constexpr InstARM32Label *NoLabel = nullptr;
    return new (Func->allocate<InstARM32Br>())
        InstARM32Br(Func, NoCondTarget, Target, NoLabel, CondARM32::AL);
  }
  /// Create a non-terminator conditional branch to a node, with a fallthrough
  /// to the next instruction in the current node. This is used for switch
  /// lowering.
  static InstARM32Br *create(Cfg *Func, CfgNode *Target,
                             CondARM32::Cond Predicate) {
    assert(Predicate != CondARM32::AL);
    constexpr CfgNode *NoUncondTarget = nullptr;
    constexpr InstARM32Label *NoLabel = nullptr;
    return new (Func->allocate<InstARM32Br>())
        InstARM32Br(Func, Target, NoUncondTarget, NoLabel, Predicate);
  }
  // Create a conditional intra-block branch (or unconditional, if
  // Condition==AL) to a label in the current block.
  static InstARM32Br *create(Cfg *Func, InstARM32Label *Label,
                             CondARM32::Cond Predicate) {
    constexpr CfgNode *NoCondTarget = nullptr;
    constexpr CfgNode *NoUncondTarget = nullptr;
    return new (Func->allocate<InstARM32Br>())
        InstARM32Br(Func, NoCondTarget, NoUncondTarget, Label, Predicate);
  }
  const CfgNode *getTargetTrue() const { return TargetTrue; }
  const CfgNode *getTargetFalse() const { return TargetFalse; }
  bool optimizeBranch(const CfgNode *NextNode);
  uint32_t getEmitInstCount() const override {
    uint32_t Sum = 0;
    if (Label)
      ++Sum;
    if (getTargetTrue())
      ++Sum;
    if (getTargetFalse())
      ++Sum;
    return Sum;
  }
  bool isUnconditionalBranch() const override {
    return getPredicate() == CondARM32::AL;
  }
  bool repointEdges(CfgNode *OldNode, CfgNode *NewNode) override;
  void emit(const Cfg *Func) const override;
  void emitIAS(const Cfg *Func) const override;
  void dump(const Cfg *Func) const override;
  static bool classof(const Inst *Inst) { return isClassof(Inst, Br); }

private:
  InstARM32Br(Cfg *Func, const CfgNode *TargetTrue, const CfgNode *TargetFalse,
              const InstARM32Label *Label, CondARM32::Cond Predicate);

  const CfgNode *TargetTrue;
  const CfgNode *TargetFalse;
  const InstARM32Label *Label; // Intra-block branch target
};

/// AdjustStack instruction - subtracts SP by the given amount and updates the
/// stack offset during code emission.
class InstARM32AdjustStack : public InstARM32 {
  InstARM32AdjustStack() = delete;
  InstARM32AdjustStack(const InstARM32AdjustStack &) = delete;
  InstARM32AdjustStack &operator=(const InstARM32AdjustStack &) = delete;

public:
  /// Note: We need both Amount and SrcAmount. If Amount is too large then it
  /// needs to be copied to a register (so SrcAmount could be a register).
  /// However, we also need the numeric Amount for bookkeeping, and it's hard to
  /// pull that from the generic SrcAmount operand.
  static InstARM32AdjustStack *create(Cfg *Func, Variable *SP, SizeT Amount,
                                      Operand *SrcAmount) {
    return new (Func->allocate<InstARM32AdjustStack>())
        InstARM32AdjustStack(Func, SP, Amount, SrcAmount);
  }
  void emit(const Cfg *Func) const override;
  void dump(const Cfg *Func) const override;
  static bool classof(const Inst *Inst) { return isClassof(Inst, Adjuststack); }
  SizeT getAmount() const { return Amount; }

private:
  InstARM32AdjustStack(Cfg *Func, Variable *SP, SizeT Amount,
                       Operand *SrcAmount);
  const SizeT Amount;
};

/// Call instruction (bl/blx). Arguments should have already been pushed.
/// Technically bl and the register form of blx can be predicated, but we'll
/// leave that out until needed.
class InstARM32Call : public InstARM32 {
  InstARM32Call() = delete;
  InstARM32Call(const InstARM32Call &) = delete;
  InstARM32Call &operator=(const InstARM32Call &) = delete;

public:
  static InstARM32Call *create(Cfg *Func, Variable *Dest, Operand *CallTarget) {
    return new (Func->allocate<InstARM32Call>())
        InstARM32Call(Func, Dest, CallTarget);
  }
  Operand *getCallTarget() const { return getSrc(0); }
  void emit(const Cfg *Func) const override;
  void dump(const Cfg *Func) const override;
  static bool classof(const Inst *Inst) { return isClassof(Inst, Call); }

private:
  InstARM32Call(Cfg *Func, Variable *Dest, Operand *CallTarget);
};

/// Pop into a list of GPRs. Technically this can be predicated, but we don't
/// need that functionality.
class InstARM32Pop : public InstARM32 {
  InstARM32Pop() = delete;
  InstARM32Pop(const InstARM32Pop &) = delete;
  InstARM32Pop &operator=(const InstARM32Pop &) = delete;

public:
  static InstARM32Pop *create(Cfg *Func, const VarList &Dests) {
    return new (Func->allocate<InstARM32Pop>()) InstARM32Pop(Func, Dests);
  }
  void emit(const Cfg *Func) const override;
  void dump(const Cfg *Func) const override;
  static bool classof(const Inst *Inst) { return isClassof(Inst, Pop); }

private:
  InstARM32Pop(Cfg *Func, const VarList &Dests);

  VarList Dests;
};

/// Push a list of GPRs. Technically this can be predicated, but we don't need
/// that functionality.
class InstARM32Push : public InstARM32 {
  InstARM32Push() = delete;
  InstARM32Push(const InstARM32Push &) = delete;
  InstARM32Push &operator=(const InstARM32Push &) = delete;

public:
  static InstARM32Push *create(Cfg *Func, const VarList &Srcs) {
    return new (Func->allocate<InstARM32Push>()) InstARM32Push(Func, Srcs);
  }
  void emit(const Cfg *Func) const override;
  void dump(const Cfg *Func) const override;
  static bool classof(const Inst *Inst) { return isClassof(Inst, Push); }

private:
  InstARM32Push(Cfg *Func, const VarList &Srcs);
};

/// Ret pseudo-instruction. This is actually a "bx" instruction with an "lr"
/// register operand, but epilogue lowering will search for a Ret instead of a
/// generic "bx". This instruction also takes a Source operand (for non-void
/// returning functions) for liveness analysis, though a FakeUse before the ret
/// would do just as well.
///
/// NOTE: Even though "bx" can be predicated, for now leave out the predication
/// since it's not yet known to be useful for Ret. That may complicate finding
/// the terminator instruction if it's not guaranteed to be executed.
class InstARM32Ret : public InstARM32 {
  InstARM32Ret() = delete;
  InstARM32Ret(const InstARM32Ret &) = delete;
  InstARM32Ret &operator=(const InstARM32Ret &) = delete;

public:
  static InstARM32Ret *create(Cfg *Func, Variable *LR,
                              Variable *Source = nullptr) {
    return new (Func->allocate<InstARM32Ret>()) InstARM32Ret(Func, LR, Source);
  }
  void emit(const Cfg *Func) const override;
  void emitIAS(const Cfg *Func) const override;
  void dump(const Cfg *Func) const override;
  static bool classof(const Inst *Inst) { return isClassof(Inst, Ret); }

private:
  InstARM32Ret(Cfg *Func, Variable *LR, Variable *Source);
};

/// Store instruction. It's important for liveness that there is no Dest operand
/// (OperandARM32Mem instead of Dest Variable).
class InstARM32Str final : public InstARM32Pred {
  InstARM32Str() = delete;
  InstARM32Str(const InstARM32Str &) = delete;
  InstARM32Str &operator=(const InstARM32Str &) = delete;

public:
  /// Value must be a register.
  static InstARM32Str *create(Cfg *Func, Variable *Value, OperandARM32Mem *Mem,
                              CondARM32::Cond Predicate) {
    return new (Func->allocate<InstARM32Str>())
        InstARM32Str(Func, Value, Mem, Predicate);
  }
  void emit(const Cfg *Func) const override;
  void emitIAS(const Cfg *Func) const override;
  void dump(const Cfg *Func) const override;
  static bool classof(const Inst *Inst) { return isClassof(Inst, Str); }

private:
  InstARM32Str(Cfg *Func, Variable *Value, OperandARM32Mem *Mem,
               CondARM32::Cond Predicate);
};

/// Exclusive Store instruction. Like its non-exclusive sibling, it's important
/// for liveness that there is no Dest operand (OperandARM32Mem instead of Dest
/// Variable).
class InstARM32Strex final : public InstARM32Pred {
  InstARM32Strex() = delete;
  InstARM32Strex(const InstARM32Strex &) = delete;
  InstARM32Strex &operator=(const InstARM32Strex &) = delete;

public:
  /// Value must be a register.
  static InstARM32Strex *create(Cfg *Func, Variable *Dest, Variable *Value,
                                OperandARM32Mem *Mem,
                                CondARM32::Cond Predicate) {
    return new (Func->allocate<InstARM32Strex>())
        InstARM32Strex(Func, Dest, Value, Mem, Predicate);
  }
  void emit(const Cfg *Func) const override;
  void dump(const Cfg *Func) const override;
  static bool classof(const Inst *Inst) { return isClassof(Inst, Strex); }

private:
  InstARM32Strex(Cfg *Func, Variable *Dest, Variable *Value,
                 OperandARM32Mem *Mem, CondARM32::Cond Predicate);
};

class InstARM32Trap : public InstARM32 {
  InstARM32Trap() = delete;
  InstARM32Trap(const InstARM32Trap &) = delete;
  InstARM32Trap &operator=(const InstARM32Trap &) = delete;

public:
  static InstARM32Trap *create(Cfg *Func) {
    return new (Func->allocate<InstARM32Trap>()) InstARM32Trap(Func);
  }
  void emit(const Cfg *Func) const override;
  void dump(const Cfg *Func) const override;
  static bool classof(const Inst *Inst) { return isClassof(Inst, Trap); }

private:
  explicit InstARM32Trap(Cfg *Func);
};

/// Unsigned Multiply Long: d.lo, d.hi := x * y
class InstARM32Umull : public InstARM32Pred {
  InstARM32Umull() = delete;
  InstARM32Umull(const InstARM32Umull &) = delete;
  InstARM32Umull &operator=(const InstARM32Umull &) = delete;

public:
  /// Everything must be a register.
  static InstARM32Umull *create(Cfg *Func, Variable *DestLo, Variable *DestHi,
                                Variable *Src0, Variable *Src1,
                                CondARM32::Cond Predicate) {
    return new (Func->allocate<InstARM32Umull>())
        InstARM32Umull(Func, DestLo, DestHi, Src0, Src1, Predicate);
  }
  void emit(const Cfg *Func) const override;
  void dump(const Cfg *Func) const override;
  static bool classof(const Inst *Inst) { return isClassof(Inst, Umull); }

private:
  InstARM32Umull(Cfg *Func, Variable *DestLo, Variable *DestHi, Variable *Src0,
                 Variable *Src1, CondARM32::Cond Predicate);

  Variable *DestHi;
};

/// Handles fp2int, int2fp, and fp2fp conversions.
class InstARM32Vcvt final : public InstARM32Pred {
  InstARM32Vcvt() = delete;
  InstARM32Vcvt(const InstARM32Vcvt &) = delete;
  InstARM32Vcvt &operator=(const InstARM32Vcvt &) = delete;

public:
  enum VcvtVariant { S2si, S2ui, Si2s, Ui2s, D2si, D2ui, Si2d, Ui2d, S2d, D2s };
  static InstARM32Vcvt *create(Cfg *Func, Variable *Dest, Variable *Src,
                               VcvtVariant Variant, CondARM32::Cond Predicate) {
    return new (Func->allocate<InstARM32Vcvt>())
        InstARM32Vcvt(Func, Dest, Src, Variant, Predicate);
  }
  void emit(const Cfg *Func) const override;
  void dump(const Cfg *Func) const override;
  static bool classof(const Inst *Inst) { return isClassof(Inst, Vcvt); }

private:
  InstARM32Vcvt(Cfg *Func, Variable *Dest, Variable *Src, VcvtVariant Variant,
                CondARM32::Cond Predicate);

  const VcvtVariant Variant;
};

/// Handles (some of) vmov's various formats.
class InstARM32Mov final : public InstARM32Pred {
  InstARM32Mov() = delete;
  InstARM32Mov(const InstARM32Mov &) = delete;
  InstARM32Mov &operator=(const InstARM32Mov &) = delete;

public:
  static InstARM32Mov *create(Cfg *Func, Variable *Dest, Operand *Src,
                              CondARM32::Cond Predicate) {
    return new (Func->allocate<InstARM32Mov>())
        InstARM32Mov(Func, Dest, Src, Predicate);
  }
  bool isRedundantAssign() const override {
    return !isMultiDest() && !isMultiSource() &&
           checkForRedundantAssign(getDest(), getSrc(0));
  }
  bool isVarAssign() const override { return llvm::isa<Variable>(getSrc(0)); }
  void emit(const Cfg *Func) const override;
  void emitIAS(const Cfg *Func) const override;
  void dump(const Cfg *Func) const override;
  static bool classof(const Inst *Inst) { return isClassof(Inst, Mov); }

  bool isMultiDest() const { return DestHi != nullptr; }

  bool isMultiSource() const {
    assert(getSrcSize() == 1 || getSrcSize() == 2);
    return getSrcSize() == 2;
  }

  Variable *getDestHi() const { return DestHi; }

private:
  InstARM32Mov(Cfg *Func, Variable *Dest, Operand *Src,
               CondARM32::Cond Predicate);

  void emitMultiDestSingleSource(const Cfg *Func) const;
  void emitSingleDestMultiSource(const Cfg *Func) const;
  void emitSingleDestSingleSource(const Cfg *Func) const;

  void emitIASSingleDestSingleSource(const Cfg *Func) const;

  Variable *DestHi = nullptr;
};

class InstARM32Vcmp final : public InstARM32Pred {
  InstARM32Vcmp() = delete;
  InstARM32Vcmp(const InstARM32Vcmp &) = delete;
  InstARM32Vcmp &operator=(const InstARM32Vcmp &) = delete;

public:
  static InstARM32Vcmp *create(Cfg *Func, Variable *Src0, Variable *Src1,
                               CondARM32::Cond Predicate) {
    return new (Func->allocate<InstARM32Vcmp>())
        InstARM32Vcmp(Func, Src0, Src1, Predicate);
  }
  void emit(const Cfg *Func) const override;
  void dump(const Cfg *Func) const override;
  static bool classof(const Inst *Inst) { return isClassof(Inst, Vcmp); }

private:
  InstARM32Vcmp(Cfg *Func, Variable *Src0, Variable *Src1,
                CondARM32::Cond Predicate);
};

/// Copies the FP Status and Control Register the core flags.
class InstARM32Vmrs final : public InstARM32Pred {
  InstARM32Vmrs() = delete;
  InstARM32Vmrs(const InstARM32Vmrs &) = delete;
  InstARM32Vmrs &operator=(const InstARM32Vmrs &) = delete;

public:
  static InstARM32Vmrs *create(Cfg *Func, CondARM32::Cond Predicate) {
    return new (Func->allocate<InstARM32Vmrs>()) InstARM32Vmrs(Func, Predicate);
  }
  void emit(const Cfg *Func) const override;
  void dump(const Cfg *Func) const override;
  static bool classof(const Inst *Inst) { return isClassof(Inst, Vmrs); }

private:
  InstARM32Vmrs(Cfg *Func, CondARM32::Cond Predicate);
};

class InstARM32Vabs final : public InstARM32Pred {
  InstARM32Vabs() = delete;
  InstARM32Vabs(const InstARM32Vabs &) = delete;
  InstARM32Vabs &operator=(const InstARM32Vabs &) = delete;

public:
  static InstARM32Vabs *create(Cfg *Func, Variable *Dest, Variable *Src,
                               CondARM32::Cond Predicate) {
    return new (Func->allocate<InstARM32Vabs>())
        InstARM32Vabs(Func, Dest, Src, Predicate);
  }
  void emit(const Cfg *Func) const override;
  void dump(const Cfg *Func) const override;
  static bool classof(const Inst *Inst) { return isClassof(Inst, Vabs); }

private:
  InstARM32Vabs(Cfg *Func, Variable *Dest, Variable *Src,
                CondARM32::Cond Predicate);
};

class InstARM32Dmb final : public InstARM32Pred {
  InstARM32Dmb() = delete;
  InstARM32Dmb(const InstARM32Dmb &) = delete;
  InstARM32Dmb &operator=(const InstARM32Dmb &) = delete;

public:
  static InstARM32Dmb *create(Cfg *Func) {
    return new (Func->allocate<InstARM32Dmb>()) InstARM32Dmb(Func);
  }
  void emit(const Cfg *Func) const override;
  void dump(const Cfg *Func) const override;
  static bool classof(const Inst *Inst) { return isClassof(Inst, Dmb); }

private:
  explicit InstARM32Dmb(Cfg *Func);
};

// Declare partial template specializations of emit() methods that already have
// default implementations. Without this, there is the possibility of ODR
// violations and link errors.

template <> void InstARM32Ldr::emit(const Cfg *Func) const;
template <> void InstARM32Movw::emit(const Cfg *Func) const;
template <> void InstARM32Movt::emit(const Cfg *Func) const;

} // end of namespace Ice

#endif // SUBZERO_SRC_ICEINSTARM32_H
