#ifndef MODULE_H
#define MODULE_H

#include <set>
#include <map>
#include <string>
#include <vector>
#include "computation/expression/expression_ref.H"
#include "computation/expression/var.H"
#include <algorithm>
#include <iostream>

enum fixity_t {unknown_fix=0, non_fix=1, left_fix=2, right_fix=3};
enum symbol_type_t {variable_symbol = 0, constructor_symbol = 1, type_symbol = 3, unknown_symbol = 4};

struct symbol_info
{
    std::string name;
    symbol_type_t symbol_type;
    int arity = -1;
    int precedence = -1;
    fixity_t fixity = unknown_fix;
    expression_ref type;

    symbol_info() {};
    symbol_info(const std::string&, symbol_type_t, int);
    symbol_info(const std::string&, symbol_type_t, int, int, fixity_t);
    symbol_info(const std::string&, symbol_type_t, int, int, fixity_t, const expression_ref&);
};

bool operator==(const symbol_info&S1, const symbol_info& S2);
bool operator!=(const symbol_info&S1, const symbol_info& S2);

bool is_haskell_varid(const std::string& s);
bool is_haskell_conid(const std::string& s);
bool is_haskell_varsym(const std::string& s);
bool is_haskell_consym(const std::string& s);
bool is_haskell_normal_con_name(const std::string& s);
bool is_haskell_builtin_con_name(const std::string& s);

bool is_haskell_var_name(const std::string& s);
bool is_haskell_con_name(const std::string& s);
bool is_haskell_module_name(const std::string& s);

bool is_qualified_symbol(const std::string&);

std::string get_module_name(const std::string&);
std::string get_unqualified_name(const std::string&);

std::vector<std::string> get_haskell_identifier_path(const std::string& s);

void parse_module(const expression_ref& M, std::string& name, expression_ref& exports, expression_ref& impdecls, expression_ref& topdecls);
expression_ref create_module(const std::string& name, const expression_ref& exports, const expression_ref& impdecls, const expression_ref& topdecls);

struct module_import
{
    std::string name;
    bool qualified = false;
    std::string as;
    std::vector<std::string> only;
    std::vector<std::string> hiding;
    module_import() = default;
    module_import(const std::string& s):name(s),as(s) {}
};

class module_loader;

class Program;

class Module
{
    std::map<std::string, symbol_info> symbols;

    std::multimap<std::string, std::string> aliases;

    std::map<std::string, symbol_info> exported_symbols_;

    bool resolved = false;

    bool optimized = false;

    bool skip_desugaring = false;

public:

    bool do_optimize = true;
    
    expression_ref module;
    expression_ref body;
    expression_ref impdecls;
    expression_ref topdecls;
    expression_ref exports;

    std::map<var,expression_ref> small_decls_in;

    std::set<var> small_decls_in_free_vars;

    std::map<var,expression_ref> small_decls_out;

    std::set<var> small_decls_out_free_vars;

    std::string name;

    bool is_resolved() const {return resolved;}

    bool is_optimized() const {return optimized;}

    std::set<std::string> dependencies() const;

    std::map<std::string,module_import> imports() const;

    const std::map<std::string, symbol_info>& exported_symbols() const {return exported_symbols_;}

    void export_symbol(const symbol_info& S);

    void export_module(const std::string& S);

    /// Add a function
    void def_function(const std::string& name);

    /// Add a constructor
    void def_constructor(const std::string& name, int arity);

    bool symbol_exists(const std::string&) const;

    void add_symbol(const symbol_info&);

    void add_alias(const std::string&, const std::string&);

    void declare_fixity(const std::string&, int precedence, fixity_t f);

    void declare_symbol(const symbol_info&);

    void add_import(bool qualified, const std::string&);

    void add_import_as(bool qualified, const std::string&, const std::string&);

    void add_impdecl(const expression_ref& impdecl);

    void import_symbol(const symbol_info&, const std::string&, bool qualified);

    void import_module(const Module& M2, bool qualified);

    void import_module(const Module& M2, const std::string&, bool qualified);

    void compile(const Program&);

    void add_local_symbols();

    void perform_imports(const Program&);

    void perform_exports();

    void desugar(const Program&);

    void import_small_decls(const Program&);

    void export_small_decls();

    std::map<std::string,expression_ref> code_defs() const;

    void get_types(const Program&);

    void optimize(const Program&);

    void load_builtins(const module_loader&);

    void load_constructors();

    bool is_declared(const std::string&) const;

    bool is_declared_local(const std::string&) const;

    static std::pair<symbol_info,expression_ref> lookup_builtin_symbol(const std::string&);

    symbol_info lookup_symbol(const std::string&) const;

    symbol_info get_operator(const std::string& name) const;

    explicit Module(const std::string&);

    explicit Module(const char*);

    explicit Module(const expression_ref&);
};

std::ostream& operator<<(std::ostream&, const Module&);

expression_ref resolve_refs(const Program&, const expression_ref&);
#endif
