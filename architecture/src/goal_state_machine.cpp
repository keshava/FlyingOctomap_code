#include <goal_state_machine.h>
#include <ltStar_lib_ortho.h>
#include <chrono>

#define RUNNING_ROS 1
#define SAVE_CSV 1

namespace goal_state_machine
{
    #ifdef SAVE_CSV
    std::ofstream csv_file;
    std::chrono::high_resolution_clock::time_point timeline_start;
    #endif

    GoalStateMachine::GoalStateMachine(ros::ServiceClient& find_frontiers_client, double distance_inFront, double distance_behind, int circle_divisions, geometry_msgs::Point& geofence_min, geometry_msgs::Point& geofence_max, rviz_interface::PublishingInput pi, double path_safety_margin, double sensing_distance)
		: find_frontiers_client(find_frontiers_client), has_more_goals(false), frontier_index(0), geofence_min(geofence_min), geofence_max(geofence_max), pi(pi), path_safety_margin(path_safety_margin), sensing_distance(sensing_distance), oppair_id(0), new_map(true)
	{
		oppairs_side  = observation_lib::OPPairs(circle_divisions, sensing_distance, distance_inFront, distance_behind, observation_lib::translateAdjustDirection);
        unobservable_set = unobservable_pair_set(); 

        frontier_srv.response.frontiers_found = 0;
        frontier_srv.response.success = false;
        frontier_request_count = 0;

		double distance_from_unknown_under = 1;
		double distance_behind_under, distance_inFront_under;
		double sensor_shape_offset = sensing_distance / std::tan(0.872665);
		double flyby_distance      = distance_behind + distance_inFront;
		distance_behind_under      = distance_behind + sensor_shape_offset;
		double diference           = flyby_distance - distance_behind_under;
		if(diference < 0)
		{
			distance_inFront_under = 0;
		}
		else
		{
			distance_inFront_under = diference; 	
		}

		oppairs_under = observation_lib::OPPairs(circle_divisions/2, distance_from_unknown_under, distance_inFront_under, distance_behind_under, observation_lib::translate);

		std::stringstream aux_envvar_home (std::getenv("HOME"));
	    std::string folder_name = aux_envvar_home.str() + "/Flying_Octomap_code/src/data";
		log_file.open (folder_name+"/current/state_manager.log", std::ofstream::app);
		#ifdef SAVE_CSV
		csv_file.open (folder_name+"/current/goal_state_machine.csv", std::ofstream::app);
        std::chrono::high_resolution_clock::time_point timeline_start = std::chrono::high_resolution_clock::now();
		csv_file << "total_millis" << std::endl;
        #endif
	}

	void GoalStateMachine::NewMap()
	{
		new_map = true;
		frontier_srv.response.success = false;
	}

	bool hasLineOfSight_UnknownAsFree(LazyThetaStarOctree::InputData const& input)
	{
		octomath::Vector3 dummy;
		octomath::Vector3 direction = input.goal - input.start;
		bool is_visible = !input.octree.castRay( input.start, direction, dummy, true, direction.norm());
		return is_visible;
	}

	observation_lib::OPPairs& GoalStateMachine::getCurrentOPPairs()
	{
		if(is_oppairs_side) return oppairs_side;
		else 				return oppairs_under;	
	}

	bool GoalStateMachine::checkFligthCorridor_(double flight_corridor_width, Eigen::Vector3d& start, Eigen::Vector3d& end, ros::Publisher const& marker_pub)
	{
		LazyThetaStarOctree::generateOffsets(octree->getResolution(), flight_corridor_width, LazyThetaStarOctree::semiSphereIn, LazyThetaStarOctree::semiSphereOut );
		octomath::Vector3 start_o(start.x(), start.y(), start.z());
		octomath::Vector3 end_o(end.x(), end.y(), end.z());
		LazyThetaStarOctree::InputData input (*octree, start_o, end_o, flight_corridor_width);
		
		return LazyThetaStarOctree::is_flight_corridor_free(input, rviz_interface::PublishingInput( marker_pub, false));
	}

