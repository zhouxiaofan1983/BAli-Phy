#include "parse.H"
#include "computation/module.H"
#include <deque>
#include <set>
#include <utility>
#include "io.H"
#include "models/parameters.H"
#include "computation/loader.H"
#include "computation/expression/expression.H"
#include "computation/expression/AST_node.H"
#include "computation/expression/apply.H"
#include "computation/expression/let.H"
#include "computation/expression/case.H"
#include "computation/expression/constructor.H"
#include "computation/expression/tuple.H"
#include "computation/expression/list.H"
#include "computation/expression/lambda.H"
#include "computation/expression/var.H"
#include "desugar.H"

using std::string;
using std::vector;
using std::set;
using std::deque;
using std::pair;

//  -----Prelude: http://www.haskell.org/onlinereport/standard-prelude.html

// See list in computation/loader.C
//

bool is_irrefutable_pat(const expression_ref& E)
{
    assert(is_AST(E,"pat"));

    return (E.size() == 1) and is_AST(E.sub()[0], "apat_var");
}


expression_ref infix_parse(const Module& m, const symbol_info& op1, const expression_ref& E1, deque<expression_ref>& T);

/// Expression is of the form ... op1 [E1 ...]. Get right operand of op1.
expression_ref infix_parse_neg(const Module& m, const symbol_info& op1, deque<expression_ref>& T)
{
    assert(not T.empty());

    expression_ref E1 = T.front();
    T.pop_front();

    // We are starting with a Neg
    if (E1.head() == AST_node("neg"))
    {
	if (op1.precedence >= 6) throw myexception()<<"Cannot parse '"<<op1.name<<"' -";

	E1 = infix_parse_neg(m, symbol_info("-",variable_symbol, 2,6,left_fix), T);

	return infix_parse(m, op1, {var("Prelude.negate"),E1}, T);
    }
    // If E1 is not a neg, E1 should be an expression, and the next thing should be an Op.
    else
	return infix_parse(m, op1, E1, T);
}

/// Expression is of the form ... op1 E1 [op2 ...]. Get right operand of op1.
expression_ref infix_parse(const Module& m, const symbol_info& op1, const expression_ref& E1, deque<expression_ref>& T)
{
    if (T.empty())
	return E1;

    symbol_info op2;
    if (T.front().is_a<var>())
    {
	auto d = T.front().as_<var>().name;
	if (m.is_declared( d ) )
	    op2 = m.get_operator( d );
	else
	{
	    op2.precedence = 9;
	    op2.fixity = left_fix;
	}
    }
    else
	throw myexception()<<"Can't use expression '"<<T.front().print()<<"' as infix operator.";

    // illegal expressions
    if (op1.precedence == op2.precedence and (op1.fixity != op2.fixity or op1.fixity == non_fix))
	throw myexception()<<"Must use parenthesis to order operators '"<<op1.name<<"' and '"<<op2.name<<"'";

    // left association: ... op1 E1) op2 ...
    if (op1.precedence > op2.precedence or (op1.precedence == op2.precedence and op1.fixity == left_fix))
	return E1;

    // right association: .. op1 (E1 op2 {...E3...}) ...
    else
    {
	T.pop_front();
	expression_ref E3 = infix_parse_neg(m, op2, T);

	expression_ref E1_op2_E3 = {var(op2.name), E1, E3};

	if (op2.symbol_type == constructor_symbol)
	{
	    assert(op2.arity == 2);
	    E1_op2_E3 = constructor(op2.name, 2) + E1 + E3;
	}

	return infix_parse(m, op1, E1_op2_E3, T);
    }
}

expression_ref desugar_infix(const Module& m, const vector<expression_ref>& T)
{
    deque<expression_ref> T2;
    T2.insert(T2.begin(), T.begin(), T.end());

    return infix_parse_neg(m, {"",variable_symbol,2,-1,non_fix}, T2);
}

expression_ref infixpat_parse(const Module& m, const symbol_info& op1, const expression_ref& E1, deque<expression_ref>& T);

