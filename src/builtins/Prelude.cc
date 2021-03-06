#pragma clang diagnostic ignored "-Wreturn-type-c-linkage"
#include "computation/computation.H"
#include "computation/expression/expression.H"

using boost::dynamic_pointer_cast;
using std::string;

extern "C" closure builtin_function_exp(OperationArgs& Args)
{
    double x = Args.evaluate(0).as_double();

    return {exp(x)};
}

extern "C" closure builtin_function_log(OperationArgs& Args)
{
    auto x = Args.evaluate(0);

    if (x.is_double())
    {
	double xx = x.as_double();
	assert(xx > 0.0);
	return {log(xx)};
    }
    else if (x.is_int())
    {
	double xx = x.as_int();
	assert(xx > 0.0);
	return {log(xx)};
    }
    else if (x.is_log_double())
    {
	log_double_t xx = x.as_log_double();
	return {log(xx)};
    }

    throw myexception()<<"log: object '"<<x.print()<<"' is not double, int, or log_double";
}

extern "C" closure builtin_function_pow(OperationArgs& Args)
{
    auto x = Args.evaluate(0);
    auto y = Args.evaluate(1);

    double yy = 0;
    if (y.is_double())
	yy = y.as_double();
    else if (y.is_int())
	yy = y.as_int();
    else
	throw myexception()<<"pow: exponent '"<<x.print()<<"' is not double or int";
    
    if (x.is_double())
    {
	double xx = x.as_double();
	assert(xx > 0.0);
	return {pow(xx,yy)};
    }
    else if (x.is_int())
    {
	double xx = x.as_int();
	assert(xx > 0.0);
	return {pow(xx,yy)};
    }
    else if (x.is_log_double())
    {
	log_double_t xx = x.as_log_double();
	return {pow(xx,yy)};
    }

    throw myexception()<<"pow: object '"<<x.print()<<"' is not double, int, or log_double";
}

extern "C" closure builtin_function_sqrt(OperationArgs& Args)
{
    double x = Args.evaluate(0).as_double();
    assert(x >= 0.0);

    return {sqrt(x)};
}

extern "C" closure builtin_function_truncate(OperationArgs& Args)
{
    double x = Args.evaluate(0).as_double();

    return {trunc(x)};
}

extern "C" closure builtin_function_ceiling(OperationArgs& Args)
{
    double x = Args.evaluate(0).as_double();

    return {ceil(x)};
}

extern "C" closure builtin_function_floor(OperationArgs& Args)
{
    double x = Args.evaluate(0).as_double();
    assert(x > 0.0);

    return {floor(x)};
}


extern "C" closure builtin_function_round(OperationArgs& Args)
{
    double x = Args.evaluate(0).as_double();
    assert(x > 0.0);

    return {round(x)};
}

extern "C" closure builtin_function_doubleToInt(OperationArgs& Args)
{
    double x = Args.evaluate(0).as_double();
    int xi = (int)x;
    return {xi};
}

extern "C" closure builtin_function_vector_from_list(OperationArgs& Args)
{
    object_ptr<EVector> v (new EVector);

    const closure* top = &Args.evaluate_slot_to_closure(0);
    while(top->exp.size())
    {
	assert(has_constructor(top->exp,":"));
	assert(top->exp.size() == 2);

	int element_index = top->exp.sub()[0].as_index_var();
	int element_reg = top->lookup_in_env( element_index );

	int next_index = top->exp.sub()[1].as_index_var();
	int next_reg = top->lookup_in_env( next_index );

	// Add the element to the list.
	v->push_back( Args.evaluate_reg_to_object(element_reg) );
	// Move to the next element or end
	top = &Args.evaluate_reg_to_closure(next_reg);
    }
    assert(has_constructor(top->exp,"[]"));

    return v;
}

extern "C" closure builtin_function_add(OperationArgs& Args)
{
    auto x = Args.evaluate(0);
    auto y = Args.evaluate(1);
  
    if (x.is_double())
	return {x.as_double() + y.as_double()};
    else if (x.is_int())
	return {x.as_int() + y.as_int()};
    else if (x.is_log_double())
	return {x.as_log_double() + y.as_log_double()};
    else if (x.is_char())
	return {x.as_char() + y.as_char()};
    else
	throw myexception()<<"Add: object '"<<x.print()<<"' is not double, int, log_double, or char'";
}

