/** \file HardwareMonitor.cc
 *  \todo clean Clean code when it is simple.
 *  \todo doc Write doxygen documentation.
 *  \authors cldurand,takaakimatsuo
 *  \date 2018/03/19
 *
 *  \brief HardwareMonitor
 */
#include "HardwareMonitor.h"
#include "classical_messages_m.h"
#include <sstream>
#include <string>
#include <unsupported/Eigen/MatrixFunctions>
#include <unsupported/Eigen/KroneckerProduct>
#include <iostream>
#include <fstream>

namespace quisp {
namespace modules {

using namespace rules;

Define_Module(HardwareMonitor);

//Hm is also responsible for calculating the rssi/oka's protocol/fidelity calcu and give it to the rd
void HardwareMonitor::initialize(int stage)
{
  EV<<"HardwareMonitor booted\n";

  output_count initial;
  initial.minus_minus=0;
  initial.minus_plus=0;
  initial.plus_minus=0;
  initial.plus_plus=0;
  initial.total_count=0;

  Pauli.X << 0,1,1,0;
  Pauli.Y << 0,Complex(0,-1),Complex(0,1),0;
  Pauli.Z << 1,0,0,-1;
  Pauli.I << 1,0,0,1;


  numQnic_rp = par("number_of_qnics_rp");// number of qnics connected to epps.
  numQnic_r = par("number_of_qnics_r");// number of qnics connected to internal hom.
  numQnic = par("number_of_qnics");// number of qnics connected to stand alone HoM or internal hom in the neighbor.
  numQnic_total = numQnic + numQnic_r + numQnic_rp;

  /*This is used to keep your own tomography data, and also to match and store the received partner's tomography data*/
   all_temporal_tomography_output_holder = new Temporal_Tomography_Output_Holder[numQnic_total];//Assumes link tomography only between neighbors.
   all_temporal_tomography_runningtime_holder = new link_cost[numQnic_total];
  /*Once all_temporal_tomography_output_holder is filled in, those data are summarized into basis based measurement outcome table. This accumulates the number of ++, +-, -+ and -- for each basis combination.*/
  tomography_data = new raw_data[numQnic_total];//Raw count table for tomography per link/qnic

  for(int i=0; i<numQnic_total; i++){
      tomography_data[i].insert(std::make_pair("XX",initial));
      tomography_data[i].insert(std::make_pair("XY",initial));
      tomography_data[i].insert(std::make_pair("XZ",initial));
      tomography_data[i].insert(std::make_pair("ZX",initial));
      tomography_data[i].insert(std::make_pair("ZY",initial));
      tomography_data[i].insert(std::make_pair("ZZ",initial));
      tomography_data[i].insert(std::make_pair("YX",initial));
      tomography_data[i].insert(std::make_pair("YY",initial));
      tomography_data[i].insert(std::make_pair("YZ",initial));
      all_temporal_tomography_runningtime_holder[i].Bellpair_per_sec = -1;
      all_temporal_tomography_runningtime_holder[i].tomography_measurements = -1;
      all_temporal_tomography_runningtime_holder[i].tomography_time = -1;
  }
  //std::cout<<"numQnic_total"<<numQnic_total<<"\n";


   /*This keeps which node is connected to which local qnic.*/
  tomography_output_filename = par("tomography_output_filename").str();
  file_dir_name= par("file_dir_name").str();
  ntable = prepareNeighborTable(ntable, numQnic_total);
  do_link_level_tomography = par("link_tomography");
  num_purification = par("initial_purification");
  X_Purification = par("X_purification");
  Z_Purification = par("Z_purification");
  Purification_type = par("Purification_type");
  num_measure = par("num_measure");
  myAddress = par("address");
  std::stringstream ss;
  for(auto it = ntable.cbegin(); it != ntable.cend(); ++it){
      ss << it->first << "(d)->(i)" << it->second.qnic.index <<", ";
  }
  std::string s = ss.str();
  par("ntable") = s;

  if(do_link_level_tomography && stage == 1){

      for(auto it = ntable.cbegin(); it != ntable.cend(); ++it){
          if(myAddress > it->second.neighborQNode_address){//You dont want 2 separate tomography processes to run for each link. Not a very good solution, but makes sure that only 1 request per link is generated.
              EV<<"Generating tomography rules... for node "<<it->second.neighborQNode_address<<"\n";
              LinkTomographyRequest *pk = new LinkTomographyRequest;
              pk->setDestAddr(it->second.neighborQNode_address);
              pk->setSrcAddr(myAddress);
              pk->setKind(6);
              send(pk,"RouterPort$o");
          }
      }
  }
}

unsigned long HardwareMonitor::createUniqueId(){
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

void HardwareMonitor::handleMessage(cMessage *msg){
    if(dynamic_cast<LinkTomographyRequest *>(msg) != nullptr){
        /*Received a tomography request from neighbor*/
        LinkTomographyRequest *request = check_and_cast<LinkTomographyRequest *>(msg);
        /*Prepare an acknowledgement*/
        LinkTomographyAck *pk = new LinkTomographyAck;
        pk->setSrcAddr(myAddress);
        pk->setDestAddr(request->getSrcAddr());
        pk->setKind(6);
        QNIC_type qnic_type;
        int qnic_index = -1;
        for(auto it = ntable.cbegin(); it != ntable.cend(); ++it){
            if(it->second.neighborQNode_address == request->getSrcAddr()){
                qnic_type = it->second.qnic.type;
                qnic_index = it->second.qnic.index;
                break;
            }
        }if(qnic_index == -1){
            error("1. Something is wrong when finding out local qnic address from neighbor address in ntable.");
        }
        pk->setQnic_index(qnic_index);
        pk->setQnic_type(qnic_type);

        send(pk,"RouterPort$o");

    }else if(dynamic_cast<LinkTomographyAck *>(msg) != nullptr){
        /*Received an acknowledgement for tomography from neighbor.*/
        LinkTomographyAck *ack = check_and_cast<LinkTomographyAck *>(msg);
        /*Create and send RuleSets*/
        int partner_address = ack->getSrcAddr();
        QNIC_type partner_qnic_type = ack->getQnic_type();
        int partner_qnic_index = ack->getQnic_index();

        QNIC_type my_qnic_type;
        int my_qnic_index = -1;

        for(auto it = ntable.cbegin(); it != ntable.cend(); ++it){
            if(it->second.neighborQNode_address == ack->getSrcAddr()){
                my_qnic_type = it->second.qnic.type;
                my_qnic_index = it->second.qnic.index;
                break;
            }
        }if(my_qnic_index == -1){
            error("2. Something is wrong when finding out local qnic address from neighbor address in ntable.");
        }
        //RuleSets sent for this node and the partner node.

        long RuleSet_id = createUniqueId();
        sendLinkTomographyRuleSet(myAddress, partner_address, my_qnic_type, my_qnic_index, RuleSet_id);
        sendLinkTomographyRuleSet(partner_address,myAddress, partner_qnic_type, partner_qnic_index, RuleSet_id);

    }else if (dynamic_cast<LinkTomographyResult *>(msg) != nullptr){
        /*Link tomography measurement result/basis from neighbor received.*/
        LinkTomographyResult *result = check_and_cast<LinkTomographyResult *>(msg);
        QNIC local_qnic = search_QNIC_from_Neighbor_QNode_address(result->getPartner_address());//Get QNIC info from neighbor address.
        auto it = all_temporal_tomography_output_holder[local_qnic.address].find(result->getCount_id());
        if (it != all_temporal_tomography_output_holder[local_qnic.address].end()){
            EV<<"Data already found.";
            tomography_outcome temp = it->second;
            if(result->getSrcAddr() == myAddress){
                temp.my_basis = result->getBasis();
                temp.my_output_is_plus = result->getOutput_is_plus();
                temp.my_GOD_clean = result->getGOD_clean();
            }else{
                temp.partner_basis = result->getBasis();
                temp.partner_output_is_plus = result->getOutput_is_plus();
                temp.partner_GOD_clean = result->getGOD_clean();
            }it->second = temp;
        }else{
            EV<<"Fresh data";
            tomography_outcome temp;
            if(result->getSrcAddr() == myAddress){
                temp.my_basis = result->getBasis();
                temp.my_output_is_plus = result->getOutput_is_plus();
                temp.my_GOD_clean = result->getGOD_clean();
            }else{
                temp.partner_basis = result->getBasis();
                temp.partner_output_is_plus = result->getOutput_is_plus();
                temp.partner_GOD_clean = result->getGOD_clean();
            }
            all_temporal_tomography_output_holder[local_qnic.address].insert(std::make_pair(result->getCount_id(), temp));
        }
        if(result->getFinish()!=-1){
            if(all_temporal_tomography_runningtime_holder[local_qnic.address].tomography_time < result->getFinish()){//Pick the slower tomography time MIN(self,partner).
                all_temporal_tomography_runningtime_holder[local_qnic.address].Bellpair_per_sec = (double)result->getMax_count()/result->getFinish().dbl();
                all_temporal_tomography_runningtime_holder[local_qnic.address].tomography_measurements = result->getMax_count();
                all_temporal_tomography_runningtime_holder[local_qnic.address].tomography_time = result->getFinish();

                //std::cout<<"Tomo done "<<local_qnic.address<<", in node["<<myAddress<<"] \n";
                StopEmitting *pk = new StopEmitting;
                pk->setQnic_address(local_qnic.address);
                pk->setDestAddr(myAddress);
                pk->setSrcAddr(myAddress);
                send(pk,"RouterPort$o");
            }

        }
    }
    delete msg;
}

void HardwareMonitor::finish(){


    //std::string file_name =  std::string("Tomography_")+std::string(getSimulation()->getNetworkType()->getFullName());

    std::string file_name = tomography_output_filename;
    std::string df = "\"default\"";
    if(file_name.compare(df)==0){
        std::cout<<df<<"=="<<file_name<<"\n";
        file_name =  std::string("Tomography_")+std::string(getSimulation()->getNetworkType()->getFullName());
    }else{
        std::cout<<df<<"!="<<file_name<<"\n";
    }

    std::string file_name_dm = file_name+std::string("_dm");

    std::ofstream tomography_stats(file_name,std::ios_base::app);
    std::ofstream tomography_dm(file_name_dm,std::ios_base::app);
    std::cout<<"Opened new file to write.\n";


    //EV<<"This is just a test!\n";

    //EV<<"numQnic_total = "<<numQnic_total;
    for(int i=0; i<numQnic_total; i++){
        int meas_total = 0;
        int GOD_clean_pair_total = 0;
        int GOD_X_pair_total = 0;
        int GOD_Z_pair_total = 0;
        int GOD_Y_pair_total = 0;

        //std::cout<<"\n \n \n \n \n QNIC["<<i<<"] \n";
        for(auto it =  all_temporal_tomography_output_holder[i].cbegin(); it != all_temporal_tomography_output_holder[i].cend(); ++it){
            //EV <<"Count["<< it->first << "] = " << it->second.my_basis << ", " << it->second.my_output_is_plus << ", " << it->second.partner_basis << ", "  << it->second.partner_output_is_plus << " " << "\n";
            std::string basis_combination = "";
            basis_combination+=it->second.my_basis;
            basis_combination+=it->second.partner_basis;
            if(tomography_data[i].count(basis_combination)!=1){
                //EV<<it->second.my_basis<<", "<<it->second.partner_basis<<" = "<<basis_combination<<"\n";
                error("Basis combination for tomography not found\n");
            }
            tomography_data[i][basis_combination].total_count++;
            meas_total++;

            EV<<it->second.my_GOD_clean<<","<<it->second.partner_GOD_clean<<"\n";
            if((it->second.my_GOD_clean =='F' && it->second.partner_GOD_clean == 'F') || (it->second.my_GOD_clean =='X' && it->second.partner_GOD_clean == 'X') || (it->second.my_GOD_clean =='Z' && it->second.partner_GOD_clean == 'Z') || (it->second.my_GOD_clean =='Y' && it->second.partner_GOD_clean == 'Y')){
                GOD_clean_pair_total++;
            }else if((it->second.my_GOD_clean =='X' && it->second.partner_GOD_clean == 'F') || (it->second.my_GOD_clean =='F' && it->second.partner_GOD_clean == 'X') ){
                GOD_X_pair_total++;
            }else if((it->second.my_GOD_clean =='Z' && it->second.partner_GOD_clean == 'F') || (it->second.my_GOD_clean =='F' && it->second.partner_GOD_clean == 'Z') ){
                GOD_Z_pair_total++;
            }else if((it->second.my_GOD_clean =='Y' && it->second.partner_GOD_clean == 'F') || (it->second.my_GOD_clean =='F' && it->second.partner_GOD_clean == 'Y') ){
                GOD_Y_pair_total++;
            }

            if(it->second.my_output_is_plus && it->second.partner_output_is_plus){
                            tomography_data[i][basis_combination].plus_plus++;
                            //std::cout<<"basis_combination(++)="<<basis_combination <<" is now "<<tomography_data[i][basis_combination].plus_plus<<"\n";
            }
            else if(it->second.my_output_is_plus && !it->second.partner_output_is_plus){
                            tomography_data[i][basis_combination].plus_minus++;
                            //std::cout<<"basis_combination(++)="<<basis_combination <<" is now "<<tomography_data[i][basis_combination].plus_minus<<"\n";
            }
            else if(!it->second.my_output_is_plus && it->second.partner_output_is_plus){
                            tomography_data[i][basis_combination].minus_plus++;
                            //std::cout<<"basis_combination(++)="<<basis_combination <<" is now "<<tomography_data[i][basis_combination].minus_plus<<"\n";
            }
            else if(!it->second.my_output_is_plus && !it->second.partner_output_is_plus){
                            tomography_data[i][basis_combination].minus_minus++;
                            //std::cout<<"basis_combination(++)="<<basis_combination <<" is now "<<tomography_data[i][basis_combination].minus_minus<<"\n";
            }
            else
                 error("This should not happen though..... ?");

        }
        //For each qnic/link, reconstruct the dm.
        Matrix4cd density_matrix_reconstructed = reconstruct_Density_Matrix(i);

        //todo: Will need to clean this up in a separate function
        Vector4cd Bellpair;
        Bellpair << 1/sqrt(2), 0, 0, 1/sqrt(2);
        Matrix4cd density_matrix_ideal = Bellpair*Bellpair.adjoint();
        double fidelity = (density_matrix_reconstructed.real()* density_matrix_ideal.real() ).trace();

        Vector4cd Bellpair_X;
        Bellpair_X << 0,1/sqrt(2), 1/sqrt(2),0;
        Matrix4cd density_matrix_X = Bellpair_X*Bellpair_X.adjoint();
        double Xerr_rate = (density_matrix_reconstructed.real()* density_matrix_X.real() ).trace();
        EV<<"Xerr = "<<Xerr_rate<<"\n";

        Vector4cd Bellpair_Z;
        Bellpair_Z << 1/sqrt(2),0,0,-1/sqrt(2);
        Matrix4cd density_matrix_Z = Bellpair_Z*Bellpair_Z.adjoint();
        double Zerr_rate = (density_matrix_reconstructed.real()* density_matrix_Z.real() ).trace();
        Complex checkZ = Bellpair_Z.adjoint()*density_matrix_reconstructed*Bellpair_Z;
        EV<<"Zerr = "<<Zerr_rate<<" or, "<<checkZ.real()<<"+"<<checkZ.imag()<<"\n";

        Vector4cd Bellpair_Y;
        Bellpair_Y << 0,Complex(0,1/sqrt(2)),Complex(0,-1/sqrt(2)),0;
        Matrix4cd density_matrix_Y = Bellpair_Y*Bellpair_Y.adjoint();
        double Yerr_rate = (density_matrix_reconstructed.real()* density_matrix_Y.real() ).trace();
        EV<<"Yerr = "<<Yerr_rate<<"\n";

        connection_setup_inf inf = return_setupInf(i);
        double bellpairs_per_sec = 10;
        double link_cost =(double)100000000/(fidelity*fidelity*all_temporal_tomography_runningtime_holder[i].Bellpair_per_sec);
        if(link_cost<1){
            link_cost = 1;
        }

        Interface_inf interface = getInterface_inf_fromQnicAddress(inf.qnic.index,inf.qnic.type);
        cModule *this_node = this->getParentModule()->getParentModule();
        cModule *neighbor_node = interface.qnic.pointer->gate("qnic_quantum_port$o")->getNextGate()->getNextGate()->getOwnerModule();
        cChannel *channel = interface.qnic.pointer->gate("qnic_quantum_port$o")->getNextGate()->getChannel();
        double dis = channel->par("distance");

        /*if(this_node->getModuleType() == QNodeType && neighbor_node->getModuleType() == QNodeType){
            if(myAddress > inf.neighbor_address){
                return;
            }
        }*/

        tomography_dm<<this_node->getFullName()<<"<--->"<<neighbor_node->getFullName()<<"\n";
        tomography_dm<<"REAL\n";
        tomography_dm<<density_matrix_reconstructed.real()<<"\n";
        tomography_dm<<"IMAGINARY\n";
        tomography_dm<<density_matrix_reconstructed.imag()<<"\n";

        std::cout<<this_node->getFullName()<<"<-->QuantumChannel{cost="<<link_cost<<";distance="<<dis<<"km;fidelity="<<fidelity<<";bellpair_per_sec="<<bellpairs_per_sec<<";}<-->"<<neighbor_node->getFullName()<< "; F="<<fidelity<<"; X="<<Xerr_rate<<"; Z="<<Zerr_rate<<"; Y="<<Yerr_rate<<endl;
        tomography_stats<<this_node->getFullName()<<"<-->QuantumChannel{cost="<<link_cost<<";distance="<<dis<<"km;fidelity="<<fidelity<<";bellpair_per_sec="<<all_temporal_tomography_runningtime_holder[i].Bellpair_per_sec<<";tomography_time="<<all_temporal_tomography_runningtime_holder[i].tomography_time<<";tomography_measurements="<<all_temporal_tomography_runningtime_holder[i].tomography_measurements<<";actualmeas="<<meas_total<<"; GOD_clean_pair_total="<<GOD_clean_pair_total<<"; GOD_X_pair_total="<<GOD_X_pair_total<<"; GOD_Y_pair_total="<<GOD_Y_pair_total<<"; GOD_Z_pair_total="<<GOD_Z_pair_total<<";}<-->"<<neighbor_node->getFullName()<< "; F="<<fidelity<<"; X="<<Xerr_rate<<"; Z="<<Zerr_rate<<"; Y="<<Yerr_rate<<endl;

    }

    tomography_stats.close();
    tomography_dm.close();
    std::cout<<"Closed file to write.\n";
}



Matrix4cd HardwareMonitor::reconstruct_Density_Matrix(int qnic_id){
    //II
       double S00 = 1.0;
       double S01 = (double)tomography_data[qnic_id]["XX"].plus_plus/(double)tomography_data[qnic_id]["XX"].total_count - (double)tomography_data[qnic_id]["XX"].plus_minus/(double)tomography_data[qnic_id]["XX"].total_count + (double)tomography_data[qnic_id]["XX"].minus_plus/(double)tomography_data[qnic_id]["XX"].total_count - (double)tomography_data[qnic_id]["XX"].minus_minus/(double)tomography_data[qnic_id]["XX"].total_count;
       double S02 = (double)tomography_data[qnic_id]["YY"].plus_plus/(double)tomography_data[qnic_id]["YY"].total_count - (double)tomography_data[qnic_id]["YY"].plus_minus/(double)tomography_data[qnic_id]["YY"].total_count + (double)tomography_data[qnic_id]["YY"].minus_plus/(double)tomography_data[qnic_id]["YY"].total_count - (double)tomography_data[qnic_id]["YY"].minus_minus/(double)tomography_data[qnic_id]["YY"].total_count;
       double S03 = (double)tomography_data[qnic_id]["ZZ"].plus_plus/(double)tomography_data[qnic_id]["ZZ"].total_count - (double)tomography_data[qnic_id]["ZZ"].plus_minus/(double)tomography_data[qnic_id]["ZZ"].total_count + (double)tomography_data[qnic_id]["ZZ"].minus_plus/(double)tomography_data[qnic_id]["ZZ"].total_count - (double)tomography_data[qnic_id]["ZZ"].minus_minus/(double)tomography_data[qnic_id]["ZZ"].total_count;
       //XX
       double S10 = (double)tomography_data[qnic_id]["XX"].plus_plus/(double)tomography_data[qnic_id]["XX"].total_count + (double)tomography_data[qnic_id]["XX"].plus_minus/(double)tomography_data[qnic_id]["XX"].total_count - (double)tomography_data[qnic_id]["XX"].minus_plus/(double)tomography_data[qnic_id]["XX"].total_count - (double)tomography_data[qnic_id]["XX"].minus_minus/(double)tomography_data[qnic_id]["XX"].total_count;
       double S11 = (double)tomography_data[qnic_id]["XX"].plus_plus/(double)tomography_data[qnic_id]["XX"].total_count - (double)tomography_data[qnic_id]["XX"].plus_minus/(double)tomography_data[qnic_id]["XX"].total_count - (double)tomography_data[qnic_id]["XX"].minus_plus/(double)tomography_data[qnic_id]["XX"].total_count + (double)tomography_data[qnic_id]["XX"].minus_minus/(double)tomography_data[qnic_id]["XX"].total_count;
       double S12 = (double)tomography_data[qnic_id]["XY"].plus_plus/(double)tomography_data[qnic_id]["XY"].total_count - (double)tomography_data[qnic_id]["XY"].plus_minus/(double)tomography_data[qnic_id]["XY"].total_count - (double)tomography_data[qnic_id]["XY"].minus_plus/(double)tomography_data[qnic_id]["XY"].total_count + (double)tomography_data[qnic_id]["XY"].minus_minus/(double)tomography_data[qnic_id]["XY"].total_count;
       double S13 = (double)tomography_data[qnic_id]["XZ"].plus_plus/(double)tomography_data[qnic_id]["XZ"].total_count - (double)tomography_data[qnic_id]["XZ"].plus_minus/(double)tomography_data[qnic_id]["XZ"].total_count - (double)tomography_data[qnic_id]["XZ"].minus_plus/(double)tomography_data[qnic_id]["XZ"].total_count + (double)tomography_data[qnic_id]["XZ"].minus_minus/(double)tomography_data[qnic_id]["XZ"].total_count;
       //YY
       double S20 = (double)tomography_data[qnic_id]["YY"].plus_plus/(double)tomography_data[qnic_id]["YY"].total_count + (double)tomography_data[qnic_id]["YY"].plus_minus/(double)tomography_data[qnic_id]["YY"].total_count - (double)tomography_data[qnic_id]["YY"].minus_plus/(double)tomography_data[qnic_id]["YY"].total_count - (double)tomography_data[qnic_id]["YY"].minus_minus/(double)tomography_data[qnic_id]["YY"].total_count;
       double S21 = (double)tomography_data[qnic_id]["YX"].plus_plus/(double)tomography_data[qnic_id]["YX"].total_count - (double)tomography_data[qnic_id]["YX"].plus_minus/(double)tomography_data[qnic_id]["YX"].total_count - (double)tomography_data[qnic_id]["YX"].minus_plus/(double)tomography_data[qnic_id]["YX"].total_count + (double)tomography_data[qnic_id]["YX"].minus_minus/(double)tomography_data[qnic_id]["YX"].total_count;
       double S22 = (double)tomography_data[qnic_id]["YY"].plus_plus/(double)tomography_data[qnic_id]["YY"].total_count - (double)tomography_data[qnic_id]["YY"].plus_minus/(double)tomography_data[qnic_id]["YY"].total_count - (double)tomography_data[qnic_id]["YY"].minus_plus/(double)tomography_data[qnic_id]["YY"].total_count + (double)tomography_data[qnic_id]["YY"].minus_minus/(double)tomography_data[qnic_id]["YY"].total_count;
       double S23 = (double)tomography_data[qnic_id]["YZ"].plus_plus/(double)tomography_data[qnic_id]["YZ"].total_count - (double)tomography_data[qnic_id]["YZ"].plus_minus/(double)tomography_data[qnic_id]["YZ"].total_count - (double)tomography_data[qnic_id]["YZ"].minus_plus/(double)tomography_data[qnic_id]["YZ"].total_count + (double)tomography_data[qnic_id]["YZ"].minus_minus/(double)tomography_data[qnic_id]["YZ"].total_count;
       //ZZ
       double S30 = (double)tomography_data[qnic_id]["ZZ"].plus_plus/(double)tomography_data[qnic_id]["ZZ"].total_count + (double)tomography_data[qnic_id]["ZZ"].plus_minus/(double)tomography_data[qnic_id]["ZZ"].total_count - (double)tomography_data[qnic_id]["ZZ"].minus_plus/(double)tomography_data[qnic_id]["ZZ"].total_count - (double)tomography_data[qnic_id]["ZZ"].minus_minus/(double)tomography_data[qnic_id]["ZZ"].total_count;
       double S31 = (double)tomography_data[qnic_id]["ZX"].plus_plus/(double)tomography_data[qnic_id]["ZX"].total_count - (double)tomography_data[qnic_id]["ZX"].plus_minus/(double)tomography_data[qnic_id]["ZX"].total_count - (double)tomography_data[qnic_id]["ZX"].minus_plus/(double)tomography_data[qnic_id]["ZX"].total_count + (double)tomography_data[qnic_id]["ZX"].minus_minus/(double)tomography_data[qnic_id]["ZX"].total_count;
       double S32 = (double)tomography_data[qnic_id]["ZY"].plus_plus/(double)tomography_data[qnic_id]["ZY"].total_count - (double)tomography_data[qnic_id]["ZY"].plus_minus/(double)tomography_data[qnic_id]["ZY"].total_count - (double)tomography_data[qnic_id]["ZY"].minus_plus/(double)tomography_data[qnic_id]["ZY"].total_count + (double)tomography_data[qnic_id]["ZY"].minus_minus/(double)tomography_data[qnic_id]["ZY"].total_count;
       double S33 = (double)tomography_data[qnic_id]["ZZ"].plus_plus/(double)tomography_data[qnic_id]["ZZ"].total_count - (double)tomography_data[qnic_id]["ZZ"].plus_minus/(double)tomography_data[qnic_id]["ZZ"].total_count - (double)tomography_data[qnic_id]["ZZ"].minus_plus/(double)tomography_data[qnic_id]["ZZ"].total_count + (double)tomography_data[qnic_id]["ZZ"].minus_minus/(double)tomography_data[qnic_id]["ZZ"].total_count;
       double S = (double)tomography_data[qnic_id]["XX"].plus_plus/(double)tomography_data[qnic_id]["XX"].total_count + (double)tomography_data[qnic_id]["XX"].plus_minus/(double)tomography_data[qnic_id]["XX"].total_count + (double)tomography_data[qnic_id]["XX"].minus_plus/(double)tomography_data[qnic_id]["XX"].total_count + (double)tomography_data[qnic_id]["XX"].minus_minus/(double)tomography_data[qnic_id]["XX"].total_count;


       EV<<S00<<", "<<S01<<", "<<S02<<", "<<S03<<"\n";
       EV<<S10<<", "<<S11<<", "<<S12<<", "<<S13<<"\n";
       EV<<S20<<", "<<S21<<", "<<S22<<", "<<S23<<"\n";
       EV<<S30<<", "<<S31<<", "<<S32<<", "<<S33<<"\n";

    Matrix4cd density_matrix_reconstructed =
            (double)1/(double)4*(
            S01*kroneckerProduct(Pauli.I,Pauli.X).eval() +
            S02*kroneckerProduct(Pauli.I,Pauli.Y).eval() +
            S03*kroneckerProduct(Pauli.I,Pauli.Z).eval() +
            S10*kroneckerProduct(Pauli.X,Pauli.I).eval() +
            S11*kroneckerProduct(Pauli.X,Pauli.X).eval() +
            S12*kroneckerProduct(Pauli.X,Pauli.Y).eval() +
            S13*kroneckerProduct(Pauli.X,Pauli.Z).eval() +
            S20*kroneckerProduct(Pauli.Y,Pauli.I).eval() +
            S21*kroneckerProduct(Pauli.Y,Pauli.X).eval() +
            S22*kroneckerProduct(Pauli.Y,Pauli.Y).eval() +
            S23*kroneckerProduct(Pauli.Y,Pauli.Z).eval() +
            S30*kroneckerProduct(Pauli.Z,Pauli.I).eval() +
            S31*kroneckerProduct(Pauli.Z,Pauli.X).eval() +
            S32*kroneckerProduct(Pauli.Z,Pauli.Y).eval() +
            S33*kroneckerProduct(Pauli.Z,Pauli.Z).eval() +
            S*kroneckerProduct(Pauli.I,Pauli.I).eval());

    EV<<"DM = "<<density_matrix_reconstructed<<"\n";
    return density_matrix_reconstructed;
    /*
    Vector4cd Bellpair;
    Bellpair << 1/sqrt(2), 0, 0, 1/sqrt(2);
    Matrix4cd density_matrix_ideal = Bellpair*Bellpair.adjoint();
    double fidelity = (density_matrix_reconstructed.real()* density_matrix_ideal.real() ).trace();
    //double Xerr = (density_matrix_reconstructed.real()* (density_matrix_ideal.real()) ).trace();

    EV<<"FOR QNIC["<<qnic_id<<"] \n";
    EV<<"F = "<<fidelity<<"\n";

    Vector4cd Bellpair_X;
    Bellpair_X << 0,1/sqrt(2), 1/sqrt(2),0;
    Matrix4cd density_matrix_X = Bellpair_X*Bellpair_X.adjoint();
    double Xerr_rate = (density_matrix_reconstructed.real()* density_matrix_X.real() ).trace();
    EV<<"Xerr = "<<Xerr_rate<<"\n";

    Vector4cd Bellpair_Z;
    Bellpair_Z << 1/sqrt(2),0,0,-1/sqrt(2);
    Matrix4cd density_matrix_Z = Bellpair_Z*Bellpair_Z.adjoint();
    double Zerr_rate = (density_matrix_reconstructed.real()* density_matrix_Z.real() ).trace();
    Complex checkZ = Bellpair_Z.adjoint()*density_matrix_reconstructed*Bellpair_Z;
    EV<<"Zerr = "<<Zerr_rate<<" or, "<<checkZ.real()<<"+"<<checkZ.imag()<<"\n";

    Vector4cd Bellpair_Y;
    Bellpair_Y << 0,Complex(0,1/sqrt(2)),Complex(0,-1/sqrt(2)),0;
    Matrix4cd density_matrix_Y = Bellpair_Y*Bellpair_Y.adjoint();
    double Yerr_rate = (density_matrix_reconstructed.real()* density_matrix_Y.real() ).trace();
    EV<<"Yerr = "<<Yerr_rate<<"\n";

    tomography_stats << "F = "<<fidelity<<" X = "<<Xerr_rate<<" Z ="<<Zerr_rate<<" Y = "<<Yerr_rate<<endl;

    double bellpairs_per_sec = 10;
    double link_cost =(double)1/(fidelity*fidelity*bellpairs_per_sec);
    writeToFile_Topology_with_LinkCost(qnic_id, link_cost, fidelity, bellpairs_per_sec);

    Vector4cd Bellpair_Y2;
    Bellpair_Y2 << 0,Complex(0,-1/sqrt(2)),Complex(0,1/sqrt(2)),0;
    Matrix4cd density_matrix_Y2 = Bellpair_Y2*Bellpair_Y2.adjoint();
    double Yerr_rate2 = (density_matrix_reconstructed.real()* density_matrix_Y2.real() ).trace();
    EV<<"Yerr = "<<Yerr_rate2<<"\n";*/


}

void HardwareMonitor::writeToFile_Topology_with_LinkCost(int qnic_id, double link_cost, double fidelity, double bellpair_per_sec){

    connection_setup_inf inf = return_setupInf(qnic_id);
    Interface_inf interface = getInterface_inf_fromQnicAddress(inf.qnic.index,inf.qnic.type);
    //if(myAddress > inf.neighbor_address)
    cModule *this_node = this->getParentModule()->getParentModule();
    cModule *neighbor_node = interface.qnic.pointer->gate("qnic_quantum_port$o")->getNextGate()->getNextGate()->getOwnerModule();
    cChannel *channel = interface.qnic.pointer->gate("qnic_quantum_port$o")->getNextGate()->getChannel();
    double dis = channel->par("distance");
    if(neighbor_node->getModuleType()!=QNodeType && neighbor_node->getModuleType()!=HoMType && neighbor_node->getModuleType()!=SPDCType)
        error("Module Type not recognized when writing to file...");

    if(neighbor_node->getModuleType()==QNodeType){
        if(myAddress > inf.neighbor_address){
            std::cout<<"\n"<<this_node->getFullName()<<"<--> QuantumChannel{ cost = "<<link_cost<<"; distance = "<<dis<<"km; fidelity = "<<fidelity<<"; bellpair_per_sec = "<<bellpair_per_sec<< ";} <-->"<<neighbor_node->getFullName()<<"\n";
        }
    }else{
        std::cout<<"\n"<<this_node->getFullName()<<"<--> QuantumChannel{ cost = "<<link_cost<<"; distance = "<<dis<<"km; fidelity = "<<fidelity<<"; bellpair_per_sec = "<<bellpair_per_sec<< ";} <-->"<<neighbor_node->getFullName()<<"\n";
    }
}


//Excludes Hom, Epps and other intermediate nodes.
QNIC HardwareMonitor::search_QNIC_from_Neighbor_QNode_address(int neighbor_address){
    QNIC qnic;
    for(auto it = ntable.cbegin(); it != ntable.cend(); ++it){

        if(it->second.neighborQNode_address == neighbor_address){
            qnic = it->second.qnic;
            break;
        }if(it == ntable.end()){
            error("Something is wrong when looking for QNIC info from neighbor QNode address. Tomography is also only available between neighbor.");
        }
    }
    return qnic;
}



void HardwareMonitor::sendLinkTomographyRuleSet(int my_address, int partner_address, QNIC_type qnic_type, int qnic_index, unsigned long RuleSet_id){
            LinkTomographyRuleSet *pk = new LinkTomographyRuleSet;
            pk->setDestAddr(my_address);
            pk->setSrcAddr(partner_address);
            pk->setNumber_of_measuring_resources(num_measure);
            pk->setKind(6);


            //Empty RuleSet
            RuleSet* tomography_RuleSet = new RuleSet(RuleSet_id, my_address,partner_address);//Tomography between this node and the sender of Ack.
            std::cout<<"Creating rules now RS_id = "<< RuleSet_id<<", partner_address = "<<partner_address<<"\n";

            int rule_index = 0;

            if(num_purification>0){/*RuleSet including purification. CUrrently, not looping.*/

                if(Purification_type == 2002){//Performs both X and Z purification for each n.
					for(int i=0; i<num_purification; i++){
					    //First stage X purification
                        Rule* Purification = new Rule(RuleSet_id, rule_index);
                        Condition* Purification_condition = new Condition();
                        Clause* resource_clause = new EnoughResourceClause(partner_address, 2);
                        Purification_condition->addClause(resource_clause);
                        Purification->setCondition(Purification_condition);
                        Action* purify_action = new PurifyAction(RuleSet_id,rule_index,true,false, num_purification, partner_address, qnic_type , qnic_index,0,1);
                        Purification->setAction(purify_action);
                        rule_index++;
                        tomography_RuleSet->addRule(Purification);

                        //Second stage Z purification (Using X purified resources)
                        Purification = new Rule(RuleSet_id, rule_index);
                        Purification_condition = new Condition();
                        resource_clause = new EnoughResourceClause(partner_address, 2);
                        Purification_condition->addClause(resource_clause);
                        Purification->setCondition(Purification_condition);
                        purify_action = new PurifyAction(RuleSet_id,rule_index,false,true, num_purification, partner_address, qnic_type , qnic_index,0,1);
                        Purification->setAction(purify_action);
                        rule_index++;
                        tomography_RuleSet->addRule(Purification);
					}
                }else if(Purification_type == 3003){
                    //First stage X purification
					for(int i=0; i<num_purification; i++){
                        Rule* Purification = new Rule(RuleSet_id, rule_index);
                        Condition* Purification_condition = new Condition();
                        Clause* resource_clause = new EnoughResourceClause(partner_address, 2);
                        Purification_condition->addClause(resource_clause);
                        Purification->setCondition(Purification_condition);

                        if(i%2==0){//X purification
                        	Action* purify_action = new PurifyAction(RuleSet_id,rule_index,true,false, num_purification, partner_address, qnic_type , qnic_index,0,1);
                        	Purification->setAction(purify_action);
                        }else{//Z purification
                        	Action* purify_action = new PurifyAction(RuleSet_id,rule_index,false,true, num_purification, partner_address, qnic_type , qnic_index,0,1);
                        	Purification->setAction(purify_action);
						}
						rule_index++;
                        tomography_RuleSet->addRule(Purification);

					}
                }else if(Purification_type == 1001){//Same as last one. X, Z double purification (purification pumping)
					for(int i=0; i<num_purification; i++){
                        Rule* Purification = new Rule(RuleSet_id, rule_index);
                        Condition* Purification_condition = new Condition();
                        Clause* resource_clause = new EnoughResourceClause(partner_address, 3);
                        Purification_condition->addClause(resource_clause);
                        Purification->setCondition(Purification_condition);
                        Action* purify_action = new DoublePurifyAction(RuleSet_id,rule_index,partner_address, qnic_type,qnic_index,0,1,2);
                        Purification->setAction(purify_action);
                        rule_index++;
                        tomography_RuleSet->addRule(Purification);
					}
                }else if(Purification_type == 1221){//Same as last one. X, Z double purification
                    for(int i=0; i<num_purification; i++){
                        if(i%2==0){
                            Rule* Purification = new Rule(RuleSet_id, rule_index);
                            Condition* Purification_condition = new Condition();
                            Clause* resource_clause = new EnoughResourceClause(partner_address, 3);
                            Purification_condition->addClause(resource_clause);
                            Purification->setCondition(Purification_condition);
                            Action* purify_action = new DoublePurifyAction(RuleSet_id,rule_index,partner_address, qnic_type,qnic_index,0,1,2);
                            Purification->setAction(purify_action);
                            rule_index++;
                            tomography_RuleSet->addRule(Purification);
                        }else{
                            Rule* Purification = new Rule(RuleSet_id, rule_index);
                            Condition* Purification_condition = new Condition();
                            Clause* resource_clause = new EnoughResourceClause(partner_address, 3);
                            Purification_condition->addClause(resource_clause);
                            Purification->setCondition(Purification_condition);
                            Action* purify_action = new DoublePurifyAction_inv(RuleSet_id,rule_index,partner_address, qnic_type,qnic_index,0,1,2);
                            Purification->setAction(purify_action);
                            rule_index++;
                            tomography_RuleSet->addRule(Purification);
                        }
                    }
                }else if(Purification_type == 1011){//Fuji-san's Doouble selection purification
                    for(int i=0; i<num_purification; i++){
                        Rule* Purification = new Rule(RuleSet_id, rule_index);
                        Condition* Purification_condition = new Condition();
                        Clause* resource_clause = new EnoughResourceClause(partner_address, 3);
                        Purification_condition->addClause(resource_clause);
                        Purification->setCondition(Purification_condition);
                        Action* purify_action = new DoubleSelectionAction(RuleSet_id,rule_index,partner_address, qnic_type,qnic_index,0,1,2);
                        Purification->setAction(purify_action);
                        rule_index++;
                        tomography_RuleSet->addRule(Purification);
                    }
                }else if(Purification_type == 1021){//Fuji-san's Double selection purification
                    for(int i=0; i<num_purification; i++){
                        Rule* Purification = new Rule(RuleSet_id, rule_index);
                        Condition* Purification_condition = new Condition();
                        Clause* resource_clause = new EnoughResourceClause(partner_address, 3);
                        Purification_condition->addClause(resource_clause);
                        Purification->setCondition(Purification_condition);
                        if(i%2==0){
                        	Action* purify_action = new DoubleSelectionAction(RuleSet_id,rule_index,partner_address, qnic_type,qnic_index,0,1,2);
                        	Purification->setAction(purify_action);
						}else{
                        	Action* purify_action = new DoubleSelectionAction_inv(RuleSet_id,rule_index,partner_address, qnic_type,qnic_index,0,1,2);
                        	Purification->setAction(purify_action);
						}
                        rule_index++;
                        tomography_RuleSet->addRule(Purification);
                    }
                }else if(Purification_type == 1031){//Fuji-san's Double selection purification
                    for(int i=0; i<num_purification; i++){
                        Rule* Purification = new Rule(RuleSet_id, rule_index);
                        Condition* Purification_condition = new Condition();
                        Clause* resource_clause = new EnoughResourceClause(partner_address, 5);
                        Purification_condition->addClause(resource_clause);
                        Purification->setCondition(Purification_condition);
                        if(i%2==0){
                        	Action* purify_action = new DoubleSelectionDualAction(RuleSet_id,rule_index,partner_address, qnic_type,qnic_index,0,1,2,3,4);
                        	Purification->setAction(purify_action);
						}else{
                        	Action* purify_action = new DoubleSelectionDualAction_inv(RuleSet_id,rule_index,partner_address, qnic_type,qnic_index,0,1,2,3,4);
                        	Purification->setAction(purify_action);
						}
                        rule_index++;
                        tomography_RuleSet->addRule(Purification);
                    }
                }else if(Purification_type == 1061){//Fuji-san's Doouble selection purification
                    for(int i=0; i<num_purification; i++){
                        Rule* Purification = new Rule(RuleSet_id, rule_index);
                        Condition* Purification_condition = new Condition();
                        Clause* resource_clause = new EnoughResourceClause(partner_address, 4);
                        Purification_condition->addClause(resource_clause);
                        Purification->setCondition(Purification_condition);
                        if(i%2==0){
                            Action* purify_action = new DoubleSelectionDualActionSecond(RuleSet_id,rule_index,partner_address, qnic_type,qnic_index,0,1,2,3);
                            Purification->setAction(purify_action);
                        }else{
                            Action* purify_action = new DoubleSelectionDualActionSecond_inv(RuleSet_id,rule_index,partner_address, qnic_type,qnic_index,0,1,2,3);
                            Purification->setAction(purify_action);
                        }
                        rule_index++;
                        tomography_RuleSet->addRule(Purification);
                    }
                }
                else if(Purification_type == 5555){//Predefined purification method
                    for(int i=0; i<2; i++){
                        Rule* Purification = new Rule(RuleSet_id, rule_index);
                        Condition* Purification_condition = new Condition();
                        Clause* resource_clause = new EnoughResourceClause(partner_address, 3);
                        Purification_condition->addClause(resource_clause);
                        Purification->setCondition(Purification_condition);
                        if(i%2==0){
                            Action* purify_action = new DoubleSelectionAction(RuleSet_id,rule_index,partner_address, qnic_type,qnic_index,0,1,2);
                            Purification->setAction(purify_action);
                        }else{
                            Action* purify_action = new DoubleSelectionAction_inv(RuleSet_id,rule_index,partner_address, qnic_type,qnic_index,0,1,2);
                             Purification->setAction(purify_action);
                        }
                        rule_index++;
                        tomography_RuleSet->addRule(Purification);
                    }

                    for(int i=0; i<num_purification; i++){
                            Rule* Purification = new Rule(RuleSet_id, rule_index);
                            Condition* Purification_condition = new Condition();
                            Clause* resource_clause = new EnoughResourceClause(partner_address, 2);
                            Purification_condition->addClause(resource_clause);
                            Purification->setCondition(Purification_condition);

                            if(i%2==0){//X purification
                                Action* purify_action = new PurifyAction(RuleSet_id,rule_index,true,false, num_purification, partner_address, qnic_type , qnic_index,0,1);
                                Purification->setAction(purify_action);
                            }else{//Z purification
                                Action* purify_action = new PurifyAction(RuleSet_id,rule_index,false,true, num_purification, partner_address, qnic_type , qnic_index,0,1);
                                Purification->setAction(purify_action);
                            }
                            rule_index++;
                            tomography_RuleSet->addRule(Purification);
                   }
				}else if(Purification_type == 5556){//Predefined purification method
                        Rule* Purification = new Rule(RuleSet_id, rule_index);
                        Condition* Purification_condition = new Condition();
                        Clause* resource_clause = new EnoughResourceClause(partner_address, 3);
                        Purification_condition->addClause(resource_clause);
                        Purification->setCondition(Purification_condition);
                        Action* purify_action = new DoubleSelectionAction(RuleSet_id,rule_index,partner_address, qnic_type,qnic_index,0,1,2);
                        Purification->setAction(purify_action);
                        rule_index++;
                        tomography_RuleSet->addRule(Purification);

                    for(int i=0; i<num_purification; i++){
                            Rule* Purification = new Rule(RuleSet_id, rule_index);
                            Condition* Purification_condition = new Condition();
                            Clause* resource_clause = new EnoughResourceClause(partner_address, 2);
                            Purification_condition->addClause(resource_clause);
                            Purification->setCondition(Purification_condition);

                            if(i%2==0){//X purification
                                Action* purify_action = new PurifyAction(RuleSet_id,rule_index,false,true, num_purification, partner_address, qnic_type , qnic_index,0,1);
                                Purification->setAction(purify_action);
                            }else{//Z purification
                                Action* purify_action = new PurifyAction(RuleSet_id,rule_index,true,false, num_purification, partner_address, qnic_type , qnic_index,0,1);
                                Purification->setAction(purify_action);
                            }
                            rule_index++;
                            tomography_RuleSet->addRule(Purification);
                   }

                }else if((X_Purification && !Z_Purification)  || (!X_Purification && Z_Purification)){//X or Z purification. Out-dated syntax.
                    Rule* Purification = new Rule(RuleSet_id, rule_index);
                    Condition* Purification_condition = new Condition();
                    Clause* resource_clause = new EnoughResourceClause(partner_address, 2);
                    Purification_condition->addClause(resource_clause);
                    Purification->setCondition(Purification_condition);
                    Action* purify_action = new PurifyAction(RuleSet_id,rule_index,X_Purification,Z_Purification, num_purification, partner_address, qnic_type , qnic_index,0,1);
                    Purification->setAction(purify_action);
                    rule_index++;
                    tomography_RuleSet->addRule(Purification);
                }else{//X, Z double purification
					error("syntax outdate or purification id not recognized.");
                    Rule* Purification = new Rule(RuleSet_id, rule_index);
                    Condition* Purification_condition = new Condition();
                    Clause* resource_clause = new EnoughResourceClause(partner_address, 3);
                    Purification_condition->addClause(resource_clause);
                    Purification->setCondition(Purification_condition);
                    Action* purify_action = new DoublePurifyAction(RuleSet_id,rule_index,partner_address, qnic_type,qnic_index,0,1,2);
                    Purification->setAction(purify_action);
                    rule_index++;
                    tomography_RuleSet->addRule(Purification);

                }



                Rule* Random_measure_tomo = new Rule(RuleSet_id, rule_index);//Let's make nodes select measurement basis randomly, because it it easier.
                Condition* total_measurements = new Condition();//Technically, there is no condition because an available resource is guaranteed whenever the rule is ran.
                Clause* measure_count_clause = new MeasureCountClause(num_measure, partner_address, qnic_type , qnic_index, 0);//3000 measurements in total. There are 3*3 = 9 patterns of measurements. So each combination must perform 3000/9 measurements.
                total_measurements->addClause(measure_count_clause);
                Random_measure_tomo->setCondition(total_measurements);
                quisp::rules::Action* measure = new RandomMeasureAction(partner_address, qnic_type , qnic_index, 0, my_address,num_measure);//Measure the local resource between it->second.neighborQNode_address.
                Random_measure_tomo->setAction(measure);
                //---------
                //Add the rule to the RuleSet
                tomography_RuleSet->addRule(Random_measure_tomo);
                //---------------------------
                pk->setRuleSet(tomography_RuleSet);
                send(pk,"RouterPort$o");

            }else{//RuleSet with no purification. Pure measurement only link level tomography.
                //-------------
                //-First rule-
                Rule* Random_measure_tomo = new Rule(RuleSet_id, 0);//Let's make nodes select measurement basis randomly, because it it easier.
                Condition* total_measurements = new Condition();//Technically, there is no condition because an available resource is guaranteed whenever the rule is ran.
                Clause* measure_count_clause = new MeasureCountClause(num_measure, partner_address, qnic_type , qnic_index, 0);//3000 measurements in total. There are 3*3 = 9 patterns of measurements. So each combination must perform 3000/9 measurements.
                Clause* resource_clause = new EnoughResourceClause(partner_address, 1);
                total_measurements->addClause(measure_count_clause);
                total_measurements->addClause(resource_clause);
                Random_measure_tomo->setCondition(total_measurements);
                quisp::rules::Action* measure = new RandomMeasureAction(partner_address, qnic_type , qnic_index, 0, my_address,num_measure);//Measure the local resource between it->second.neighborQNode_address.
                Random_measure_tomo->setAction(measure);
                //---------
                //Add the rule to the RuleSet
                tomography_RuleSet->addRule(Random_measure_tomo);
                tomography_RuleSet->finalize();
                //---------------------------
                pk->setRuleSet(tomography_RuleSet);
                send(pk,"RouterPort$o");
            }
}



int HardwareMonitor::checkNumBuff(int qnic_index, QNIC_type qnic_type){
    Enter_Method("checkNumBuff()");

    cModule *qnode = nullptr;
    if (qnic_type>=QNIC_N) error("Only 3 qnic types are currently recognized...."); // avoid segfaults <3
    qnode = getQNode()->getSubmodule(QNIC_names[qnic_type], qnic_index);
    return qnode->par("numBuffer");
}


Interface_inf HardwareMonitor::getInterface_inf_fromQnicAddress(int qnic_index, QNIC_type qnic_type){
    cModule *local_qnic;
    if (qnic_type>=QNIC_N) error("Only 3 qnic types are currently recognized...."); // avoid segfaults <3
    local_qnic = getQNode()->getSubmodule(QNIC_names[qnic_type], qnic_index);//QNIC itself
    Interface_inf inf;
    inf.qnic.pointer = local_qnic;
    inf.qnic.address = local_qnic->par("self_qnic_address");//Extract from QNIC parameter
    inf.qnic.index = qnic_index;
    inf.qnic.type = qnic_type;
    inf.buffer_size = local_qnic->par("numBuffer");

    //Just read link cost from channel parameter for now as a dummy (or as an initialization).
    //int cost = local_qnic->gate("qnic_quantum_port$o")->getNextGate()->getChannel()->par("cost");//This is false because the channel may only be between the node and HOM.
    inf.link_cost = 1;//Dummy it up. This cost must be the cost based on the neighboring QNode (excluding SPDC and HOM nodes)

    return inf;
}

connection_setup_inf HardwareMonitor::return_setupInf(int qnic_address){
    Enter_Method("return_setupInf()");
    connection_setup_inf inf = {
        .qnic = {
            .type = QNIC_N,
            .index = -1,
        },
        .neighbor_address = -1,
        .quantum_link_cost = -1
    };
    for(auto it = ntable.cbegin(); it != ntable.cend(); ++it){
        if(it->second.qnic.address == qnic_address){
            inf.qnic.type = it->second.qnic.type;
            inf.qnic.index = it->second.qnic.index;
            inf.qnic.address = it->second.qnic.address;
            inf.neighbor_address = it->second.neighborQNode_address;
            //cModule *node = getModuleByPath("network.HoM");
            inf.quantum_link_cost = it->second.link_cost;
            break;
        }
    }
    return inf;
}





//This neighbor table includes all neighbors of qnic, qnic_r and qnic_rp
HardwareMonitor::NeighborTable HardwareMonitor::prepareNeighborTable(NeighborTable ntable, int total_numQnic){
    cModule *qnode = getQNode();//Get the parent QNode that runs this connection manager.
        for (int index=0; index<numQnic; index++){//Travese through all local qnics to check where they are connected to. HoM and EPPS will be ignored in this case.
            Interface_inf inf = getInterface_inf_fromQnicAddress(index, QNIC_E);
            neighborInfo n_inf = findNeighborAddress(inf.qnic.pointer);
            int neighborNodeAddress = n_inf.address;//get the address of the Node nearby.
            inf.neighborQNode_address = n_inf.neighborQNode_address;
            ntable[neighborNodeAddress] = inf;
        }
        for (int index=0; index<numQnic_r; index++){
            Interface_inf inf = getInterface_inf_fromQnicAddress(index, QNIC_R);
            neighborInfo n_inf = findNeighborAddress(inf.qnic.pointer);
            int neighborNodeAddress = n_inf.address;//get the address of the Node nearby.
            inf.neighborQNode_address = n_inf.neighborQNode_address;
            ntable[neighborNodeAddress] = inf;
        }
        for (int index=0; index<numQnic_rp; index++){
            Interface_inf inf = getInterface_inf_fromQnicAddress(index, QNIC_RP);
            neighborInfo n_inf = findNeighborAddress(inf.qnic.pointer);
            int neighborNodeAddress = n_inf.address;//get the address of the Node nearby.
            inf.neighborQNode_address = n_inf.neighborQNode_address;
            ntable[neighborNodeAddress] = inf;
        }
        return ntable;
}


//This method finds out the address of the neighboring node with respect to the local unique qnic addres.
neighborInfo  HardwareMonitor::findNeighborAddress(cModule *qnic_pointer){
    cGate *gt = qnic_pointer->gate("qnic_quantum_port$o")->getNextGate();//qnic_quantum_port$o is connected to the node's outermost quantum_port
    //EV<<"gt = "<<gt->getName()<<"\n";
    cGate *neighbor_gt = gt->getNextGate();
    //EV<<"neighbor_gt = "<<neighbor_gt->getName()<<"\n";
    cModule *neighbor_node = neighbor_gt->getOwnerModule();//Ownner could be HoM, EPPS, QNode
    //EV<<"neighbor_node = "<<neighbor_node->getName()<<"\n";
    neighborInfo neighbor_is_QNode = checkIfQNode(neighbor_node);
    return neighbor_is_QNode;
}



cModule* HardwareMonitor::getQNode(){
         cModule *currentModule = getParentModule();//We know that Connection manager is not the QNode, so start from the parent.
         try{
             cModuleType *QNodeType =  cModuleType::get("networks.QNode");//Assumes the node in a network has a type QNode
             while(currentModule->getModuleType()!=QNodeType){
                 currentModule = currentModule->getParentModule();
             }
             return currentModule;
         }catch(std::exception& e){
             error("No module with QNode type found. Have you changed the type name in ned file?");
             endSimulation();
         }
         return currentModule;
}



neighborInfo HardwareMonitor::checkIfQNode(cModule *thisNode){

    neighborInfo inf;//Return this
    if(thisNode->getModuleType()!=QNodeType){//Not a Qnode!

        if(thisNode->getModuleType()==HoMType){
            EV<<thisNode->getModuleType()->getFullName()<<" == "<<HoMType->getFullName()<<"\n";
            inf.isQNode=false;
            int address_one = thisNode->getSubmodule("Controller")->par("neighbor_address");
            int address_two = thisNode->getSubmodule("Controller")->par("neighbor_address_two");
            int myaddress = par("address");
            EV<<"\n myaddress = "<<myaddress<<", address = "<<address_one<<", address_two = "<<address_two<<" in "<<thisNode->getSubmodule("Controller")->getFullName()<<"\n";
            //endSimulation();
            if(address_one==myaddress){
                inf.neighborQNode_address = address_two;
            }else if(address_two==myaddress){
                inf.neighborQNode_address = address_one;
            }else{
                //endSimulation();
                //EV<<"address _one = "<<address_one<<", address_two = "<<address_two;
                //error("Something is wrong with tracking the neighbor address. It is here.");
            }
        }else if(thisNode->getModuleType()== SPDCType){
            error("TO BE IMPLEMENTED");
        }else{
            error("This simulator only recognizes the following network level node types: QNode, EPPS and HoM. Not %s",thisNode->getClassName());
            endSimulation();
        }
    }
    else{
        inf.isQNode=true;
        inf.neighborQNode_address = thisNode->par("address");
    }
    inf.type = thisNode->getModuleType();
    inf.address = thisNode->par("address");
    return inf;
}

HardwareMonitor::NeighborTable HardwareMonitor::passNeighborTable(){
    Enter_Method("passNeighborTable()");
    return ntable;
}

} // namespace modules
} // namespace quisp