	bool GoalStateMachine::IsOPPairValid() 
    {
		geometry_msgs::Point start, end;
		start.x = getCurrentOPPairs().get_current_start().x();
		start.y = getCurrentOPPairs().get_current_start().y();
		start.z = getCurrentOPPairs().get_current_start().z();
		end.x = getCurrentOPPairs().get_current_end().x();
		end.y = getCurrentOPPairs().get_current_end().y();
		end.z = getCurrentOPPairs().get_current_end().z();
    	// int sleep_seconds = 0;
    	bool start_inside_geofence = is_inside_geofence(getCurrentOPPairs().get_current_start());
    	if(start_inside_geofence)
    	{
			bool end_inside_geofence = is_inside_geofence(getCurrentOPPairs().get_current_end());
    		if(end_inside_geofence)
	    	{
	    		// bool fc_free = is_flightCorridor_free(path_safety_margin + 1);
	    		Eigen::Vector3d start_e = getCurrentOPPairs().get_current_start();
	    		Eigen::Vector3d end_e = getCurrentOPPairs().get_current_end();

	    		bool fc_free = checkFligthCorridor_(path_safety_margin + 1, start_e, end_e, pi.marker_pub);
				if(!fc_free)
				{
					#ifdef RUNNING_ROS
					if(pi.publish)
					{
						rviz_interface::publish_arrow_straight_line(start, end, pi.marker_pub, false, oppair_id);
    					oppair_id++;
					}
					#endif
    				// ros::Duration(sleep_seconds).sleep();
				}
				return fc_free;
	    	}
	    	else
	    	{
	    		geometry_msgs::Point outsider;
    			outsider.x = getCurrentOPPairs().get_current_end().x();
    			outsider.y = getCurrentOPPairs().get_current_end().y();
    			outsider.z = getCurrentOPPairs().get_current_end().z();
				#ifdef RUNNING_ROS
				if(pi.publish)
				{
					rviz_interface::publish_arrow_straight_line(start, end, pi.marker_pub, false, oppair_id);
				}
		    	rviz_interface::build_endOPP_outsideGeofence(outsider, pi.waypoint_array, oppair_id);
				#endif
    			oppair_id++;
    			pi.marker_pub.publish(pi.waypoint_array);
    			// ROS_INFO_STREAM("End outside geofence.");
    			// ros::Duration(sleep_seconds).sleep();
    			return false;
	    	}
    	}
    	else
    	{
    		geometry_msgs::Point outsider;
			outsider.x = getCurrentOPPairs().get_current_start().x();
			outsider.y = getCurrentOPPairs().get_current_start().y();
			outsider.z = getCurrentOPPairs().get_current_start().z();
			#ifdef RUNNING_ROS
			rviz_interface::build_startOPP_outsideGeofence(outsider, pi.waypoint_array, oppair_id);
			if(pi.publish)
			{
				rviz_interface::publish_arrow_straight_line(start, end, pi.marker_pub, false, oppair_id);
			}
			#endif
			oppair_id++;
			pi.marker_pub.publish(pi.waypoint_array);
			// ROS_INFO_STREAM("Start outside geofence.");
			// ros::Duration(s).sleep();
			return false;
    	}
    }

    bool GoalStateMachine::IsVisible()
    {
    	Eigen::Vector3d start_e = getCurrentOPPairs().get_current_start();
        octomath::Vector3 start(start_e.x(), start_e.y(), start_e.z());
		octomath::Vector3 end  (get_current_frontier().x, get_current_frontier().y, get_current_frontier().z);
		LazyThetaStarOctree::InputData input (*octree, start, end, 0);
		bool has_visibility = hasLineOfSight_UnknownAsFree(input);
        return has_visibility;
    }

