//#define DEBUG_OPTIMIZE

#include <memory>
#include "computation/context.H"
#include "computation/machine/graph_register.H"
#include "computation/program.H"
#include "loader.H"
#include "module.H"
#include "parser/desugar.H"
#include "expression/expression.H"
#include "expression/lambda.H"
#include "expression/AST_node.H"
#include "expression/var.H"

using std::string;
using std::vector;
using std::map;
using std::pair;
using std::set;
using std::unique_ptr;
using boost::dynamic_pointer_cast;
using boost::optional;

using std::cerr;
using std::endl;

long total_create_context1 = 0;
long total_create_context2 = 0;

object_ptr<reg_heap>& context::memory() const {return memory_;}

const std::vector<int>& context::heads() const {return memory()->get_heads();}

std::vector<std::pair<std::string,int>>& context::parameters() const {return memory()->get_parameters();}

std::map<std::string, int>& context::identifiers() const {return memory()->get_identifiers();}

reg& context::access(int i) const {return memory()->access(i);}

reg& context::operator[](int i) const {return memory()->access(i);}

void context::set_C(int R, closure&& c) const {memory()->set_C(R,std::move(c));}

int context::allocate() const {return memory()->allocate();}

closure context::preprocess(const closure& C) const
{
    return memory()->preprocess(C);
}

string context::parameter_name(int i) const
{
    return parameters()[i].first;
}

int context::add_identifier(const string& name) const
{
    if (maybe_find_parameter(name))
	throw myexception()<<"Cannot add identifier '"<<name<<"': there is already a parameter with that name.";

    return memory()->add_identifier(name);
}

void context::rename_parameter(int i, const string& new_name)
{
    parameters()[i].first = new_name;
}

/// Return the value of a particular index, computing it if necessary
const closure& context::lazy_evaluate(int index) const
{
    return memory()->lazy_evaluate_head(index, context_index);
}

/// Return the value of a particular index, computing it if necessary
const expression_ref& context::evaluate(int index) const
{
    return lazy_evaluate(index).exp;
}

/// Return the value of a particular index, computing it if necessary
const expression_ref& context::perform(int index, bool ec) const
{
    int H = heads()[index];

    return perform_expression(reg_var(H), ec);
}

const closure& context::lazy_evaluate_expression_(closure&& C, bool ec) const
{
    try {
	int R = push_temp_head();
	set_C(R, std::move(C) );

	const closure& result = ec?memory()->lazy_evaluate(R, context_index) : memory()->lazy_evaluate_unchangeable(R);
    
	pop_temp_head();
	return result;
    }
    catch (myexception& e)
    {
	pop_temp_head();
	throw e;
    }
}

const expression_ref& context::evaluate_expression_(closure&& C,bool ec) const
{
    const expression_ref& result = lazy_evaluate_expression_(std::move(C),ec).exp;
#ifndef NDEBUG
    if (result.head().is_a<lambda2>())
	throw myexception()<<"Evaluating lambda as object: "<<result.print();
#endif
    return result;
}

const closure& context::lazy_evaluate_expression(const expression_ref& E, bool ec) const
{
    return lazy_evaluate_expression_( preprocess(E), ec);
}

const expression_ref& context::evaluate_expression(const expression_ref& E,bool ec) const
{
    return evaluate_expression_( preprocess(E), ec);
}

const expression_ref& context::perform_expression(const expression_ref& E,bool ec) const
{
    expression_ref E2 = {get_expression(perform_io_head),E};
    return evaluate_expression_( preprocess(E2), ec);
}

expression_ref context::recursive_evaluate_reg(int r) const
{
    closure C1 = memory()->lazy_evaluate(r, context_index);
    expression_ref E1 = deindexify(trim_unnormalize(C1));

    if (E1.is_atomic())
	return E1;

    unique_ptr<expression> E (new expression(E1.as_expression()));

    // Having finished with C, it is now safe to do evaluation.
    for(auto& e: E->sub)
    {
	if (e.is_index_var())
	{
	    int r = e.as_index_var();
	    e = recursive_evaluate_reg(r);
	}
	else if (e.is_a<reg_var>())
	{
	    int r = e.as_<reg_var>().target;
	    e = recursive_evaluate_reg(r);
	}
    }
    return expression_ref(std::move(E));
}

expression_ref context::recursive_evaluate_parameter(int i) const
{
    return recursive_evaluate_reg(get_parameter_reg(i));
}

expression_ref context::recursive_evaluate(int i) const
{
    return recursive_evaluate_reg(get_compute_expression_reg(i));
}

void context::perform_transition_kernel(int i)
{
    int r = memory()->transition_kernels()[i];
    expression_ref E = {reg_var(r), get_context_index()};
    perform_expression(E);
}

