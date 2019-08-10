//----------------------------------------------------------------------------------------------------------------------
// The MIT License (MIT)
//
// Copyright (c) 2018 Hector Perez Leon
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
// Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
// WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS
// OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//----------------------------------------------------------------------------------------------------------------------

#ifndef FLIGHT_PLAN_COMMS_H
#define FLIGHT_PLAN_COMMS_H

#include <ros/package.h>
#include <ros/ros.h>
#include <uav_abstraction_layer/Land.h>
#include <uav_abstraction_layer/State.h>
#include <uav_abstraction_layer/TakeOff.h>
#include <uav_abstraction_layer/ual.h>
#include <upat_follower/GeneratePath.h>
#include <upat_follower/PreparePath.h>
#include <upat_follower/PrepareTrajectory.h>
#include <upat_follower/Visualize.h>
#include <upat_follower/follower.h>
#include <upat_follower/generator.h>
#include <Eigen/Eigen>
#include <fstream>
#include "ecl/geometry.hpp"
#include "geometry_msgs/PoseStamped.h"
#include "geometry_msgs/Quaternion.h"
#include "nav_msgs/Path.h"
#include "std_msgs/Int8.h"
#include <architecture_math.h>
#include <tf/transform_datatypes.h>
#include "std_msgs/Empty.h"

namespace flight_plan_comms {

double calculateYawRate(double current_yaw, double desired_yaw, double time_interval);


class UALCommunication {
   public:
    UALCommunication();
    ~UALCommunication();

    void runMission();
    void runMission_try2();
    void executeFlightPlan();
    void callVisualization();
    bool flag_hover_ = false;

   private:
    double vxy_ = 2.0;
    double vz_up_ = 3.0;
    double vz_dn_ = 1.0;
    // Callbacks
    void ualStateCallback(const uav_abstraction_layer::State &_ual_state);
    void ualPoseCallback(const geometry_msgs::PoseStamped::ConstPtr &_ual_pose);
    void velocityCallback(const geometry_msgs::TwistStamped &_velocity);
    void flightPlanCallback(const nav_msgs::Path::ConstPtr &_flight_plan);

    // Methods
    void followFlightPlan();
    void followFlightPlan_velocity();
    void runFlightPlan();
    bool prepare();
    double publishYawControl();
    double checkYaw();
    void runFlightPlan_segments();
    bool prepare_yaw();
    bool prepare_position();

    nav_msgs::Path csvToPath(std::string _file_name);
    std::vector<double> csvToVector(std::string _file_name);
    void saveDataForTesting();
    // Node handlers
    ros::NodeHandle nh_, pnh_;
    // Subscribers
    ros::Subscriber sub_pose_, sub_state_, sub_velocity_, sub_flight_plan_;
    // Publishers
    ros::Publisher pub_set_velocity_, pub_set_pose_, flight_plan_state_, pub_flight_plan_;
    // Services
    ros::ServiceClient client_take_off_, client_land_, client_generate_path_, client_visualize_;
    // Variables
    upat_follower::Follower follower_;
    std::string folder_data_name_;
    bool on_path_, end_path_;
    nav_msgs::Path target_path_, vel_percentage_path_, init_path_, current_path_, flight_plan;
    geometry_msgs::PoseStamped ual_pose_;
    geometry_msgs::TwistStamped velocity_;
    geometry_msgs::Quaternion desired_quaternion;
    nav_msgs::Path temp_segment_path; 
    uav_abstraction_layer::State ual_state_;
    std::vector<double> times_;
    // Params
    int uav_id_, generator_mode_;
    bool save_test_;
    double reach_tolerance_;
    std::string init_path_name_;
    std::string pkg_name_ = "upat_follower";

    enum comms_state_t {wait_for_flight= 1, init_segment = 2, execute_yaw= 3, execute_position = 4};
    void switchState(comms_state_t new_comms_state);
    comms_state_t comms_state;
    double take_off_height;

    int current_target;
};

}  // namespace upat_follower

#endif /* FLIGHT_PLAN_COMMS_H */