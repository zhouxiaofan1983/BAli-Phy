#include "startup/help.hh"

#include <regex>
#include <list>
#include <boost/optional/optional_io.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/algorithm/string.hpp>
#include "startup/cmd_line.H"
#include "startup/paths.H"
#include "util.H"
#include "../io.H"
#include "setup.H"
#include "version.H"
#include "models/rules.H"
#include "models/parse.H"

using std::string;
using std::list;
using std::map;
using std::vector;
using std::cout;
using boost::optional;

namespace po = boost::program_options;
using po::variables_map;

namespace fs = boost::filesystem;

string get_topic_from_string(const string& s)
{
    string s2 = s;
    int index = s2.find_first_of("\r\n");
    if (index != string::npos)
	s2 = s2.substr(0,index);
    if (s2.empty() or s2[s2.size()-1] == '.')
	;
    else if (s2[s2.size()-1] == ':')
	s2[s2.size()-1] = '.';
    else
	s2.push_back('.');
    return s2;
}

string pad(const string& s, int n)
{
    if (s.size() < n)
	return s+string(n-s.size(),' ');
    return s;
}


void help_on_help(std::ostream& o, const map<string,string>& help)
{
    o<<"Help topics via --help=arg are available for:\n";
    o<<"  =simple              Command-line flags and a short description.\n";
    o<<"  =advanced            Info on advanced command-line flags.\n";
    o<<"  =expert              Info on expert flags - these might be broken!.\n\n";
    o<<"  =functions           A list of functions and result type.\n";
    o<<"  =<function name>     Function type, description, and argument names.\n\n";
    o<<"  =help                This list of topics.\n\n";
    for(auto& x: help)
	o<<"  ="<<pad(x.first,18)<<"  "<<get_topic_from_string(x.second)<<"\n";
    o<<"\n";
}

std::map<string,string> load_help_files(const std::vector<fs::path>& package_paths)
{
    map<string,string> help;

    for(auto& package_path: package_paths)
    {
	auto path = package_path / "help";

	if (fs::exists(path))
	    for(auto& dir_entry: fs::recursive_directory_iterator(path))
	    {
		auto abs_path = fs::canonical(dir_entry.path() );
		string topic = abs_path.stem().string();
		
		if (abs_path.extension() == ".txt" and not help.count(topic))
		    help[topic] = boost::trim_copy(read_file(abs_path.string(), "help file"));
	    }
    }

    return help;
}

list<string> get_lines(const string& line)
{
    list<string> lines;

    for(int pos=0; pos<line.size();)
    {
	int next = line.find('\n', pos);
	if (next == string::npos)
	    next = line.size();
	lines.push_back(line.substr(pos,next-pos));
	pos = next + 1;
    }
    return lines;
}


// Given a line containing no line breaks, return the first wrapped line and the rest of the line.
optional<list<string>> wrap(string line, int width)
{
    if (line.size() < width) return list<string>{line};

    int loc = line.find_last_of(" \t", width-1);

    if (loc != string::npos)
    {
	if (loc+1 < line.size())
	    return list<string>{line.substr(0,loc),line.substr(loc+1)};
	else
	    return list<string>{line.substr(0,loc)};
    }

    return boost::none;
}

void wrap_and_indent_one_line(int indent, int width, list<string>& lines, list<string>& wrapped_lines)
{
    lines.front() = string(indent,' ') + lines.front();
    if (auto wrap_first = wrap(lines.front(), width))
    {
	// Move wrapped first line into wrapped_lines
	wrapped_lines.push_back(wrap_first->front());
	wrap_first->pop_front();

	// Move remaining lines into wrapped_lines
	lines.pop_front();
	lines.insert(lines.begin(), wrap_first->begin(), wrap_first->end());
    }
    else
    {
	// Move the unwrapped lines into wrapped_lines
	wrapped_lines.push_back(lines.front());
	lines.pop_front();
    }
}

string indent_and_wrap(int indent_first_line, int indent_other_lines, int width, const string& text)
{
    if (text.empty()) return text;

    list<string> wrapped_lines;

    auto lines = get_lines(text);
    assert(not lines.empty());

    // 1. Indent and wrap the first line
    wrap_and_indent_one_line(indent_first_line, width, lines, wrapped_lines);

    while(not lines.empty())
	wrap_and_indent_one_line(indent_other_lines, width, lines, wrapped_lines);

    return join(wrapped_lines, '\n');
}

string indent_and_wrap_citation(int indent, int extra_indent, int width, const string& text)
{
    if (text.empty()) return text;

    list<string> wrapped_lines;

    auto lines = get_lines(text);
    assert(not lines.empty());

    for(auto& line: lines)
	wrapped_lines.push_back(indent_and_wrap(indent, indent+extra_indent, width, line));

    return join(wrapped_lines, '\n');
}


int terminal_width()
{
    int width = 80;
    return width-2;
}

string indent_and_wrap(int indent, int width, const string& text)
{
    return indent_and_wrap(indent, indent, width, text);
}

string indent(int indent, const string& text)
{
    return indent_and_wrap(indent, 100000, text);
}

