#include "plansys2_pddl_parser/Domain.h"

namespace parser
{namespace pddl
{

void Oneof::PDDLPrint(
  std::ostream & s, unsigned indent, const TokenStruct<std::string> & ts,
  const Domain & d) const
{
  tabindent(s, indent);
  s << "( oneof\n";
  for (unsigned i = 0; i < conds.size(); ++i) {
    conds[i]->PDDLPrint(s, indent + 1, ts, d);
    s << "\n";
  }
  tabindent(s, indent);
  s << ")";
}

plansys2_msgs::msg::Node::SharedPtr Oneof::getTree(
  plansys2_msgs::msg::Tree & tree,
  const Domain & d,
  const std::vector<std::string> & replace) const
{
  plansys2_msgs::msg::Node::SharedPtr node = std::make_shared<plansys2_msgs::msg::Node>();
  node->node_type = plansys2_msgs::msg::Node::ONE_OF;
  node->node_id = tree.nodes.size();
  tree.nodes.push_back(*node);

  for (unsigned i = 0; i < conds.size(); ++i) {
    plansys2_msgs::msg::Node::SharedPtr child = conds[i]->getTree(tree, d, replace);
    node->children.push_back(child->node_id);
  }

  tree.nodes[node->node_id] = *node;
  return node;
}

void Oneof::parse(Stringreader & f, TokenStruct<std::string> & ts, Domain & d)
{
  for (f.next(); f.getChar() != ')'; f.next() ) {
    f.assert_token("(");
//		Condition * condition = d.createCondition( f );
    Condition * condition = new TypeGround(d.preds.get(f.getToken(d.preds) ) );
    condition->parse(f, ts, d);
    conds.push_back(condition);
  }
  ++f.c;
}

}}  // namespaces