int context::n_transition_kernels() const
{
    return memory()->transition_kernels().size();
}

optional<int> context::parameter_is_modifiable_reg(int index) const
{
    return memory()->parameter_is_modifiable_reg(index);
}

int context::parameter_as_modifiable_reg(int index) const
{
    return memory()->parameter_as_modifiable_reg(index);
}

optional<int> context::compute_expression_is_modifiable_reg(int index) const
{
    return memory()->compute_expression_is_modifiable_reg(index);
}

int context::compute_expression_as_modifiable_reg(int index) const
{
    return memory()->compute_expression_as_modifiable_reg(index);
}

/// Get the value of a non-constant, non-computed index -- or should this be the nth parameter?
const expression_ref& context::get_reg_value(int R) const
{
    return memory()->get_reg_value_in_context(R, context_index);
}

/// Get the value of a non-constant, non-computed index -- or should this be the nth parameter?
const expression_ref& context::get_modifiable_value(int R) const
{
    R = get_modifiable_reg(R);

    return get_reg_value(R);
}

/// Get the value of a non-constant, non-computed index -- or should this be the nth parameter?
const expression_ref& context::get_parameter_value(int index) const
{
    return memory()->get_parameter_value_in_context(index, context_index);
}

/// Get the value of a non-constant, non-computed index
const expression_ref& context::get_parameter_value(const std::string& name) const
{
    auto index = find_parameter(name);

    return get_parameter_value(index);
}

void context::set_modifiable_value_(int R, closure&& C)
{
    R = get_modifiable_reg(R);

    set_reg_value(R, std::move(C) );
}

void context::set_modifiable_value(int R, const expression_ref& E)
{
    assert(not E.size());
    assert(not E.is_index_var());
    assert(not E.is_a<reg_var>());
    assert(not E.is_a<var>());
    set_modifiable_value_(R, E);
}

void context::set_parameter_value(int index, const expression_ref& E)
{
    assert(not E.size());
    assert(not E.is_index_var());
    assert(not E.is_a<reg_var>());
    assert(not E.is_a<var>());
    set_parameter_value_(index, E);
}

void context::set_parameter_value_(int index, closure&& C)
{
    assert(index >= 0);

    int P = parameter_as_modifiable_reg(index);

    set_reg_value(P, std::move(C) );
}

void context::set_reg_value(int P, closure&& C)
{
    memory()->set_reg_value_in_context(P, std::move(C), context_index);
}

/// Update the value of a non-constant, non-computed index
void context::set_parameter_value(const std::string& var, const expression_ref& O)
{
    int i = find_parameter(var);
    
    set_parameter_value(i, O);
}

int context::n_parameters() const
{
    return parameters().size();
}

optional<int> context::maybe_find_parameter(const string& s) const
{
    return memory()->maybe_find_parameter(s);
}

int context::find_parameter(const string& s) const
{
    return memory()->find_parameter(s);
}

int context::add_modifiable_parameter_with_value(const string& full_name, const expression_ref& value)
{
    int p = n_parameters();
    memory()->add_modifiable_parameter(full_name);

    set_parameter_value_(p, value);

    return p;
}

int context::add_parameter(const string& full_name, const expression_ref& E)
{
    int p = n_parameters();
    memory()->add_parameter(full_name, E);
    return p;
}

const vector<int>& context::random_modifiables() const
{
    return memory()->random_modifiables();
}

const expression_ref context::get_range_for_reg(int r) const
{
    return memory()->get_range_for_reg(context_index, r);
}

double context::get_rate_for_reg(int r) const
{
    return memory()->get_rate_for_reg(r);
}

const expression_ref context::get_parameter_range(int p) const
{
    return memory()->get_parameter_range(context_index, p);
}

/// Add an expression that may be replaced by its reduced form
int context::add_compute_expression(const expression_ref& E)
{
    return add_compute_expression_( preprocess(E) );
}

/// Add an expression that may be replaced by its reduced form
int context::add_compute_expression_(closure&& C)
{
    int R = memory()->allocate_head();

    set_C( R, std::move(C) );

    return heads().size() - 1;
}

/// Change the i-th compute expression to e
void context::set_compute_expression(int i, const expression_ref& E)
{
    set_compute_expression_( i, preprocess(E) );
}

/// Change the i-th compute expression to e
void context::set_compute_expression_(int i, closure&& C)
{
    int R = allocate();

    memory()->set_head(i, R);

    set_C( R, std::move(C) );
}

int context::n_expressions() const
{
    return heads().size();
}

expression_ref context::get_expression(int i) const
{
    int H = heads()[i];
    return reg_var(H);
}

void context::pop_temp_head() const
{
    memory()->pop_temp_head();
}

void context::compile()
{
}