	geometry_msgs::Point GoalStateMachine::get_current_frontier() 
    {
    	if(!frontier_srv.response.success) 
    	{
    		log_file << "[Goal SM] A get_current_frontier without frontiers!" << std::endl;
    		ROS_ERROR("[Goal SM] A get_current_frontier without frontiers!");
    	}
        return frontier_srv.response.frontiers[frontier_index].xyz_m;
    }

	void GoalStateMachine::get_current_frontier(Eigen::Vector3d & frontier) 
    {
    	if(!frontier_srv.response.success) 
    	{
    		log_file << "[Goal SM] B get_current_frontier without frontiers!" << std::endl;
    		ROS_ERROR("[Goal SM] B get_current_frontier without frontiers!");
    	}
		frontier << frontier_srv.response.frontiers[frontier_index].xyz_m.x, frontier_srv.response.frontiers[frontier_index].xyz_m.y, frontier_srv.response.frontiers[frontier_index].xyz_m.z;
    }

	bool GoalStateMachine::is_inside_geofence(Eigen::Vector3d target) const
	{
		if(        target.x() < geofence_min.x 
                || target.y() < geofence_min.y 
                || target.z() < geofence_min.z 

                || target.x() > geofence_max.x 
                || target.y() > geofence_max.y  
                || target.z() > geofence_max.z)
        {
            return false;
        }
        else
        {
        	return true;
        }
	}

	bool GoalStateMachine::hasNextFrontier() const
	{
		if(!frontier_srv.response.success) return false;
		return frontier_index < frontier_srv.response.frontiers_found-1;
	}

	void GoalStateMachine::resetOPPair(Eigen::Vector3d& uav_position)
	{
		is_oppairs_side = true;
        Eigen::Vector3d new_frontier;
        get_current_frontier(new_frontier);
		
		oppairs_side.NewFrontier(new_frontier, uav_position, pi);
	    visualization_msgs::MarkerArray marker_array;
	    geometry_msgs::Point curr_frontier_geom;
	    curr_frontier_geom.x = new_frontier.x();
	    curr_frontier_geom.y = new_frontier.y();
	    curr_frontier_geom.z = new_frontier.z();

	    rviz_interface::build_sphere_basic(curr_frontier_geom, marker_array, "unknown_point", 0, 0, 1);

		// // To generate the points to pass under, the sensing_distance is used
		curr_frontier_geom.z = curr_frontier_geom.z-sensing_distance;
        Eigen::Vector3d new_frontier_under(new_frontier.x(), new_frontier.y(), curr_frontier_geom.z);
		oppairs_under.NewFrontier(new_frontier_under, uav_position, pi);
	    rviz_interface::build_sphere_basic(curr_frontier_geom, marker_array, "under_unknown_point", 0.5, 0.5, 1);
	    pi.marker_pub.publish(marker_array);
	}

	bool GoalStateMachine::findFrontiers_CallService(Eigen::Vector3d& uav_position)
	{
		frontier_srv.request.min = geofence_min;
		frontier_srv.request.max = geofence_max;
		frontier_srv.request.current_position.x = uav_position.x();
		frontier_srv.request.current_position.y = uav_position.y();
		frontier_srv.request.current_position.z = uav_position.z();
		frontier_srv.request.frontier_amount = 20;
		frontier_srv.request.request_id = frontier_request_count;
		frontier_srv.request.new_request = new_map;

		if(find_frontiers_client.call(frontier_srv)) 
        { 
        	has_more_goals = frontier_srv.response.success;
            if(frontier_srv.response.success)
            {
            	frontier_index = 0;
        		new_map = false;
				resetOPPair(uav_position);
				return true;
            }
        } 
        else
        {
        	ROS_WARN("[Goal SM] Frontier node not accepting is frontier requests."); 
        } 
        return false;
	}