extern "C" closure builtin_function_multiply(OperationArgs& Args)
{
    auto x = Args.evaluate(0);
    auto y = Args.evaluate(1);
  
    if (x.is_double())
	return {x.as_double() * y.as_double()};
    else if (x.is_int())
	return {x.as_int() * y.as_int()};
    else if (x.is_log_double())
	return {x.as_log_double() * y.as_log_double()};
    else if (x.is_char())
	return {x.as_char() * y.as_char()};
    else
	throw myexception()<<"Multiply: object '"<<x.print()<<"' is not double, int, log_double, or char'";
}

extern "C" closure builtin_function_divide(OperationArgs& Args)
{
    auto x = Args.evaluate(0);
    auto y = Args.evaluate(1);
  
    if (x.is_double())
	return {x.as_double() / y.as_double()};
    else if (x.is_int())
	return {x.as_int() / y.as_int()};
    else if (x.is_log_double())
	return {x.as_log_double() / y.as_log_double()};
    else if (x.is_char())
	return {x.as_char() / y.as_char()};
    else
	throw myexception()<<"Divide: object '"<<x.print()<<"' is not double, int, log_double, or char'";
}

extern "C" closure builtin_function_mod(OperationArgs& Args)
{
    using boost::dynamic_pointer_cast;

    auto x = Args.evaluate(0);
    auto y = Args.evaluate(1);
  
    if (x.is_int())
	return { x.as_int() % y.as_int() };
    else if (x.is_char())
	return { x.as_char() % y.as_char() };
    else
	throw myexception()<<"Mod: object '"<<x.print()<<"' is not int, or char'";
}

extern "C" closure builtin_function_subtract(OperationArgs& Args)
{
    auto x = Args.evaluate(0);
    auto y = Args.evaluate(1);
  
    if (x.is_double())
	return {x.as_double() - y.as_double()};
    else if (x.is_int())
	return {x.as_int() - y.as_int()};
    else if (x.is_log_double())
	return {x.as_log_double() - y.as_log_double()};
    else if (x.is_char())
	return {x.as_char() - y.as_char()};
    else
	throw myexception()<<"Minus: object '"<<x.print()<<"' is not double, int, log_double, or char'";
}

extern "C" closure builtin_function_negate(OperationArgs& Args)
{
    auto x = Args.evaluate(0);
  
    if (x.is_double())
	return {-x.as_double()};
    else if (x.is_int())
	return {-x.as_int()};
    else if (x.is_char())
	return {-x.as_char()};
    else
	throw myexception()<<"Negate: object '"<<x.print()<<"' is not double, int, or char'";
}

extern "C" closure builtin_function_equals(OperationArgs& Args)
{
    auto x = Args.evaluate(0);
    auto y = Args.evaluate(1);
  
    if (x.is_double())
	return {x.as_double() == y.as_double()};
    else if (x.is_int())
	return {x.as_int() == y.as_int()};
    else if (x.is_log_double())
	return {x.as_log_double() == y.as_log_double()};
    else if (x.is_char())
	return {x.as_char() == y.as_char()};
    else
	throw myexception()<<"==: object '"<<x.print()<<"' is not double, int, log_double, or char'";
}

extern "C" closure builtin_function_notequals(OperationArgs& Args)
{
    auto x = Args.evaluate(0);
    auto y = Args.evaluate(1);
  
    if (x.is_double())
	return {x.as_double() != y.as_double()};
    else if (x.is_int())
	return {x.as_int() != y.as_int()};
    else if (x.is_log_double())
	return {x.as_log_double() != y.as_log_double()};
    else if (x.is_char())
	return {x.as_char() != y.as_char()};
    else
	throw myexception()<<"/=: object '"<<x.print()<<"' is not double, int, log_double, or char'";
}

extern "C" closure builtin_function_greaterthan(OperationArgs& Args)
{
    auto x = Args.evaluate(0);
    auto y = Args.evaluate(1);
  
    if (x.is_double())
	return {x.as_double() > y.as_double()};
    else if (x.is_int())
	return {x.as_int() > y.as_int()};
    else if (x.is_log_double())
	return {x.as_log_double() > y.as_log_double()};
    else if (x.is_char())
	return {x.as_char() > y.as_char()};
    else
	throw myexception()<<">: object '"<<x.print()<<"' is not double, int, log_double, or char'";
}

extern "C" closure builtin_function_greaterthanorequal(OperationArgs& Args)
{
    auto x = Args.evaluate(0);
    auto y = Args.evaluate(1);
  
    if (x.is_double())
	return {x.as_double() >= y.as_double()};
    else if (x.is_int())
	return {x.as_int() >= y.as_int()};
    else if (x.is_log_double())
	return {x.as_log_double() >= y.as_log_double()};
    else if (x.is_char())
	return {x.as_char() >= y.as_char()};
    else
	throw myexception()<<">=: object '"<<x.print()<<"' is not double, int, log_double, or char'";
}

