/*!
 *  Copyright (c) 2016 by Contributors
 * \file op_attr_types.h
 * \brief Data structures that can appear in operator attributes.
 */
#ifndef NNVM_OP_ATTR_TYPES_H_
#define NNVM_OP_ATTR_TYPES_H_

#include <vector>
#include <string>
#include <utility>
#include <functional>
#include "./base.h"
#include "./tuple.h"
#include "./node.h"

namespace nnvm {

// These types are optional attributes in each operator.
// Each attribute can be required by some passes.

/*!
 * \brief Return list of input arguments names of each operator.
 *
 * \param n The node.
 * \return list of inputs
 * \note Register under "FListInputNames", default return {"data"}.
 *
 *  FListInputNames enables automatic variable creation for missing arguments.
 */
using FListInputNames = std::function<std::vector<std::string> (const Node& n)>;

/*!
 * \brief Return list of output arguments names of each operator.
 *
 * \param n The node.
 * \return list of inputs
 * \note Register under "FListOutputNames", default return {"outputs"}.
 *
 *  FListOutputNames customized naming for operator outputs.
 */
using FListOutputNames = std::function<std::vector<std::string> (const Node& n)>;

/*!
 * \brief Check whether operator will mutate k-th input.
 * \param n The node.
 * \return list of input indices it mutates.
 *
 * \note Register under "FMutateInputs", default return false
 * FMutateInputs enables mutation order handling correctly.
 */
using FMutateInputs = std::function<std::vector<uint32_t> (const Node& n)>;

/*!
 * \brief Inference function of certain type.
 * \tparam AttrType The type of the attribute to be infered.
 * \return whether all attributes are inferred.
 */
template<typename AttrType>
using FInferNodeEntryAttr = std::function<bool (const Node& n,
                                                std::vector<AttrType> *in_ptr,
                                                std::vector<AttrType> *out_ptr)>;
/*!
 * \brief Shape inference function.
 *  Update the shapes given the input shape information.
 *  TShape.ndim() == 0 means the shape is still unknown.
 *
 * \note Register under "FInferShape",
 *  by default do not update any shapes.
 *
 *  FInferShape is needed by shape inference
 */
using FInferShape = FInferNodeEntryAttr<TShape>;

/*!
 * \brief Type inference function.
 *  Update the type given the known type information.
 *
 * \note Register under "FInferType",
 *  by default set all the output types to 0.
 */
using FInferType = FInferNodeEntryAttr<int>;

/*!
 * \brief Whether this op is an explicit backward operator
 *
 *  If TIsBackwardOp is set to be true:
 *   - The first control_deps of the node points to the corresponding forward operator.
 *   - The outputs operator corresponds to exactly inputs of forward op one by one.
 *
 * \note Register under "TIsBackwardOp", default to false.
 *
 * This enables easier shape/type inference for backward operators for slice and reduction.
 */
using TIsBackwardOp = bool;

/*!
 * \brief Get possible inplace options.
 *  This function enables optimization to reuse memory of inputs in output.
 * \param n The node
 * \param in_data The input data.
 * \param out_data The output data.
 * \return list of pair of that maps input->output,
 *   indicating possible in place operations.
 *
 * \note Register under "FInplaceOption", by default no inplace can happen.
 */
using FInplaceOption = std::function<
  std::vector<std::pair<int, int> > (const Node& n)>;

/*!
 * \brief Get the gradient node of the op node
 *  This function generates the backward graph of the node
 * \param nodeptr The node to take gradient
 * \param out_grads Gradient of current node's outputs
 * \return gradients of the inputs
 *
 * \note Register under "FGradient"
 */
using FGradient = std::function<std::vector<NodeEntry>(
    const NodePtr& nodeptr,
    const std::vector<NodeEntry>& out_grads)>;

}  // namespace nnvm

#endif  // NNVM_OP_ATTR_TYPES_H_
