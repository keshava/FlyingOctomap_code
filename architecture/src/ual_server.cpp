//----------------------------------------------------------------------------------------------------------------------
// GRVC UAL
//----------------------------------------------------------------------------------------------------------------------
// The MIT License (MIT)
//
// Copyright (c) 2016 GRVC University of Seville
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
#include <architecture_msgs/PositionRequest.h>
#include <geometry_msgs/Pose.h>
#include <geometry_msgs/PoseStamped.h>
#include <geometry_msgs/TwistStamped.h>
#include <nav_msgs/Path.h>
#include <ros/ros.h>
#include <uav_abstraction_layer/State.h>
#include <uav_abstraction_layer/ual.h>
#include <Eigen/Eigen>
#include <Eigen/Geometry> 

nav_msgs::Path uav_current_path, uav_target_path;
Eigen::Vector3f target_point, current_point, fix_orientation_point;
geometry_msgs::PoseStamped target_pose, current_pose, fix_orientation_pose;
Eigen::Quaterniond q_current, q_target_fix;
uav_abstraction_layer::State uav_state;
bool new_target = false;
int cont_init_d_to_target = 0;
float init_d_to_target = 0;
int cont_smooth_vel = 1;
int max_velocity_portions = 50;
enum movement_state_t {hover, velocity, fix_orientation};
movement_state_t movement_state;

void update_current_variables(geometry_msgs::PoseStamped ual_pose){
    current_pose = ual_pose;
    current_point = Eigen::Vector3f(current_pose.pose.position.x, current_pose.pose.position.y, current_pose.pose.position.z);
    q_current.x() = current_pose.pose.orientation.x;
    q_current.y() = current_pose.pose.orientation.y;
    q_current.z() = current_pose.pose.orientation.z;
    q_current.w() = current_pose.pose.orientation.w;
    return;
}

bool target_position_cb(architecture_msgs::PositionRequest::Request &req,
                        architecture_msgs::PositionRequest::Response &res) {
    if (!new_target) {
        if (target_pose.pose.orientation.x != req.pose.orientation.x ||
            target_pose.pose.orientation.y != req.pose.orientation.y ||
            target_pose.pose.orientation.z != req.pose.orientation.z ||
            target_pose.pose.orientation.w != req.pose.orientation.w){
                fix_orientation_pose.pose.orientation = req.pose.orientation;
                fix_orientation_pose.pose.position = target_pose.pose.position;
                movement_state = fix_orientation;
        }else{
                movement_state = velocity;
        }
        target_pose.pose = req.pose;
        uav_target_path.poses.push_back(target_pose);
        target_point = Eigen::Vector3f(target_pose.pose.position.x, target_pose.pose.position.y, target_pose.pose.position.z);
        fix_orientation_point = Eigen::Vector3f(fix_orientation_pose.pose.position.x, fix_orientation_pose.pose.position.y, fix_orientation_pose.pose.position.z);
    }
    new_target = true;
    return true;
}

void state_cb(const uav_abstraction_layer::State msg) {
    uav_state = msg;
    return;
}


geometry_msgs::TwistStamped calculateSmoothVelocity(Eigen::Vector3f x0, Eigen::Vector3f x2, double d) {
    double cruising_speed = 1.0;
    geometry_msgs::TwistStamped output_vel;

    Eigen::Vector3f unit_vec = (x2 - x0) / d;
    double velocity_portion = cruising_speed / max_velocity_portions;

    if (cont_init_d_to_target == 0) {
        init_d_to_target = d;
        cont_init_d_to_target++;
    }

    output_vel.twist.linear.x = unit_vec(0) * velocity_portion * cont_smooth_vel;
    output_vel.twist.linear.y = unit_vec(1) * velocity_portion * cont_smooth_vel;
    output_vel.twist.linear.z = unit_vec(2) * velocity_portion * cont_smooth_vel;

    if (d >= 3) {
        if (cont_smooth_vel < max_velocity_portions) cont_smooth_vel++;
    } else {
        if (cont_smooth_vel > 0) cont_smooth_vel--;
    }

    if (d < 1) {
        cont_smooth_vel = 0;
        init_d_to_target = 0;
        cont_init_d_to_target = 0;
    }

    output_vel.header.frame_id = "uav_1_home";
    return output_vel;
}

