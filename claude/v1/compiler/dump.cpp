#include "dump.h"
#include <algorithm>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>
#include <map>

namespace {

std::string indent(int n) { return std::string(n * 2, ' '); }

std::string srcPath(const SourceMap& sm, int file_id) {
    if (file_id < 0) return "<unknown>";
    return sm.at(file_id).path;
}

std::string locTag(const SourceMap& sm, int file_id, int tok) {
    std::ostringstream os;
    os << "  file=" << srcPath(sm, file_id) << " tok=" << tok;
    return os.str();
}

std::string paramList(const std::vector<std::pair<std::string, std::string>>& params,
                      const std::vector<bool>& mut_flags) {
    std::ostringstream os;
    os << "(";
    for (size_t i = 0; i < params.size(); i++) {
        if (i) os << ", ";
        if (i < mut_flags.size() && mut_flags[i]) os << "mutable ";
        os << params[i].second << " " << params[i].first;
    }
    os << ")";
    return os.str();
}

std::string tupleReturn(const std::vector<std::pair<std::string, std::string>>& fields) {
    std::ostringstream os;
    os << "(";
    for (size_t i = 0; i < fields.size(); i++) {
        if (i) os << ", ";
        os << fields[i].second << " " << fields[i].first;
    }
    os << ")";
    return os.str();
}

void dumpField(std::ostream& os, int depth, const FieldDef& f) {
    os << indent(depth) << "field " << f.name << " " << f.type;
    if (f.default_val) os << " = <expr>";
    os << "\n";
}

void dumpMethod(std::ostream& os, int depth, const MethodDef& m) {
    os << indent(depth);
    if (m.is_virtual) os << "virtual ";
    if (m.is_const_method) os << "const ";
    os << "method " << m.name << paramList(m.params, m.param_mutable);
    if (m.has_explicit_return) os << " -> " << m.return_type;
    if (m.is_delete) os << " = delete";
    if (m.is_default) os << " = default";
    os << (m.body ? "" : " (no body)");
    os << "\n";
}

void dumpClass(std::ostream& os, int depth, const SourceMap& sm, const SlidDef& s);

void dumpClass(std::ostream& os, int depth, const SourceMap& sm, const SlidDef& s) {
    os << indent(depth) << "class " << s.name;
    if (!s.base_name.empty()) os << " : " << s.base_name;
    if (!s.type_params.empty()) {
        os << " <";
        for (size_t i = 0; i < s.type_params.size(); i++) {
            if (i) os << ",";
            os << s.type_params[i];
        }
        os << ">";
    }
    if (s.has_leading_ellipsis) os << " ...leading";
    if (s.has_trailing_ellipsis) os << " ...trailing";
    if (!s.is_local) os << " imported(" << s.impl_module << ")";
    os << locTag(sm, s.name_file_id, s.name_tok) << "\n";

    if (s.has_explicit_ctor_decl) {
        os << indent(depth + 1) << "ctor"
           << (s.is_const_ctor ? " const" : "")
           << (s.explicit_ctor_body ? "" : " (no body)") << "\n";
    }
    if (s.has_explicit_dtor_decl) {
        os << indent(depth + 1) << "dtor"
           << (s.dtor_is_virtual ? " virtual" : "")
           << (s.is_const_dtor ? " const" : "")
           << (s.dtor_body ? "" : " (no body)") << "\n";
    }
    for (auto& f : s.fields) dumpField(os, depth + 1, f);
    for (auto& a : s.aliases)
        os << indent(depth + 1) << "alias " << a.name << " = " << a.body << "\n";
    for (auto& c : s.consts)
        os << indent(depth + 1) << "const " << c.name
           << (c.declared_type.empty() ? "" : (" " + c.declared_type)) << "\n";
    for (auto& e : s.nested_enums) {
        os << indent(depth + 1) << "enum " << e.name << "\n";
        for (auto& v : e.values)
            os << indent(depth + 2) << "value " << v << "\n";
    }
    for (auto& m : s.methods) dumpMethod(os, depth + 1, m);
    for (auto& ns : s.nested_slids) dumpClass(os, depth + 1, sm, ns);
    for (auto& lc : s.local_classes) dumpClass(os, depth + 1, sm, lc);
}

void dumpFunction(std::ostream& os, int depth, const SourceMap& sm, const FunctionDef& f) {
    os << indent(depth);
    if (f.is_foreign) os << "foreign ";
    if (!f.is_local) os << "imported ";
    os << "function ";
    if (!f.namespace_name.empty()) os << f.namespace_name << ":";
    os << f.user_name;
    if (!f.type_params.empty()) {
        os << "<";
        for (size_t i = 0; i < f.type_params.size(); i++) {
            if (i) os << ",";
            os << f.type_params[i];
        }
        os << ">";
    }
    os << paramList(f.params, f.param_mutable);
    if (!f.return_type.empty()) os << " -> " << f.return_type;
    else if (!f.tuple_return_fields.empty()) os << " -> " << tupleReturn(f.tuple_return_fields);
    if (f.is_foreign) os << " = import";
    os << locTag(sm, f.file_id, f.tok) << "\n";
    if (f.name != f.user_name)
        os << indent(depth + 1) << "symbol " << f.name << "\n";
    for (auto& lc : f.local_classes) dumpClass(os, depth + 1, sm, lc);
}

void dumpExternalMethod(std::ostream& os, int depth, const SourceMap& sm,
                        const ExternalMethodDef& m) {
    os << indent(depth);
    if (m.is_virtual) os << "virtual ";
    if (m.is_const_method) os << "const ";
    os << "external_method " << m.slid_name << ":" << m.method_name
       << paramList(m.params, m.param_mutable);
    if (m.has_explicit_return) os << " -> " << m.return_type;
    if (m.is_delete) os << " = delete";
    if (m.is_default) os << " = default";
    os << locTag(sm, m.file_id, m.tok) << "\n";
}

void dumpConst(std::ostream& os, int depth, const SourceMap& sm, const ConstDef& c) {
    os << indent(depth) << "const ";
    if (!c.namespace_name.empty()) os << c.namespace_name << ":";
    os << c.name;
    if (!c.declared_type.empty()) os << " " << c.declared_type;
    os << locTag(sm, c.file_id, c.tok) << "\n";
}

void dumpGlobal(std::ostream& os, int depth, const SourceMap& sm, const GlobalDef& g) {
    os << indent(depth) << "global ";
    if (g.namespace_name.empty()) os << "<unnamed>";
    else os << g.namespace_name;
    os << (g.is_lazy() ? " lazy" : " static");
    if (!g.visible_in_function.empty()) os << " fn=" << g.visible_in_function;
    if (!g.impl_module.empty()) os << " imported(" << g.impl_module << ")";
    os << locTag(sm, g.file_id, g.tok) << "\n";
    for (auto& f : g.fields) dumpField(os, depth + 1, f);
    if (g.has_ctor_decl)
        os << indent(depth + 1) << "ctor" << (g.ctor_body ? "" : " (no body)") << "\n";
    if (g.has_dtor_decl)
        os << indent(depth + 1) << "dtor" << (g.dtor_body ? "" : " (no body)") << "\n";
}

void dumpNamespace(std::ostream& os, int depth, const SourceMap& sm, const NamespaceDef& n) {
    os << indent(depth) << "namespace " << n.name << locTag(sm, n.file_id, n.tok) << "\n";
    for (auto& f : n.functions) dumpFunction(os, depth + 1, sm, f);
    for (auto& c : n.consts) dumpConst(os, depth + 1, sm, c);
}

void dumpAlias(std::ostream& os, int depth, const SourceMap& sm, const AliasDef& a) {
    os << indent(depth) << "alias ";
    if (!a.namespace_name.empty()) os << a.namespace_name << ":";
    os << a.name << " = " << a.target << locTag(sm, a.file_id, a.tok) << "\n";
}

void dumpTypeAlias(std::ostream& os, int depth, const SourceMap& sm, const TypeAliasDef& a) {
    os << indent(depth) << "type_alias " << a.name << " = " << a.body
       << locTag(sm, a.file_id, a.tok) << "\n";
}

void dumpTypeAliasTemplate(std::ostream& os, int depth, const SourceMap& sm,
                           const TypeAliasTemplate& a) {
    os << indent(depth) << "type_alias_template " << a.name << "<";
    for (size_t i = 0; i < a.type_params.size(); i++) {
        if (i) os << ",";
        os << a.type_params[i];
    }
    os << "> = " << a.body << locTag(sm, a.file_id, a.tok) << "\n";
}

void dumpUnnamedGlobal(std::ostream& os, int depth, const SourceMap& sm,
                       const UnnamedGlobal& u) {
    os << indent(depth) << "unnamed_global " << u.type_name
       << locTag(sm, u.file_id, u.tok) << "\n";
}

} // namespace

