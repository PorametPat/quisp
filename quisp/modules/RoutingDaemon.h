/*
 * RoutingDaemon.h
 *
 *  Created on: 2018/06/12
 *      Author: takaakimatsuo
 */

#ifndef MODULES_ROUTINGDAEMON_H_
#define MODULES_ROUTINGDAEMON_H_

#include <omnetpp.h>
#include <modules/QNIC.h>

using namespace omnetpp;


/** \class RoutingDaemon RoutingDaemon.cc
 *  \todo Documentation of the class header.
 *
 *  \brief RoutingDaemon
 */

namespace quisp {
namespace modules {

class RoutingDaemon : public cSimpleModule
{
    private:
        int myAddress;
        typedef std::map<int, QNIC> RoutingTable;  // destaddr -> {gate_index (We need this to access qnic, but it is not unique because we have 3 types of qnics), qnic_address (unique)}
        RoutingTable qrtable;
    protected:
        virtual void initialize(int stage) override; 
        virtual void handleMessage(cMessage *msg) override;
        virtual int numInitStages() const override {return 3;};  // what is Initstage? just return 3 (three stages)
    public:
        virtual int return_QNIC_address_to_destAddr(int destAddr); // return the qnic address according to the destination address
};

}
}

#endif /* MODULES_ROUTINGDAEMON_H_ */