/// Expression is of the form ... op1 [E1 ...]. Get right operand of op1.
expression_ref infixpat_parse_neg(const Module& m, const symbol_info& op1, deque<expression_ref>& T)
{
    assert(not T.empty());

    expression_ref E1 = T.front();
    T.pop_front();

    // We are starting with a Neg float
    if (E1.head() == AST_node("neg_h_float"))
    {
	if (op1.precedence >= 6) throw myexception()<<"Cannot parse '"<<op1.name<<"' -";

	double d = E1.sub()[0].as_double();
	d = -d;

	return infixpat_parse(m, op1, d, T);
    }
    // We are starting with a Neg integer
    else if (E1.head() == AST_node("neg_h_integer"))
    {
	if (op1.precedence >= 6) throw myexception()<<"Cannot parse '"<<op1.name<<"' -";

	int I = E1.sub()[0].as_int();
	I = -I;

	return infixpat_parse(m, op1, I, T);
    }
    // If E1 is not a neg, E1 should be an expression, and the next thing should be an Op.
    else
	return infixpat_parse(m, op1, E1, T);
}

/// Expression is of the form ... op1 E1 [op2 ...]. Get right operand of op1.
expression_ref infixpat_parse(const Module& m, const symbol_info& op1, const expression_ref& E1, deque<expression_ref>& T)
{
    if (T.empty())
	return E1;

    symbol_info op2;
    if (is_var(T.front()))
    {
	auto d = T.front().as_<var>().name;
	if (m.is_declared( d ) )
	    op2 = m.get_operator( d );
	else
	{
	    op2.precedence = 9;
	    op2.fixity = left_fix;
	}
    }
    else if (T.front().head().is_a<AST_node>())
    {
	auto& n = T.front().head().as_<AST_node>();
	// FIXME:correctness - each "Decls"-frame should first add all defined variables to bounds, which should contain symbol_infos.
	if (n.type == "id")
	{
	    string name = n.value;
	    if (m.is_declared( name ) )
		op2 = m.get_operator( name );
	    else
	    {
		op2.precedence = 9;
		op2.fixity = left_fix;
	    }
	}
	else
	    throw myexception()<<"Can't use expression '"<<T.front().print()<<"' as infix operator.";
    }
    else
	throw myexception()<<"Can't use expression '"<<T.front().print()<<"' as infix operator.";


    // illegal expressions
    if (op1.precedence == op2.precedence and (op1.fixity != op2.fixity or op1.fixity == non_fix))
	throw myexception()<<"Must use parenthesis to order operators '"<<op1.name<<"' and '"<<op2.name<<"'";

    // left association: ... op1 E1) op2 ...
    if (op1.precedence > op2.precedence or (op1.precedence == op2.precedence and op1.fixity == left_fix))
	return E1;

    // right association: .. op1 (E1 op2 {...E3...}) ...
    else
    {
	T.pop_front();
	expression_ref E3 = infixpat_parse_neg(m, op2, T);

	if (op2.symbol_type != constructor_symbol)
	    throw myexception()<<"Using non-constructor operator '"<<op2.name<<"' in pattern is not allowed.";
	if (op2.arity != 2)
	    throw myexception()<<"Using constructor operator '"<<op2.name<<"' with arity '"<<op2.arity<<"' is not allowed.";
	expression_ref constructor_pattern = constructor(op2.name, 2) + E1 + E3;

	return infixpat_parse(m, op1, constructor_pattern, T);
    }
}

expression_ref desugar_infixpat(const Module& m, const vector<expression_ref>& T)
{
    deque<expression_ref> T2;
    T2.insert(T2.begin(), T.begin(), T.end());

    return infixpat_parse_neg(m, {"",variable_symbol,2,-1,non_fix}, T2);
}

