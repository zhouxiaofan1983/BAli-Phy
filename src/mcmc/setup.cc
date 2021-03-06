/*
  Copyright (C) 2004-2010 Benjamin Redelings

  This file is part of BAli-Phy.

  BAli-Phy is free software; you can redistribute it and/or modify it under
  the terms of the GNU General Public License as published by the Free
  Software Foundation; either version 2, or (at your option) any later
  version.

  BAli-Phy is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
  for more details.

  You should have received a copy of the GNU General Public License
  along with BAli-Phy; see the file COPYING.  If not see
  <http://www.gnu.org/licenses/>.  */

///
/// \file   mcmc/setup.C
/// \brief  Provides routines to create default transition kernels and start a Markov chain.
///
/// The function do_sampling( ) creates transition kernel for known parameter names.
/// It then starts the Markov chain for the MCMC run and runs it for a specified
/// number of iterations.
///
/// \author Benjamin Redelings
/// 

#include "mcmc/setup.H"
#include "mcmc/logger.H"
#include "util.H"
#include "sample.H"
#include "alignment/alignment-util.H"
#include "alignment/alignment-util2.H"
#include "alignment/alignment-constraint.H"
#include "timer_stack.H"
#include "bounds.H"
#include "AIS.H"
#include "computation/expression/expression.H"

using boost::program_options::variables_map;
using boost::dynamic_bitset;
using boost::optional;
using std::vector;
using std::endl;
using std::string;
using std::ostream;
using std::map;
using std::shared_ptr;

/// \brief Add a Metropolis-Hastings sub-move for each parameter in \a names to \a M
void add_modifiable_MH_move(const Model& P, const string& name, const proposal_fn& proposal, int r, const vector<double>& parameters,
			    MCMC::MoveAll& M, double weight=1)
{
    double rate = P.get_rate_for_reg(r);
    std::cerr<<"rate = "<<rate<<"\n";
    M.add(rate * weight, MCMC::MH_Move( Proposal2M(proposal, r, parameters), name) );
}

/// \brief Add a Metropolis-Hastings sub-move for each parameter in \a names to \a M
void add_MH_move(Model& P, const proposal_fn& proposal, const vector<string>& names,
		 const vector<string>& pnames, const vector<double>& pvalues,
		 MCMC::MoveAll& M, double weight=1)
{
    // FIXME - Change names to vector<vector<string>>, and deduce a move name from pathname shared by all parameters.
    // FIXME - then replace the routine above with this one!

    // 1. Set the parameter values to their defaults, if they are not set yet.
    if (pnames.size() != pvalues.size()) std::abort();

    for(int i=0;i<pnames.size();i++)
	set_if_undef(*P.keys.modify(), pnames[i], json(pvalues[i]));

    // 2. For each MCMC parameter, create a move for it.
    for(const auto& parameter_name: names) 
    {
	assert(P.maybe_find_parameter(parameter_name));

	Proposal2 proposal2(proposal, parameter_name, pnames, P);

	M.add(weight, MCMC::MH_Move(proposal2, "MH_sample_"+parameter_name));
    }
}

/// \brief Add a Metropolis-Hastings sub-move for parameter name to M
///
/// \param P       The model that contains the parameters
/// \param p       The proposal function
/// \param name    The name of the parameter to create a move for
/// \param pname   The name of the proposal width for this move
/// \param sigma   A default proposal width, in case the user didn't specify one
/// \param M       The group of moves to which to add the newly-created sub-move
/// \param weight  How often to run this move.
///
void add_MH_move(Model& P,const proposal_fn& proposal, const string& name, const string& pname,double sigma,
		 MCMC::MoveAll& M,double weight=1)
{
    vector<int> indices = flatten( parameters_with_extension(P,name) );
    vector<string> names;
    for(int i: indices)
	names.push_back(P.parameter_name(i));

    add_MH_move(P, proposal, names, {pname}, {sigma}, M, weight);
}

double default_sampling_rate(const Model& /*M*/, const string& /*parameter_name*/)
{
    return 1.0;
}

bool add_slice_move(const Model& P, int r, MCMC::MoveAll& M, double weight = 1.0)
{
    double rate = P.get_rate_for_reg(r);
    std::cerr<<"rate = "<<rate<<"\n";
    auto range = P.get_range_for_reg(r);
    if (not range.is_a<Bounds<double>>()) return false;

    auto& bounds = range.as_<Bounds<double>>();
    string name = "m_real_"+convertToString<int>(r);
    M.add( rate * weight, MCMC::Modifiable_Slice_Move(name, r, bounds, 1.0) );
    return true;
}

void add_boolean_MH_moves(const Model& P, MCMC::MoveAll& M, double weight = 1.0)
{
    for(int r: P.random_modifiables())
    {
	auto range = P.get_range_for_reg(r);

	if (not range.head().is_a<constructor>()) continue;
	if (range.head().as_<constructor>().f_name != "TrueFalseRange") continue;

	string name = "m_bool_flip_"+convertToString<int>(r);
	add_modifiable_MH_move(P,name, bit_flip, r, vector<double>{}, M, weight);
    }
}

/// Find parameters with distribution name Dist
void add_real_slice_moves(const Model& P, MCMC::MoveAll& M, double weight = 1.0)
{
    for(int r: P.random_modifiables())
	add_slice_move(P, r, M, weight);
}

