/*!
 *  Copyright (c) 2016 by Contributors
 * \file gradients.cc
 * \brief Passes that takes gradient of the graph
 * This code code was modified based on mxnet codebase by Min Lin
 */
#include <nnvm/pass.h>
#include <nnvm/op_attr_types.h>
#include <algorithm>
#include <functional>

namespace nnvm {
namespace pass {
namespace {

// default aggregate gradient function
// require operator zeros and elemwise_sum to be presented.
NodeEntry DefaultAggregateGradient(std::vector<NodeEntry>&& v) {
  if (v.size() == 1) {
    return std::move(v[0]);
  } else if (v.size() == 0) {
    NodeAttrs attrs;
    attrs.op = Op::Get("zeros");
    attrs.name = "zero_grad";
    attrs.op->attr_parser(&attrs);
    NodePtr zero_node = Node::Create(std::move(attrs));
    return NodeEntry{zero_node, 0, 0};
  } else {
    NodeAttrs attrs;
    attrs.op = Op::Get("elemwise_sum");
    attrs.name = "grad_sum";
    attrs.dict["num_args"] = std::to_string(v.size());
    attrs.op->attr_parser(&attrs);
    NodePtr sum_node = Node::Create(std::move(attrs), std::move(v));
    return NodeEntry{sum_node, 0, 0};
  }
}

bool CheckGradAllZero(const std::vector<NodeEntry>& grads,
                      const std::vector<const Op*>& zero_ops) {
  if (!grads.size() || !zero_ops.size()) return false;
  for (const auto& g : grads) {
    bool found = false;
    for (const auto& op : zero_ops) {
      if (g.node->op() == op) {
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  return true;
}

// helper entry
struct GradEntry {
#ifdef _MSC_VER
  NodeEntry sum = NodeEntry{nullptr, 0, 0};
#else
  NodeEntry sum{nullptr, 0, 0};
#endif
  std::vector<NodeEntry> grads;
  bool need_attr_hint{true};
};

Graph Gradient(Graph src) {
  using nnvm::FGradient;
  using MirrorFun = std::function<int (const Node& node)>;
  using AttrHintFun = std::function<NodeEntry (const NodeEntry& src, const NodeEntry &like)>;

  CHECK_NE(src.attrs.count("grad_ys"), 0U)
      << "Gradient require grad_ys to be presented.";
  CHECK_NE(src.attrs.count("grad_ys_out_grad"), 0U)
      << "Gradient require grad_ys_out_grad to be presented.";
  CHECK_NE(src.attrs.count("grad_xs"), 0U)
      << "Gradient require grad_xs to be presented.";
  const std::vector<NodeEntry>& ys =
      src.GetAttr<std::vector<NodeEntry> >("grad_ys");
  const std::vector<NodeEntry>& ys_out_grad =
      src.GetAttr<std::vector<NodeEntry> >("grad_ys_out_grad");
  const std::vector<NodeEntry>& xs =
      src.GetAttr<std::vector<NodeEntry> >("grad_xs");
  using AggFun = std::function<NodeEntry (std::vector<NodeEntry>&& inputs)>;
  AggFun agg_fun = DefaultAggregateGradient;
  if (src.attrs.count("grad_aggregate_fun") != 0) {
    agg_fun = src.GetAttr<AggFun>("grad_aggregate_fun");
  }
  MirrorFun mirror_fun = nullptr;
  if (src.attrs.count("grad_mirror_fun") != 0) {
    mirror_fun = src.GetAttr<MirrorFun>("grad_mirror_fun");
  }
  AttrHintFun attr_hint_fun = nullptr;
  if (src.attrs.count("attr_hint_fun") != 0) {
    attr_hint_fun = src.GetAttr<AttrHintFun>("attr_hint_fun");
  }
  std::vector<const Op*> zero_ops;
  if (src.attrs.count("zero_ops") != 0) {
    zero_ops = src.GetAttr<std::vector<const Op*> >("zero_ops");
  }
  const Op* copy_op = (src.attrs.count("copy_op") != 0) ?
      Op::Get(src.GetAttr<std::string>("copy_op")) :
      nullptr;

  // topo sort
  std::vector<NodePtr> topo_order;
  std::unordered_map<Node*, std::vector<GradEntry> > output_grads;

  DFSVisit(ys, [&](const NodePtr& node) {
      if (output_grads.count(node.get()) == 0) {
        output_grads[node.get()].resize(node->num_outputs());
      }
      topo_order.push_back(node);
    });

  CHECK_EQ(ys.size(), ys_out_grad.size());
  for (size_t i = 0; i < ys.size(); ++i) {
    NodeEntry ograd = ys_out_grad[i];
    output_grads[ys[i].node.get()][ys[i].index].grads = { ograd };
  }

  // Check that all xs are reachable from ys
  for (size_t i = 0; i < xs.size(); ++i) {
    CHECK(output_grads.find(xs[i].node.get()) != output_grads.end())
        << "Cannot differentiate with respect to the " << i+1 << "-th variable "
        << "because it is unreachable from the outputs.";
  }

  // construct mirror reduece memory strategy if needed
  std::unordered_map<Node*, NodePtr> mirror_map;
  if (mirror_fun != nullptr) {
    for (const NodePtr& n : topo_order) {
      if (mirror_fun(*n)) {
        NodePtr new_node = Node::Create(*n);
        new_node->attrs.name += "_mirror";
        for (auto& e : new_node->inputs) {
          e.node = mirror_map.at(e.node.get());
        }
        for (auto& n : new_node->control_deps) {
          n = mirror_map.at(n.get());
        }
        mirror_map[n.get()] = std::move(new_node);
      } else {
        mirror_map[n.get()] = n;
      }
    }
  }

  // traverse backward
  static auto& grad_fun_map = Op::GetAttr<FGradient>("FGradient");
  static auto& finfer_shape = Op::GetAttr<FInferShape>("FInferShape");

  std::vector<NodeEntry> out_agg_grads;
  for (auto rit = topo_order.rbegin(); rit != topo_order.rend(); ++rit) {
    const NodePtr& ptr = *rit;
    if (ptr->is_variable()) continue;
    out_agg_grads.clear();
    auto& out_grad_vec = output_grads.at(ptr.get());
    for (uint32_t i = 0; i < out_grad_vec.size(); ++i) {
      GradEntry& e = out_grad_vec[i];
      e.sum = agg_fun(std::move(e.grads));
      if (e.need_attr_hint && attr_hint_fun != nullptr) {
        e.sum = attr_hint_fun(e.sum, NodeEntry{ptr, 0, i});
      }
      out_agg_grads.push_back(e.sum);
    }
    if ((*rit)->inputs.size() != 0) {
      NodePtr fwd_node = (mirror_map.size() == 0 ? ptr : mirror_map.at(ptr.get()));
      std::vector<NodeEntry> input_grads;
      if (grad_fun_map.count(ptr->op())) {
        input_grads = grad_fun_map[ptr->op()](fwd_node, out_agg_grads);
        CHECK_EQ((*rit)->inputs.size(), input_grads.size())
            << "Gradient function not returning enough gradient";
      } else if (CheckGradAllZero(out_agg_grads, zero_ops)) {
        for (size_t i = 0; i < fwd_node->num_inputs(); ++i) {
          std::ostringstream os;
          if (1 == fwd_node->num_inputs()) {
            os << fwd_node->attrs.name << "_backward";
          } else {
            os << fwd_node->attrs.name << "_in" << i << "_backward";
          }
          NodeAttrs attrs;
          attrs.op = zero_ops[0];
          attrs.name = os.str();
          if (attrs.op->attr_parser != nullptr) {
            attrs.op->attr_parser(&attrs);
          }
          std::vector<NodeEntry> input {fwd_node->inputs[i]};
          std::vector<NodePtr> control_deps {fwd_node};
          auto p = Node::Create(std::move(attrs), std::move(input), std::move(control_deps));
          input_grads.emplace_back(nnvm::NodeEntry{p, 0, 0});
        }
      } else {
        LOG(FATAL) << "Operator " << fwd_node->op()->name << " is non-differentiable "
                   << "because it didn't register FGradient attribute.";
      }
      auto git = input_grads.begin();
      for (auto it = (*rit)->inputs.begin(); it != (*rit)->inputs.end(); ++it, ++git) {
        auto& ge = output_grads[it->node.get()][it->index];
        // if any of the backward op can do shape inference, the hint is not necessary.
        if (finfer_shape.count(git->node->op())) {
          ge.need_attr_hint = false;
        }
        ge.grads.emplace_back(std::move(*git));
      }
    }
  }
  // take out the xs' grads
  Graph ret;
  ret.outputs.resize(xs.size());
  NodeEntryMap<std::pair<size_t, size_t> > unique_grads;
  size_t counter = 0;
  for (const NodeEntry& e : xs) {
    GradEntry& entry = output_grads[e.node.get()][e.index];
    // aggregate sum if there haven't been
    if (entry.sum.node.get() == nullptr) {
      entry.sum = agg_fun(std::move(entry.grads));
      if (entry.need_attr_hint && attr_hint_fun != nullptr) {
        entry.sum = attr_hint_fun(entry.sum, e);
      }
    }
    if (copy_op != nullptr) {
      auto kv = unique_grads.find(entry.sum);
      if (kv == unique_grads.end()) {
        unique_grads.emplace(std::move(entry.sum), std::make_pair(1, counter));
      } else {
        std::ostringstream os;
        os << entry.sum.node->attrs.name << "_" << kv->second.first << "_copy";
        kv->second.first++;
        NodeAttrs attrs;
        attrs.op = copy_op;
        attrs.name = os.str();
        if (attrs.op->attr_parser != nullptr) {
            attrs.op->attr_parser(&attrs);
        }
        std::vector<NodeEntry> inputs {entry.sum};
        NodePtr copy_node = Node::Create(std::move(attrs), std::move(inputs));
        unique_grads.emplace(NodeEntry{std::move(copy_node), 0, 0}, std::make_pair(1, counter));
      }
    } else {
        ret.outputs[counter] = entry.sum;
    }
    ++counter;
  }
  if (copy_op != nullptr) {
    for (const auto& kv : unique_grads) {
      ret.outputs[kv.second.second] = kv.first;
    }
  }
  return ret;
}

// register pass
NNVM_REGISTER_PASS(Gradient)
.describe("Return a gradient graph of src.attrs[\"ys\"] wrt src.attrs[\"xs\"]")
.set_body(Gradient)
.set_change_graph(true)
.depend_graph_attr("grad_ys")
.depend_graph_attr("grad_xs")
.depend_graph_attr("grad_ys_out_grad");

}  // namespace
}  // namespace pass
}  // namespace nnvm