geometry_msgs::TwistStamped calculateVelocity(Eigen::Vector3f x0, Eigen::Vector3f x2, double d) {
    double cruising_speed = 1.0;
    geometry_msgs::TwistStamped output_vel;

    Eigen::Vector3f unit_vec = (x2 - x0) / d;

    if (d >= 1.0) {
        output_vel.twist.linear.x = unit_vec(0) * cruising_speed;
        output_vel.twist.linear.y = unit_vec(1) * cruising_speed;
        output_vel.twist.linear.z = unit_vec(2) * cruising_speed;
    } else {
        output_vel.twist.linear.x = 0;
        output_vel.twist.linear.y = 0;
        output_vel.twist.linear.z = 0;
    }
    output_vel.header.frame_id = "uav_1_home";
    return output_vel;
}

int main(int _argc, char **_argv) {
    grvc::ual::UAL ual(_argc, _argv);

    ros::NodeHandle nh;
    ros::Subscriber sub_state = nh.subscribe<uav_abstraction_layer::State>("/uav_1/ual/State", 10, state_cb);
    ros::Publisher pub_current_path = nh.advertise<nav_msgs::Path>("/ual/current_path", 10);
    ros::Publisher pub_target_path = nh.advertise<nav_msgs::Path>("/ual/target_path", 10);
    ros::ServiceServer target_position_service = nh.advertiseService("/uav_1/ual/target_position", target_position_cb);

    int uav_id;
    ros::param::param<int>("~uav_id", uav_id, 1);

    while (!ual.isReady() && ros::ok()) {
        ROS_WARN("UAL %d not ready!", uav_id);
        sleep(1);
    }
    ROS_INFO("UAL %d ready!", uav_id);
    double flight_level = 5.0;
    ual.takeOff(flight_level);

    geometry_msgs::TwistStamped velocity_to_pub;
    velocity_to_pub.header.frame_id = "uav_1_home";
    uav_current_path.header.frame_id = "uav_1_home";
    uav_target_path.header.frame_id = "uav_1_home";
    current_pose.pose.position.x = 0;
    current_pose.pose.position.y = 0;
    current_pose.pose.position.z = flight_level;
    current_pose.pose.orientation.x = 0;
    current_pose.pose.orientation.y = 0;
    current_pose.pose.orientation.z = 0;
    current_pose.pose.orientation.w = 0;
    uav_target_path.poses.push_back(current_pose);

    update_current_variables(ual.pose());

    while (ros::ok()) {
        current_pose = ual.pose();
        double d_to_target = (target_point - current_point).norm();
        std::cout << "Movement state: [" << movement_state << "]" << std::endl;
        switch(movement_state) {
            case hover:
                if (new_target) {
                    ual.goToWaypoint(target_pose, false);
                }
                new_target = false;
                break;
            case velocity:
                while (d_to_target > 0.8 && new_target) {
                    velocity_to_pub = calculateVelocity(current_point, target_point, d_to_target);
                    ual.setVelocity(velocity_to_pub);
                    uav_current_path.poses.push_back(ual.pose());
                    pub_current_path.publish(uav_current_path);
                    update_current_variables(ual.pose());
                    d_to_target = (target_point - current_point).norm();
                    pub_target_path.publish(uav_target_path);
                    sleep(0.1);
                }
                movement_state = hover;
                break;
            case fix_orientation:
                update_current_variables(ual.pose());
                ual.goToWaypoint(fix_orientation_pose, false);
                sleep(5);
                while ((fix_orientation_point - current_point).norm() > 0.2){
                    ual.goToWaypoint(fix_orientation_pose, false);
                    update_current_variables(ual.pose());
                    sleep(0.1);
                }
                movement_state = velocity;
                break;
        }
        uav_current_path.poses.push_back(ual.pose());
        pub_current_path.publish(uav_current_path);
        sleep(1);
    }

    return 0;
}