/// Find parameters with distribution name Dist
void add_real_MH_moves(const Model& P, MCMC::MoveAll& M, double weight = 1.0)
{
    for(int r: P.random_modifiables())
    {
	auto range = P.get_range_for_reg(r);
	if (not range.is_a<Bounds<double>>()) continue;

	auto& bounds = range.as_<Bounds<double>>();
	string name = "m_real_cauchy_"+convertToString<int>(r);
	if (bounds.has_lower_bound and bounds.lower_bound >= 0.0)
	    add_modifiable_MH_move(P,name, Reflect(bounds, log_scaled(Between(-20,20,shift_cauchy))), r, {1.0}, M, weight);
	else
	    add_modifiable_MH_move(P,name, Reflect(bounds, shift_cauchy), r, {1.0}, M, weight);
    }
}

/// Find parameters with distribution name Dist
void add_integer_uniform_MH_moves(const Model& P, MCMC::MoveAll& M, double weight)
{
    for(int r: P.random_modifiables())
    {
	auto range = P.get_range_for_reg(r);
	if (not range.is_a<Bounds<int>>()) continue;

	auto& bounds = range.as_<Bounds<int>>();
	if (not bounds.has_lower_bound or not bounds.has_upper_bound) continue;
	string name = "m_int_uniform_"+convertToString<int>(r);
	double l = (int)bounds.lower_bound;
	double u = (int)bounds.upper_bound;
	add_modifiable_MH_move(P,name, discrete_uniform, r, {double(l),double(u)}, M, weight);
    }
}

void add_integer_slice_moves(const Model& P, MCMC::MoveAll& M, double weight)
{
    for(int r: P.random_modifiables())
    {
	auto range = P.get_range_for_reg(r);
	double rate = P.get_rate_for_reg(r);
	std::cerr<<"rate = "<<rate<<"\n";
	if (not range.is_a<Bounds<int>>()) continue;

	// FIXME: righteousness.
	// We need a more intelligent way of determining when we should do this.
	// For example, we do not want to do this for categorical-type variables.
	auto& bounds = range.as_<Bounds<int>>();
	if (bounds.has_upper_bound and bounds.has_lower_bound) continue;

	string name = "m_int_"+convertToString<int>(r);
	M.add( rate * weight, MCMC::Integer_Modifiable_Slice_Move(name, r, bounds) );
    }
}

optional<int> scale_is_modifiable_reg(const Model& M, int s)
{
    auto& P = dynamic_cast<const Parameters&>(M);
    return P.compute_expression_is_modifiable_reg(P.branch_scale_index(s));
}

bool all_scales_modifiable(const Model& M)
{
    auto& P = dynamic_cast<const Parameters&>(M);

    for(int s=0;s<P.n_branch_scales();s++)
	if (not scale_is_modifiable_reg(M,s))
	    return false;

    return true;
}

void add_alignment_and_parameter_moves(MCMC::MoveAll& moves, Model& M, double weight = 1.0, double enabled = true)
{
    if (not dynamic_cast<const Parameters*>(&M)) return;

    int n = dynamic_cast<const Parameters&>(M).n_imodels();

    for(int i=0; i<n; i++)
    {
	string prefix = "I"+convertToString(i+1);

	vector<int> partitions = dynamic_cast<const Parameters&>(M).partitions_for_imodel(i);

	string pname1 = model_path({prefix,"rs07:log_rate"});
	if (auto index = M.maybe_find_parameter(pname1))
	{
	    auto proposal = [index,partitions](Model& P){ return realign_and_propose_parameter(P, *index, partitions, shift_cauchy, {0.25}) ;};

	    moves.add(weight, MCMC::MH_Move(Generic_Proposal(proposal),"realign_and_sample_"+pname1), enabled);
	}

	string pname2 = model_path({prefix,"rs07:mean_length"});
	if (auto index = M.maybe_find_parameter(pname2))
	{
	    auto proposal = [index,partitions](Model& P){ return realign_and_propose_parameter(P, *index, partitions, log_scaled(more_than(0.0, shift_laplace)), {0.5}) ;};

	    moves.add(weight, MCMC::MH_Move(Generic_Proposal(proposal),"realign_and_sample_"+pname2), enabled);
	}
    }
}

//FIXME - how to make a number of variants with certain things fixed, for burn-in?
// 0. Get an initial tree estimate using NJ or something? (as an option...)
// 1. First estimate tree and parameters with alignment fixed.
// 2. Then allow the alignment to change.

