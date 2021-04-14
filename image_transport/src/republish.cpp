/*********************************************************************
* Software License Agreement (BSD License)
* 
*  Copyright (c) 2009, Willow Garage, Inc.
*  All rights reserved.
* 
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
* 
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of the Willow Garage nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
* 
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*********************************************************************/

#include "image_transport/image_transport.h"
#include "image_transport/publisher_plugin.h"
#include <pluginlib/class_loader.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

int main(int argc, char** argv)
{
  ros::init(argc, argv, "image_republisher", ros::init_options::AnonymousName);
  if (argc < 2) {
    printf("Usage: %s in_transport in:=<in_base_topic> [out_transport] out:=<out_base_topic>\n", argv[0]);
    return 0;
  }
  ros::NodeHandle nh;
  std::string in_topic  = nh.resolveName("in");
  std::string in_transport = argv[1];
  std::string out_topic = nh.resolveName("out");
  bool lazy;
  nh.getParam("lazy", lazy);
  std::atomic_bool subscribed(false);
  std::mutex subscription_mutex;

  image_transport::ImageTransport it(nh);
  image_transport::Subscriber sub;
  
  if (argc < 3) {
    // Use all available transports for output
    image_transport::Publisher pub;
    
    std::function<void(void)> on_new_subscriber([&lazy, &subscribed, &sub, &pub, &it, &in_topic, &in_transport, &subscription_mutex](){
        std::unique_lock<std::mutex> lk(subscription_mutex);
        if(!subscribed.load()){
          // Use Publisher::publish as the subscriber callback
          typedef void (image_transport::Publisher::*PublishMemFn)(const sensor_msgs::ImageConstPtr&) const;
          PublishMemFn pub_mem_fn = &image_transport::Publisher::publish;
          sub = it.subscribe(in_topic, 1, boost::bind(pub_mem_fn, &pub, _1), ros::VoidPtr(), in_transport);
          subscribed = true;
        }
      });
    image_transport::SubscriberStatusCallback on_subscriber_left(
      [&lazy, &subscribed, &sub, &pub, &it, &subscription_mutex](const image_transport::SingleSubscriberPublisher& ssp){
        std::unique_lock<std::mutex> lk(subscription_mutex);
        if(lazy && pub.getNumSubscribers() == 0){
          sub.shutdown();
          subscribed = false;
        }
      }
    );

    pub = it.advertise(out_topic, 1, boost::bind(on_new_subscriber), on_subscriber_left);
    if(!lazy){
      on_new_subscriber();
    }
    

    ros::spin();
  }
  else {
    // Use one specific transport for output
    std::string out_transport = argv[2];

    // Load transport plugin
    typedef image_transport::PublisherPlugin Plugin;
    pluginlib::ClassLoader<Plugin> loader("image_transport", "image_transport::PublisherPlugin");
    std::string lookup_name = Plugin::getLookupName(out_transport);
    boost::shared_ptr<Plugin> pub( loader.createInstance(lookup_name) );

    std::function<void(void)> on_new_subscriber(
      [&lazy, &subscribed, &sub, &pub, &it, &in_topic, &in_transport, &subscription_mutex](){
        std::unique_lock<std::mutex> lk(subscription_mutex);
        if(!subscribed.load()){
          // Use PublisherPlugin::publish as the subscriber callback
          typedef void (Plugin::*PublishMemFn)(const sensor_msgs::ImageConstPtr&) const;
          PublishMemFn pub_mem_fn = &Plugin::publish;
          sub = it.subscribe(in_topic, 1, boost::bind(pub_mem_fn, pub.get(), _1), pub, in_transport);
          subscribed = true;
        }
      }
    );
    image_transport::SubscriberStatusCallback on_subscriber_left(
      [&lazy, &subscribed, &sub, &pub, &it, &subscription_mutex](const image_transport::SingleSubscriberPublisher& ssp){
        std::unique_lock<std::mutex> lk(subscription_mutex);
        if(lazy && pub->getNumSubscribers() == 0){
          sub.shutdown();
          subscribed = false;
        }
      }
    );

    pub->advertise(nh, out_topic, 1, boost::bind(on_new_subscriber),
                   on_subscriber_left, ros::VoidPtr(), false);

    if(!lazy){
      on_new_subscriber();
    }

    ros::spin();
  }

  return 0;
}
