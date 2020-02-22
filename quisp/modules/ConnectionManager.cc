/** \file ConnectionManager.cc
 *  \todo clean Clean code when it is simple.
 *  \todo doc Write doxygen documentation.
 *  \authors cldurand,takaakimatsuo
 *
 *  \brief ConnectionManager
 */
#include <vector>
#include <omnetpp.h>
#include "RoutingDaemon.h"
#include "HardwareMonitor.h"
#include <rules/RuleSet.h>
#include <classical_messages_m.h>

using namespace omnetpp;
using namespace quisp::messages;
using namespace quisp::rules;

namespace quisp {
namespace modules {

/** \class ConnectionManager ConnectionManager.cc
 *  \todo Documentation of the class header.
 *
 *  \brief ConnectionManager
 */
class ConnectionManager : public cSimpleModule{
    private:
        int myAddress; // source address
        RoutingDaemon *routingdaemon; // from routing daemon
        HardwareMonitor *hardwaremonitor; // from hardware monitor
    protected:
        virtual void initialize() override; 
        virtual void handleMessage(cMessage *msg) override; 
        // generating RuleSet for entanglement swapping
        virtual RuleSet* generateRuleSet_EntanglementSwapping(unsigned int RuleSet_id, int owner, int left_node, int right_node);
        // create unique id with hash or something?
        virtual unsigned long createUniqueId();
};

Define_Module(ConnectionManager);

// initialize
void ConnectionManager::initialize(){
  EV<<"ConnectionManager booted\n";
  cModule *rd = getParentModule()->getSubmodule("rd");
  routingdaemon = check_and_cast<RoutingDaemon *>(rd);
  cModule *hm = getParentModule()->getSubmodule("hm");
  hardwaremonitor = check_and_cast<HardwareMonitor *>(hm);
  myAddress = par("address");
}



unsigned long ConnectionManager::createUniqueId(){
    std::string time = SimTime().str();
    std::string address = std::to_string(myAddress);
    std::string random = std::to_string(intuniform(0,10000000));
    std::string hash_seed = address+time+random;
    std::hash<std::string> hash_fn;
    size_t  t = hash_fn(hash_seed);
    unsigned long RuleSet_id = static_cast<long>(t);
    std::cout<<"Hash is "<<hash_seed<<", t = "<<t<<", long = "<<RuleSet_id<<"\n";
    return RuleSet_id;
}



// might just be 2*l
static int compute_path_division_size (int l /**< number of links (path length, number of nodes -1) */) {
  if (l > 1) {
    int hl = (l>>1);
    return compute_path_division_size(hl) + compute_path_division_size(l-hl) + 1;
  }
  return l;
}

/** Treat subpath [i:...] of length l */
static int fill_path_division (int * path /**< Nodes on the connection setup path */,
    int i /**< Left of the subpath to consider */, int l /**< Length of the subpath */,
    int * link_left /**< Left part of the list of "links" */, int * link_right /**< Right part */,
    int * swapper /**< Swappers to create those links (might be -1 for real links) */,
    int fill_start /** [0:fill_start[ is already filled */) {
  if (l > 1) {
    int hl = (l>>1);
    fill_start = fill_path_division (path, i, hl, link_left, link_right, swapper, fill_start);
    fill_start = fill_path_division (path, i+hl, l-hl, link_left, link_right, swapper, fill_start);
    swapper[fill_start] = path[i+hl];
  }
  if (l > 0) {
    link_left[fill_start] = path[i];
    link_right[fill_start] = path[i+l];
    if (l == 1) swapper[fill_start] = -1;
    fill_start++;
  }
  return fill_start;
}

void ConnectionManager::handleMessage(cMessage *msg){

    if(dynamic_cast<ConnectionSetupRequest *>(msg)!= nullptr){
       ConnectionSetupRequest *pk = check_and_cast<ConnectionSetupRequest *>(msg);

       int actual_dst = pk->getActual_destAddr();

       if(actual_dst == myAddress){
           // FIXME: is the destination in the stack? Should I add it manually in the end?

           //In Destination node
           //Need to create rule sets. Feel free to change things accordingly.

           // hop_count: number of nodes on the path excluding me (destination node)
           // Should be the same as pk->getStack_of_linkCostsArraySize()
           int hop_count = pk->getStack_of_QNodeIndexesArraySize();

           // Let's store the path in an array to limit indirections
           int * path = new int[hop_count+1];

           // Let's also prepare one ruleset for every node
           // FIXME: maybe it's better to have a map, indexed by node addresses
           RuleSet ** rulesets = new RuleSet * [hop_count+1];

           for (int i = 0; i<hop_count; i++) {
             path[i] = pk->getStack_of_QNodeIndexes(i);
             // TODO: initialize rulets
             // The RuleSet class needs to store the address of the node it is
             //   being sent to.
             // rulesets[i] = new RuleSet(path[i]);
             EV << "     Qnode on the path => " << path[i] << std::endl;
           }
           path[hop_count] = myAddress;
           // rulesets[hop_count] = new RuleSet(myAddress);

           EV << "Last Qnode on the path => " << path[hop_count] << std::endl;

           // Number of division elements
           int divisions = compute_path_division_size(hop_count);
           // One division is: A (left node) ---- B (swapper) ---- C (right node)
           // Sometimes there is no swag rapper, it will be -1 then.
           // For one division, A and C need to do purification rules,
           //   and B needs to do swapping rules.
           int *link_left = new int[divisions],
               *link_right = new int[divisions],
               *swapper = new int[divisions];
           // fill_path_division should yield *exactly* the anticipated number
           // of divisions.
           if (fill_path_division(path, 0, hop_count,
                 link_left, link_right, swapper, 0) < divisions)
             error("Something went wrong in path division computation.");

           /* TODO: Remember you have link costs <3
           for(int i = 0; i<hop_count; i++){
               //The link cost is just a dummy variable (constant 1 for now and how it is set in a bad way (read from the channel but from only 1 channels from Src->BSA and ignoring BSA->Dest).
               //If you need to test with different costs, try changing the value.
               //But we need to implement actual link-tomography for this eventually.
               EV<<"\nThis is one of the stacked link costs....."<<pk->getStack_of_linkCosts(i)<<"\n";
           }
           */

           //Change int to RuleSet*
           std::map<std::string, int> dictionary = {};
           for (int i=0; i<divisions; i++) {
               if(swapper[0]>0){
                  dictionary[std::to_string(link_left[i])] = 0;
                  dictionary[std::to_string(swapper[i])] = 0;
                  dictionary[std::to_string(link_right[i])] = 0;
               }
           }

           for (std::map<std::string, int> ::iterator it=dictionary.begin(); it!=dictionary.end(); ++it)
             EV << it->first << " => " << it->second << '\n';

           // Go over every division
           for (int i=0; i<divisions; i++) {
             if (swapper[i]>0)
               EV << "Division: " << link_left[i] << " ---( " << swapper[i] << " )--- " << link_right[i] << std::endl;
             else
               EV << "Division: " << link_left[i] << " -------------- " << link_right[i] << std::endl;
//#if 0
             /**
              * \todo Create rules for every node
              */
             if (swapper[i]>0) { // This is a swapping relationship
               RuleSet* withEntanglementSwapping = this->generateRuleSet_EntanglementSwapping(createUniqueId(), swapper[i], link_left[i], link_right[i]);

               // 1. Create swapping rules for swapper[i]
               // Rule * swaprule = new SwapRule(...);
               // TODO: IMPLEMENT SwapRule that will have two FidelityClause to
               // check the fidelity of the qubit, and one SwapAction.
               // ruleset_of_swapper_i.rules.append(swaprule);
               // 2. Create swapping tracking rules for link_left[i] and link_right[i]
               //    Right now, those might be empty. In the end they are used to make sure that the left and right
               //    nodes are correctly tracking the estimation of the state of the qubits that get swapped.
             }
             // Whatever happens, this 'i' line is also a 'link' relationship
             // 3. Create the purification rules for link_left[i] and link_right[i]
             // Rule * purifyrule = ...;
             // Rule * discardrule = ...; // do we need it or is it hardcoded?
             // ruleset_of_link_left_i.rules.append(purifyrule);
             // ...
//#endif
           }

           //Packet returning Rule sets
           //Rule set includes Objects composed of clauses and actions.
           //Clauses and actions have functions, like check and
           /**\todo
            * Document how to use Rules, and it would be helpful if you could implement the part where it actually uses it.
            * Dummy non-practical usage example...
            * (For example, do fidelity check and do swap between two local qubits in a single qnic.)
            * Create the response
            * Full 1yr email-support (maybe tele-communication too).
            * Psychological support. Financial support.
            */
           error("Yay!");
           //error("Here");
           delete msg;
           return;
       }else{
           int local_qnic_address_to_actual_dst = routingdaemon->return_QNIC_address_to_destAddr(actual_dst);
           if(local_qnic_address_to_actual_dst==-1){//is not found
               error("QNIC to destination not found");
           }else{
               //Use the QNIC address to find the next hop QNode, by asking the Hardware Monitor (neighbor table).
               EV<<"\n"<<local_qnic_address_to_actual_dst<<"|||||||||||||||||||||||||||||||||||||||||||||||||\n";
               connection_setup_inf dst_inf = hardwaremonitor->return_setupInf(local_qnic_address_to_actual_dst);
               EV << "DST_INF " << dst_inf.qnic.type << "," << dst_inf.qnic.index << "\n";
               connection_setup_inf src_inf = hardwaremonitor->return_setupInf(pk->getSrcAddr());
               EV << "SRC_INF " << src_inf.qnic.type << "," << src_inf.qnic.index << "\n";
               int num_accumulated_nodes = pk->getStack_of_QNodeIndexesArraySize();
               int num_accumulated_costs = pk->getStack_of_linkCostsArraySize();
               int num_accumulated_pair_info = pk->getStack_of_QNICsArraySize();

               //Update information and send it to the next Qnode.
               pk->setDestAddr(dst_inf.neighbor_address);
               pk->setSrcAddr(myAddress);
               pk->setStack_of_QNodeIndexesArraySize(num_accumulated_nodes+1);
               pk->setStack_of_linkCostsArraySize(num_accumulated_costs+1);
               pk->setStack_of_QNodeIndexes(num_accumulated_nodes, myAddress);
               pk->setStack_of_linkCosts(num_accumulated_costs, dst_inf.quantum_link_cost);
               pk->setStack_of_QNICsArraySize(num_accumulated_pair_info+1);
               QNIC_id_pair pair_info = {
                   .fst = src_inf.qnic,
                   .snd = dst_inf.qnic
               };
               pk->setStack_of_QNICs(num_accumulated_pair_info, pair_info);
               pair_info = pk->getStack_of_QNICs(num_accumulated_pair_info);
               EV << "PAIR_INF " << pair_info.fst.type << "," << pair_info.fst.index << " : " << pair_info.snd.type << "," << pair_info.snd.index << "\n";


               send(pk,"RouterPort$o");

           }
       }

    }
}


RuleSet* ConnectionManager::generateRuleSet_EntanglementSwapping(unsigned int RuleSet_id,int owner, int left_node, int right_node){
    /*int rule_index = 0;
    RuleSet* EntanglementSwapping = new RuleSet(RuleSet_id, owner,left_node,right_node);
    Rule* SWAP = new Rule(RuleSet_id, rule_index);
    Condition* SWAP_condition = new Condition();
    Clause* resource_clause_left = new EnoughResourceClauseLeft(1);
    Clause* resource_clause_right = new EnoughResourceClauseRight(1);
    SWAP_condition->addClause(resource_clause_left);
    SWAP_condition->addClause(resource_clause_right);
    SWAP->setCondition(SWAP_condition);
    Action* swap_action = new PurifyAction(RuleSet_id,rule_index,true,false, num_purification_tomography, partner_address, qnic_type , qnic_index,0,1);
    Purification->setAction(purify_action);
    //rule_index++;
    tomography_RuleSet->addRule(Purification);

    return EntanglementSwapping;*/
}

} // namespace modules
} // namespace quisp