void dumpProgram(const Program& prog, const SourceMap& sm, std::ostream& os) {
    os << "program:\n";

    if (!prog.enums.empty()) {
        os << "  enums:\n";
        for (auto& e : prog.enums) {
            os << "    enum " << e.name << locTag(sm, e.file_id, e.tok) << "\n";
            for (auto& v : e.values) os << "      value " << v << "\n";
        }
    }
    if (!prog.classes.empty()) {
        os << "  classes:\n";
        for (auto& s : prog.classes) dumpClass(os, 2, sm, s);
    }
    if (!prog.functions.empty()) {
        os << "  functions:\n";
        for (auto& f : prog.functions) dumpFunction(os, 2, sm, f);
    }
    if (!prog.external_methods.empty()) {
        os << "  external_methods:\n";
        for (auto& m : prog.external_methods) dumpExternalMethod(os, 2, sm, m);
    }
    if (!prog.consts.empty()) {
        os << "  consts:\n";
        for (auto& c : prog.consts) dumpConst(os, 2, sm, c);
    }
    if (!prog.globals.empty()) {
        os << "  globals:\n";
        for (auto& g : prog.globals) dumpGlobal(os, 2, sm, g);
    }
    if (!prog.namespaces.empty()) {
        os << "  namespaces:\n";
        for (auto& n : prog.namespaces) dumpNamespace(os, 2, sm, n);
    }
    if (!prog.aliases.empty()) {
        os << "  aliases:\n";
        for (auto& a : prog.aliases) dumpAlias(os, 2, sm, a);
    }
    if (!prog.type_aliases.empty()) {
        os << "  type_aliases:\n";
        for (auto& a : prog.type_aliases) dumpTypeAlias(os, 2, sm, a);
    }
    if (!prog.type_alias_templates.empty()) {
        os << "  type_alias_templates:\n";
        for (auto& a : prog.type_alias_templates) dumpTypeAliasTemplate(os, 2, sm, a);
    }
    if (!prog.unnamed_globals.empty()) {
        os << "  unnamed_globals:\n";
        for (auto& u : prog.unnamed_globals) dumpUnnamedGlobal(os, 2, sm, u);
    }
    if (!prog.imported_headers.empty()) {
        os << "  imported_headers:\n";
        std::vector<std::string> sorted = prog.imported_headers;
        std::sort(sorted.begin(), sorted.end());
        for (auto& h : sorted) os << "    " << h << "\n";
    }
    if (!prog.slid_modules.empty()) {
        os << "  slid_modules:\n";
        for (auto& [k, v] : prog.slid_modules)
            os << "    " << k << " -> " << v << "\n";
    }
    if (!prog.instantiations.empty()) {
        os << "  instantiations:\n";
        std::vector<std::string> lines;
        for (auto& inst : prog.instantiations) {
            std::ostringstream line;
            line << inst.func_name << "<";
            for (size_t i = 0; i < inst.type_args.size(); i++) {
                if (i) line << ",";
                line << inst.type_args[i];
            }
            line << ">(";
            for (size_t i = 0; i < inst.param_types.size(); i++) {
                if (i) line << ",";
                line << inst.param_types[i];
            }
            line << ")";
            lines.push_back(line.str());
        }
        std::sort(lines.begin(), lines.end());
        for (auto& l : lines) os << "    " << l << "\n";
    }
}