set<string> find_bound_vars(const expression_ref& E)
{
    if (is_AST(E, "apat_var"))
	return {E.as_<AST_node>().value};

    if (not E.size()) return set<string>();

    set<string> bound;
    for(const auto& e: E.sub())
	add(bound, find_bound_vars(e));

    return bound;
}

set<string> find_all_ids(const expression_ref& E)
{
    if (is_AST(E,"id"))
	return {E.as_<AST_node>().value};

    if (E.is_atomic()) return set<string>();

    set<string> bound;
    for(const auto& e:E.sub())
	add(bound, find_all_ids(e));

    return bound;
}

expression_ref make_apply(const vector<expression_ref>& v)
{
    assert(not v.empty());
    expression_ref E = v[0];
    for(int i=1;i<v.size();i++)
	E = {E,v[i]};
    return E;
}

bool is_function_binding(const expression_ref& decl)
{
    if (not is_AST(decl,"Decl"))
	return false;

    expression_ref lhs = decl.sub()[0];
    assert(not is_AST(lhs,"funlhs2"));
    assert(not is_AST(lhs,"funlhs3"));
    return is_AST(lhs,"funlhs1");
}

bool is_pattern_binding(const expression_ref& decl)
{
    return is_AST(decl,"Decl") and not is_function_binding(decl);
}

set<string> get_pattern_bound_vars(const expression_ref& decl)
{
    assert(is_AST(decl,"Decl"));

    expression_ref lhs = decl.sub()[0];

    return find_bound_vars(lhs);
}

string get_func_name(const expression_ref& decl)
{
    assert(is_AST(decl,"Decl"));

    expression_ref lhs = decl.sub()[0];
    assert(is_AST(lhs,"funlhs1"));

    expression_ref name = lhs.sub()[0];

    if (name.is_a<var>())
	return name.as_<var>().name;
    else if (name.head().is_a<AST_node>())
    {
	assert(is_AST(name,"id"));
	return name.head().as_<AST_node>().value;
    }
    else
	std::abort();
}

vector<expression_ref> get_patterns(const expression_ref& decl)
{
    assert(is_AST(decl,"Decl"));

    expression_ref lhs = decl.sub()[0];
    assert(is_AST(lhs,"funlhs1"));

    vector<expression_ref> patterns = lhs.sub();
    patterns.erase(patterns.begin());
    return patterns;
}

expression_ref get_body(const expression_ref& decl)
{
    expression_ref rhs = decl.sub()[1];
    assert(rhs.size() == 1);
    return rhs.sub()[0];
}

expression_ref translate_funlhs(const expression_ref& E)
{
    if (is_AST(E,"funlhs1"))
	return E;
    else if (is_AST(E,"funlhs2"))
    {
	// Let's just ignore pat elements here -- they can be fixed up by desugar, I think.

	// TODO: We want to look at the infix patterns here to make sure that operator parses as the top-level element.
	//       In cases like x:y +++ z the parser should identify that +++ is the function because it isn't a constructor.
	//       What if the constructor is an imported symbol?  Then we need to export +++ before we can check the infix parsing.
    
	return AST_node("funlhs1") + E.sub()[1] + E.sub()[0] + E.sub()[2];
    }
    else if (is_AST(E,"funlhs3"))
    {
	expression_ref fun1 = translate_funlhs(E.sub()[0]);
	vector<expression_ref> args = fun1.sub();
	for(int i=1;i<E.size();i++)
	    fun1 = fun1 + E.sub()[i];
	return fun1;
    }
    else
	return {};
}

expression_ref translate_funlhs_decl(const expression_ref& E)
{
    if (expression_ref funlhs = translate_funlhs(E.sub()[0]))
    {
	vector<expression_ref> sub = E.sub();
	sub[0] = funlhs;
	return expression_ref{E.head(),sub};
    }
    else
	return E;
}

