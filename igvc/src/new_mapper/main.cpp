#include <cv_bridge/cv_bridge.h>
#include <igvc_msgs/map.h>
#include <pcl/point_cloud.h>
#include <pcl_ros/point_cloud.h>
#include <pcl_ros/transforms.h>
#include <ros/publisher.h>
#include <ros/ros.h>
#include <sensor_msgs/Image.h>
#include <stdlib.h>
#include <math.h>
#include <Eigen/Core>
#include <opencv2/core/eigen.hpp>
#include <opencv2/opencv.hpp>
#include <nav_msgs/Odometry.h>
#include "tf/transform_datatypes.h"

igvc_msgs::map msgBoi;  // >> message to be sent
cv_bridge::CvImage img_bridge;
sensor_msgs::Image imageBoi;  // >> image in the message

ros::Publisher map_pub;
ros::Publisher debug_pub;
ros::Subscriber odom_sub;
ros::Publisher debug_pcl_pub;
cv::Mat *published_map;  // matrix will be publishing
std::map<std::string, tf::StampedTransform> transforms;
tf::TransformListener *tf_listener;
std::string topics;
Eigen::Map<Eigen::Matrix<unsigned char, Eigen::Dynamic, Eigen::Dynamic>> *eigenRep;

double resolution;
double orientation;
int start_x;  // start x location
int start_y;  // start y location
int length_y;
int width_x;
bool debug;
double cur_x;
double cur_y;

std::tuple<double, double> rotate(double x, double y) {
  double newX = x * cos(orientation) + y * -1 * sin(orientation);
  double newY = x * sin(orientation) + y * cos(orientation);
  return (std::make_tuple(newX, newY));
}

void odom_callback(const nav_msgs::Odometry::ConstPtr &msg) {
    cur_x = msg->pose.pose.position.x;
    cur_y = msg->pose.pose.position.y;
    tf::Quaternion quat;
    tf::quaternionMsgToTF(msg->pose.pose.orientation, quat);
    double roll, pitch, yaw;
    tf::Matrix3x3(quat).getRPY(roll, pitch, yaw);
    ROS_INFO_STREAM("orientation " << yaw);
    orientation = yaw;
}

void frame_callback(const pcl::PointCloud<pcl::PointXYZ>::ConstPtr &msg, const std::string &topic)
{
  // transform pointcloud into the occupancy grid, no filtering right now

  bool offMap = false;
  int count = 0;

  // make transformed clouds
  pcl::PointCloud<pcl::PointXYZ>::Ptr transformed =
      pcl::PointCloud<pcl::PointXYZ>::Ptr(new pcl::PointCloud<pcl::PointXYZ>());

  ros::Time time = ros::Time::now();
  if(transforms.find(topic) == transforms.end()) {
    if(tf_listener->waitForTransform("/base_footprint", msg->header.frame_id, ros::Time(0), ros::Duration(3.0))) {
      ROS_INFO_STREAM("\n\ngetting transform for " << topic << "\n\n");
      tf::StampedTransform transform;
      tf_listener->lookupTransform("/base_footprint", msg->header.frame_id, ros::Time(0), transform);
      transforms.insert(std::pair<std::string, tf::StampedTransform>(topic, transform));
    } else {
      ROS_ERROR_STREAM("\n\nfailed to find transform using empty transform\n\n");
      tf::StampedTransform transform;
      transform.setOrigin(tf::Vector3(0.0, 0.0, 0.0));
      tf::Quaternion q;
      q.setRPY(0, 0, 0);
      transform.setRotation(q);
      transform.child_frame_id_ = "/base_footprint";
      transform.frame_id_ = "/lidar";
      transform.stamp_ = ros::Time::now();
      transforms.insert(std::pair<std::string, tf::StampedTransform>(topic, transform));
    }
  }
  pcl_ros::transformPointCloud(*msg, *transformed, transforms.at(topic));

  pcl::PointCloud<pcl::PointXYZ>::const_iterator point;

  for (point = transformed->begin(); point < transformed->points.end(); point++)
  {
    //ROS_INFO_STREAM("points starts at " << point->x << "," << point->y);
    double x_loc, y_loc;
    std::tie(x_loc, y_loc) = rotate(point->x, point->y);
    //ROS_INFO_STREAM("rotated " << x_loc << "," << y_loc);
    int point_x = static_cast<int>(std::round(x_loc / resolution + cur_x / resolution + start_x));
    int point_y = static_cast<int>(std::round(y_loc / resolution + cur_y / resolution + start_y));
    //ROS_INFO_STREAM("point = " << point_x << "," << point_y);
    if (point_x >= 0 && point_y >= 0 && point_x < length_y && start_y < width_x)
    {
      //ROS_INFO_STREAM("putting point at " << point_x << "," << point_y);
      published_map->at<uchar>(point_x, point_y) = (uchar)255;
      count++;
    }
    else if (!offMap)
    {
      ROS_WARN_STREAM("Some points out of range, won't be put on map.");
      offMap = true;
    }
  }
  published_map->at<uchar>(start_x + cur_x / resolution, start_y + cur_y / resolution) = (uchar)100;
  // make image message from img bridge

  img_bridge = cv_bridge::CvImage(msgBoi.header, sensor_msgs::image_encodings::MONO8, *published_map);
  img_bridge.toImageMsg(imageBoi);    // from cv_bridge to sensor_msgs::Image
  time = ros::Time::now();  // so times are exact same
  imageBoi.header.stamp = time;
  // set fields of map message
  msgBoi.header.stamp = time;
  msgBoi.image = imageBoi;
  msgBoi.length = length_y;
  msgBoi.width = width_x;
  msgBoi.resolution = resolution;
  msgBoi.orientation = orientation;
  map_pub.publish(msgBoi);
  if (debug)
  {
    debug_pub.publish(imageBoi);
    ROS_INFO_STREAM("\nThe robot is located at " << cur_x << "," << cur_y << "," << orientation);
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr fromOcuGrid=
              pcl::PointCloud<pcl::PointXYZRGB>::Ptr(new pcl::PointCloud<pcl::PointXYZRGB>());
    for(int i = 0; i < width_x; i++){ // init frame so edges are easily visible
      for(int j = 0; j < length_y; j++){
        if(published_map->at<uchar>(i, j) == (uchar)255) {
          pcl::PointXYZRGB p(255, published_map->at<uchar>(i, j), published_map->at<uchar>(i, j));
          p.x = (i - start_x) * resolution;
          p.y = (j - start_y) * resolution;
          fromOcuGrid->points.push_back(p);
        }
      }
    }
    fromOcuGrid->header.frame_id = "/odom";
    fromOcuGrid->header.stamp = msg->header.stamp;
    debug_pcl_pub.publish(fromOcuGrid);
  }
}

