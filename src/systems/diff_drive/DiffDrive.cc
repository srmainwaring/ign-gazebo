/*
 * Copyright (C) 2018 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#include <ignition/math/Odometry.hh>
#include <ignition/math/Quaternion.hh>
#include <ignition/msgs/odometry.pb.h>
#include <ignition/plugin/Register.hh>
#include <ignition/transport/Node.hh>

#include "ignition/gazebo/components/JointPosition.hh"
#include "ignition/gazebo/components/JointVelocityCmd.hh"
#include "ignition/gazebo/Model.hh"

#include "DiffDrive.hh"

using namespace ignition;
using namespace gazebo;
using namespace systems;

class ignition::gazebo::systems::DiffDrivePrivate
{
  /// \brief Callback for velocity subscription
  /// \param[in] _msg Velocity message
  public: void OnCmdVel(const ignition::msgs::Twist &_msg);

  /// \brief Update odometry and publish an odometry message.
  /// \param[in] _info System update information.
  /// \param[in] _ecm The EntityComponentManager of the given simulation
  /// instance.
  public: void UpdateOdometry(const ignition::gazebo::UpdateInfo &_info,
    ignition::gazebo::EntityComponentManager &_ecm);

  /// \brief Ignition communication node.
  public: transport::Node node;

  /// \brief Entity of the left joint
  public: std::vector<Entity> leftJoints;

  /// \brief Entity of the right joint
  public: std::vector<Entity> rightJoints;

  /// \brief Name of left joint
  public: std::vector<std::string> leftJointNames;

  /// \brief Name of right joint
  public: std::vector<std::string> rightJointNames;

  /// \brief Calculated speed of left joint
  public: double leftJointSpeed{0};

  /// \brief Calculated speed of right joint
  public: double rightJointSpeed{0};

  /// \brief Distance between wheels
  public: double wheelSeparation{1.0};

  /// \brief Wheel radius
  public: double wheelRadius{0.2};

  /// \brief Model interface
  public: Model model{kNullEntity};

  /// \brief Diff drive odometry.
  public: math::DiffDriveOdometry odom;

  /// \brief Diff drive odometry message publisher.
  public: transport::Node::Publisher odomPub;
};

//////////////////////////////////////////////////
DiffDrive::DiffDrive()
  : dataPtr(std::make_unique<DiffDrivePrivate>())
{
}

//////////////////////////////////////////////////
void DiffDrive::Configure(const Entity &_entity,
    const std::shared_ptr<const sdf::Element> &_sdf,
    EntityComponentManager &_ecm,
    EventManager &/*_eventMgr*/)
{
  this->dataPtr->model = Model(_entity);

  if (!this->dataPtr->model.Valid(_ecm))
  {
    ignerr << "DiffDrive plugin should be attached to a model entity. "
           << "Failed to initialize." << std::endl;
    return;
  }

  // Ugly, but needed because the sdf::Element::GetElement is not a const
  // function and _sdf is a const shared pointer to a const sdf::Element.
  auto ptr = const_cast<sdf::Element *>(_sdf.get());

  // Get params from SDF
  sdf::ElementPtr sdfElem = ptr->GetElement("left_joint");
  while (sdfElem)
  {
    this->dataPtr->leftJointNames.push_back(sdfElem->Get<std::string>());
    sdfElem = sdfElem->GetNextElement("left_joint");
  }
  sdfElem = ptr->GetElement("right_joint");
  while (sdfElem)
  {
    this->dataPtr->rightJointNames.push_back(sdfElem->Get<std::string>());
    sdfElem = sdfElem->GetNextElement("right_joint");
  }

  this->dataPtr->wheelSeparation = _sdf->Get<double>("wheel_separation",
      this->dataPtr->wheelSeparation).first;
  this->dataPtr->wheelRadius = _sdf->Get<double>("wheel_radius",
      this->dataPtr->wheelRadius).first;

  // Setup odometry.
  this->dataPtr->odom.SetWheelParams(this->dataPtr->wheelSeparation,
      this->dataPtr->wheelRadius, this->dataPtr->wheelRadius);

  // Subscribe to commands
  std::string topic{"/model/" + this->dataPtr->model.Name(_ecm) + "/cmd_vel"};
  if (_sdf->HasElement("topic"))
    topic = _sdf->Get<std::string>("topic");
  this->dataPtr->node.Subscribe(topic, &DiffDrivePrivate::OnCmdVel,
      this->dataPtr.get());

  std::string odomTopic{"/model/" + this->dataPtr->model.Name(_ecm) +
    "/odometry"};
  transport::AdvertiseMessageOptions opts;
  opts.SetMsgsPerSec(50);
  this->dataPtr->odomPub = this->dataPtr->node.Advertise<msgs::Odometry>(
      odomTopic, opts);

  ignmsg << "DiffDrive subscribing to twist messages on [" << topic << "]"
         << std::endl;
}

