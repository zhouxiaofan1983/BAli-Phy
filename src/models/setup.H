#ifndef SMODEL_SETUP_H
#define SMODEL_SETUP_H
#include <vector>
#include <valarray>
#include <string>
#include <set>
#include <boost/program_options.hpp>
#include "util/ptree.H"
#include "object.H"
#include "alignment/alignment.H"
#include "sequence/alphabet.H"
#include "computation/expression/expression_ref.H"
#include "unification.H"
#include "util/json.hh"

class Rules;

class pretty_model_t
{
public:
    ptree main;
    std::vector<std::string> term_names;
    std::vector<pretty_model_t> terms;

    std::string show(const Rules& R, bool = true) const;
    std::string show_main(const Rules&, bool = true) const;
    std::string show_extracted(const Rules&) const;

    pretty_model_t(const ptree& m);
};

class model_t
{
    ptree description;
public:
    ptree type;
    std::set<term_t> constraints;
    expression_ref expression;

    pretty_model_t pretty_model() const {return {description};};

    std::string show(const Rules&, bool = true) const;

    std::string show_pretty(const Rules&, bool = true) const;
    std::string show_main(const Rules&, bool = true) const;
    std::string show_extracted(const Rules&) const;

    model_t() = default;
    model_t(const ptree&,const ptree&, const std::set<term_t>&, const expression_ref&);
};

void to_json(json& j, const pretty_model_t& m);

model_t get_model(const Rules& R, const std::string& type, const std::string& model, const std::vector<std::pair<std::string,std::string>>& = std::vector<std::pair<std::string,std::string>>());

std::string default_markov_model(const alphabet& a);
#endif
