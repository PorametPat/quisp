/** \file RoutingDaemon.cc
 *  \todo clean Clean code when it is simple.
 *  \todo doc Write doxygen documentation.
 *  \authors takaakimatsuo
 *
 *  \brief RoutingDaemon
 */
#include <vector>
#include <omnetpp.h>
#include <classical_messages_m.h>
#include "RoutingDaemon.h"

using namespace omnetpp;

namespace quisp {
namespace modules {

Define_Module(RoutingDaemon);


/**
 *  brief: boot RoutingDaemon
*/
void RoutingDaemon::initialize(int stage){
        EV<<"Routing Daemon booted\n"; 
        EV<<"Routing table initialized \n";

        myAddress = getParentModule()->par("address"); // getting parameter "address"
        //Topology creation for routing table
        cTopology *topo = new cTopology("topo"); // get the parameter topology
        cMsgPar *yes = new cMsgPar(); // attaching some(?) parameter to the message
        yes->setStringValue("yes"); // set value "yes"
        topo->extractByParameter("includeInTopo", yes->str().c_str());//Any node that has a parameter includeInTopo will be included in routing
        delete(yes);

        //EV << "cTopology found " << topo->getNumNodes() << " nodes\n";
        if(topo->getNumNodes()==0 || topo==nullptr){//If no node with the parameter & value found, do nothing.
                    return;
        }

        cTopology::Node *thisNode = topo->getNodeFor(getParentModule()->getParentModule());//The parent node with this specific router

        int number_of_links_total = 0;

        //Initialize channel weights for all existing links.
        for (int x = 0; x < topo->getNumNodes(); x++) {//Traverse through all nodes
            //For Bidirectional channels, parameters are stored in LinkOut not LinkIn.
            for (int j = 0; j < topo->getNode(x)->getNumOutLinks(); j++) {//Traverse through all links from a specific node.
                //thisNode->disable();//You can also disable nodes or channels accordingly to represent broken hardwares
                //EV<<"\n thisNode is "<< topo->getNode(x)->getModule()->getFullName() <<" has "<<topo->getNode(x)->getNumOutLinks()<<" links \n";
                double channel_cost = topo->getNode(x)->getLinkOut(j)->getLocalGate()->getChannel()->par("cost");//Get assigned cost for each channel written in .ned file

                //EV<<topo->getNode(x)->getLinkOut(j)->getLocalGate()->getFullName()<<" =? "<<"QuantumChannel"<<"\n";
                //if(strcmp(topo->getNode(x)->getLinkOut(j)->getLocalGate()->getChannel()->getFullName(),"QuantumChannel")==0){
                if(strstr(topo->getNode(x)->getLinkOut(j)->getLocalGate()->getFullName(), "quantum")){ // calculating quantum channel cost
                    //Otherwise, keep the quantum channels and set the weight
                    //EV<<"\n Quantum Channel!!!!!! cost is"<<channel_cost<<"\n";
                    topo->getNode(x)->getLinkOut(j)->setWeight(channel_cost);//Set channel weight
                }else{
                    //Ignore classical link in quantum routing table
                    topo->getNode(x)->getLinkOut(j)->disable();
                }
            }
        }

        for (int i = 0; i < topo->getNumNodes(); i++) {//Traverse through all the destinations from the thisNode
                if (topo->getNode(i) == thisNode){
                    continue;  // skip the node that is running this specific router app
                }

                //Apply dijkstra's algorithm to each node to find all shortest paths.
                topo->calculateWeightedSingleShortestPathsTo(topo->getNode(i));//Overwrites getNumPaths() and so on.

                //Check the number of shortest paths towards the target node. This may be more than 1 if multiple paths have the same minimum cost.
                //EV<<"\n Quantum....\n";
                if (thisNode->getNumPaths() == 0){// if there are no path
                    error("Path not found. This means that a node is completely separated...Probably not what you want now");
                    continue;  // not connected
                }

                // Qnic phase?
                cGate *parentModuleGate = thisNode->getPath(0)->getLocalGate();//Returns the next link/gate in the ith shortest paths towards the target node.
                int gateIndex = parentModuleGate->getIndex(); // get the index
                QNIC thisqnic; // define qnic
                int destAddr = topo->getNode(i)->getModule()->par("address"); // get destination address
                thisqnic.address = parentModuleGate->getPreviousGate()->getOwnerModule()->par("self_qnic_address"); // qnic address of my node
                thisqnic.type = (QNIC_type) (int) parentModuleGate->getPreviousGate()->getOwnerModule()->par("self_qnic_type"); // the type of qnic
                thisqnic.index = parentModuleGate->getPreviousGate()->getOwnerModule()->getIndex(); // get qnic index
                thisqnic.pointer = parentModuleGate->getPreviousGate()->getOwnerModule(); // get owner module

                qrtable[destAddr] = thisqnic;//Store gate index per destination from this node
                //EV<<"\n Quantum: "<<topo->getNode(i)->getModule()->getFullName()<<"\n";
                //EV <<"\n  Quantum: Towards node address " << destAddr << " use qnic with address = "<<parentModuleGate->getPreviousGate()->getOwnerModule()->getFullName()<<"\n";
                if(!strstr(parentModuleGate->getFullName(), "quantum")){
                    error("Quantum routing table referring to classical gates...");
                }
        }
       delete topo;
}
/** return_QNIC_address to destAddr
 * 
 * brief: returning qnic address to destination address
 * */ 
int RoutingDaemon::return_QNIC_address_to_destAddr(int destAddr){ 
    Enter_Method("return_QNIC_address_to_destAddr"); // ?
    RoutingTable::iterator it = qrtable.find(destAddr); // get routing table
    if (it == qrtable.end()) { // if the state is end?
         EV << "Quantum: address " << destAddr << " unreachable from this node  \n";
         return -1;
    }
    return it->second.address;
}

void RoutingDaemon::handleMessage(cMessage *msg){

}

} // namespace modules
} // namespace quisp