log_double_t context::get_probability() const
{
    return memory()->probability_for_context(context_index);
}

int context::add_probability_factor(const expression_ref& E)
{
    return memory()->register_probability(preprocess(E));
}


void context::collect_garbage() const
{
    memory()->collect_garbage();
}

void context::show_graph() const
{
    get_probability();
    collect_garbage();
    int t = memory()->token_for_context(context_index);
    dot_graph_for_token(*memory(), t);
}

context& context::operator+=(const string& module_name)
{
    if (not get_Program().contains_module(module_name))
	(*this) += get_Program().get_module_loader()->load_module(module_name);

    return *this;
}

context& context::operator+=(const vector<string>& module_names)
{
    for(const auto& name: module_names)
	(*this) += name;

    return *this;
}

void context::allocate_identifiers_for_modules(const vector<string>& module_names)
{
    // 2. Give each identifier a pointer to an unused location; define parameter bodies.
    for(const auto& name: module_names)
    {
	const Module& M = get_Program().get_module(name);

	for(const auto& def: M.code_defs())
	    add_identifier(def.first);
    }
      
    // 3. Use these locations to translate these identifiers, at the cost of up to 1 indirection per identifier.
    for(const auto& name: module_names)
    {
	const Module& M = get_Program().get_module(name);

	for(const auto& def: M.code_defs())
	{
	    auto& name = def.first;

	    // get the root for each identifier
	    auto loc = identifiers().find(name);
	    assert(loc != identifiers().end());
	    int R = loc->second;

	    // get the body for the  decl
	    auto& body = def.second;

#ifdef DEBUG_OPTIMIZE
	    std::cerr<<name<<" := "<<body<<"\n\n";
	    std::cerr<<name<<" := "<<preprocess(body).exp<<"\n\n\n\n";
#endif

	    // load the body into the machine
	    assert(R != -1);
	    set_C(R, preprocess(body) );
	}
    }
}

// \todo FIXME:cleanup If we can make this only happen once, we can assume old_module_names is empty.
context& context::operator+=(const Module& M)
{
    Program& PP = get_Program();

    // Get module_names, but in a set<string>
    set<string> old_module_names = PP.module_names_set();

    // 1. Add the new modules to the program, add notes, perform imports, and resolve symbols.
    get_Program().add(M);

    // 2. Give each identifier a pointer to an unused location; define parameter bodies.
    vector<string> new_module_names;
    for(auto& module: PP)
	if (not old_module_names.count(module.name))
	    new_module_names.push_back(module.name);

    allocate_identifiers_for_modules(new_module_names);

    return *this;
}

context& context::operator=(const context& C)
{
    total_create_context1++;
    memory_->release_context(context_index);
  
    memory_ = C.memory_;
    context_index = memory_->copy_context(C.context_index);
    perform_io_head = C.perform_io_head;

    return *this;
}

context::context(const Program& P)
    :memory_(new reg_heap(P.get_module_loader())),
     context_index(memory_->get_unused_context())
{
    for(auto& M: P.modules())
	(*this) += M;

    // For Prelude.unsafePerformIO
    if (not P.size())
	(*this) += "Prelude";

    perform_io_head = add_compute_expression(var("Prelude.unsafePerformIO"));
}

context::context(const context& C)
    :memory_(C.memory_),
     context_index(memory_->copy_context(C.context_index)),
     perform_io_head(C.perform_io_head)
{
    total_create_context2++;
}

context::~context()
{
    memory_->release_context(context_index);
}

int context::push_temp_head() const
{
    return memory()->push_temp_head();
}

int context::get_parameter_reg(int index) const
{
    assert(index >= 0 and index < n_parameters());

    return parameters()[index].second;
}

int context::get_compute_expression_reg(int index) const
{
    assert(index >= 0 and index < heads().size());

    return heads()[index];
}

int context::get_modifiable_reg(int r) const
{
    assert(is_modifiable(access(r).C.exp));

    return r;
}

std::ostream& operator<<(std::ostream& o, const context& C)
{
    for(int index = 0;index < C.n_expressions(); index++)
    {
	o<<index<<" "<<C.get_expression(index);
	o<<"\n";
    }
    return o;
}

Program& context::get_Program()
{
    if (not memory()->P)
	throw myexception()<<"Program used after being cleared!";

    return *(memory()->P);
}

const Program& context::get_Program() const
{
    if (not memory()->P)
	throw myexception()<<"Program used after being cleared!";

    return *(memory()->P);
}

void context::clear_program()
{
    memory()->P.reset();
}

void context::clear_identifiers()
{
    memory()->identifiers.clear();
}

const vector<string>& context::get_args() const
{
    return memory()->args;
}

void context::set_args(const vector<string>& a)
{
    memory()->args = a;
}
