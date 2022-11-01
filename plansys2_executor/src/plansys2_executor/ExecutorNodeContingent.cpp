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

#include "plansys2_executor/ExecutorNodeContingent.hpp"

namespace plansys2
{

using ExecutePlan = plansys2_msgs::action::ExecutePlan;
using namespace std::chrono_literals;

ExecutorNodeContingent::ExecutorNodeContingent()
: ExecutorNodeBase()
{
  using namespace std::placeholders;

}

rclcpp_action::GoalResponse
ExecutorNodeContingent::handle_goal(
  const rclcpp_action::GoalUUID & uuid,
  std::shared_ptr<const ExecutePlan::Goal> goal)
{
  RCLCPP_DEBUG(this->get_logger(), "Received goal request with order");

  current_plan_ = {};

  return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
}

void
ExecutorNodeContingent::execute(const std::shared_ptr<GoalHandleExecutePlan> goal_handle)
{
  auto feedback = std::make_shared<ExecutePlan::Feedback>();
  auto result = std::make_shared<ExecutePlan::Result>();

  cancel_plan_requested_ = false;

  current_plan_ = goal_handle->get_goal()->plan;

  if (!current_plan_.has_value()) {
    RCLCPP_ERROR(get_logger(), "No plan found");
    result->success = false;
    goal_handle->succeed(result);

    // Publish void plan
    executing_plan_pub_->publish(plansys2_msgs::msg::Plan());
    return;
  }

  executing_plan_pub_->publish(current_plan_.value());

  auto action_map = std::make_shared<std::map<std::string, ActionExecutionInfo>>();

  for (const auto & plan_item: current_plan_.value().items) {
    auto index = BTBuilder::to_action_id(plan_item, 3);

    auto action_info = domain_client_->getAction(
      get_action_name(plan_item.action),
      get_action_params(plan_item.action));
    if (action_info) {
      (*action_map)[index] = ActionExecutionInfo();
      (*action_map)[index].action_executor = ActionExecutor::make_shared(
        plan_item.action,
        shared_from_this());
      (*action_map)[index].durative_action_info =
        std::make_shared<plansys2_msgs::msg::DurativeAction>();
      (*action_map)[index].durative_action_info->name = action_info->name;
      (*action_map)[index].durative_action_info->parameters = action_info->parameters;
      (*action_map)[index].durative_action_info->observe = action_info->observe;
      (*action_map)[index].durative_action_info->at_start_requirements = action_info->preconditions;
      (*action_map)[index].durative_action_info->at_end_effects = action_info->effects;
      (*action_map)[index].duration = plan_item.duration;
      std::string action_name = action_info->name;
    }
  }

  auto bt_builder_plugin = this->get_parameter("bt_builder_plugin").as_string();
  if (bt_builder_plugin.empty()) {
    bt_builder_plugin = "SimpleBTBuilder";
  }

  std::shared_ptr<plansys2::BTBuilder> bt_builder;
  try {
    bt_builder = bt_builder_loader_.createSharedInstance("plansys2::" + bt_builder_plugin);
  } catch (pluginlib::PluginlibException & ex) {
    RCLCPP_ERROR(get_logger(), "pluginlib error: %s", ex.what());
  }

  bt_builder->initialize();

  auto blackboard = BT::Blackboard::create();

  blackboard->set("action_map", action_map);
  blackboard->set("node", shared_from_this());
  blackboard->set("domain_client", domain_client_);
  blackboard->set("problem_client", problem_client_);

  BT::BehaviorTreeFactory factory;
  factory.registerNodeType<ApplyAtEndEffect>("ApplyAtEndEffect");
  factory.registerNodeType<ApplyAtStartEffect>("ApplyAtStartEffect");
  factory.registerNodeType<CheckAction>("CheckAction");
  factory.registerNodeType<CheckAtEndReq>("CheckAtEndReq");
  factory.registerNodeType<ApplyObservation>("ApplyObservation");
  factory.registerNodeType<CheckOverAllReq>("CheckOverAllReq");
  factory.registerNodeType<CheckTimeout>("CheckTimeout");
  factory.registerNodeType<ExecuteAction>("ExecuteAction");
  factory.registerNodeType<WaitAction>("WaitAction");
  factory.registerNodeType<WaitAtStartReq>("WaitAtStartReq");

  auto bt_xml_tree = bt_builder->get_tree(current_plan_.value());

  std::filesystem::path tp = std::filesystem::temp_directory_path();
  std::ofstream out(std::string("/tmp/") + get_namespace() + "/bt.xml");
  out << bt_xml_tree;
  out.close();

  auto tree = factory.createTreeFromText(bt_xml_tree, blackboard);

#ifdef ZMQ_FOUND
  unsigned int publisher_port = this->get_parameter("publisher_port").as_int();
  unsigned int server_port = this->get_parameter("server_port").as_int();
  unsigned int max_msgs_per_second = this->get_parameter("max_msgs_per_second").as_int();

  std::unique_ptr<BT::PublisherZMQ> publisher_zmq;
  if (this->get_parameter("enable_groot_monitoring").as_bool()) {
    RCLCPP_DEBUG(
      get_logger(),
      "[%s] Groot monitoring: Publisher port: %d, Server port: %d, Max msgs per second: %d",
      get_name(), publisher_port, server_port, max_msgs_per_second);
    try {
      publisher_zmq.reset(
        new BT::PublisherZMQ(
          tree, max_msgs_per_second, publisher_port,
          server_port));
    } catch (const BT::LogicError & exc) {
      RCLCPP_ERROR(get_logger(), "ZMQ error: %s", exc.what());
    }
  }
#endif

  auto info_pub = create_wall_timer(
    1s, [this, &action_map]() {
      auto msgs = get_feedback_info(action_map);
      for (const auto & msg: msgs) {
        execution_info_pub_->publish(msg);
      }
    });

  rclcpp::Rate rate(10);
  auto status = BT::NodeStatus::RUNNING;

  while (status == BT::NodeStatus::RUNNING && !cancel_plan_requested_) {
    try {
      status = tree.tickRoot();
    } catch (std::exception & e) {
      std::cerr << e.what() << std::endl;
      status == BT::NodeStatus::FAILURE;
    }

    feedback->action_execution_status = get_feedback_info(action_map);
    goal_handle->publish_feedback(feedback);

//      dotgraph_msg.data = bt_builder->get_dotgraph(
//          action_map, this->get_parameter("enable_dotgraph_legend").as_bool());
//      dotgraph_pub_->publish(dotgraph_msg);

    rate.sleep();
  }

  if (cancel_plan_requested_) {
    tree.haltTree();
  }

  if (status == BT::NodeStatus::FAILURE) {
    tree.haltTree();
    RCLCPP_ERROR(get_logger(), "Executor BT finished with FAILURE state");
  }

  result->success = status == BT::NodeStatus::SUCCESS;
  result->action_execution_status = get_feedback_info(action_map);

  if (rclcpp::ok()) {
    goal_handle->succeed(result);
    if (result->success) {
      RCLCPP_INFO(this->get_logger(), "Plan Succeeded");
    } else {
      RCLCPP_INFO(this->get_logger(), "Plan Failed");
    }
  }
}


}  // namespace plansys2