/// \brief Construct Metropolis-Hastings moves for scalar numeric parameters with a corresponding slice move
///
/// \param P   The model and state.
///
MCMC::MoveAll get_parameter_MH_moves(Model& M)
{
    MCMC::MoveAll MH_moves("parameters:MH");

    if (Parameters* P = dynamic_cast<Parameters*>(&M))
	for(int i=0;i<P->n_branch_scales();i++)
	    if (auto r = scale_is_modifiable_reg(M, i))
	    {
		string name = "scale_scale_"+convertToString<int>(i+1);
		add_modifiable_MH_move(M, name, log_scaled(Between(-20,20,shift_cauchy)),    *r, {1.0}, MH_moves);
	    }


    /*
      add_MH_move(P, log_scaled(Between(-20,20,shift_cauchy)),    "*.HKY.kappa",     "kappa_scale_sigma",  0.3,  MH_moves);
      add_MH_move(P, log_scaled(Between(-20,20,shift_cauchy)),    "*.rho",     "rho_scale_sigma",  0.2,  MH_moves);
      add_MH_move(P, log_scaled(Between(-20,20,shift_cauchy)),    "*.TN.kappaPur", "kappa_scale_sigma",  0.3,  MH_moves);
      add_MH_move(P, log_scaled(Between(-20,20,shift_cauchy)),    "*.TN.kappaPyr", "kappa_scale_sigma",  0.3,  MH_moves);
      add_MH_move(P, log_scaled(Between(-20,20,shift_cauchy)),    "*.M0.omega",  "omega_scale_sigma",  0.3,  MH_moves);
      add_MH_move(P, log_scaled(Between(0,20,shift_cauchy)),
      "*.M2.omega",  "omega_scale_sigma",  0.3,  MH_moves);
      add_MH_move(P, log_scaled(Between(0,20,shift_cauchy)),
      "*.M2a.omega1",  "omega_scale_sigma",  0.3,  MH_moves);
      add_MH_move(P, log_scaled(Between(0,20,shift_cauchy)),
      "*.M2a.omega3",  "omega_scale_sigma",  0.3,  MH_moves);
      add_MH_move(P, log_scaled(Between(0,20,shift_cauchy)),
      "*.M8b.omega1",  "omega_scale_sigma",  0.3,  MH_moves);
      add_MH_move(P, log_scaled(Between(0,20,shift_cauchy)),
      "*.M8b.omega3",  "omega_scale_sigma",  0.3,  MH_moves);
      add_MH_move(P, Between(0,1,shift_cauchy),   "*.INV.p",         "INV.p_shift_sigma", 0.03, MH_moves);
      add_MH_move(P, Between(0,1,shift_cauchy),   "*.Beta.mu",         "beta.mu_shift_sigma", 0.03, MH_moves);
      add_MH_move(P, Between(0,1,shift_cauchy),   "*.f",              "f_shift_sigma",      0.1,  MH_moves);
      add_MH_move(P, Between(0,1,shift_cauchy),   "*.g",              "g_shift_sigma",      0.1,  MH_moves);
      add_MH_move(P, Between(0,1,shift_cauchy),   "*.h",              "h_shift_sigma",      0.1,  MH_moves);
      add_MH_move(P, log_scaled(Between(-20,20,shift_cauchy)),    "*.gamma.sigma/mu","gamma.sigma_scale_sigma",  0.25, MH_moves);
      add_MH_move(P, log_scaled(Between(-20,0,shift_cauchy)),    "*.Beta.varOverMu", "beta.Var_scale_sigma",  0.25, MH_moves);
      add_MH_move(P, log_scaled(Between(-20,20,shift_cauchy)),    "*.LogNormal.sigma_over_mu","log-normal.sigma_scale_sigma",  0.25, MH_moves);
    */
    if (dynamic_cast<Parameters*>(&M) and all_scales_modifiable(M))
	MH_moves.add(4,MCMC::SingleMove(scale_means_only, "scale_Scales_only", "scale"));
  
    add_MH_move(M, shift_delta,                  "b*.delta",       "lambda_shift_sigma",     0.35, MH_moves, 10);
    add_MH_move(M, Between(-20,20,shift_cauchy),  model_path({"*","rs07:log_rate"}),      "lambda_shift_sigma",    0.25, MH_moves, 10);
    add_MH_move(M, log_scaled(more_than(0.0, shift_laplace)), model_path({"*","rs07:mean_length"}), "length_shift_sigma",   0.5, MH_moves, 10);

    // Add these moves disabled
    add_alignment_and_parameter_moves(MH_moves, M, 1.0, false);

    return MH_moves;
}

MCMC::MoveAll get_scale_slice_moves(Parameters& P)
{
    MCMC::MoveAll slice_moves("parameters:scale:MH");
    for(int i=0;i<P.n_branch_scales();i++)
	if (auto r = scale_is_modifiable_reg(P,i))
	    add_slice_move(P, *r, slice_moves);

    return slice_moves;
}

/// \brief Construct 1-D slice-sampling moves for (some) scalar numeric parameters
///
/// \param P   The model and state.
///
MCMC::MoveAll get_parameter_slice_moves(Model& M)
{
    MCMC::MoveAll slice_moves("parameters:slice");

    if (Parameters* P = dynamic_cast<Parameters*>(&M))
    {
	// scale parameters - do we need this?
	for(int i=0;i<P->n_branch_scales();i++)
	    if (auto r = scale_is_modifiable_reg(M,i))
		add_slice_move(*P, *r, slice_moves);

	if (all_scales_modifiable(M))
	    slice_moves.add(2,MCMC::Scale_Means_Only_Slice_Move("scale_Scales_only_slice",0.6));
    }

    // Add slice moves for continuous 1D distributions
    add_real_slice_moves(M, slice_moves, 1.0);
    add_integer_slice_moves(M, slice_moves, 1.0);

    return slice_moves;
}