int main(int argc, char **argv)
{
  ros::init(argc, argv, "new_mapper");
  ros::NodeHandle nh;
  ros::NodeHandle pNh("~");

  std::list<ros::Subscriber> subs;
  tf_listener = new tf::TransformListener();

  double cont_start_x;
  double cont_start_y;

  if(!pNh.hasParam("topics") && !pNh.hasParam("occupancy_grid_length") &&
     !pNh.hasParam("occupancy_grid_length") && !pNh.hasParam("occupancy_grid_resolution") &&
     !pNh.hasParam("start_X") & !pNh.hasParam("start_Y") &&
     !pNh.hasParam("debug")) {
    ROS_ERROR_STREAM("missing parameters exiting");
    return 0;
  }

  // assumes all params inputted in meters
  pNh.getParam("topics", topics);
  pNh.getParam("occupancy_grid_length", length_y);
  pNh.getParam("occupancy_grid_width", width_x);
  pNh.getParam("occupancy_grid_resolution", resolution);
  pNh.getParam("start_X", cont_start_x);
  pNh.getParam("start_Y", cont_start_y);
  pNh.getParam("orientation", orientation);
  pNh.getParam("debug", debug);

  // convert from meters to grid
  length_y = (int)std::round(length_y / resolution);
  width_x = (int)std::round(width_x / resolution);
  start_x = (int)std::round(cont_start_x / resolution);
  start_y = (int)std::round(cont_start_y / resolution);
  ROS_INFO_STREAM("cv::Mat length: " << length_y << "  width: " << width_x << "  resolution: " << resolution);

  // set up tokens and get list of subscribers
  std::istringstream iss(topics);
  std::vector<std::string> tokens{ std::istream_iterator<std::string>(iss), std::istream_iterator<std::string>() };
  odom_sub = nh.subscribe("/odometry/filtered", 1, odom_callback);

  for (std::string topic : tokens)
  {
    ROS_INFO_STREAM("Mapper subscribing to " << topic);
    subs.push_back(nh.subscribe<pcl::PointCloud<pcl::PointXYZ>>(topic, 1, boost::bind(frame_callback, _1, topic)));
  }

  published_map = new cv::Mat(length_y, width_x, CV_8UC1);
  eigenRep = new Eigen::Map<Eigen::Matrix<unsigned char, Eigen::Dynamic, Eigen::Dynamic>>(
      (unsigned char *)published_map, width_x, length_y);

  map_pub = nh.advertise<igvc_msgs::map>("/map", 1);

  if (debug)
  {

    debug_pub = nh.advertise<sensor_msgs::Image>("/map_debug", 1);
    debug_pcl_pub = nh.advertise<pcl::PointCloud<pcl::PointXYZRGB>>("/map_debug_pcl", 1);
    /*for(int i = 0; i < width_x; i++){ // init frame so edges are easily visible
      for(int j = 0; j < length_y; j++){
        if(i == 0 || i == width_x - 1 || j == 0 || j == length_y - 1) {
          published_map->at<uchar>(i, j) = (uchar)255;
        }
      }
      }*/
  }

  ros::spin();
}