extern "C" closure builtin_function_lessthan(OperationArgs& Args)
{
    auto x = Args.evaluate(0);
    auto y = Args.evaluate(1);
  
    if (x.is_double())
	return {x.as_double() < y.as_double()};
    else if (x.is_int())
	return {x.as_int() < y.as_int()};
    else if (x.is_log_double())
	return {x.as_log_double() < y.as_log_double()};
    else if (x.is_char())
	return {x.as_char() < y.as_char()};
    else
	throw myexception()<<"<: object '"<<x.print()<<"' is not double, int, log_double, or char'";
}

extern "C" closure builtin_function_lessthanorequal(OperationArgs& Args)
{
    auto x = Args.evaluate(0);
    auto y = Args.evaluate(1);
  
    if (x.is_double())
	return {x.as_double() <= y.as_double()};
    else if (x.is_int())
	return {x.as_int() <= y.as_int()};
    else if (x.is_log_double())
	return {x.as_log_double() <= y.as_log_double()};
    else if (x.is_char())
	return {x.as_char() <= y.as_char()};
    else
	throw myexception()<<"<=: object '"<<x.print()<<"' is not double, int, log_double, or char'";
}

extern "C" closure builtin_function_doubleToLogDouble(OperationArgs& Args)
{
    double d = Args.evaluate(0).as_double();
    return {log_double_t(d)};
}

extern "C" closure builtin_function_intToDouble(OperationArgs& Args)
{
    int i = Args.evaluate(0).as_int();
    return {double(i)};
}

#include "iota.H"

extern "C" closure builtin_function_iotaUnsigned(OperationArgs& Args)
{
    return iota_function<unsigned>(Args);
}

extern "C" closure builtin_function_join(OperationArgs& Args)
{
    Args.evaluate_slot_to_reg(0);
    int R = Args.evaluate_slot_to_reg(1);

    return {index_var(0),{R}};
}

extern "C" closure builtin_function_seq(OperationArgs& Args)
{
    Args.evaluate_slot_no_record(0);

    int index = Args.reference(1).as_index_var();
    int R = Args.current_closure().lookup_in_env( index);

    return {index_var(0),{R}};
}

extern "C" closure builtin_function_show(OperationArgs& Args)
{
    auto x = Args.evaluate(0);
  
    object_ptr<String> v (new String);
    *v = x.print();
    return v;
}

extern "C" closure builtin_function_builtinError(OperationArgs& Args)
{
    std::string message = Args.evaluate(0).as_<String>();
  
    throw myexception()<<message;
}

extern "C" closure builtin_function_putStrLn(OperationArgs& Args)
{
    std::string message = Args.evaluate(0).as_<String>();

    std::cout<<message<<std::endl;

    return constructor("()",0);
}

extern "C" closure builtin_function_reapply(OperationArgs& Args)
{
    int index1 = Args.reference(0).as_index_var();
    int R1 = Args.current_closure().lookup_in_env( index1 );

    int index2 = Args.reference(1).as_index_var();
    int R2 = Args.current_closure().lookup_in_env( index2 );

    expression_ref apply_E;
    {
	expression_ref fE = index_var(1);
	expression_ref argE = index_var(0);
	apply_E = {fE, argE};
    }

    // %1 %0 {R1,R2}
    int apply_reg = Args.allocate({apply_E,{R1, R2}});

    // FIXME - aren't we trying to eliminate general evaluation of regs that aren't children?  See below:

    // Evaluate the newly create application reg - and depend upon it!
    if (Args.evaluate_changeables())
	Args.evaluate_reg_to_object(apply_reg);

    return {index_var(0),{apply_reg}};
}

extern "C" closure builtin_function_read_int(OperationArgs& Args)
{
    string s = Args.evaluate(0).as_<String>();

    if (auto i = can_be_converted_to<int>(s))
	return {*i};
    else
	throw myexception()<<"Cannot convert string '"<<s<<"' to int!";
}

extern "C" closure builtin_function_read_double(OperationArgs& Args)
{
    string s = Args.evaluate(0).as_<String>();

    if (auto d = can_be_converted_to<double>(s))
	return {*d};
    else
	throw myexception()<<"Cannot convert string '"<<s<<"' to double!";
}