//////////////////////////////////////////////////
void DiffDrive::PreUpdate(const ignition::gazebo::UpdateInfo &_info,
    ignition::gazebo::EntityComponentManager &_ecm)
{
  // If the joints haven't been identified yet, look for them
  if (this->dataPtr->leftJoints.empty() ||
      this->dataPtr->rightJoints.empty())
  {
    for (const std::string &name : this->dataPtr->leftJointNames)
    {
      Entity joint = this->dataPtr->model.JointByName(_ecm, name);
      if (joint != kNullEntity)
        this->dataPtr->leftJoints.push_back(joint);
    }

    for (const std::string &name : this->dataPtr->rightJointNames)
    {
      Entity joint = this->dataPtr->model.JointByName(_ecm, name);
      if (joint != kNullEntity)
        this->dataPtr->rightJoints.push_back(joint);
    }
  }

  if (this->dataPtr->leftJoints.empty() || this->dataPtr->rightJoints.empty())
    return;

  // Nothing left to do if paused.
  if (_info.paused)
    return;

  for (Entity joint : this->dataPtr->leftJoints)
  {
    // Update wheel velocity
    auto vel = _ecm.Component<components::JointVelocityCmd>(joint);

    if (vel == nullptr)
    {
      _ecm.CreateComponent(
          joint, components::JointVelocityCmd({this->dataPtr->leftJointSpeed}));
    }
    else
    {
      *vel = components::JointVelocityCmd({this->dataPtr->leftJointSpeed});
    }

  }

  for (Entity joint : this->dataPtr->rightJoints)
  {
    // Update wheel velocity
    auto vel = _ecm.Component<components::JointVelocityCmd>(joint);

    if (vel == nullptr)
    {
      _ecm.CreateComponent(joint,
          components::JointVelocityCmd({this->dataPtr->rightJointSpeed}));
    }
    else
    {
      *vel = components::JointVelocityCmd({this->dataPtr->rightJointSpeed});
    }
  }

  // Update odometry.
  this->dataPtr->UpdateOdometry(_info, _ecm);
}

//////////////////////////////////////////////////
void DiffDrivePrivate::UpdateOdometry(const ignition::gazebo::UpdateInfo &_info,
    ignition::gazebo::EntityComponentManager &_ecm)
{
  // Get the first joint positions for the left and right side.
  auto leftPos = _ecm.Component<components::JointPosition>(this->leftJoints[0]);
  if (!leftPos)
  {
    _ecm.CreateComponent(this->leftJoints[0], components::JointPosition());
  }

  auto rightPos = _ecm.Component<components::JointPosition>(
      this->rightJoints[0]);
  if (!rightPos)
  {
    _ecm.CreateComponent(this->rightJoints[0], components::JointPosition());
  }

  // Initialize, if not already initialized.
  if (!this->odom.Initialized())
  {
    this->odom.Init(std::chrono::steady_clock::time_point(_info.simTime));
    return;
  }

  // Abort if the joints were not found or just created.
  if (!leftPos || !rightPos)
    return;

  this->odom.Update(leftPos->Data()[0], rightPos->Data()[0],
      std::chrono::steady_clock::time_point(_info.simTime));

  // Construct the odometry message and publish it.
  msgs::Odometry msg;
  msg.mutable_pose()->mutable_position()->set_x(this->odom.X());
  msg.mutable_pose()->mutable_position()->set_y(this->odom.Y());

  math::Quaterniond orientation(0, 0, *this->odom.Heading());
  msgs::Set(msg.mutable_pose()->mutable_orientation(), orientation);

  msg.mutable_twist()->mutable_linear()->set_x(this->odom.LinearVelocity());
  msg.mutable_twist()->mutable_angular()->set_z(*this->odom.AngularVelocity());

  // Publish the message
  this->odomPub.Publish(msg);
}

//////////////////////////////////////////////////
void DiffDrivePrivate::OnCmdVel(const msgs::Twist &_msg)
{
  auto linVel = _msg.linear().x();
  auto angVel = _msg.angular().z();

  this->rightJointSpeed =
      (linVel + angVel * this->wheelSeparation / 2.0) / this->wheelRadius;
  this->leftJointSpeed =
    (linVel - angVel * this->wheelSeparation / 2.0) / this->wheelRadius;
}

IGNITION_ADD_PLUGIN(DiffDrive,
                    ignition::gazebo::System,
                    DiffDrive::ISystemConfigure,
                    DiffDrive::ISystemPreUpdate)

IGNITION_ADD_PLUGIN_ALIAS(DiffDrive, "ignition::gazebo::systems::DiffDrive")
