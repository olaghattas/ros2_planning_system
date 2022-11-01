// Copyright 2019 Intelligent Robotics Lab
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "plansys2_problem_expert/ProblemExpert.hpp"

#include <optional>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>
#include <memory>
#include <set>
#include <map>

#include "plansys2_core/Utils.hpp"
#include "plansys2_pddl_parser/Domain.h"
#include "plansys2_pddl_parser/Instance.h"
#include "plansys2_problem_expert/Utils.hpp"

#include "plansys2_core/Types.hpp"

namespace plansys2
{

ProblemExpert::ProblemExpert(std::shared_ptr<DomainExpert> & domain_expert)
: domain_expert_(domain_expert)
{
}

bool
ProblemExpert::addInstance(const plansys2::Instance & instance)
{
  if (!isValidType(instance.type)) {
    return false;
  }

  std::optional<plansys2::Instance> existing_instance = getInstance(instance.name);
  bool exist_instance = existing_instance.has_value();

  if (exist_instance && existing_instance.value().type != instance.type) {
    return false;
  }

  if (!exist_instance) {
    instances_.push_back(instance);
  }

  return true;
}

std::vector<plansys2::Instance>
ProblemExpert::getInstances()
{
  return instances_;
}

bool
ProblemExpert::removeInstance(const plansys2::Instance & instance)
{
  bool found = false;
  int i = 0;

  while (!found && i < instances_.size()) {
    if (instances_[i].name == instance.name) {
      found = true;
      instances_.erase(instances_.begin() + i);
    }
    i++;
  }

  removeInvalidPredicates(predicates_, instance);
  removeInvalidFunctions(functions_, instance);
  removeInvalidGoals(instance);

  return found;
}

std::optional<plansys2::Instance>
ProblemExpert::getInstance(const std::string & instance_name)
{
  plansys2::Instance ret;

  bool found = false;
  int i = 0;
  while (i < instances_.size() && !found) {
    if (instances_[i].name == instance_name) {
      found = true;
      ret = instances_[i];
    }
    i++;
  }

  if (found) {
    return ret;
  } else {
    return {};
  }
}

std::vector<plansys2::Predicate>
ProblemExpert::getPredicates()
{
  return predicates_;
}

bool
ProblemExpert::addPredicate(const plansys2::Predicate & predicate)
{
  if (!existPredicate(predicate)) {
    if (isValidPredicate(predicate)) {
      predicates_.push_back(predicate);
      return true;
    } else {
      return false;
    }
  } else {
    return true;
  }
}

bool
ProblemExpert::removePredicate(const plansys2::Predicate & predicate)
{
  bool found = false;
  int i = 0;

  if (!isValidPredicate(predicate)) {  // if predicate is not valid, error
    return false;
  }
  while (!found && i < predicates_.size()) {
    if (parser::pddl::checkNodeEquality(predicates_[i], predicate)) {
      found = true;
      predicates_.erase(predicates_.begin() + i);
    }
    i++;
  }

  return true;
}

std::optional<plansys2::Predicate>
ProblemExpert::getPredicate(const std::string & expr)
{
  plansys2::Predicate ret;
  plansys2::Predicate pred = parser::pddl::fromStringPredicate(expr);

  bool found = false;
  size_t i = 0;
  while (i < predicates_.size() && !found) {
    if (parser::pddl::checkNodeEquality(predicates_[i], pred)) {
      found = true;
      ret = predicates_[i];
    }
    i++;
  }

  if (found) {
    return ret;
  } else {
    return {};
  }
}

std::vector<plansys2_msgs::msg::Tree>
ProblemExpert::getConditionals()
{
  return conditionals_;
}

bool
ProblemExpert::addConditional(const plansys2_msgs::msg::Tree & condition)
{
  if (!existConditional(condition)) {
    if (isValidCondition(condition)) {
      if (condition.nodes[0].node_type == plansys2_msgs::msg::Node::ONE_OF &&
        condition.nodes[0].children.size() == 1)
      {
        addPredicate(condition.nodes[1]);
      } else {
        conditionals_.push_back(condition);
      }
      return true;
    } else {
      return false;
    }
  } else {
    return true;
  }
}

bool
ProblemExpert::removeConditional(const plansys2_msgs::msg::Tree & condition)
{
  if (!isValidCondition(condition)) {    // if predicate is not valid, error
    return false;
  }
  auto it = std::find_if(
    conditionals_.begin(), conditionals_.end(), [&condition](const auto & ele) {
      return parser::pddl::checkTreeEquality(ele, condition);
    });
  if (it != conditionals_.end()) {
    conditionals_.erase(it);

    // TODO refactor to its own function
    std::vector<plansys2_msgs::msg::Tree> conditionals_to_remove;
    std::vector<plansys2_msgs::msg::Tree> conditionals_to_add;
    if (condition.nodes[0].node_type == plansys2_msgs::msg::Node::UNKNOWN) {
      for (auto c : conditionals_) {
        if (c.nodes[0].node_type == plansys2_msgs::msg::Node::ONE_OF) {
          plansys2_msgs::msg::Tree new_one_of;
          new_one_of.nodes.push_back(c.nodes[0]);
          new_one_of.nodes[0].children.clear();
          int num_children = 0;
          for (auto child_ind : c.nodes[0].children) {
            if (!parser::pddl::checkNodeEquality(c.nodes[child_ind], condition.nodes[1])) {
              new_one_of.nodes.push_back(c.nodes[child_ind]);
              new_one_of.nodes[0].children.push_back(num_children + 1);
              num_children++;
            }
          }
          conditionals_to_remove.push_back(c);
          if (num_children > 0) {
            conditionals_to_add.push_back(new_one_of);
          }
        }
      }
    }
    for (const auto & c : conditionals_to_remove) {
      removeConditional(c);
    }
    for (const auto & c : conditionals_to_add) {
      addConditional(c);
    }


  }
  return true;
}

bool
ProblemExpert::existConditional(const plansys2_msgs::msg::Tree & condition)
{
  return std::find(conditionals_.begin(), conditionals_.end(), condition) != conditionals_.end();
}

std::vector<plansys2::Function>
ProblemExpert::getFunctions()
{
  return functions_;
}

bool
ProblemExpert::addFunction(const plansys2::Function & function)
{
  if (!existFunction(function)) {
    if (isValidFunction(function)) {
      functions_.push_back(function);
      return true;
    } else {
      return false;
    }
  } else {
    return updateFunction(function);
  }
}

bool
ProblemExpert::removeFunction(const plansys2::Function & function)
{
  bool found = false;
  int i = 0;

  if (!isValidFunction(function)) {  // if function is not valid, error
    return false;
  }
  while (!found && i < functions_.size()) {
    if (parser::pddl::checkNodeEquality(functions_[i], function)) {
      found = true;
      functions_.erase(functions_.begin() + i);
    }
    i++;
  }

  return true;
}

bool
ProblemExpert::updateFunction(const plansys2::Function & function)
{
  if (existFunction(function)) {
    if (isValidFunction(function)) {
      removeFunction(function);
      functions_.push_back(function);
      return true;
    } else {
      return false;
    }
  } else {
    return false;
  }
}

std::optional<plansys2::Function>
ProblemExpert::getFunction(const std::string & expr)
{
  plansys2::Function ret;
  plansys2::Function func = parser::pddl::fromStringFunction(expr);

  bool found = false;
  size_t i = 0;
  while (i < functions_.size() && !found) {
    if (parser::pddl::checkNodeEquality(functions_[i], func)) {
      found = true;
      ret = functions_[i];
    }
    i++;
  }

  if (found) {
    return ret;
  } else {
    return {};
  }
}

void
ProblemExpert::removeInvalidPredicates(
  std::vector<plansys2::Predicate> & predicates,
  const plansys2::Instance & instance)
{
  for (auto rit = predicates.rbegin(); rit != predicates.rend(); ++rit) {
    if (std::find_if(
        rit->parameters.begin(), rit->parameters.end(),
        [&](const plansys2_msgs::msg::Param & param) {
          return param.name == instance.name;
        }) != rit->parameters.end())
    {
      predicates.erase(std::next(rit).base());
    }
  }
}

void
ProblemExpert::removeInvalidFunctions(
  std::vector<plansys2::Function> & functions,
  const plansys2::Instance & instance)
{
  for (auto rit = functions.rbegin(); rit != functions.rend(); ++rit) {
    if (std::find_if(
        rit->parameters.begin(), rit->parameters.end(),
        [&](const plansys2_msgs::msg::Param & param) {
          return param.name == instance.name;
        }) != rit->parameters.end())
    {
      functions.erase(std::next(rit).base());
    }
  }
}

void ProblemExpert::removeInvalidGoals(const plansys2::Instance & instance)
{
  // Get subgoals.
  auto subgoals = parser::pddl::getSubtrees(goal_);

  // Check for subgoals before continuing.
  if (subgoals.empty()) {
    return;
  }

  // Remove invalid subgoals.
  for (auto rit = subgoals.rbegin(); rit != subgoals.rend(); ++rit) {
    // Get predicates.
    std::vector<plansys2_msgs::msg::Node> predicates;
    parser::pddl::getPredicates(predicates, *rit);

    // Check predicates for removed instance.
    bool params_valid = true;
    for (const auto & predicate : predicates) {
      if (std::find_if(
          predicate.parameters.begin(), predicate.parameters.end(),
          [&](const plansys2_msgs::msg::Param & param) {
            return param.name == instance.name;
          }) != predicate.parameters.end())
      {
        params_valid = false;
        break;
      }
    }

    // Remove invalid subgoal.
    if (!params_valid) {
      subgoals.erase(std::next(rit).base());
      continue;
    }

    // Get functions.
    std::vector<plansys2_msgs::msg::Node> functions;
    parser::pddl::getFunctions(functions, *rit);

    // Check functions for removed instance.
    params_valid = true;
    for (const auto & function : functions) {
      if (std::find_if(
          function.parameters.begin(), function.parameters.end(),
          [&](const plansys2_msgs::msg::Param & param) {
            return param.name == instance.name;
          }) != function.parameters.end())
      {
        params_valid = false;
        break;
      }
    }

    // Remove invalid subgoal.
    if (!params_valid) {
      subgoals.erase(std::next(rit).base());
    }
  }

  // Create a new goal from the remaining subgoals.
  auto tree = parser::pddl::fromSubtrees(subgoals, goal_.nodes[0].node_type);
  if (tree) {
    goal_ = plansys2::Goal(*tree);
  } else {
    goal_.nodes.clear();
  }
}

plansys2::Goal
ProblemExpert::getGoal()
{
  return goal_;
}

bool
ProblemExpert::setGoal(const plansys2::Goal & goal)
{
  if (isValidGoal(goal)) {
    goal_ = goal;
    return true;
  } else {
    return false;
  }
}

bool ProblemExpert::isGoalSatisfied(const plansys2::Goal & goal)
{
  return check(goal, predicates_, functions_);
}

bool
ProblemExpert::clearGoal()
{
  goal_.nodes.clear();
  return true;
}

bool
ProblemExpert::clearKnowledge()
{
  instances_.clear();
  predicates_.clear();
  functions_.clear();
  conditionals_.clear();
  clearGoal();

  return true;
}

bool
ProblemExpert::isValidType(const std::string & type)
{
  auto valid_types = domain_expert_->getTypes();
  auto it = std::find(valid_types.begin(), valid_types.end(), type);

  return it != valid_types.end();
}

bool
ProblemExpert::existInstance(const std::string & name)
{
  bool found = false;
  int i = 0;

  while (!found && i < instances_.size()) {
    if (instances_[i].name == name) {
      found = true;
    }
    i++;
  }

  return found;
}

bool
ProblemExpert::existPredicate(const plansys2::Predicate & predicate)
{
  bool found = false;
  int i = 0;

  while (!found && i < predicates_.size()) {
    if (parser::pddl::checkNodeEquality(predicates_[i], predicate)) {
      found = true;
    }
    i++;
  }

  return found;
}

bool
ProblemExpert::existFunction(const plansys2::Function & function)
{
  bool found = false;
  int i = 0;

  while (!found && i < functions_.size()) {
    if (parser::pddl::checkNodeEquality(functions_[i], function)) {
      found = true;
    }
    i++;
  }

  return found;
}

bool
ProblemExpert::isValidPredicate(const plansys2::Predicate & predicate)
{
  bool valid = false;

  const std::optional<plansys2::Predicate> & model_predicate =
    domain_expert_->getPredicate(predicate.name);
  if (model_predicate) {
    if (model_predicate.value().parameters.size() == predicate.parameters.size()) {
      bool same_types = true;
      int i = 0;
      while (same_types && i < predicate.parameters.size()) {
        auto arg_type = getInstance(predicate.parameters[i].name);

        if (!arg_type.has_value()) {
          same_types = false;
        } else if (arg_type.value().type != model_predicate.value().parameters[i].type) {
          bool isSubtype = false;
          for (std::string subType : model_predicate.value().parameters[i].sub_types) {
            if (arg_type.value().type == subType) {
              isSubtype = true;
              break;
            }
          }
          if (!isSubtype) {
            same_types = false;
          }
        }
        i++;
      }
      valid = same_types;
    }
  }

  return valid;
}

bool
ProblemExpert::isValidFunction(const plansys2::Function & function)
{
  bool valid = false;

  const std::optional<plansys2::Function> & model_function =
    domain_expert_->getFunction(function.name);
  if (model_function) {
    if (model_function.value().parameters.size() == function.parameters.size()) {
      bool same_types = true;
      int i = 0;
      while (same_types && i < function.parameters.size()) {
        auto arg_type = getInstance(function.parameters[i].name);

        if (!arg_type.has_value()) {
          same_types = false;
        } else if (arg_type.value().type != model_function.value().parameters[i].type) {
          bool isSubtype = false;
          for (std::string subType : model_function.value().parameters[i].sub_types) {
            if (arg_type.value().type == subType) {
              isSubtype = true;
              break;
            }
          }
          if (!isSubtype) {
            same_types = false;
          }
        }
        i++;
      }
      valid = same_types;
    }
  }

  return valid;
}

bool
ProblemExpert::isValidGoal(const plansys2::Goal & goal)
{
  return checkPredicateTreeTypes(goal, domain_expert_);
}

bool ProblemExpert::isValidCondition(const plansys2_msgs::msg::Tree & cond)
{
  return checkPredicateTreeTypes(cond, domain_expert_);
}

bool
ProblemExpert::checkPredicateTreeTypes(
  const plansys2_msgs::msg::Tree & tree,
  std::shared_ptr<DomainExpert> & domain_expert,
  uint8_t node_id)
{
  if (node_id >= tree.nodes.size()) {
    return false;
  }

  switch (tree.nodes[node_id].node_type) {
    case plansys2_msgs::msg::Node::AND: {
        bool ret = true;

        for (auto & child_id: tree.nodes[node_id].children) {
          ret = ret && checkPredicateTreeTypes(tree, domain_expert, child_id);
        }
        return ret;
      }

    case plansys2_msgs::msg::Node::OR: {
        bool ret = true;

        for (auto & child_id: tree.nodes[node_id].children) {
          ret = ret && checkPredicateTreeTypes(tree, domain_expert, child_id);
        }
        return ret;
      }

    case plansys2_msgs::msg::Node::NOT: {
        return checkPredicateTreeTypes(tree, domain_expert, tree.nodes[node_id].children[0]);
      }

    case plansys2_msgs::msg::Node::UNKNOWN: {
        return tree.nodes[node_id].children.size() == 1 && checkPredicateTreeTypes(
          tree,
          domain_expert,
          tree.nodes[
            node_id].children[0]);
      }

    case plansys2_msgs::msg::Node::ONE_OF: {
        bool ret = true;

        for (auto & child_id: tree.nodes[node_id].children) {
          ret = ret && checkPredicateTreeTypes(tree, domain_expert, child_id);
        }
        return ret;
      }

    case plansys2_msgs::msg::Node::PREDICATE: {
        return isValidPredicate(tree.nodes[node_id]);
      }

    case plansys2_msgs::msg::Node::FUNCTION: {
        return isValidFunction(tree.nodes[node_id]);
      }

    case plansys2_msgs::msg::Node::EXPRESSION: {
        bool ret = true;

        for (auto & child_id: tree.nodes[node_id].children) {
          ret = ret && checkPredicateTreeTypes(tree, domain_expert, child_id);
        }
        return ret;
      }

    case plansys2_msgs::msg::Node::FUNCTION_MODIFIER: {
        bool ret = true;

        for (auto & child_id: tree.nodes[node_id].children) {
          ret = ret && checkPredicateTreeTypes(tree, domain_expert, child_id);
        }
        return ret;
      }

    case plansys2_msgs::msg::Node::NUMBER: {
        return true;
      }

    default:
      // LCOV_EXCL_START
      std::cerr << "checkPredicateTreeTypes: Error parsing expresion [" <<
        parser::pddl::toString(tree, node_id) << "]" << std::endl;
      // LCOV_EXCL_STOP
  }

  return false;
}

std::string
ProblemExpert::getProblem()
{
  parser::pddl::Domain domain(domain_expert_->getDomain());
  parser::pddl::Instance problem(domain);

  problem.name = "problem_1";

  for (const auto & instance : instances_) {
    bool is_constant = domain.getType(instance.type)->parseConstant(instance.name).first;
    if (is_constant) {
      std::cout << "Skipping adding constant as an problem :object: " << instance.name << " " <<
        instance.type << std::endl;
    } else {
      problem.addObject(instance.name, instance.type);
    }
  }

  for (plansys2_msgs::msg::Node predicate : predicates_) {
    StringVec v;

    for (size_t i = 0; i < predicate.parameters.size(); i++) {
      v.push_back(predicate.parameters[i].name);
    }

    std::transform(predicate.name.begin(), predicate.name.end(), predicate.name.begin(), ::tolower);

    problem.addInit(predicate.name, v);
  }

  for (auto cond: conditionals_) {
    auto root_node = cond.nodes[0];
    std::vector<plansys2_msgs::msg::Node> predicates;
    parser::pddl::getPredicates(predicates, cond, 0);
    switch (root_node.node_type) {
      case plansys2_msgs::msg::Node::UNKNOWN: {
          auto predicate = predicates[0];
          StringVec v;
          for (size_t i = 0; i < predicate.parameters.size(); i++) {
            v.push_back(predicate.parameters[i].name);
          }
          std::transform(
            predicate.name.begin(), predicate.name.end(),
            predicate.name.begin(), ::tolower);
          problem.addInitUnknown(predicate.name, v);
          break;
        }
      case plansys2_msgs::msg::Node::ONE_OF: {
          std::vector<StringVec> v_vecs;
          std::vector<std::string> names;
          for (plansys2_msgs::msg::Node predicate: predicates) {
            StringVec v;
            for (size_t i = 0; i < predicate.parameters.size(); i++) {
              v.push_back(predicate.parameters[i].name);
            }
            v_vecs.push_back(v);

            std::transform(
              predicate.name.begin(), predicate.name.end(),
              predicate.name.begin(), ::tolower);
            names.push_back(predicate.name);
          }
          problem.addInitOneOf(names, v_vecs);
          break;
        }
      case plansys2_msgs::msg::Node::OR: {
          std::vector<StringVec> vecs;
          std::vector<std::string> names;
          std::vector<bool> negates;
          for (auto predicate: predicates) {
            StringVec v;
            for (size_t i = 0; i < predicate.parameters.size(); i++) {
              v.push_back(predicate.parameters[i].name);
            }
            std::transform(
              predicate.name.begin(), predicate.name.end(),
              predicate.name.begin(), ::tolower);

            names.push_back(predicate.name);
            vecs.push_back(v);
            negates.push_back(predicate.negate);
          }
          problem.addInitOr({names[0], names[1]}, {vecs[0], vecs[1]}, {negates[0], negates[1]});
          break;
        }
      default: {
          std::cerr << "Failed to add conditional node " << std::endl;
          break;
        }
    }


  }

  for (plansys2_msgs::msg::Node function : functions_) {
    StringVec v;

    for (size_t i = 0; i < function.parameters.size(); i++) {
      v.push_back(function.parameters[i].name);
    }

    std::transform(
      function.name.begin(), function.name.end(),
      function.name.begin(), ::tolower);

    problem.addInit(function.name, function.value, v);
  }

  const std::string gs = parser::pddl::toString(goal_);
  problem.addGoal(gs);

  std::ostringstream stream;
  stream << problem;
  return stream.str();
}

bool
ProblemExpert::addProblem(const std::string & problem_str)
{
  if (problem_str.empty()) {
    std::cerr << "Empty problem." << std::endl;
    return false;
  }
  parser::pddl::Domain domain(domain_expert_->getDomain());

  std::string lc_problem = problem_str;
  std::transform(
    problem_str.begin(), problem_str.end(), lc_problem.begin(),
    [](unsigned char c) {return std::tolower(c);});

  lc_problem = remove_comments(lc_problem);

  std::cout << "Domain:\n" << domain << std::endl;
  std::cout << "Problem:\n" << lc_problem << std::endl;

  parser::pddl::Instance problem(domain);

  std::string domain_name = problem.getDomainName(lc_problem);
  if (domain_name.empty()) {
    std::cerr << "Domain name is empty" << std::endl;
    return false;
  } else if (!domain_expert_->existDomain(domain_name)) {
    std::cerr << "Domain name does not exist: " << domain_name << std::endl;
    return false;
  }

  domain.name = domain_name;
  try {
    problem.parse(lc_problem);
  } catch (std::runtime_error ex) {
    // all errors thrown by the Stringreader object extend std::runtime_error
    std::cerr << ex.what() << std::endl;
    return false;
  }

  std::cout << "Parsed problem: " << problem << std::endl;

  for (unsigned i = 0; i < domain.types.size(); ++i) {
    if (domain.types[i]->constants.size() ) {
      for (unsigned j = 0; j < domain.types[i]->constants.size(); ++j) {
        plansys2::Instance instance;
        instance.name = domain.types[i]->constants[j];
        instance.type = domain.types[i]->name;
        std::cout << "Adding constant: " << instance.name << " " << instance.type << std::endl;
        addInstance(instance);
      }
    }
  }

  for (unsigned i = 0; i < domain.types.size(); ++i) {
    if (domain.types[i]->objects.size() ) {
      for (unsigned j = 0; j < domain.types[i]->objects.size(); ++j) {
        plansys2::Instance instance;
        instance.name = domain.types[i]->objects[j];
        instance.type = domain.types[i]->name;
        std::cout << "Adding instance: " << instance.name << " " << instance.type << std::endl;
        addInstance(instance);
      }
    }
  }


  for (auto ground : problem.init) {
    plansys2_msgs::msg::Tree tree;
    auto tree_node = ground->getTree(tree, domain);
    switch (tree_node->node_type) {
      case plansys2_msgs::msg::Node::PREDICATE: {
          plansys2::Predicate pred_node(*tree_node);
          std::cout << "Adding predicate: " <<
            parser::pddl::toString(tree, tree_node->node_id) << std::endl;
          if (!addPredicate(pred_node)) {
            std::cerr << "Failed to add predicate: " << parser::pddl::toString(
              tree,
              tree_node->node_id) <<
              std::endl;
          }
        }
        break;
      case plansys2_msgs::msg::Node::FUNCTION: {
          plansys2::Function func_node(*tree_node);
          std::cout << "Adding function: " <<
            parser::pddl::toString(tree, tree_node->node_id) << std::endl;
          if (!addFunction(func_node)) {
            std::cerr << "Failed to add function: " << parser::pddl::toString(
              tree,
              tree_node->node_id) <<
              std::endl;
          }
        }
        break;
      default:
        break;
    }
  }

  for (auto cond: problem.init_cond) {
    plansys2_msgs::msg::Tree tree;
    auto tree_node = cond->getTree(tree, domain);
    switch (tree_node->node_type) {
      case plansys2_msgs::msg::Node::UNKNOWN: {
          auto tmp(*tree_node);
          std::cout << "Adding unknown predicate: " << parser::pddl::toString(
            tree,
            tree_node->node_id) <<
            std::endl;
          if (!addConditional(tree)) {
            std::cerr << "Failed to add unknown predicate: " << parser::pddl::toString(
              tree,
              tree_node->node_id) <<
              std::endl;
          }
          break;
        }
      case plansys2_msgs::msg::Node::ONE_OF: {
          auto tmp(*tree_node);
          std::cout << "Adding oneof: " <<
            parser::pddl::toString(tree, tree_node->node_id) << std::endl;
          if (!addConditional(tree)) {
            std::cerr << "Failed to add oneof : " << parser::pddl::toString(
              tree,
              tree_node->node_id) <<
              std::endl;
          }
          break;
        }
      case plansys2_msgs::msg::Node::OR: {
          plansys2_msgs::msg::Tree or_tree;
          auto or_node = cond->getTree(or_tree, domain);
          std::cout << "Adding or node: " <<
            parser::pddl::toString(or_tree, or_node->node_id) << std::endl;
          if (!addConditional(tree)) {
            std::cerr << "Failed to add or : " <<
              parser::pddl::toString(or_tree, or_node->node_id) << std::endl;
          }
          break;
        }
      default:
        break;
    }
  }
  plansys2_msgs::msg::Tree goal;
  auto node = problem.goal->getTree(goal, domain);
  std::cout << "Adding Goal: " << parser::pddl::toString(goal) << std::endl;
  if (setGoal(goal)) {
    std::cout << "Goal insertion ok" << std::endl;
  } else {
    std::cout << "Goal insertion failed" << std::endl;
  }

  return true;
}

}  // namespace plansys2
