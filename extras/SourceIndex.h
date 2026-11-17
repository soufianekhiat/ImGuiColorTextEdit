//	TextEditor - A syntax highlighting text editor for Dear ImGui.
//	SourceIndex, an addon of the soufianekhiat/ImGuiColorTextEdit fork.
//
//	This work is licensed under the terms of the MIT license.
//	For a copy, see <https://opensource.org/licenses/MIT>.


#pragma once


//
//	Include files
//

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>


//
//	SourceIndex
//
//	What a set of C-like source documents (HLSL, GLSL, C, C++) include and
//	declare, kept as documents change: the include tree (what a document
//	includes, what includes it) and a table of symbols (functions, structs and
//	classes, namespaces, enums and their values, typedefs, macros, globals and
//	members) with where each is, so that an editor can go to a definition
//	(TextEditor::SetGoToDefinitionCallback). There is no compiler behind it: a
//	tokenizer and rules for declarations as they are usually written; function
//	bodies are skipped, so locals are not symbols. Documents are named by
//	their paths (or any names), compared lexically normalized ("a/x/../b.h" is
//	"a/b.h"). Not thread-safe: one thread uses an index.
//

class SourceIndex {
public:
	enum class Kind {
		function,
		structure,	// struct, class, union, interface, cbuffer and tbuffer
		namespaceScope,
		enumeration,
		enumerator,
		typeAlias,	// typedef and using
		macro,
		variable,	// outside types
		member		// inside types
	};

	struct Symbol {
		std::string name;		// as declared, maybe qualified (void a::b() {})
		std::string qualified;	// with the namespaces and types around it: a::b::name
		Kind kind;
		std::string document;
		size_t line;			// from 0, like TextEditor::DocPos
		size_t index;			// code point on the line, from 0, like TextEditor::DocPos
		bool definition;		// a function's body, a type's members, a variable; else a declaration
	};

	struct Include {
		std::string text;		// between the quotes or angle brackets
		std::string document;	// the document it resolves to; empty when none does
		size_t line;
		bool angled;
	};

	// documents: setting one again parses it again
	void SetDocument(const std::string& name, const std::string& text);
	void RemoveDocument(const std::string& name);
	bool HasDocument(const std::string& name) const;
	std::vector<std::string> GetDocuments() const;	// sorted

	// where includes resolve: the including document's folder, these folders in order, then the
	// name as written (relative to where documents are named from)
	void SetIncludeFolders(const std::vector<std::string>& folders);

	// the include tree
	std::vector<Include> GetIncludes(const std::string& name) const;
	std::vector<std::string> GetIncludedBy(const std::string& name) const;		// directly, sorted
	std::vector<std::string> GetIncludeClosure(const std::string& name) const;	// directly or not, in include order

	// the symbols of a document, in order
	std::vector<Symbol> GetSymbols(const std::string& name) const;

	// the symbols named `identifier` (a qualified one, a::b, matches the end of qualified names),
	// nearest to `from` first: in it, in what it includes, in the documents that include it and
	// what they include, then anywhere; definitions before declarations
	std::vector<Symbol> FindDefinitions(const std::string& identifier, const std::string& from) const;

	static std::string Normalize(const std::string& name);

private:
	struct Parsed {
		std::vector<Include> includes;	// unresolved
		std::vector<Symbol> symbols;
	};

	std::map<std::string, Parsed> documents;
	std::vector<std::string> includeFolders;

	// includes resolved, and the reverse, rebuilt when a document or a folder changes
	mutable bool resolved = false;
	mutable std::map<std::string, std::vector<std::string>> includesOf;
	mutable std::map<std::string, std::set<std::string>> includedBy;
	void resolve() const;
	std::string resolve(const std::string& includer, const Include& include) const;
};