/// \brief Construct dynamic programming moves to sample alignments.
///
/// \param P   The model and state.
///
MCMC::MoveAll get_alignment_moves(Parameters& P)
{
    // args for branch-based stuff
    vector<int> branches(P.t().n_branches());
    for(int i=0;i<branches.size();i++)
	branches[i] = i;

    // args for node-based stuff
    vector<int> internal_nodes;
    for(int i=P.t().n_leaves();i<P.t().n_nodes();i++)
	internal_nodes.push_back(i);

    using namespace MCMC;

    //----------------------- alignment -------------------------//
    MoveAll alignment_moves("alignment");

    //--------------- alignment::alignment_branch ---------------//
    MoveEach alignment_branch_moves("alignment_branch_master");
    alignment_branch_moves.add(1.0,
			       MoveArgSingle("sample_alignments","alignment:alignment_branch",
					     sample_alignments_one,
					     branches)
	);
    if (P.t().n_leaves() >2) {
	alignment_branch_moves.add(0.15,MoveArgSingle("sample_tri","alignment:alignment_branch:nodes",
						      sample_tri_one,
						      branches)
	    );
	alignment_branch_moves.add(0.1,MoveArgSingle("sample_tri_branch","alignment:nodes:length",
						     sample_tri_branch_one,
						     branches)
				   ,false);
	alignment_branch_moves.add(0.1,MoveArgSingle("sample_tri_branch_aligned","alignment:nodes:length",
						     sample_tri_branch_type_one,
						     branches)
				   ,false);
    }
    alignment_moves.add(1, alignment_branch_moves, false);
    alignment_moves.add(1, SingleMove(walk_tree_sample_alignments, "walk_tree_sample_alignments","alignment:alignment_branch:nodes") );
    alignment_moves.add(0.1, SingleMove(realign_from_tips, "realign_from_tips","lengths:alignment:topology") );

    //---------- alignment::nodes_master (nodes_moves) ----------//
    MoveEach nodes_moves("nodes_master","alignment:nodes");
    if (P.t().n_leaves() >= 3)
	nodes_moves.add(10,MoveArgSingle("sample_node","alignment:nodes",
					 sample_node_move,
					 internal_nodes)
	    );
    if (P.t().n_leaves() >= 4)
	nodes_moves.add(1,MoveArgSingle("sample_two_nodes","alignment:nodes",
					sample_two_nodes_move,
					internal_nodes)
	    );

    int nodes_weight = (int)(P.load_value("nodes_weight",1.0)+0.5);

    alignment_moves.add(nodes_weight, nodes_moves);
 
    return alignment_moves;
}

MCMC::MoveAll get_h_moves(Model& M)
{
    using namespace MCMC;

    MoveAll h_moves("haskell_moves");

    for(int i=0;i<M.n_transition_kernels();i++)
	h_moves.add(1,IOMove(i));

    return h_moves;
}

/// \brief Construct moves to sample the tree
///
/// \param P   The model and state.
///
MCMC::MoveAll get_tree_moves(Parameters& P)
{
    // args for branch-based stuff
    vector<int> branches(P.t().n_branches());
    for(int i=0;i<branches.size();i++)
	branches[i] = i;

    // args for branch-based stuff
    vector<int> internal_branches;
    for(int i=P.t().n_leaves();i<P.t().n_branches();i++)
	internal_branches.push_back(i);

    using namespace MCMC;

    //-------------------- tree (tree_moves) --------------------//
    MoveAll tree_moves("tree");
    MoveAll topology_move("topology");
    MoveEach NNI_move("NNI");
    MoveAll SPR_move("SPR");

    bool has_imodel = P.variable_alignment();

    if (has_imodel)
	NNI_move.add(1,MoveArgSingle("three_way_NNI","alignment:nodes:topology",
				     three_way_topology_sample,
				     internal_branches)
	    );
    else
	NNI_move.add(1,MoveArgSingle("three_way_NNI","topology",
				     three_way_topology_sample,
				     internal_branches)
	    );

    NNI_move.add(1,MoveArgSingle("two_way_NNI","alignment:nodes:topology",
				 two_way_topology_sample,
				 internal_branches)
		 ,false
	);

    //FIXME - doesn't yet deal with gaps=star
    if (has_imodel)
	NNI_move.add(0.001,MoveArgSingle("three_way_NNI_and_A","alignment:alignment_branch:nodes:topology",
					 three_way_topology_and_alignment_sample,
					 internal_branches)
		     ,false
	    );


    if (has_imodel)
    {
	// Try to intelligently consider which branch to do the alignment integration on, based on the current alignment.
	SPR_move.add(1, SingleMove(sample_SPR_all,"SPR_and_A_all", "topology:lengths:nodes:alignment:alignment_branch"));

	// If the alignment is variable, then we really need to propose things in a way that doesn't depend on the current alignment.
	SPR_move.add(0.5, SingleMove(sample_SPR_flat,"SPR_and_A_flat", "topology:lengths:nodes:alignment:alignment_branch"));
	SPR_move.add(0.5, SingleMove(sample_SPR_nodes,"SPR_and_A_nodes", "topology:lengths:nodes:alignment:alignment_branch"));
    }
    else {
	SPR_move.add(0.1,SingleMove(sample_SPR_flat,"SPR_flat", "topology:lengths"));
	SPR_move.add(0.1,SingleMove(sample_SPR_nodes,"SPR_nodes", "topology:lengths"));
	SPR_move.add(1,SingleMove(sample_SPR_all,"SPR_all", "topology:lengths"));
    }

    topology_move.add(1,NNI_move,false);
    topology_move.add(1,SPR_move);
    if (P.t().n_leaves() >3)
	tree_moves.add(1,topology_move);
  
    //-------------- tree::lengths (length_moves) -------------//
    MoveAll length_moves("lengths");
    MoveEach length_moves1("lengths1");

    length_moves1.add(1,MoveArgSingle("change_branch_length","lengths",
				      change_branch_length_move,
				      branches)
	);
    length_moves1.add(1,MoveArgSingle("change_branch_length_multi","lengths",
				      change_branch_length_multi_move,
				      branches)
	);

    length_moves1.add(0.01,MoveArgSingle("change_branch_length_and_T","lengths:nodes:topology",
					 change_branch_length_and_T,
					 internal_branches)
	);
    length_moves.add(1,length_moves1,false);
    // FIXME - Do we really want to do this, under slice sampling?
    length_moves.add(1,SingleMove(walk_tree_sample_branch_lengths,
				  "walk_tree_sample_branch_lengths","lengths")
	);

    tree_moves.add(1,length_moves);
    tree_moves.add(1,SingleMove(walk_tree_sample_NNI_and_branch_lengths,"walk_tree_NNI_and_lengths", "topology:lengths"));

    if (has_imodel)
	tree_moves.add(2,SingleMove(walk_tree_sample_NNI,"walk_tree_NNI", "topology:lengths"));
    else  // w/o integrating over 5way alignments, this move is REALLY cheap!
	tree_moves.add(4,SingleMove(walk_tree_sample_NNI,"walk_tree_NNI", "topology:lengths"));

    if (has_imodel)
	tree_moves.add(0.5,SingleMove(walk_tree_sample_NNI_and_A,"walk_tree_NNI_and_A","topology:lengths:nodes:alignment:alignment_branch"));

    return tree_moves;
}

