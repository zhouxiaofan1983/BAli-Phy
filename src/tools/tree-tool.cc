/*
  Copyright (C) 2005-2009 Benjamin Redelings

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

#include <iostream>
#include <list>
#include <utility>
#include "tree/tree.H"
#include "tree/sequencetree.H"
#include "tree/tree-util.H"
#include "tree-dist.H"
#include "myexception.H"
#include "util/assert.hh"

#include <boost/program_options.hpp>

using boost::dynamic_bitset;

namespace po = boost::program_options;
using po::variables_map;

using std::cout;
using std::cerr;
using std::endl;
using std::string;
using std::vector;
using std::list;
using std::valarray;
using std::pair;

using boost::optional;

variables_map parse_cmd_line(int argc,char* argv[]) 
{ 
    using namespace po;

    // named options
    options_description invisible("Invisible options");
    invisible.add_options()
	("tree", value<string>(),"tree to operate on");

    options_description general("General options");
    general.add_options()
	("help,h", "produce help message")
	("verbose,v","Output more log messages on stderr.")
	;

    options_description commands("Commands");
    commands.add_options()
	("prune",value<string>(),"Comma-separated taxa to remove")
	("resolve","Comma-separated taxa to remove")
	;
    options_description visible("All options");
    visible.add(general).add(commands);
    options_description all("All options");
    all.add(general).add(commands).add(invisible);

    // positional options
    positional_options_description p;
    p.add("tree", 1);
  
    variables_map args;     
    store(command_line_parser(argc, argv).
	  options(all).positional(p).run(), args);
    notify(args);    

    if (args.count("help")) {
	cout<<"Perform various operations on Newick trees.\n\n";
	cout<<"Usage: tree-tool <tree-file> [OPTIONS]\n\n";
	cout<<general<<"\n";
	cout<<commands<<"\n";
	exit(0);
    }

    if (args.count("verbose")) log_verbose = 1;

    return args;
}

void resolve(Tree& T, int node)
{
    while(true)
    {
	auto neighbors = T.neighbors(node);
	if (neighbors.size() <= 3) return;

	int new_node = T.create_node_on_branch(T.directed_branch(neighbors[0],node)).name();
	if (not T.directed_branch(new_node,node).has_length())
	    T.directed_branch(new_node,node).set_length(0.0);
	if (not T.directed_branch(new_node,neighbors[0]).has_length())
	    T.directed_branch(new_node,neighbors[0]).set_length(0.0);
	
	T.reconnect_branch(neighbors[1],node,new_node);
	assert(T.node(node).degree() < neighbors.size());
    }
}

vector<int> polytomies(const Tree& T)
{
    vector<int> p;
    for(int n=0;n<T.n_nodes();n++)
	if (T.node(n).degree() > 3)
	    p.push_back(n);
    return p;
}


int main(int argc,char* argv[]) 
{ 
    try {
	//----------- Parse command line  ----------//
	variables_map args = parse_cmd_line(argc,argv);

	vector<string> prune;
	if (args.count("prune")) {
	    string p = args["prune"].as<string>();
	    prune = split(p,',');
	}
      

	//----------- Read the topology -----------//
	SequenceTree T = load_T(args);

	if (args.count("resolve"))
	{
	    for(int n: polytomies(T))
		resolve(T,n);
	    assert(polytomies(T).empty());
	}
	std::cout<<T<<std::endl;
    }
    catch (std::exception& e) {
	std::cerr<<"tree-tool: Error! "<<e.what()<<endl;
	exit(1);
    }
    return 0;
}