vector<expression_ref> parse_fundecls(const vector<expression_ref>& v)
{
    // Now we go through and translate groups of FunDecls.
    vector<expression_ref> decls;
    for(int i=0;i<v.size();i++)
    {
	if (not is_AST(v[i],"Decl"))
	{
	    decls.push_back(v[i]);
	    continue;
	}

	// If its not a function binding, accept it as is, and continue.
	if (v[i].sub()[0].is_a<var>())
	    decls.push_back(v[i].head() + v[i].sub()[0] + v[i].sub()[1].sub()[0]);
	else if (is_AST(v[i].sub()[0], "funlhs1"))
	{
	    vector<vector<expression_ref> > patterns;
	    vector<expression_ref> bodies;
	    string name = get_func_name(v[i]);
	    patterns.push_back( get_patterns(v[i]) );
	    bodies.push_back( get_body(v[i]) );

	    for(int j=i+1;j<v.size();j++)
	    {
		if (not is_function_binding(v[j])) break;
		if (get_func_name(v[j]) != name) break;

		patterns.push_back( get_patterns(v[j]) );
		bodies.push_back( get_body(v[j]) );

		if (patterns.back().size() != patterns.front().size())
		    throw myexception()<<"Function '"<<name<<"' has different numbers of arguments!";
	    }
	    decls.push_back(AST_node("Decl") + var(name) + def_function(patterns,bodies) );

	    // skip the other bindings for this function
	    i += (patterns.size()-1);
	}
	else
	    std::abort();
    }
    return decls;
}

expression_ref get_fresh_id(const string& s, const expression_ref& /* E */)
{
    return AST_node("id",s);
}

/*
 * We probably want to move away from using dummies to represent patterns.
 * - Dummies can't represent e.g. irrefutable patterns.
 */

// What would be involved in moving the renamer to a kind of phase 2?
// How do we get the exported symbols before we do the desugaring that depends on imports?

