#pragma clang diagnostic ignored "-Wreturn-type-c-linkage"
#include "computation/computation.H"
#include "myexception.H"
#include "computation/machine/graph_register.H"
#include "rng.H"
#include "util.H"
#include "probability/choose.H"
#include "computation/expression/expression.H"

using boost::dynamic_pointer_cast;
using namespace std;

extern "C" closure builtin_function_getArgs(OperationArgs& Args)
{
  reg_heap& M = Args.memory();

  EVector V;
  for(const auto& arg: M.args)
    V.push_back(String(arg));

  return V;
}