const std::string ansi_plain("\033[0m");
const std::string ansi_bold("\033[1m");
const std::string ansi_under("\033[4m");
const std::string ansi_inverse("\033[7m");
const std::string ansi_bold_off("\033[21m");
const std::string ansi_under_off("\033[24m");
const std::string ansi_inverse_off("\033[27m");
const std::string ansi_black("\033[1;30m");
const std::string ansi_red("\033[1;31m");
const std::string ansi_green("\033[1;32m");
const std::string ansi_yellow("\033[1;33m");
const std::string ansi_blue("\033[1;34m");
const std::string ansi_magenta("\033[1;35m");
const std::string ansi_cyan("\033[1;36m");

const std::string ansi_bg_grey("\033[1;48;2;180;180;180m");

string bold(const string& line)
{
    return ansi_bold + line + ansi_plain;
}

string highlight_bg(const string& line)
{
    return ansi_bg_grey + line + ansi_plain;
}

string inverse(const string& line)
{
    return ansi_inverse + line + ansi_inverse_off;
}

string underline(const string& line)
{
    return ansi_under + line + ansi_under_off;
}

string header(const string& text)
{
    return underline(text) + ":\n\n";
}

optional<string> get_authors(const Rule& rule)
{
    auto citation = rule.get_child_optional("citation");
    if (not citation)
	return boost::none;

    vector<string> authors;
    if (auto authors_ = citation->get_child_optional("author"))
	for(auto& author: *authors_)
	    if (auto name = author.second.get_optional<string>("name"))
	    {
		auto names = split(*name,", ");
		if (names.size() == 2)
		{
		    string ref = names[0];
		    if (not names[1].empty())
		    {
			names[1] = string(1,names[1][0]) + '.';
			if (authors.empty())
			    ref = names[0] + ", " + names[1];
			else
			    ref = names[1] + " " +names[0];
		    }
		    authors.push_back(ref);
		}
		else
		{
		    authors.push_back(*name);
		}
	    }

    if (authors.size())
    {
	if (authors.size() <= 2)
	    return join(authors," and ");
	else
	    return authors[0]+" et al";
    }
    return boost::none;
}


optional<string> get_citation(const Rule& rule, bool show_title)
{
    auto citation = rule.get_child_optional("citation");
    if (not citation)
	return boost::none;

    if (citation->is_a<string>())
	return citation->get_value<string>();

    vector<string> cite;
    auto title = citation->get_optional<string>("title");
    auto year = citation->get_optional<string>("year");
    auto authors = get_authors(rule);

    if (authors)
	cite.push_back(*authors);
    if (year)
	cite.push_back("("+*year+")");

    if (authors or year)
    {
	string x = join(cite, " ");
	cite.clear();
	cite.push_back(x);
    }
    if (title and show_title)
	cite.push_back(*title);

    return join(cite,"\n");
}



optional<string> get_citation_id(const Rule& rule, const string& idtype)
{
    // 1. Check if there is a citation field.
    auto citation = rule.get_child_optional("citation");
    if (not citation) return boost::none;

    // 2. Try to get the DOI
    if (auto identifiers = citation->get_child_optional("identifier"))
    {
	for(auto& identifier: *identifiers)
	{
	    auto type = identifier.second.get_child_optional("type");
	    if (not type or type->get_value<string>() != idtype) continue;

	    auto id = identifier.second.get_child_optional("id");
	    if (id)
		return id->get_value<string>();
	}
    }
    return boost::none;
}

optional<string> get_citation_url(const Rule& rule)
{
    // 1. Check if there is a citation field.
    auto citation = rule.get_child_optional("citation");
    if (not citation) return boost::none;

    // 2. Try to get the URL from the "link" field.
    if (auto links = citation->get_child_optional("link"))
    {
	for(auto& link: *links)
	{
	    auto url = link.second.get_child_optional("url");
	    if (not url) continue;

	    auto anchor = link.second.get_child_optional("anchor");
	    if (anchor)
		return url->get_value<string>()+"/#"+anchor->get_value<string>();
	    else
		return url->get_value<string>();
	}
	
    }

    if (auto doi = get_citation_id(rule,"doi"))
    {
	return "https://doi.org/"+*doi;
    }
    else if (auto pmcid = get_citation_id(rule,"pmcid"))
    {
	return "https://www.ncbi.nlm.nih.gov/pmc/articles/"+*pmcid;
    }
    else if (auto pmid = get_citation_id(rule,"pmid"))
    {
	return "https://www.ncbi.nlm.nih.gov/pubmed/"+*pmid;
    }

    return boost::none;
}

