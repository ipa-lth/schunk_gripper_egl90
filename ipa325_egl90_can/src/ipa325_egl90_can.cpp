#include "ipa325_egl90_can/egl90_can_node.h"

#include <signal.h>
#include <errno.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <net/if.h>
#include <sys/types.h>
#include <sys/ioctl.h>
//#include <linux/can/raw.h>
#include <iostream>
#include <iomanip>
#include <string>

bool Egl90_can_node::_shutdownSignal = false;

Egl90_can_node::Egl90_can_node()
{
    //signal handler
    signal((int) SIGINT, Egl90_can_node::signalHandler);

    _nh = ros::NodeHandle("~");
    std::string nodename = ros::this_node::getName();

    _srv_ack = _nh.advertiseService(nodename+"/acknowledge", &Egl90_can_node::acknowledge, this);
    _srv_reference = _nh.advertiseService(nodename+"/reference_motion", &Egl90_can_node::moveToReferencePos, this);
    _srv_movePos = _nh.advertiseService(nodename+"/move_pos", &Egl90_can_node::movePos, this);
    _srv_moveGrip = _nh.advertiseService(nodename+"/move_grip", &Egl90_can_node::moveGrip, this);
    _srv_getState = _nh.advertiseService(nodename+"/get_state", &Egl90_can_node::getState, this);

    struct sockaddr_can address;
    struct ifreq interreq;

    // TODO: Make this parameters
    _can_id = 0x050C; // 0x05 for master, module id 0xC = 12
    _can_module_id = 0x070C; // 0x07 for slave, module id 0xC = 12
    _can_socket_id = "can0"; // name within linux ifconfig

    //Open CanSocket
    if ((_can_socket = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
        ROS_ERROR("Error while opening can socket");
        exit(-1);
    }

    strcpy(interreq.ifr_name, _can_socket_id.c_str());
    ioctl(_can_socket, SIOCGIFINDEX, &interreq);

    address.can_family = AF_CAN;
    address.can_ifindex = interreq.ifr_ifindex;

    ROS_INFO("%s at index %d\n", _can_socket_id.c_str(), interreq.ifr_ifindex);

    //Bind CanSocket
    if (bind(_can_socket, (struct sockaddr *) &address, sizeof(address)) < 0) {
        ROS_ERROR("Error in socket bind");
        exit(-2);
    }
    else
    {
        ROS_INFO("Can socket binding was successful!");
    }

}

bool Egl90_can_node::moveToReferencePos(std_srvs::Trigger::Request &req, std_srvs::Trigger::Response &res)
{
    struct can_frame txframe, rxframe;
    bool error_flag = false;

    txframe.can_id = _can_id;
    txframe.can_dlc = 0x02;
    txframe.data[0] = 0x01;
    txframe.data[1] = 0x92;

    write(_can_socket, &txframe, sizeof(struct can_frame));
    do
    {
        ros::Duration(0.01).sleep();
        read(_can_socket, &rxframe, sizeof(struct can_frame));
    } while (!isCanAnswer(0x92, rxframe, error_flag) || _shutdownSignal);

    if (error_flag)
    {
        res.success = false;
        res.message = "Module did reply with error 0x02!";
        return true;
    }
    else
    {
        do // wait for position reached signal
        {
            ros::Duration(0.01).sleep();
            read(_can_socket, &rxframe, sizeof(struct can_frame));
        } while ((rxframe.can_dlc < 2 && rxframe.data[2] != 0x94) || _shutdownSignal); // 0x94 is CMD_POS_REACHED

        // TODO: timeout while reading socket
        bool timeout = false;
        if (timeout)
        {
            res.success = false;
            res.message = "Module reached position!";
            return true;
        }
        else
        {
            res.success = true;
            res.message = "Module did reply properly!";
        }
    }

    return true;
}

bool Egl90_can_node::acknowledge(std_srvs::Trigger::Request &req, std_srvs::Trigger::Response &res)
{
    struct can_frame txframe, rxframe;
    bool error_flag = false;

    txframe.can_id = _can_id;
    txframe.can_dlc = 0x02;
    txframe.data[0] = 0x01;
    txframe.data[1] = 0x8B;

    write(_can_socket, &txframe, sizeof(struct can_frame));
    do
    {
        ros::Duration(0.01).sleep();
        read(_can_socket, &rxframe, sizeof(struct can_frame));
    } while (!isCanAnswer(0x8B, rxframe, error_flag) || _shutdownSignal);

    if (error_flag)
    {
        res.success = false;
        res.message = "Module did reply with error 0x02!";
    }
    else
    {
        res.success = true;
        res.message = "Module did reply properly!";
    }

    return true;
}

bool Egl90_can_node::getState(std_srvs::Trigger::Request &req, std_srvs::Trigger::Response &res)
{
    struct can_frame txframe, rxframe1, rxframe2, rxframe3;

    txframe.can_id = _can_id;
    txframe.can_dlc = 0x02;
    txframe.data[0] = 0x01;
    txframe.data[1] = 0x95;

    write(_can_socket, &txframe, sizeof(struct can_frame));

    ros::Duration(0.01).sleep();
    // TODO the fragmented CAN message protocol is weird, this will only work if the can is empty besided this module
    read(_can_socket, &rxframe1, sizeof(struct can_frame));
    read(_can_socket, &rxframe2, sizeof(struct can_frame));
    read(_can_socket, &rxframe3, sizeof(struct can_frame));

    statusData status;
    status.c[0] = rxframe1.data[3];
    status.c[1] = rxframe1.data[4];
    status.c[2] = rxframe1.data[5];
    status.c[3] = rxframe1.data[6];
    status.c[4] = rxframe1.data[7];

    status.c[5] = rxframe2.data[2];
    status.c[6] = rxframe2.data[3];
    status.c[7] = rxframe2.data[4];
    status.c[8] = rxframe2.data[5];
    status.c[9] = rxframe2.data[6];
    status.c[10] = rxframe2.data[7];

    status.c[11] = rxframe3.data[2];
    status.c[12] = rxframe3.data[3];
    status.c[13] = rxframe3.data[4];

    ROS_INFO("Position: %f,\nVelocity: %f,\nCurrent: %f", status.status.position, status.status.speed, status.status.current);
/*
    bool isReferenced;
    bool isMoving;
    bool isInProgMode;
    bool isWarning;
    bool isError;
    bool isBraked;
    bool isMotionInterrupted;
    bool isTargetReached;
    int errorCode;
*/
    ROS_WARN("Status bits not correct!");
    ROS_INFO("IsReferenced: %s,\nIsMoving: %s,\nIsInProgMode: %s\nIsWarning: %s\nIsError: %s\nIsBraked: %s\nisMotionInterrupted: %s\nIsTargetReached: %s\nErrorCode: %X",
             status.status.isReferenced ? "True" : "False",
             status.status.isMoving ? "True" : "False",
             status.status.isInProgMode ? "True" : "False",
             status.status.isWarning ? "True" : "False",
             status.status.isError ? "True" : "False",
             status.status.isBraked ? "True" : "False",
             status.status.isMotionInterrupted ? "True" : "False",
             status.status.isTargetReached ? "True" : "False",
             status.status.errorCode);

    res.success = true;
    res.message = "ok";

    return true;
}

bool Egl90_can_node::movePos(ipa325_egl90_can::MovePos::Request &req, ipa325_egl90_can::MovePos::Response &res)
{
    fdata pos;
    pos.f = req.position;
    bool error_flag = false;

    struct can_frame txframe, rxframe;

    txframe.can_id = _can_id;
    txframe.can_dlc = 6;

    txframe.data[0] = 5;
    txframe.data[1] = 0xB0;

    txframe.data[2] = pos.c[0];
    txframe.data[3] = pos.c[1];
    txframe.data[4] = pos.c[2];
    txframe.data[5] = pos.c[3];

    write(_can_socket, &txframe, sizeof(struct can_frame));
    do
    {
        ros::Duration(0.01).sleep();
        read(_can_socket, &rxframe, sizeof(struct can_frame));
    } while (!isCanAnswer(0xB0, rxframe, error_flag) || _shutdownSignal);

    if (error_flag)
    {
        res.success = false;
        res.message = "Module did reply with error 0x02!";
    }
    else
    {
        do // wait for position reached signal
        {
            ros::Duration(0.01).sleep();
            read(_can_socket, &rxframe, sizeof(struct can_frame));
        } while ((rxframe.can_dlc < 2 && rxframe.data[2] != 0x94) || _shutdownSignal); // 0x94 is CMD_POS_REACHED

        // TODO: timeout while reading socket
        bool timeout = false;
        if (timeout)
        {
            res.success = false;
            res.message = "Module reached position!";
            return true;
        }
        else
        {
            res.success = true;
            res.message = "Module did reply properly!";
        }
    }

    return true;
}

bool Egl90_can_node::moveGrip(ipa325_egl90_can::MoveGrip::Request &req, ipa325_egl90_can::MoveGrip::Response &res)
{
     ROS_ERROR("move_grip seems not to be available in module 12");
     return false;
// --------------------------------------------------//
     fdata cur;
     cur.f = req.current;
     bool error_flag = false;

     struct can_frame txframe, rxframe;

     txframe.can_id = _can_id;
     txframe.can_dlc = 6;

     txframe.data[0] = 5;
     txframe.data[1] = 0xB7; // Move_grip seems to be not available in module 12

     txframe.data[2] = cur.c[0];
     txframe.data[3] = cur.c[1];
     txframe.data[4] = cur.c[2];
     txframe.data[5] = cur.c[3];

     write(_can_socket, &txframe, sizeof(struct can_frame));
     do
     {
         ros::Duration(0.01).sleep();
         read(_can_socket, &rxframe, sizeof(struct can_frame));
     } while (!isCanAnswer(0xB7, rxframe, error_flag) || _shutdownSignal);

     if (error_flag)
     {
         res.success = false;
         res.message = "Module did reply with error 0x02!";
     }
     else
     {
         do // wait for position reached signal
         {
             ros::Duration(0.01).sleep();
             read(_can_socket, &rxframe, sizeof(struct can_frame));
         } while ((rxframe.can_dlc < 2 && rxframe.data[2] != 0x93) || _shutdownSignal); // 0x93 is CMD_MOVE_BLOCKED

         // TODO: timeout while reading socket
         bool timeout = false;
         if (timeout)
         {
             res.success = false;
             res.message = "Module reached position!";
             return true;
         }
         else
         {
             res.success = true;
             res.message = "Module did reply properly!";
         }
     }

     return true;
}

bool Egl90_can_node::isCanAnswer(unsigned int cmd, const can_frame& rxframe, bool& error_flag)
{
    error_flag = (rxframe.data[0] == 0x02);
    return (rxframe.can_id == _can_module_id &&
            rxframe.data[1] == cmd);
}

void Egl90_can_node::spin()
{
    ros::Rate rate(100);

    //wait for shutdown
    while(ros::ok() && !Egl90_can_node::_shutdownSignal)
    {
        ros::spinOnce();
        rate.sleep();
    }
}

void Egl90_can_node::signalHandler(int signal)
{
    ROS_INFO_NAMED("driver", "shutdown");
    _shutdownSignal = true;
}

