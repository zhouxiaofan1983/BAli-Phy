/*
  Copyright (C) 2011 Benjamin Redelings

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
/// \file   logger.C
/// \brief  Provides classes for constructing MCMC samplers.
///
/// This file provides classes for constructing MCMC samplers.  The
/// class Sampler is used to run the main loop of the sampler for
/// bali-phy.
///
/// \author Benjamin Redelings
/// 

#include <iostream>

#include "mcmc.H"
#include "logger.H"
#include "util.H"

#include "substitution/substitution.H"    // for get_model_probabilitiesby_alignment_column( )

#include "monitor.H"         // for show_smodel( )
#include "n_indels2.H"
#include "substitution/parsimony.H"
#include "alignment/alignment-util.H"
#include "dp/2way.H"
#include "computation/expression/expression.H"

using std::endl;
using std::pair;

namespace MCMC {
    using std::vector;
    using std::valarray;
    using std::cerr;
    using std::clog;
    using std::string;
    using std::make_shared;
    using std::ostream;

    int SortedTableFunction::n_fields() const
    {
	return F->n_fields();
    }

    vector<string> SortedTableFunction::field_names() const
    {
	vector<string> names = F->field_names();

//      Was this to uniquify by substitution model?
//	for(int i=0;i<names.size();i++)
//	    if (sorted_index[i] != -1)
//		names[i] += "[S" + convertToString(sorted_index[i]+1) + "]";

	return names;
    }

/// \brief Force identifiability by sorting certain parameters according to the order of indices[0]
///
/// \param v The values of all parameters.
/// \param indices The indices of parameter values to reorder.
///
/// Parameter values indexed by indices[i] are sorted so that the parameter values indexed
/// by indices[0] are in increasing order.
///
    vector<expression_ref> make_identifiable(const vector<expression_ref>& v,const vector< vector<int> >& indices)
    {
	assert(indices.size());
	int N = indices[0].size();

	vector<expression_ref> v_sub = select(v,indices[0]);

	vector<int> O = iota(N);
	std::sort(O.begin(),O.end(), 
		  [&v_sub](int i, int j) 
		  {
		      double di = v_sub[i].as_double();
		      double dj = v_sub[j].as_double();
		      return di < dj;
		  });

	vector<int> O_all = iota<int>(v.size());
	for(int i=0;i<indices.size();i++) 
	{
	    assert(indices[i].size() == N);
	    for(int j=0;j<N;j++) {
		// indices[i][j] -> indices[i][O[j]]
		O_all[indices[i][j]] = indices[i][O[j]];
	    }
	}
	vector<expression_ref> v2 = apply_mapping(v,invert(O_all));

	return v2;
    }

    vector<expression_ref> SortedTableFunction::operator()(const Model& M, long t)
    {
	vector<expression_ref> v = (*F)(M,t);

	for(int i=0;i<indices.size();i++)
	    v = make_identifiable(v, indices[i]);

	return v;
    }

    SortedTableFunction::SortedTableFunction(const TableFunction<expression_ref>& f, const std::vector< std::vector< std::vector< int> > >& i_)
	:F(f), indices(i_), sorted_index(f.n_fields(),-1)
    { 
	for(int i=0;i<indices.size();i++)
	    for(int j=0;j<indices[i].size();j++)
		for(int k=0;k<indices[i][j].size();k++)
		{
		    int index = indices[i][j][k];
		    assert(0 <= index and index < sorted_index.size());
		    sorted_index[index] = i;
		}
    }

    string TableViewerFunction::operator()(const Model& M, long t)
    {
	vector<string> fields = function->field_names();
	vector<string> values = (*function)(M,t);
	std::stringstream output;

	for(int i=0;i<values.size();i++)
	{
	    output<<"    ";
	    output<<fields[i]<<" = "<<values[i];
	}
	output<<"\n";

	return output.str();
    }

    TableViewerFunction::TableViewerFunction(const TableFunction<string>& f)
	:function(f)
    { }

    expression_ref get_computation(const Model& M, int index)
    {
	expression_ref result = M.evaluate(index);

	if (result.is_double())
	    return result;
	else if (result.is_int())
	    return result;
	else if (result.head().is_a<constructor>())
	{
	    auto& c = result.head().as_<constructor>();
	    if (c.f_name == "Prelude.True")
		return 1;
	    else if (c.f_name == "Prelude.False")
		return 0;
	    else
		return -1;
	}
	else
	    return -1;
    }

    int alignment_length(const data_partition& P)
    {
	auto branches = P.t().all_branches_from_node(0);

	int total = P.seqlength(0);
	for(int b: branches)
	    total += P.get_pairwise_alignment(b).count_insert();

	return total;
    }

    int n_indels(const data_partition& P)
    {
	auto branches = P.t().all_branches_from_node(0);

	int total = 0;
	for(int b: branches)
	    total += n_indels( P.get_pairwise_alignment(b) );
	return total;
    }

    int total_length_indels(const data_partition& P)
    {
	auto branches = P.t().all_branches_from_node(0);

	int total = 0;
	for(int b: branches)
	    total += total_length_indels( P.get_pairwise_alignment(b) );
	return total;
    }

    int alignment_length(const Parameters& P)
    {
	int total = 0;
	for(int p=0;p<P.n_data_partitions();p++)
	    total += alignment_length(P[p]);
	return total;
    }

    string Get_Total_Alignment_Length_Function::operator()(const Model& M, long)
    {
	const Parameters& P = dynamic_cast<const Parameters&>(M);

	return convertToString(alignment_length(P));
    }

    string Get_Total_Num_Substitutions_Function::operator()(const Model& M, long)
    {
	const Parameters& P = dynamic_cast<const Parameters&>(M);

	int total = 0;
	for(int p=0;p<P.n_data_partitions();p++)
	    total += n_mutations(P[p]);
	return convertToString(total);
    }

    string Get_Total_Num_Indels_Function::operator()(const Model& M, long)
    {
	const Parameters& P = dynamic_cast<const Parameters&>(M);

	int total = 0;
	for(int p=0;p<P.n_data_partitions();p++)
	    total += n_indels(P[p]);
	return convertToString(total);
    }

    string Get_Total_Total_Length_Indels_Function::operator()(const Model& M, long)
    {
	const Parameters& P = dynamic_cast<const Parameters&>(M);

	int total = 0;
	for(int p=0;p<P.n_data_partitions();p++)
	    total += total_length_indels(P[p]);
	return convertToString(total);
    }

    vector<int> sequence_lengths(const data_partition& P)
    {
	vector<int> L(P.t().n_leaves());
	for(int i=0;i<L.size();i++)
	    L[i] = P.seqlength(i);
	return L;
    }

    string Get_Rao_Blackwellized_Parameter_Function::operator()(const Model& M, long)
    {
	if (parameter == -1) std::abort();

	owned_ptr<Model> M2 = M;
	vector<log_double_t> Prs;
	log_double_t total = 0;

	auto cur_value = M.get_parameter_value(parameter);

        // Record probabilities
	for(const auto& v: values)
	{
	    if (v.type() != cur_value.type())
		throw myexception()<<"Rao-Blackwellization: Trying to set parameter with value '"<<cur_value<<"' to '"<<v<<"'";
	    M2->set_parameter_value(parameter, v);
	    log_double_t Pr = M2->probability();
	    total += Pr;
	    Prs.push_back(Pr);
	}

	// Rescale probabilities
	for(auto& Pr: Prs)
	    Pr /= total;

	// Compute expectation
	log_double_t result = 0;
	for(int i=0;i<values.size();i++)
	{
	    const auto& v = values[i];
	    log_double_t Pr = Prs[i];
	    log_double_t value = 0;
	    if (v.head().is_a<constructor>())
	    {
		auto& b = v.head().as_<constructor>();
		if (b.f_name == "Prelude.True")
		    value = 1;
		else if (b.f_name == "Prelude.False")
		    value = 0;
	    }
	    else if (v.is_int())
		value = v.as_int();
	    else if (v.is_double())
		value = v.as_double();

	    result += Pr*value;
	}

	return convertToString( result );
    }

    Get_Rao_Blackwellized_Parameter_Function::Get_Rao_Blackwellized_Parameter_Function(int p, const vector<expression_ref>& v)
	:parameter(p), values(v)
    { }

    string Get_Tree_Length_Function::operator()(const Model& M, long)
    {
	const Parameters& P = dynamic_cast<const Parameters&>(M);

	return convertToString( tree_length(P.t()) );
    }

    string TreeFunction::operator()(const Model& M, long)
    {
	const Parameters& P = dynamic_cast<const Parameters&>(M);

	return write_newick(P,true);
    }

    string MAP_Function::operator()(const Model& M, long t)
    {
	std::ostringstream output;

	log_double_t Pr = M.probability();
	if (Pr < MAP_score)
	    goto out;

	MAP_score = Pr;

	output<<"iterations = "<<t<<"       MAP = "<<MAP_score<<"\n";
	output<<(*F)(M,t)<<"\n";
  
    out:
	return output.str();
    }



    string AlignmentFunction::operator()(const Model& M, long)
    {
	const Parameters& P = dynamic_cast<const Parameters&>(M);
	std::ostringstream output;
	output<<P[p].A()<<"\n";
	return output.str();
    }

    string Show_SModels_Function::operator()(const Model& M, long)
    {
	const Parameters& P = dynamic_cast<const Parameters&>(M);
	std::ostringstream output;
	show_smodels(output, P);
	output<<"\n";
	return output.str();
    }

    string Subsample_Function::operator()(const Model& M, long t)
    {
	if (t%subsample == 0) 
	    return (*function)(M,t);
	else
	    return "";
    }

    string Mixture_Components_Function::operator()(const Model& M, long)
    {
	std::ostringstream output;
	const Parameters& P = dynamic_cast<const Parameters&>(M);
	vector<vector<double> > model_pr = substitution::get_model_probabilities_by_alignment_column(P[p]);
	for(int i=0;i<model_pr.size();i++)
	    output<<join(model_pr[i],' ')<<"\n";

	output<<endl;

	return output.str();
    }

    string Ancestral_Sequences_Function::operator()(const Model& M, long)
    {
	std::ostringstream output;

	const Parameters& P = dynamic_cast<const Parameters&>(M);

	const alphabet& a = P[p].get_alphabet();

	alignment A = P[p].A();

	// FIXME! Handle inferring N/X in leaf sequences for 1 or 2 sequences.
	if (P.t().n_leaves() <= 2)
	{
	    A.print_fasta_to_stream(output);
	    output<<endl;
	    return output.str();
	}

	const vector<unsigned> smap = P[p].state_letters();

	vector<vector<pair<int,int> > > states = substitution::sample_ancestral_states(P[p]);
    
	for(int i=0;i<A.n_sequences();i++)
	{
	    vector<int> columns = A.get_columns_for_characters(i);
	    assert(columns.size() == states[i].size());
	    for(int j=0;j<columns.size();j++)
	    {
		int c = columns[j];
		assert(A.character(c,i));
		int state = states[i][j].second;
		int letter = smap[state];
		if (a.is_letter(A(c,i)))
		    assert( A(c,i) == letter );
		else
		    A.set_value(c,i, letter);
	    }
	}

	A.print_fasta_to_stream(output);
	output<<endl;

	return output.str();
    }

    void FunctionLogger::operator()(const Model& M, long t)
    {
	(*log_file)<<function(M,t);
	log_file->flush();
    }

    FunctionLogger::FunctionLogger(const std::string& filename, const logger_function<string>& L)
	:log_file(new checked_ofstream(filename,false)),function(L)
    { }

    string ConcatFunction::operator()(const Model& M, long t)
    {
	string output;

	for(int i=0;i<functions.size();i++)
	    output += (functions[i])(M,t);

	return output;
    }


    ConcatFunction& operator<<(ConcatFunction& CF,const logger_function<string>& F)
    {
	CF.add_function(F);
	return CF;
    }

    ConcatFunction& operator<<(ConcatFunction& CF,const string& s)
    {
	CF.add_function( String_Function(s));
	return CF;
    }

    ConcatFunction operator<<(const ConcatFunction& CF,const logger_function<string>& F)
    {
	ConcatFunction CF2 = CF;
	return CF2<<F;
    }

    ConcatFunction operator<<(const ConcatFunction& CF,const string& s)
    {
	ConcatFunction CF2 = CF;
	return CF2<<s;
    }

    ConcatFunction operator<<(const logger_function<string>& F1,const logger_function<string>& F2)
    {
	ConcatFunction CF;
	CF<<F1<<F2;
	return CF;
    }

    ConcatFunction operator<<(const logger_function<string>& F,const string& s)
    {
	ConcatFunction CF;
	CF<<F<<s;
	return CF;
    }


}