/// \brief Construct Metropolis-Hastings moves for scalar numeric parameters without a corresponding slice move
///
/// \param P   The model and state.
///
MCMC::MoveAll get_parameter_MH_but_no_slice_moves(Model& M)
{
    using namespace MCMC;

    MoveAll parameter_moves("parameters");

    // Why 1.5?
    // Well, there's some danger here that we could flip in a periodic fashion, and only observe variable when its True (or only if its False).
    //  - It seems to be OK, though.  Why?
    //  - Note that this should only be an issue when this does not affect the likelihood.
    // Also, how hard would it be to make a Gibbs flipper?  We could (perhaps) run that once per iteration to avoid periodicity.

    add_boolean_MH_moves(M, parameter_moves, 1.5);
    add_integer_uniform_MH_moves(M, parameter_moves, 0.1);

    // Actually there ARE slice moves for this, but they don't jump modes!
    add_real_MH_moves(M, parameter_moves, 0.1);

    // FIXME - we need a proposal that sorts after changing
    //         then we can un-hack the recalc function in smodel.C
    //         BUT, this assumes that we have the DP::rate* names in *numerical* order
    //          whereas we probably find them in *lexical* order....
    //          ... or creation order?  That might be OK for now! 

    for(int i=0;;i++) {
	string name = "M3.omega" + convertToString(i+1);
	if (not has_parameter(M,name))
	    break;
    
	add_MH_move(M, log_scaled(Between(-20,20,shift_cauchy)), name, "omega_scale_sigma", 1, parameter_moves);
	//    Proposal2 m(log_scaled(shift_cauchy), name, vector<string>(1,"omega_scale_sigma"), P);
	//    parameter_moves.add(1, MCMC::MH_Move(m,"sample_M3.omega"));
    }

    {
	auto index = M.maybe_find_parameter("lambdaScaleBranch");
	if (index and (M.get_parameter_value(*index).as_int() != -1 or M.contains_key("lambda_search_all")))
	{
	    M.set_parameter_value(*index, 0);
	    Generic_Proposal m(move_scale_branch);
	    parameter_moves.add(1.0, MCMC::MH_Move(m,"sample_lambdaScaleBranch"));
	}
    }

    if (M.contains_key("sample_foreground_branch"))
    {
	Generic_Proposal m(move_subst_type_branch);
	parameter_moves.add(1.0, MCMC::MH_Move(m,"sample_foreground_branch"));
    }

    return parameter_moves;
}

void enable_disable_transition_kernels(MCMC::MoveAll& sampler, const variables_map& args)
{
    vector<string> disable;
    vector<string> enable;
    if (args.count("disable"))
	disable = split(args["disable"].as<string>(),',');
    if (args.count("enable"))
	enable = split(args["enable"].as<string>(),',');
  
    for(int i=0;i<disable.size();i++)
	sampler.disable(disable[i]);
  
    for(int i=0;i<enable.size();i++)
	sampler.enable(enable[i]);
}

/// Find the minimum branch length
double min_branch_length(const TreeInterface& t)
{
    double min_branch = t.branch_length(0);
    for(int i=1;i<t.n_branches();i++)
	min_branch = std::min(min_branch, t.branch_length(i));
    return min_branch;
}

/// Replace negative or zero branch lengths with saner values.
void set_min_branch_length(Parameters& P, double min_branch)
{
    auto t = P.t();

    for(int b=0;b<t.n_branches();b++) 
	if (t.branch_length(b) < min_branch)
	    P.setlength(b, min_branch);
}

void avoid_zero_likelihood(owned_ptr<Model>& P, ostream& out_log,ostream& /* out_both */)
{
    if (not P.as<Parameters>()) return;

    Parameters& PP = *P.as<Parameters>();

    for(int i=0;i<20 and P->likelihood() == 0.0;i++)
    {
	double min_branch = min_branch_length(PP.t());
	out_log<<"  likelihood = "<<P->likelihood()<<"  min(T[b]) = "<<min_branch<<"\n";

	min_branch = std::max(min_branch, 0.0001);
	set_min_branch_length(PP, 2.0*min_branch);
    }
}

