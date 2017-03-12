/*!
 *  Copyright (c) 2016 by Contributors
 *  Implementation of API functions related to arith
 * \file api_arith.cc
 */
#include <tvm/expr.h>
#include <tvm/ir.h>
#include <tvm/api_registry.h>
#include <tvm/arithmetic.h>

namespace tvm {
namespace arith {

TVM_REGISTER_API(_arith_intset_single_point)
.set_body([](TVMArgs args, TVMRetValue *ret) {
    *ret = IntSet::single_point(args[0]);
  });

TVM_REGISTER_API(_arith_intset_interval)
.set_body([](TVMArgs args, TVMRetValue *ret) {
    *ret = IntSet::interval(args[0], args[1]);
  });

TVM_REGISTER_API(_arith_EvalModular)
.set_body([](TVMArgs args, TVMRetValue *ret) {
    *ret = EvalModular(args[0], Map<Var, IntSet>());
  });

TVM_REGISTER_API(_arith_DeduceBound)
.set_body([](TVMArgs args, TVMRetValue *ret) {
    *ret = DeduceBound(args[0], args[1],
        args[2].operator Map<Var, IntSet>(),
        args[3].operator Map<Var, IntSet>());
  });

TVM_REGISTER_API(_IntervalSetGetMin)
.set_body([](TVMArgs args, TVMRetValue *ret) {
    *ret = args[0].operator IntSet().min();
  });

TVM_REGISTER_API(_IntervalSetGetMax)
.set_body([](TVMArgs args, TVMRetValue *ret) {
    *ret = args[0].operator IntSet().max();
  });

TVM_REGISTER_API(_IntSetIsNothing)
.set_body([](TVMArgs args, TVMRetValue *ret) {
    *ret = args[0].operator IntSet().is_nothing();
  });

TVM_REGISTER_API(_IntSetIsEverything)
.set_body([](TVMArgs args, TVMRetValue *ret) {
    *ret = args[0].operator IntSet().is_everything();
  });

}  // namespace arith
}  // namespace tvm