string get_help_for_rule(const Rules& rules, const Rule& rule)
{
    std::ostringstream help;
    if (auto title = rule.get_optional<string>("title"))
	help<<bold(*title)<<std::endl<<std::endl;

    string name = rule.get<string>("name");
    string result_type = unparse_type(rule.get_child("result_type"));
    auto args = rule.get_child("args");
    vector<string> args_names_types;
    // Actually, we may have a problem here...
    if (auto constraints = rule.get_child_optional("constraints"))
    {
	vector<string> cs;
	for(auto& x: *constraints)
	    cs.push_back(unparse_type(x.second));
//		help<<join(cs,", ")<<" => ";
    }
    for(auto& argpair: args)
    {
	auto& arg = argpair.second;
	string arg_name = arg.get<string>("arg_name");
	if (auto default_value = arg.get_child_optional("default_value"))
	    arg_name += " " + show_model_abbrev(*default_value, rules, 15);
	args_names_types.push_back(arg_name);
    }
    help<<header("Usage");
    help<<"   "<<bold(name);
    if (args_names_types.size()) help<<"["<<join(args_names_types,", ")<<"]";
    help<<"\n\n";
    
    if (auto synonyms = rule.get_child_optional("synonyms"))
    {
	vector<string> syn;
	for(auto& x: *synonyms)
	    syn.push_back(x.second);
	help<<header("Synonyms");
	help<<indent_and_wrap(3, terminal_width(),join(syn,", "))<<"\n\n";
    }

    if (not args.empty())
	help<<header("Arguments");
    for(auto& argpair: args)
    {
	auto& arg = argpair.second;

	// 1. arg: description
	optional<string> description = arg.get_optional<string>("description");
	help<<"   "<<arg.get<string>("arg_name")<<": "<<description<<".\n";

	// 2. type: type
	help<<"       type: "<<unparse_type(arg.get_child("arg_type"))<<"\n";

	// 3. default =/~ default
	if (auto default_value = arg.get_child_optional("default_value"))
	    help<<"       default "<<show_model(*default_value, rules)<<"\n";

	help<<std::endl;
    }
    help<<header("Result type");
    help<<indent(3,result_type)<<"\n\n";

    if (auto description = rule.get_optional<string>("description"))
    {
	help<<header("Description");
	help<<indent_and_wrap(3, terminal_width(), *description)<<std::endl<<std::endl;
    }

    if (auto examples = rule.get_child_optional("examples"))
    {
	help<<header("Examples");
	for(auto& x: *examples)
	{
	    help<<indent(3, x.second.get_value<string>())<<"\n\n";
	}
    }

    if (auto citation = get_citation(rule,true))
    {
	help<<header("Citation");
	help<<indent_and_wrap_citation(3,3,terminal_width(),*citation)<<std::endl;
	if (auto url = get_citation_url(rule))
	    help<<indent(3,*url)<<std::endl;
	help<<std::endl;
    }
    
    return help.str();
}

string do_quotes(const string& line)
{
    return std::regex_replace(line,std::regex("([^\\\\]|^)`([^`]*)`"),string("$1")+inverse("$2").c_str());
}

string do_double_emph(string line)
{
    line = std::regex_replace(line,std::regex("([^\\\\]|^)__([^_ ][^_]*)__"),string("$1")+bold("$2").c_str());
    return std::regex_replace(line,std::regex("([^\\\\]|^)\\*\\*([^* ][^*]*)\\*\\*"),string("$1")+bold("$2").c_str());
}

string do_single_emph(string line)
{
    line = std::regex_replace(line,std::regex("([^\\\\]|^)\\*([^* ][^*]*)\\*"),string("$1")+underline("$2").c_str());
    return std::regex_replace(line,std::regex("([^\\\\]|^)_([^_ ][^_]*)_"),string("$1")+underline("$2").c_str());
}

string do_unescape(const string& line)
{
    return std::regex_replace(line,std::regex("\\\\([`_*])"),"$1");
}

string pseudo_markdown(const string& lines)
{
    std::ostringstream marked;
    for(auto& line: split(lines,'\n'))
    {
	bool header = false;
	if (startswith(line,"# "))
	{
	    header = true;
	    line = line.substr(2);
	}
	line = do_quotes(line);
	line = do_double_emph(line);
	line = do_single_emph(line);
	line = do_unescape(line);
	if (header)
	    line = bold(line);
	marked<<line<<"\n";
    }

    return marked.str();
}


void show_help(const string& topic, const vector<fs::path>& package_paths)
{
    auto help = load_help_files(package_paths);
	
    if (topic == "help")
    {
	help_on_help(std::cout, help);
	return;
    }
    if (help.count(topic))
    {
	std::cout<<pseudo_markdown(help[topic]);
	std::cout<<std::endl;
	return;
    }

    Rules R(package_paths);
    if (topic == "functions")
    {
	for(auto& rule: R.get_rules())
	{
	    if (rule.second.get_child_optional("name"))
	    {
		string name = rule.second.get_child("name").get_value<string>();
		string result_type = unparse_type(rule.second.get_child("result_type"));
		std::cout<<name<<" :: "<<result_type << std::endl;
	    }
	}
	return;
    }

    if (auto rule = R.get_rule_for_func(topic))
    {
	std::cout<<get_help_for_rule(R, *rule);
	return;
    }
    else
    {
	cout<<"Help topic '"<<topic<<"' not found.\n\n";
	help_on_help(cout,help);
	return;
    }
}