double fraction_non_gap(const Parameters& P)
{
    double total_char = 0;
    double total_cell = 0;
    for(int i=0;i<P.n_data_partitions();i++)
    {
	const alignment& A = P[i].A();
	total_char += sum(sequence_lengths(A));
	total_cell += A.n_sequences()*A.length();
    }
    return total_char/total_cell;
}

void log_preburnin(ostream& o, const Model& M, const string& name, int iter)
{
    auto& P = dynamic_cast<const Parameters&>(M);

    double TL = tree_length(P.t());
    o<<" "<<name<<" #"<<iter+1<<"   prior = "<<P.prior()<<"   likelihood = "<<P.likelihood();
    o<<"   |T| = "<<TL;
    o<<"   |A| = "<<MCMC::alignment_length(P);
    for(int s=0;s<P.n_branch_scales();s++)
	o<<"   Scale"<<s+1<<"*|T| = "<<P.branch_scale(s)*TL;
    o<<std::endl;
}


void do_pre_burnin(const variables_map& args, owned_ptr<Model>& P, ostream& out_log,ostream& out_both)
{
    using namespace MCMC;
    using namespace boost::chrono;

    if (not P.as<Parameters>()) return;

    if (P.as<Parameters>()->t().n_nodes() < 2) return;

    int n_pre_burnin = args["pre-burnin"].as<int>();

    if (n_pre_burnin <= 0) return;

    duration_t t1 = total_cpu_time();
    out_both<<"Beginning pre-burnin: "<<n_pre_burnin<<" iterations."<<endl;

    log_preburnin(out_both, *P, "Start", 0);
    out_both<<endl;

    MoveStats Stats;

    // 0. Then sample  (a) scale and (b) branch lengths and (c) parameters
    if (P->contains_key("pre-burnin-A"))
    {
	MoveAll pre_burnin("pre-burnin");

	pre_burnin.add(1,get_scale_slice_moves(*P.as<Parameters>()));
	if (all_scales_modifiable(*P))
	    pre_burnin.add(1,MCMC::SingleMove(scale_means_only, "scale_means_only","mean"));
	pre_burnin.add(1,SingleMove(walk_tree_sample_branch_lengths, "walk_tree_sample_branch_lengths","lengths") );
	pre_burnin.add(2,get_parameter_slice_moves(*P));
	pre_burnin.add(1,SingleMove(realign_from_tips, "realign_from_tips","lengths:alignment:topology") );
	add_alignment_and_parameter_moves(pre_burnin, *P);

	// enable and disable moves
	enable_disable_transition_kernels(pre_burnin,args);

	for(int i=0;i<n_pre_burnin;i++)
	{
	    // turn training on
	    {
		Parameters& PP = *P.as<Parameters>();
		PP.set_parameter_value(PP.find_parameter("*IModels.training"), new constructor("Prelude.True",0));
	    }

	    pre_burnin.iterate(P,Stats);
	    // turn training off
	    {
		Parameters& PP = *P.as<Parameters>();
		PP.set_parameter_value(PP.find_parameter("*IModels.training"), new constructor("Prelude.False",0));
	    }

	    log_preburnin(out_both, *P, "(S)+(L)+(P)+Alignment", i);
	    show_parameters(out_log, *P, false);
	}
	out_both<<endl;
    }

    if (P.as<Parameters>()->variable_alignment() and not args.count("tree"))
	P.as<Parameters>()->variable_alignment(false);

    // 1. First sample (a) scale of the tree
    {
	MoveAll pre_burnin("pre-burnin");

	pre_burnin.add(2,get_scale_slice_moves(*P.as<Parameters>()));
	if (all_scales_modifiable(*P))
	    pre_burnin.add(2,MCMC::SingleMove(scale_means_only, "scale_means_only","mean"));

	// enable and disable moves
	enable_disable_transition_kernels(pre_burnin,args);

	for(int i=0;i<n_pre_burnin;i++) {
	    pre_burnin.iterate(P,Stats);
	    log_preburnin(out_both, *P, "Tree (S)ize", i);
	    show_parameters(out_log, *P, false);
	}
    }
    out_both<<endl;

    // 2. Then sample (a) scale and (b) branch lengths
    {
	MoveAll pre_burnin("pre-burnin");

	pre_burnin.add(1,get_scale_slice_moves(*P.as<Parameters>()));
	if (all_scales_modifiable(*P))
	    pre_burnin.add(1,MCMC::SingleMove(scale_means_only, "scale_means_only","mean"));
	pre_burnin.add(1,SingleMove(walk_tree_sample_branch_lengths, "walk_tree_sample_branch_lengths","lengths") );

	// enable and disable moves
	enable_disable_transition_kernels(pre_burnin,args);

	for(int i=0;i<n_pre_burnin;i++) {
	    pre_burnin.iterate(P,Stats);
	    log_preburnin(out_both, *P, "(S)+Branch (L)engths", i);
	    show_parameters(out_log, *P, false);
	}
    }
    out_both<<endl;

    // 3. Then sample  (a) scale and (b) branch lengths and (c) parameters
    {
	MoveAll pre_burnin("pre-burnin");

	pre_burnin.add(1,get_scale_slice_moves(*P.as<Parameters>()));
	if (all_scales_modifiable(*P))
	    pre_burnin.add(1,MCMC::SingleMove(scale_means_only, "scale_means_only","mean"));
	pre_burnin.add(1,SingleMove(walk_tree_sample_branch_lengths, "walk_tree_sample_branch_lengths","lengths") );
	pre_burnin.add(2,get_parameter_slice_moves(*P));

	// enable and disable moves
	enable_disable_transition_kernels(pre_burnin,args);

	for(int i=0;i<n_pre_burnin;i++) {
	    pre_burnin.iterate(P,Stats);
	    log_preburnin(out_both, *P, "(S)+(L)+(P)arameters", i);
	    show_parameters(out_log, *P, false);
	}
    }
    out_both<<endl;

    // 4. Then do a tree search - NNI - w/ the actual model
    {
	MoveAll pre_burnin("pre-burnin");

	pre_burnin.add(1,get_scale_slice_moves(*P.as<Parameters>()));
	if (all_scales_modifiable(*P))
	    pre_burnin.add(1,MCMC::SingleMove(scale_means_only, "scale_means_only","mean"));
	pre_burnin.add(2,SingleMove(walk_tree_sample_NNI_and_branch_lengths, "NNI_and_lengths","tree:topology:lengths"));
	pre_burnin.add(1,get_parameter_slice_moves(*P));

	// enable and disable moves
	enable_disable_transition_kernels(pre_burnin,args);

	int n_pre_burnin2 = n_pre_burnin + (int)log(P.as<Parameters>()->t().n_leaves());
	for(int i=0;i<n_pre_burnin2;i++) {
	    pre_burnin.iterate(P,Stats);
	    log_preburnin(out_both, *P, "(S)+(L)+(P)+NNI", i);
	    show_parameters(out_log, *P, false);
	}
    }
    out_both<<endl;

    // 5. Then do an further tree search - SPR - ignore indel information
    {
	MoveAll pre_burnin("pre-burnin");
	pre_burnin.add(1,get_scale_slice_moves(*P.as<Parameters>()));
	if (all_scales_modifiable(*P))
	    pre_burnin.add(1,MCMC::SingleMove(scale_means_only, "scale_Scales_only","scale"));
	pre_burnin.add(1,SingleMove(walk_tree_sample_branch_lengths, "walk_tree_sample_branch_lengths","tree:lengths"));
	pre_burnin.add(1,SingleMove(sample_SPR_search_all,"SPR_search_all", "tree:topology:lengths"));

	// enable and disable moves
	enable_disable_transition_kernels(pre_burnin,args);

	for(int i=0;i<1;i++) {
	    pre_burnin.iterate(P,Stats);
	    log_preburnin(out_both, *P, "SPR", i);
 	    show_parameters(out_log, *P, false);
	}
	out_both<<endl;
    }

    // 6. Then do a tree search - NNI - w/ the actual model
    {
	MoveAll pre_burnin("pre-burnin");

	pre_burnin.add(1,get_scale_slice_moves(*P.as<Parameters>()));
	if (all_scales_modifiable(*P))
	    pre_burnin.add(1,MCMC::SingleMove(scale_means_only, "scale_means_only","mean"));
	pre_burnin.add(2,SingleMove(walk_tree_sample_NNI_and_branch_lengths, "NNI_and_lengths","tree:topology:lengths"));
	pre_burnin.add(1,get_parameter_slice_moves(*P));

	// enable and disable moves
	enable_disable_transition_kernels(pre_burnin,args);

	for(int i=0;i<n_pre_burnin;i++) {
	    pre_burnin.iterate(P,Stats);
	    log_preburnin(out_both, *P, "(S)+(L)+(P)+NNI", i);
	    show_parameters(out_log, *P, false);
	}
    }
    out_both<<endl;

    // Set all alignments that COULD be variable back to being variable.
    P.as<Parameters>()->variable_alignment(true);


    // 4. Then do an initial tree search - SPR - with variable alignment
    if (P->contains_key("pre-burnin-A"))
    {
	// turn training on
	{
	    Parameters& PP = *P.as<Parameters>();
	    PP.set_parameter_value(PP.find_parameter("*IModels.training"), new constructor("Prelude.True",0));
	}

	MoveAll pre_burnin("pre-burnin+A");
	pre_burnin.add(4,get_scale_slice_moves(*P.as<Parameters>()));
	if (all_scales_modifiable(*P))
	    pre_burnin.add(4,MCMC::SingleMove(scale_means_only, "scale_means_only", "mean"));
	pre_burnin.add(1,SingleMove(walk_tree_sample_branch_lengths, "walk_tree_sample_branch_lengths", "tree:lengths"));
	pre_burnin.add(1,SingleMove(sample_SPR_A_search_all,"SPR_search_all", "tree:topology:lengths"));

	// enable and disable moves
	enable_disable_transition_kernels(pre_burnin,args);

	for(int i=0;i<n_pre_burnin;i++)
	{
	    log_preburnin(out_both, *P, "SPR+A (training)", i);
	    show_parameters(out_log,*P,false);
	    pre_burnin.iterate(P,Stats);
	}

	// turn training back off
	{
	    Parameters& PP = *P.as<Parameters>();
	    PP.set_parameter_value(PP.find_parameter("*IModels.training"), new constructor("Prelude.False",0));
	}
	for(int i=0;i<n_pre_burnin;i++)
	{
	    log_preburnin(out_both, *P, "SPR+A", i);
	    show_parameters(out_log,*P,false);
	    pre_burnin.iterate(P,Stats);
	}

    }
//    out_both<<endl;

    out_log<<Stats<<endl;
    duration_t t2 = total_cpu_time();
    out_both<<"Finished pre-burnin in "<<duration_cast<boost::chrono::duration<double> >(t2-t1)<<".\n"<<endl;


    int B = P.as<Parameters>()->t().n_branches();
    for(int i=0; i<P.as<Parameters>()->n_branch_scales(); i++)
	if (P.as<Parameters>()->branch_scale(i) > 0.5*B)
	    P.as<Parameters>()->branch_scale(i, 0.5*B);
}

