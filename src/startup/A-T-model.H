#ifndef A_T_MODEL_H
#define A_T_MODEL_H

#include <vector>
#include <iostream>
#include <string>

#include <boost/program_options.hpp>

#include "models/parameters.H"
#include "computation/module.H"
#include "alignment/alignment.H"
#include "setup.H"
#include "models/setup.H"
#include "owned-ptr.H"
#include "util/json.hh"

void setup_heating(int proc_id, const boost::program_options::variables_map& args, Parameters& P);

void setup_partition_weights(const boost::program_options::variables_map& args, Parameters& P);

std::vector<model_t>
get_smodels(const boost::program_options::variables_map& args, const std::vector<alignment>& A,
	    shared_items<std::string>& smodel_names_mapping);

std::vector<model_t> 
get_imodels(const shared_items<std::string>& imodel_names_mapping, const SequenceTree& T);

void log_summary(std::ostream& out_cache, std::ostream& out_screen,std::ostream& out_both,
		 const shared_items<std::string>& imodels, const shared_items<std::string>& smodels,
		 const Parameters& P,const boost::program_options::variables_map& args);

void set_foreground_branches(Parameters& P, const SequenceTree& T);

owned_ptr<Model> create_A_and_T_model(const Rules& R, boost::program_options::variables_map& args, const std::shared_ptr<module_loader>& L,
				      std::ostream& out_cache, std::ostream& out_screen, std::ostream& out_both, json& info,
				      int proc_id);

void write_initial_alignments(boost::program_options::variables_map& args, int proc_id, const std::string& dir_name);


#endif