expression_ref desugar(const Module& m, const expression_ref& E, const set<string>& bound)
{
    vector<expression_ref> v;
    if (E.is_expression())
	v = E.sub();
      
    if (E.head().is_a<AST_node>())
    {
	auto& n = E.head().as_<AST_node>();
	if (n.type == "infixexp")
	{
	    vector<expression_ref> args = E.sub();
	    while(is_AST(args.back(),"infixexp"))
	    {
		expression_ref E2 = args.back();
		args.pop_back();
		args.insert(args.end(),E2.sub().begin(),E2.sub().end());
	    }
	    for(auto& e: args)
		e = desugar(m, e, bound);
	    return desugar_infix(m, args);
	}
	else if (n.type == "pat")
	{
	    // 1. Collect the entire pat expression in 'args'.
	    vector<expression_ref> args = v;
	    while(is_AST(args.back(),"pat"))
	    {
		expression_ref rest = args.back();
		args.pop_back();
		args.insert(args.end(), rest.sub().begin(), rest.sub().end());
	    }

	    // 2. We could probably do this later.
	    for(auto& arg: args)
		arg = desugar(m, arg, bound);

	    return desugar_infixpat(m, args);
	}
	else if (n.type == "Tuple")
	{
	    for(auto& e: v)
		e = desugar(m, e, bound);
	    return get_tuple(v);
	}
	else if (n.type == "List")
	{
	    for(auto& e: v)
		e = desugar(m, e, bound);
	    return get_list(v);
	}
	else if (n.type == "Decls" or n.type == "TopDecls")
	{
	    set<string> bound2 = bound;
	    bool top = is_AST(E,"TopDecls");

	    // Find all the names bound here
	    for(auto& e: v)
	    {
		if (not is_AST(e,"Decl")) continue;

		// Translate funlhs2 and funlhs3 declaration forms to funlhs1 form.
		e = translate_funlhs_decl(e);

		// Bind the function id to avoid errors on the undeclared id later.
		if (is_function_binding(e))
		{
		    string name = get_func_name(e);
		    if (top)
		    {
			assert(not is_qualified_symbol(name));
			string qualified_name = m.name + "." + name;
			bound2.insert(qualified_name);
		    }
		    else
			bound2.insert(name);
		}
		else if (is_pattern_binding(e))
		{
		    auto vars = get_pattern_bound_vars(e);
		    set<string> vars2;
		    if (top)
			for(auto& name: vars)
			{
			    assert(not is_qualified_symbol(name));
			    string qualified_name = m.name + "." + name;
			    vars2.insert(qualified_name);
			}
		    else
			vars2 = vars;
		    add(bound2, vars2);
		}
	    }

	    // Replace ids with dummies
	    for(auto& e: v)
	    {
		if (is_AST(e,"FixityDecl")) continue;
		e = desugar(m, e, bound2);
	    }

	    // Convert fundecls to normal decls
	    vector<expression_ref> decls = parse_fundecls(v);
	    if (top)
	    {
		vector<expression_ref> new_decls;
		for(auto& decl: decls)
		{
		    if (is_AST(decl,"Decl") and decl.size() == 2)
		    {
			auto x = decl.sub()[0].as_<var>();
			if (not is_qualified_symbol(x.name))
			    x.name = m.name + "." + x.name;
			new_decls.push_back(AST_node("Decl") + x + decl.sub()[1]);
		    }
		    else
			new_decls.push_back(decl);
		}
		decls = new_decls;
	    }

	    return expression_ref{E.head(),decls};
	}
	else if (n.type == "Decl")
	{
	    // Is this a set of function bindings?
	    if (is_AST(v[0], "funlhs1"))
	    {
		set<string> bound2 = bound;
		for(const auto& e: v[0].sub())
		    add(bound2, find_bound_vars(e));

		// Replace bound vars in (a) the patterns and (b) the body
		for(auto& e: v)
		    e = desugar(m, e, bound2);

		return expression_ref{E.head(),v};
	    }

	    // Is this a set of pattern bindings?

	    /*
	     * Wait.... so we want to do a recursive de-sugaring, but we can't do that because we 
	     * don't know the set of bound variables yet.
	     */
	}
	else if (n.type == "rhs")
	{
	    if (E.size() == 2)
	    {
		expression_ref decls = E.sub()[1];
		assert(is_AST(decls,"Decls"));
		expression_ref E2 = AST_node("Let") + decls + E.sub()[0];
		E2 = AST_node("rhs") + E2;
		return desugar(m,E2,bound);
	    }
	    else
	    { }      // Fall through and let the standard case handle this.
	}
	else if (n.type == "apat_var")
	{
	    return var(n.value);
	}
	else if (n.type == "WildcardPattern")
	{
	    return var(-1);
	}
	else if (n.type == "id")
	{
	    // Local vars bind id's tighter than global vars.
	    if (includes(bound,n.value))
		return var(n.value);
	    // If the variable is free, then try top-level names.
	    else if (m.is_declared(n.value))
	    {
		const symbol_info& S = m.lookup_symbol(n.value);
		string qualified_name = S.name;
		return var(qualified_name);
	    }
	    else
		throw myexception()<<"Can't find id '"<<n.value<<"'";
	}
	else if (n.type == "Apply")
	{
	    for(auto& e: v)
		e = desugar(m, e, bound);

	    expression_ref E2 = v[0];

	    for(int i=1;i<v.size();i++)
		E2 = {E2,v[i]};
	    return E2;
	}
	else if (n.type == "ListComprehension")
	{
	    expression_ref E2 = E;
	    // [ e | True   ]  =  [ e ]
	    // [ e | q      ]  =  [ e | q, True ]
	    // [ e | b, Q   ]  =  if b then [ e | Q ] else []
	    // [ e | p<-l, Q]  =  let {ok p = [ e | Q ]; ok _ = []} in Prelude.concatMap ok l
	    // [ e | let decls, Q] = let decls in [ e | Q ]

	    expression_ref True = AST_node("SimpleQual") + constructor("Prelude.True",0);

	    assert(v.size() >= 2);
	    if (v.size() == 2 and (v[1] == True))
		E2 = AST_node("List") + v[0];
	    else if (v.size() == 2)
		E2 = E.head() + v[0] + v[1] + True;
	    else 
	    {
		expression_ref B = v[1];
		v.erase(v.begin()+1);
		E2 = expression_ref{E.head(),v};

		if (is_AST(B, "SimpleQual"))
		    E2 = AST_node("If") + B.sub()[0] + E2 + AST_node("id","[]");
		else if (is_AST(B, "PatQual"))
		{
		    expression_ref p = B.sub()[0];
		    expression_ref l = B.sub()[1];
		    if (is_irrefutable_pat(p))
		    {
			expression_ref f  = AST_node("Lambda") + p + E2;
			E2 = AST_node("Apply") + AST_node("id","Prelude.concatMap") + f + l;
		    }
		    else
		    {
			// Problem: "ok" needs to be a fresh variable.
			expression_ref ok = get_fresh_id("ok",E);

			expression_ref lhs1 = AST_node("funlhs1") + ok + p;
			expression_ref rhs1 = AST_node("rhs") + E2;
			expression_ref decl1 = AST_node("Decl") + lhs1 + rhs1;

			expression_ref lhs2 = AST_node("funlhs1") + ok + AST_node("WildcardPattern");
			expression_ref rhs2 = AST_node("rhs") + AST_node("id","[]");
			expression_ref decl2 = AST_node("Decl") + lhs2 + rhs2;

			expression_ref decls = AST_node("Decls") + decl1 + decl2;
			expression_ref body = AST_node("Apply") + AST_node("id","Prelude.concatMap") + ok + l;

			E2 = AST_node("Let") + decls + body;
		    }
		}
		else if (is_AST(B, "LetQual"))
		    E2 = AST_node("Let") + B.sub()[0] + E2;
	    }
	    return desugar(m,E2,bound);
	}
	else if (n.type == "Lambda")
	{
	    // FIXME: Try to preserve argument names (in block_case( ), probably) when they are irrefutable apat_var's.

	    // 1. Extract the lambda body
	    expression_ref body = v.back();
	    v.pop_back();

	    // 2. Find bound vars and convert vars to dummies.
	    set<string> bound2 = bound;
	    for(auto& e: v) {
		add(bound2, find_bound_vars(e));
		e = desugar(m, e, bound);
	    }

	    // 3. Desugar the body, binding vars mentioned in the lambda patterns.
	    body = desugar(m, body, bound2);

	    return def_function({v},{body}); 
	}
	else if (n.type == "Do")
	{
	    assert(is_AST(E.sub()[0],"Stmts"));
	    vector<expression_ref> stmts = E.sub()[0].sub();

	    // do { e }  =>  e
	    if (stmts.size() == 1) 
		return desugar(m,stmts[0],bound);

	    expression_ref first = stmts[0];
	    stmts.erase(stmts.begin());
	    expression_ref do_stmts = AST_node("Do") + expression_ref(AST_node("Stmts"),stmts);
	    expression_ref result;
      
	    // do {e ; stmts }  =>  e >> do { stmts }
	    if (is_AST(first,"SimpleStmt"))
	    {
		expression_ref e = first.sub()[0];
		expression_ref qop = AST_node("id","Prelude.>>");
		result = AST_node("infixexp") + e + qop + do_stmts;
	    }

	    // do { p <- e ; stmts} => let {ok p = do {stmts}; ok _ = fail "..."} in e >>= ok
	    // do { v <- e ; stmts} => e >>= (\v -> do {stmts})
	    else if (is_AST(first,"PatStmt"))
	    {
		expression_ref p = first.sub()[0];
		expression_ref e = first.sub()[1];
		expression_ref qop = AST_node("id","Prelude.>>=");

		if (is_irrefutable_pat(p))
		{
		    expression_ref lambda = AST_node("Lambda") + p + do_stmts;
		    result = AST_node("infixexp") + e + qop + lambda;
		}
		else
		{
		    expression_ref fail = AST_node("Apply") + AST_node("id","Prelude.fail") + "Fail!";
		    expression_ref ok = get_fresh_id("ok",E);
	  
		    expression_ref lhs1 = AST_node("funlhs1") + ok + p;
		    expression_ref rhs1 = AST_node("rhs") + do_stmts;
		    expression_ref decl1 = AST_node("Decl") + lhs1 + rhs1;
	  
		    expression_ref lhs2 = AST_node("funlhs1") + ok + AST_node("WildcardPattern");
		    expression_ref rhs2 = AST_node("rhs") + fail;
		    expression_ref decl2 = AST_node("Decl") + lhs2 + rhs2;
		    expression_ref decls = AST_node("Decls") + decl1 +  decl2;

		    expression_ref body = AST_node("infixexp") + e + qop + ok;

		    result = AST_node("Let") + decls + body;
		}
	    }
	    // do {let decls ; rest} = let decls in do {stmts}
	    else if (is_AST(first,"LetStmt"))
	    {
		expression_ref decls = first.sub()[0];
		result = AST_node("Let") + decls + do_stmts;
	    }
	    else if (is_AST(first,"EmptyStmt"))
		result = do_stmts;

	    return desugar(m,result,bound);
	}
	else if (n.type == "constructor_pattern")
	{
	    string gcon = v[0].as_<String>();
	    v.erase(v.begin());

	    // If the variable is free, then try top-level names.
	    if (not m.is_declared(gcon))
		throw myexception()<<"Constructor pattern '"<<E<<"' has unknown constructor '"<<gcon<<"'";

	    const symbol_info& S = m.lookup_symbol(gcon);
	    if (S.symbol_type != constructor_symbol)
		throw myexception()<<"Constructor pattern '"<<E<<"' has head '"<<gcon<<"' which is not a constructor!";

	    if (S.arity != v.size())
		throw myexception()<<"Constructor pattern '"<<E<<"' has arity "<<v.size()<<" which does not equal "<<S.arity<<".";

	    // Desugar the 
	    for(auto& e: v)
		e = desugar(m, e, bound);

	    if (v.size())
		return expression_ref{ constructor(S.name,S.arity), v };
	    else
		return constructor(S.name,S.arity);
	}
	else if (n.type == "If")
	{
	    for(auto& e: v)
		e = desugar(m, e, bound);

	    return case_expression(v[0],true,v[1],v[2]);
	}
	else if (n.type == "LeftSection")
	{
	    // FIXME... the infixexp needs to parse the same as if it was parenthesized.
	    // FIXME... probably we need to do a disambiguation on the infix expression. (infixexp op x)
	    std::set<var> free_vars;
	    for(auto& e: v) {
		e = desugar(m, e, bound);
		add(free_vars, get_free_indices(e));
	    }
	    return apply_expression(v[1],v[0]);
	}
	else if (n.type == "RightSection")
	{
	    // FIXME... probably we need to do a disambiguation on the infix expression. (x op infixexp)
	    // FIXME... the infixexp needs to parse the same as if it was parenthesized.
	    std::set<var> free_vars;
	    for(auto& e: v) {
		e = desugar(m, e, bound);
		add(free_vars, get_free_indices(e));
	    }
	    int safe_var_index = 0;
	    if (not free_vars.empty())
		safe_var_index = max_index(free_vars)+1;
	    var vsafe(safe_var_index);
	    return lambda_quantify(vsafe,apply_expression(apply_expression(v[0],vsafe),v[1]));
	}
	else if (n.type == "Let")
	{
	    expression_ref decls_ = v[0];
	    assert(is_AST(decls_,"Decls"));
	    expression_ref body = v[1];

	    // transform "let (a,b) = E in F" => "case E of (a,b) -> F"
	    {
		expression_ref decl = decls_.sub()[0];
		if (is_AST(decl,"Decl"))
		{
		    expression_ref pat = decl.sub()[0];
		    expression_ref rhs = decl.sub()[1];
		    // let pat = rhs in body -> case rhs of {pat->body}
		    if (is_AST(pat,"pat"))
		    {
			assert(is_AST(rhs,"rhs"));
			expression_ref pat0 = pat.sub()[0];
			if (is_AST(pat0,"Tuple"))
			{
			    if (decls_.size() != 1) throw myexception()<<"Can't currently handle pattern let with more than one decl.";
			    expression_ref alt = AST_node("alt") + pat + body;
			    expression_ref alts = AST_node("alts") + alt;
			    expression_ref EE = AST_node("Case") + rhs.sub()[0] + alts;
			    return desugar(m, EE, bound);
			}
		    }
		}
	    }

	    // parse the decls and bind declared names internally to the decls.
	    v[0] = desugar(m, v[0], bound);

	    vector<pair<var,expression_ref>> decls;

	    // find the bound var names + construct arguments to let_obj()
	    set<string> bound2 = bound;
	    for(const auto& decl: v[0].sub())
	    {
		if (is_AST(decl,"EmptyDecl")) continue;

		var x = decl.sub()[0].as_<var>().name;
		auto E = decl.sub()[1];

		decls.push_back({x,E});
		bound2.insert(x.name);
	    }

	    // finally desugar let-body, now that we know the bound vars.
	    body = desugar(m, body, bound2);

	    // construct the new let expression.
	    return let_expression(decls, body);
	}
	else if (n.type == "Case")
	{
	    expression_ref case_obj = desugar(m, v[0], bound);
	    vector<expression_ref> alts = v[1].sub();
	    vector<expression_ref> patterns;
	    vector<expression_ref> bodies;
	    for(const auto& alt: alts)
	    {
		set<string> bound2 = bound;
		add(bound2, find_bound_vars(alt.sub()[0]));
		patterns.push_back(desugar(m, alt.sub()[0], bound2) );

		// Handle where-clause.
		assert(alt.size() == 2 or alt.size() == 3);
		expression_ref body = alt.sub()[1];

		if (is_AST(body,"GdPat"))
		    throw myexception()<<"Guard patterns not yet implemented!";

		if (alt.size() == 3)
		{
		    assert(is_AST(alt.sub()[2],"Decls"));
		    body = AST_node("Let") + alt.sub()[2] + body;
		}

		bodies.push_back(desugar(m, body, bound2) );
	    }
	    return case_expression(case_obj, patterns, bodies);
	}
	else if (n.type == "enumFrom")
	{
	    expression_ref E2 = var("Prelude.enumFrom");
	    for(auto& e: v) {
		e = desugar(m, e, bound);
		E2 = {E2,e};
	    }
	    return E2;
	}
	else if (n.type == "enumFromTo")
	{
	    expression_ref E2 = var("Prelude.enumFromTo");
	    for(auto& e: v) {
		e = desugar(m, e, bound);
		E2 = {E2,e};
	    }
	    return E2;
	}
    }

    for(auto& e: v)
	e = desugar(m, e, bound);
    if (E.size())
	return expression_ref{E.head(),v};
    else
	return E;
}

expression_ref desugar(const Module& m, const expression_ref& E)
{
    return desugar(m,E,set<string>{});
}

expression_ref parse_haskell_line(const Module& P, const string& line)
{
    return desugar(P, parse_haskell_line(line));
}

bool is_all_space(const string& line)
{
    for(int i=0;i<line.size();i++)
	if (not ::isspace(line[i])) return false;
    return true;
}

Module read_model(const string& filename)
{
    // 1. Read module
    Module M ( module_loader({}).read_module_from_file(filename) );

    return M;
}

void read_add_model(Model& M, const std::string& filename)
{
    auto m = read_model(filename);
    M += m;
    add_model(M, m.name);
}

void add_model(Model& M, const std::string& name)
{
    M += name;
    string prefix = name;
    M.perform_expression({var("Distributions.do_log"),prefix,{var("Distributions.gen_model"),var(name+".main")}});
}
