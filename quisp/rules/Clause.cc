/** \file Clause.cc
 *
 *  \authors cldurand,,takaakimatsuo
 *  \date 2018/07/03
 *
 *  \brief Clause
 */
#include "Clause.h"
#include "tools.h"

namespace quisp {
namespace rules {


/*
bool FidelityClause::check(qnicResources* resources) const {
    stationaryQubit* qubit = NULL;
    checkQnic();//This is not doing anything...
    if(qubit = getQubit(resources, qnic_type, qnic_id, partner, resource)){
        return (qubit->getFidelity() >= threshold);
    }
    return false;
}*/

bool FidelityClause::check(std::map<int,stationaryQubit*> resource) const {
    stationaryQubit* qubit = nullptr;
    /*checkQnic();//This is not doing anything...
    if(qubit = getQubit(resources, qnic_type, qnic_id, partner, resource)){
        return (qubit->getFidelity() >= threshold);
    }
    return false;*/
}

bool EnoughResourceClause::check(std::map<int,stationaryQubit*> resource) const{
    //std::cout<<"!!In enough clause \n";
    bool enough = false;

    int num_free = 0;
    for (auto it=resource.begin(); it!=resource.end(); ++it) {
           if(!it->second->isLocked()){
               num_free++;
           }
           if(num_free >= num_resource_required){
               enough = true;
           }
    }
    //std::cout<<"Enough = "<<enough<<"\n";
    return enough;
}

// need qnic index?
bool EnoughResourceClauseLeft::check(std::map<int,stationaryQubit*> resource) const{
    //std::cout<<"!!In enough clause \n";
    bool enough_left = false;

    int num_free = 0;
    for (auto it=resource.begin(); it!=resource.end(); ++it) {
           if(!it->second->isLocked()){
               num_free++;
           }
           if(num_free >= num_resource_required_left){
               enough_left = true;
           }
    }
    //std::cout<<"Enough = "<<enough<<"\n";
    return enough_left;
}

bool EnoughResourceClauseRight::check(std::map<int,stationaryQubit*> resource) const{
    //std::cout<<"!!In enough clause \n";
    bool enough_right = false;

    int num_free = 0;
    for (auto it=resource.begin(); it!=resource.end(); ++it) {
           if(!it->second->isLocked()){
               num_free++;
           }
           if(num_free >= num_resource_required_right){
               enough_right = true;
           }
    }
    //std::cout<<"Enough = "<<enough<<"\n";
    return enough_right;
}

/*
bool MeasureCountClause::check(qnicResources* resources) const {
    //EV<<"MeasureCountClause invoked!!!! \n";
    if(current_count<max_count){
        current_count++;//Increment measured counter.
        EV<<"Measurement count is now "<<current_count<<" < "<<max_count<<"\n";
        return true;
    }
    else{
        EV<<"Count is enough";
        return false;
    }
}*/

bool MeasureCountClause::check(std::map<int,stationaryQubit*> resources) const {
    //std::cout<<"MeasureCountClause invoked!!!! \n";
    if(current_count<max_count){
           current_count++;//Increment measured counter.
           //std::cout<<"Measurement count is now "<<current_count<<" < "<<max_count<<"\n";
           return true;
    }else{
           //std::cout<<"Count is enough\n";
           return false;
    }
}

bool MeasureCountClause::checkTerminate(std::map<int,stationaryQubit*> resources) const {
    EV<<"Tomography termination clause invoked.\n";
    bool done = false;
    if(current_count >=max_count){
        //EV<<"TRUE: Current count = "<<current_count<<" >=  "<<max_count<<"(max)\n";
        done = true;
    }
    return done;
}

/*
bool MeasureCountClause::checkTerminate(qnicResources* resources) const {
    EV<<"Tomography termination clause invoked.\n";
    bool done = false;
    if(current_count >=max_count){
        EV<<"TRUE: Current count = "<<current_count<<" >=  "<<max_count<<"(max)\n";
        done = true;
    }
    return done;
}*/

/*
bool PurificationCountClause::check(qnicResources* resources) const {
    stationaryQubit* qubit = NULL;
    //checkQnic();//This is not doing anything...

    qubit = getQubitPurified(resources, qnic_type, qnic_id, partner, num_purify_must);
    if(qubit != nullptr){
        return true;//There is a qubit that has been purified "num_purify_must" times.
    }else{
        return false;
    }
}*/


bool PurificationCountClause::check(std::map<int,stationaryQubit*> resource) const {
    stationaryQubit* qubit = nullptr;
    //checkQnic();//This is not doing anything...

    /*
    qubit = getQubitPurified(resources, qnic_type, qnic_id, partner, num_purify_must);
    if(qubit != nullptr){
        return true;//There is a qubit that has been purified "num_purify_must" times.
    }else{
        return false;
    }*/
}

/*
bool PurificationCountClause::checkTerminate(qnicResources* resources) const {
        return false;
}*/




//Clause *EXAMPLE_CLAUSE = new FidelityClause(0,0,.6);

} // namespace rules
} // namespace quisp