	bool GoalStateMachine::pointToNextGoal(Eigen::Vector3d& uav_position)
	{	
		#ifdef SAVE_CSV
        std::chrono::high_resolution_clock::time_point pointToNextGoal_start = std::chrono::high_resolution_clock::now();
		#endif
        Eigen::Vector3d unknown;
		while(has_more_goals)
		{
			if(hasNextFrontier())
			{
				get_current_frontier(unknown);

				bool existsNextOPPair = oppairs_side.Next();
				while(existsNextOPPair)
				{
					if( !IsUnobservable(unknown) && IsVisible() && IsOPPairValid() )
					{
						has_more_goals = true;
						return true;
					}		
					existsNextOPPair = oppairs_side.Next();
				}
				is_oppairs_side = false;

				existsNextOPPair = oppairs_under.Next(); 
				while(existsNextOPPair)
				{
					if( !IsUnobservable(unknown) && IsVisible() && IsOPPairValid() )
					{
						has_more_goals = true;
						return true;
					}
					existsNextOPPair = oppairs_under.Next();	 
				}
			}
			frontier_index++;
			if(!hasNextFrontier())
			{
				findFrontiers_CallService(uav_position);
			}
		}


		#ifdef SAVE_CSV
		auto end_millis         = std::chrono::high_resolution_clock::now();
		auto time_span          = std::chrono::duration_cast<std::chrono::duration<double>>(end_millis - pointToNextGoal_start);
		double total_millis  	= std::chrono::duration_cast<std::chrono::milliseconds>(time_span).count();
    	csv_file << total_millis  << std::endl;
		csv_file.close();	
		#endif
		return has_more_goals;
	}

	void GoalStateMachine::DeclareUnobservable()
	{
		// The frontier is unobser
        // Eigen::Vector3d unknown (get_current_frontier().x, get_current_frontier().y, get_current_frontier().z);
		Eigen::Vector3d unknown;
		get_current_frontier(unknown);

        unobservable_set.insert(std::make_pair(unknown, getCurrentOPPairs().get_current_start()));

		// #ifdef SAVE_LOG	
	    
		ROS_INFO_STREAM("[Goal SM] (" << unknown.x() << ", " << unknown.y() << ", " << unknown.z() << ") unobservable from (" << getCurrentOPPairs().get_current_start().x() << ", " << getCurrentOPPairs().get_current_start().y() << ", " << getCurrentOPPairs().get_current_start().z() << ")");
		log_file << "[Goal SM] (" << unknown.x() << ", " << unknown.y() << ", " << unknown.z() << ") unobservable from (" << getCurrentOPPairs().get_current_start().x() << ", " << getCurrentOPPairs().get_current_start().y() << ", " << getCurrentOPPairs().get_current_start().z() << ")" << std::endl;
		for (auto i = unobservable_set.begin(); i != unobservable_set.end(); ++i)
		{
			log_file << "(" << i->first.x() << ", " << i->first.y() << ", " << i->first.z() << ") - (" << i->second.x() << ", " << i->second.y() << ", " << i->second.z() << ")"	<< std::endl;
		}
		// #endif
	}

	bool GoalStateMachine::IsUnobservable(Eigen::Vector3d const& unknown)
	{
		return IsUnobservable(unknown, getCurrentOPPairs().get_current_start());
	}

	bool GoalStateMachine::IsUnobservable(Eigen::Vector3d const& unobservable, Eigen::Vector3d const& viewpoint)
	{
		bool is_unobservable = ! (unobservable_set.find(std::make_pair(unobservable, viewpoint)) ==  unobservable_set.end() );
		// if (is_unobservable) ros::Duration(60).sleep();
		return is_unobservable;
	}


	bool GoalStateMachine::NextGoal(Eigen::Vector3d& uav_position)
	{
		// if(resetOPPair_flag)
		// {
		// 	resetOPPair(uav_position);
		// 	resetOPPair_flag = false;
		// }
		return pointToNextGoal(uav_position);
	}
}