/// \brief Create transition kernels and start a Markov chain
///
/// \param args            Contains command line arguments
/// \param P               The model and current state
/// \param max_iterations  The number of iterations to run (unless interrupted).
/// \param files           Files to log output into
///
void do_sampling(const variables_map& args,
		 owned_ptr<Model>& P,
		 long int max_iterations,
		 ostream& s_out,
		 const vector<MCMC::Logger>& loggers)
{
    using namespace MCMC;

    bool is_parameters = P.as<Parameters>();

    //------------------ Construct the sampler  -----------------//
    int subsample = args["subsample"].as<int>();
  
    // full sampler
    Sampler sampler("sampler");

    MoveAll slice_moves = get_parameter_slice_moves(*P);
    MoveAll MH_but_no_slice_moves = get_parameter_MH_but_no_slice_moves(*P);
    MoveAll MH_moves = get_parameter_MH_moves(*P);

    if (is_parameters)
    {
	owned_ptr<Parameters> PP = P.as<Parameters>();
	//----------------------- alignment -------------------------//
	MoveAll alignment_moves = get_alignment_moves(*PP);
    
	//------------------------- tree ----------------------------//
	MoveAll tree_moves = get_tree_moves(*PP);
    
	//-------------- parameters (parameters_moves) --------------//
	if (PP->variable_alignment() and PP->t().n_nodes() > 1)
	{
	    double factor = P->load_value("alignment_sampling_factor",1.0);
	    std::cerr<<"alignment sampling factor = "<<factor<<"\n";
	    sampler.add(factor,alignment_moves);
	}
	if (PP->t().n_nodes() > 1)
	    sampler.add(2,tree_moves);

	// FIXME - We certainly don't want to do MH_sample_mu[i] O(branches) times
	// - It does call the likelihood function, doesn't it?
	// FIXME -   However, it is probably not so important to resample most parameters in a way that is interleaved with stuff... (?)
	// FIXME -   Certainly, we aren't going to be interleaved with branches, anyway!
	sampler.add(5 + log(1+PP->t().n_branches()), MH_but_no_slice_moves);
	if (P->load_value("MH_sampling",false))
	    sampler.add(5 + log(1+PP->t().n_branches()),MH_moves);
	else
	    sampler.add(1,MH_moves);
    }
    else
    {
	sampler.add(5, MH_but_no_slice_moves);
	sampler.add(1, MH_moves);
    }

    // Add slice moves.
    if (P->load_value("slice_sampling", true))
	sampler.add(1,slice_moves);

    //------------------- Add moves defined via notes ---------------------------//
    sampler.add(1, get_h_moves(*P));

    for(int i=0;i<loggers.size();i++)
	sampler.add_logger(loggers[i]);

    //------------------- Enable and Disable moves ---------------------------//
    enable_disable_transition_kernels(sampler,args);

    //------------------ Report status before starting MCMC -------------------//
    sampler.show_enabled(s_out);
    s_out<<"\n";

    if (is_parameters)
    {
	owned_ptr<Parameters> PP = P.as<Parameters>();
	//-------------------- Report alignment alignments -----------------------//
	for(int i=0;i<PP->n_data_partitions();i++)
	    std::cout<<"Partition "<<i+1<<": using "<<(*PP)[i].alignment_constraint().size1()<<" constraints.\n";
    
//	for(int i=0;i<PP->n_data_partitions();i++) 
//	{
//	    dynamic_bitset<> s2 = constraint_satisfied((*PP)[i].alignment_constraint(), (*PP)[i].A());
//	    dynamic_bitset<> s1(s2.size());
//	    report_constraints(s1,s2,i);
//	} 
    }
    
    if (P->load_value("AIS",false))
    {
	// before we do this, just run 20 iterations of a sampler that keeps the alignment fixed
	// - first, we need a way to change the tree on a sampler that has internal node sequences?
	// - well, this should be exactly the -t sampler.
	// - but then how do we copy stuff over?
	AIS_Sampler A(sampler);
	vector<double> beta(1,1);
	int AIS_temp_levels = P->load_value("AIS:levels",50);
	double AIS_temp_factor = P->load_value("AIS:factor",0.95);

	if (AIS_temp_factor >= 1)
	    throw myexception()<<"AIS:factor must be less than 1!";
	if (AIS_temp_factor <= 0)
	    throw myexception()<<"AIS:factor must be greater than 0!";

	for(int i=0;i<AIS_temp_levels;i++)
	    beta.push_back(beta.back()*AIS_temp_factor);
	beta.push_back(0);
    
	A.go(P,std::cerr,beta);
    }
    else
	sampler.go(P,subsample,max_iterations,s_out);
}
