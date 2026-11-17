//	TextEditor - A syntax highlighting text editor for Dear ImGui.
//	SourceIndex, an addon of the soufianekhiat/ImGuiColorTextEdit fork.
//
//	This work is licensed under the terms of the MIT license.
//	For a copy, see <https://opensource.org/licenses/MIT>.


//
//	Include files
//

#include <algorithm>
#include <filesystem>
#include <functional>

#include "SourceIndex.h"


//
//	Tokens
//

namespace {
	struct Token {
		enum class Type {
			identifier,
			number,
			string,
			punctuation
		};

		Type type;
		std::string text;
		size_t line;
		size_t index;	// code point on the line
	};

	inline bool isIdentifierStart(unsigned char c) { return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80; }
	inline bool isIdentifierPart(unsigned char c) { return isIdentifierStart(c) || (c >= '0' && c <= '9'); }

	// the tokens of `text` outside comments and preprocessor lines; those lines give includes and macros
	struct Tokenizer {
		const std::string& text;
		std::vector<Token> tokens;
		std::vector<SourceIndex::Include> includes;
		std::vector<Token> macros;	// the names #define gives

		size_t at = 0;
		size_t line = 0;
		size_t index = 0;
		bool lineStart = true;	// only white space before, on this line

		explicit Tokenizer(const std::string& source) : text(source) { run(); }

		inline char peek(size_t ahead=0) const { return at + ahead < text.size() ? text[at + ahead] : '\0'; }

		// moves one byte on, counting lines and code points
		void advance() {
			if (text[at] == '\n') {
				line++;
				index = 0;
				lineStart = true;

			} else if ((static_cast<unsigned char>(text[at]) & 0xc0) != 0x80) {
				index++;
			}

			at++;
		}

		void run() {
			while (at < text.size()) {
				const auto c = static_cast<unsigned char>(text[at]);

				if (c == '\n' || c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
					advance();

				} else if (c == '/' && peek(1) == '/') {
					while (at < text.size() && text[at] != '\n') { advance(); }

				} else if (c == '/' && peek(1) == '*') {
					advance();
					advance();

					while (at < text.size() && !(text[at] == '*' && peek(1) == '/')) { advance(); }

					if (at < text.size()) {
						advance();
						advance();
					}

				} else if (c == '#' && lineStart) {
					directive();

				} else {
					const auto startLine = line;
					const auto startIndex = index;
					lineStart = false;

					if (c == 'R' && peek(1) == '"') {
						rawString();
						tokens.push_back({Token::Type::string, "", startLine, startIndex});

					} else if (isIdentifierStart(c)) {
						const auto begin = at;

						while (at < text.size() && isIdentifierPart(static_cast<unsigned char>(text[at]))) { advance(); }

						tokens.push_back({Token::Type::identifier, text.substr(begin, at - begin), startLine, startIndex});

					} else if ((c >= '0' && c <= '9') || (c == '.' && peek(1) >= '0' && peek(1) <= '9')) {
						const auto begin = at;

						while (at < text.size()) {
							const auto d = static_cast<unsigned char>(text[at]);
							const auto exponent = (d == '+' || d == '-') && at > begin && std::string("eEpP").find(text[at - 1]) != std::string::npos;

							if (!isIdentifierPart(d) && d != '.' && !exponent) {
								break;
							}

							advance();
						}

						tokens.push_back({Token::Type::number, text.substr(begin, at - begin), startLine, startIndex});

					} else if (c == '"' || c == '\'') {
						advance();

						while (at < text.size() && text[at] != static_cast<char>(c) && text[at] != '\n') {
							if (text[at] == '\\' && at + 1 < text.size()) {
								advance();
							}

							advance();
						}

						if (at < text.size() && text[at] == static_cast<char>(c)) {
							advance();
						}

						tokens.push_back({Token::Type::string, "", startLine, startIndex});

					} else if (c == ':' && peek(1) == ':') {
						advance();
						advance();
						tokens.push_back({Token::Type::punctuation, "::", startLine, startIndex});

					} else {
						advance();
						tokens.push_back({Token::Type::punctuation, std::string(1, static_cast<char>(c)), startLine, startIndex});
					}
				}
			}
		}

		// R"delimiter( ... )delimiter"
		void rawString() {
			advance();
			advance();
			std::string delimiter;

			while (at < text.size() && text[at] != '(' && text[at] != '\n') {
				delimiter += text[at];
				advance();
			}

			const auto end = ")" + delimiter + "\"";

			while (at < text.size() && text.compare(at, end.size(), end) != 0) { advance(); }

			for (size_t i = 0; i < end.size() && at < text.size(); i++) { advance(); }
		}

		// a preprocessor line, continued by backslashes: includes and defines are kept
		void directive() {
			const auto directiveLine = line;
			advance();

			auto skipSpaces = [&]() { while (at < text.size() && (text[at] == ' ' || text[at] == '\t')) { advance(); } };
			skipSpaces();
			const auto begin = at;

			while (at < text.size() && isIdentifierPart(static_cast<unsigned char>(text[at]))) { advance(); }

			const auto word = text.substr(begin, at - begin);
			skipSpaces();

			if (word == "include" && (peek() == '"' || peek() == '<')) {
				const auto close = peek() == '"' ? '"' : '>';
				const auto angled = close == '>';
				advance();
				const auto nameBegin = at;

				while (at < text.size() && text[at] != close && text[at] != '\n') { advance(); }

				includes.push_back({text.substr(nameBegin, at - nameBegin), "", directiveLine, angled});

			} else if (word == "define" && isIdentifierStart(static_cast<unsigned char>(peek()))) {
				const auto nameLine = line;
				const auto nameIndex = index;
				const auto nameBegin = at;

				while (at < text.size() && isIdentifierPart(static_cast<unsigned char>(text[at]))) { advance(); }

				macros.push_back({Token::Type::identifier, text.substr(nameBegin, at - nameBegin), nameLine, nameIndex});
			}

			// the rest of the logical line
			while (at < text.size() && text[at] != '\n') {
				if (text[at] == '\\' && peek(1) == '\n') {
					advance();
				}

				advance();
			}
		}
	};


	//
	//	Declarations
	//

	bool isOneOf(const std::string& word, std::initializer_list<const char*> words) {
		for (auto w : words) {
			if (word == w) {
				return true;
			}
		}

		return false;
	}

	bool isStatementKeyword(const std::string& word) {
		return isOneOf(word, {"return", "if", "else", "for", "while", "do", "switch", "case", "default", "break", "continue", "discard", "goto", "throw", "delete", "new"});
	}

	struct Scope {
		enum class Type {
			named,			// a namespace or a type: its name qualifies what is in it
			transparent,	// a cbuffer or extern "C": what is in it is outside
			enumeration,
			body			// a function's or anything else: skipped
		};

		Type type;
		std::string name;
		bool isType = false;
		bool scopedEnum = false;
	};

	struct Parser {
		const std::vector<Token>& tokens;
		const std::string& document;
		std::vector<SourceIndex::Symbol>& symbols;
		std::vector<Scope> scopes;

		Parser(const std::vector<Token>& t, const std::string& d, std::vector<SourceIndex::Symbol>& s) : tokens(t), document(d), symbols(s) {}

		std::string qualifier() const {
			std::string q;

			for (const auto& scope : scopes) {
				if (scope.type == Scope::Type::named && !scope.name.empty()) {
					q += scope.name + "::";
				}
			}

			return q;
		}

		bool inType() const {
			for (auto i = scopes.rbegin(); i != scopes.rend(); i++) {
				if (i->type == Scope::Type::named) {
					return i->isType;
				}
			}

			return false;
		}

		void add(const Token& name, const std::string& declared, SourceIndex::Kind kind, bool definition, const std::string& prefix) {
			symbols.push_back({declared, prefix + declared, kind, document, name.line, name.index, definition});
		}

		// the first token of a declaration after attributes ([numthreads(8, 8, 1)]), template
		// headers and specifiers that say nothing about what it declares
		size_t start(const std::vector<Token>& d) const {
			size_t i = 0;

			while (i < d.size()) {
				if (d[i].text == "[") {
					int depth = 0;

					for (; i < d.size(); i++) {
						if (d[i].text == "[") { depth++; }
						else if (d[i].text == "]" && --depth == 0) { i++; break; }
					}

				} else if (d[i].text == "template" && i + 1 < d.size() && d[i + 1].text == "<") {
					int depth = 0;

					for (i++; i < d.size(); i++) {
						if (d[i].text == "<") { depth++; }
						else if (d[i].text == ">" && --depth == 0) { i++; break; }
					}

				} else if (isOneOf(d[i].text, {"export", "inline", "extern", "static", "constexpr", "virtual", "explicit", "friend"}) && !(d[i].text == "extern" && i + 1 < d.size() && d[i + 1].type == Token::Type::string)) {
					i++;

				} else {
					break;
				}
			}

			return i;
		}

		// a function: a name and its qualifiers right before the first '(' at depth 0, with a type
		// before them and no '=' before; the name's first token in `nameAt`
		bool function(const std::vector<Token>& d, size_t first, size_t* nameAt, std::string* name) const {
			int depth = 0;

			for (size_t i = first; i < d.size(); i++) {
				const auto& t = d[i].text;

				if (t == "=" || (t == ":" && depth == 0) || t == ";") {
					return false;

				} else if (t == "[") {
					depth++;

				} else if (t == "]") {
					depth--;

				} else if (t == "(" && depth == 0) {
					if (i == first || d[i - 1].type != Token::Type::identifier || isStatementKeyword(d[i - 1].text)) {
						return false;
					}

					auto n = i - 1;

					while (n >= first + 2 && d[n - 1].text == "::" && d[n - 2].type == Token::Type::identifier) {
						n -= 2;
					}

					// the return type: one token at least ("operator" overloads are skipped)
					if (n == first || d[n - 1].text == "operator") {
						return false;
					}

					*nameAt = n;
					name->clear();

					for (auto k = n; k < i; k++) {
						*name += d[k].text;
					}

					return true;
				}
			}

			return false;
		}

		// struct X, class X, enum [class] X, ...: the keyword's position, or npos
		size_t typeKeyword(const std::vector<Token>& d, size_t first, const std::initializer_list<const char*>& words) const {
			for (auto i = first; i < d.size(); i++) {
				if (d[i].type == Token::Type::identifier && isOneOf(d[i].text, words)) {
					return i;
				}

				if (d[i].text == "(" || d[i].text == "=") {
					break;
				}
			}

			return std::string::npos;
		}

		// a statement ending with '{': what it opens
		void open(const std::vector<Token>& d) {
			const auto first = start(d);
			const auto prefix = qualifier();
			size_t nameAt;
			std::string name;

			if (first < d.size() && d[first].text == "namespace") {
				std::string full;
				const Token* where = nullptr;

				for (auto i = first + 1; i < d.size(); i++) {
					if (d[i].type == Token::Type::identifier) {
						if (!where) { where = &d[i]; }
						full += d[i].text;

					} else if (d[i].text == "::") {
						full += "::";
					}
				}

				if (where) {
					add(*where, full, SourceIndex::Kind::namespaceScope, true, prefix);
				}

				scopes.push_back({Scope::Type::named, full});

			} else if (first + 1 < d.size() && d[first].text == "extern" && d[first + 1].type == Token::Type::string) {
				scopes.push_back({Scope::Type::transparent, ""});

			} else if (function(d, first, &nameAt, &name)) {
				add(d[nameAt], name, SourceIndex::Kind::function, true, prefix);
				scopes.push_back({Scope::Type::body, ""});

			} else if (auto e = typeKeyword(d, first, {"enum"}); e != std::string::npos) {
				auto i = e + 1;
				const auto scoped = i < d.size() && (d[i].text == "class" || d[i].text == "struct");

				if (scoped) { i++; }

				if (i < d.size() && d[i].type == Token::Type::identifier) {
					add(d[i], d[i].text, SourceIndex::Kind::enumeration, true, prefix);
					scopes.push_back({Scope::Type::enumeration, d[i].text, false, scoped});

				} else {
					scopes.push_back({Scope::Type::enumeration, "", false, false});
				}

			} else if (auto k = typeKeyword(d, first, {"struct", "class", "union", "interface", "cbuffer", "tbuffer"}); k != std::string::npos) {
				const auto block = d[k].text == "cbuffer" || d[k].text == "tbuffer";
				auto i = k + 1;

				while (i < d.size() && d[i].text == "[") {	// C++ attributes after the keyword
					while (i < d.size() && d[i].text != "]") { i++; }
					i++;
				}

				if (i < d.size() && d[i].type == Token::Type::identifier) {
					add(d[i], d[i].text, SourceIndex::Kind::structure, true, prefix);
					scopes.push_back(block ? Scope{Scope::Type::transparent, ""} : Scope{Scope::Type::named, d[i].text, true});

				} else {
					scopes.push_back(block ? Scope{Scope::Type::transparent, ""} : Scope{Scope::Type::named, "", true});
				}

			} else {
				scopes.push_back({Scope::Type::body, ""});
			}
		}

		// a statement ending with ';' at namespace or type scope
		void declare(const std::vector<Token>& d) {
			const auto first = start(d);

			if (first >= d.size() || d[first].type != Token::Type::identifier || isStatementKeyword(d[first].text)) {
				return;
			}

			const auto prefix = qualifier();
			const auto& word = d[first].text;
			size_t nameAt;
			std::string name;

			if (word == "typedef") {
				// the last identifier outside brackets: typedef float4 color; typedef int (*f)(int);
				const Token* alias = nullptr;
				int depth = 0;

				for (auto i = first + 1; i < d.size(); i++) {
					if (d[i].text == "[" || d[i].text == "(") { depth++; }
					else if (d[i].text == "]" || d[i].text == ")") { depth--; }
					else if (d[i].type == Token::Type::identifier && (depth == 0 || (i > 0 && d[i - 1].text == "*"))) { alias = &d[i]; }
				}

				if (alias) {
					add(*alias, alias->text, SourceIndex::Kind::typeAlias, true, prefix);
				}

			} else if (word == "using") {
				if (first + 2 < d.size() && d[first + 1].type == Token::Type::identifier && d[first + 2].text == "=") {
					add(d[first + 1], d[first + 1].text, SourceIndex::Kind::typeAlias, true, prefix);
				}

			} else if (isOneOf(word, {"struct", "class", "union", "enum", "interface"}) && d.size() - first <= 3 &&
			           d.back().type == Token::Type::identifier) {
				add(d.back(), d.back().text, word == "enum" ? SourceIndex::Kind::enumeration : SourceIndex::Kind::structure, false, prefix);

			} else if (function(d, first, &nameAt, &name)) {
				add(d[nameAt], name, SourceIndex::Kind::function, false, prefix);

			} else {
				variables(d, first, prefix);
			}
		}

		// type name [= value | : semantic | [size]] [, name ...]
		void variables(const std::vector<Token>& d, size_t first, const std::string& prefix) {
			const auto kind = inType() ? SourceIndex::Kind::member : SourceIndex::Kind::variable;
			int depth = 0;
			bool initializing = false;	// past '=' or ':' of the current declarator: its name is known
			bool declarator = false;	// a later declarator, after a comma: no type before its name
			size_t i = first;

			auto name = [&](size_t at) {
				// the identifier before `at`, with a type before it unless it follows a comma
				if (at > first && d[at - 1].type == Token::Type::identifier && (declarator || at - 1 > first)) {
					add(d[at - 1], d[at - 1].text, kind, true, prefix);
				}
			};

			for (; i < d.size(); i++) {
				const auto& t = d[i].text;

				if (t == "(" || t == "[" || t == "{" || (t == "<" && !initializing)) {
					if (depth == 0 && !initializing && t == "(") {
						return;	// a call or a macro, not a declaration
					}

					if (depth == 0 && !initializing && t == "[") {
						name(i);
						initializing = true;
					}

					depth++;

				} else if (t == ")" || t == "]" || t == "}" || (t == ">" && !initializing)) {
					depth--;

				} else if (depth == 0 && (t == "=" || t == ":") && !initializing) {
					name(i);
					initializing = true;

				} else if (depth == 0 && t == ",") {
					if (!initializing) {
						name(i);
					}

					initializing = false;
					declarator = true;
					first = i + 1;
				}
			}

			if (!initializing) {
				name(d.size());
			}
		}

		void run() {
			std::vector<Token> statement;
			int parentheses = 0;	// '{' and ';' inside them belong to the statement
			int initializer = 0;	// braces of a value: float a[2] = { 1, 2 };
			bool expectEnumerator = true;

			for (const auto& token : tokens) {
				const auto& t = token.text;
				const auto scope = scopes.empty() ? Scope::Type::named : scopes.back().type;

				if (scope == Scope::Type::body) {
					if (t == "{") { scopes.push_back({Scope::Type::body, ""}); }
					else if (t == "}") { scopes.pop_back(); }
					continue;
				}

				if (scope == Scope::Type::enumeration) {
					if (t == "(" || t == "[") { parentheses++; }
					else if (t == ")" || t == "]") { parentheses--; }
					else if (t == "," && parentheses == 0) { expectEnumerator = true; }

					else if (t == "}") {
						scopes.pop_back();
						expectEnumerator = true;
						statement.clear();

					} else if (expectEnumerator && token.type == Token::Type::identifier) {
						const auto& e = scopes.back();
						auto prefix = qualifier();

						if (e.scopedEnum) {
							prefix += e.name + "::";
						}

						add(token, t, SourceIndex::Kind::enumerator, true, prefix);
						expectEnumerator = false;
					}

					continue;
				}

				if (t == "(") { parentheses++; }
				else if (t == ")") { parentheses = std::max(0, parentheses - 1); }

				if (parentheses > 0) {
					statement.push_back(token);

				} else if (initializer > 0) {
					if (t == "{") { initializer++; }
					else if (t == "}") { initializer--; }
					statement.push_back(token);

				} else if (t == "{") {
					const auto value = std::any_of(statement.begin(), statement.end(), [](const Token& s) { return s.text == "="; });

					if (value) {
						initializer = 1;
						statement.push_back(token);

					} else {
						open(statement);
						statement.clear();
					}

				} else if (t == "}") {
					if (!scopes.empty()) {
						scopes.pop_back();
					}

					statement.clear();

				} else if (t == ";") {
					declare(statement);
					statement.clear();

				} else if (t == ":" && statement.size() == 1 && isOneOf(statement[0].text, {"public", "private", "protected"})) {
					statement.clear();

				} else {
					statement.push_back(token);
				}
			}
		}
	};
}


//
//	SourceIndex::Normalize
//

std::string SourceIndex::Normalize(const std::string& name) {
	return std::filesystem::path(name).lexically_normal().generic_string();
}


//
//	SourceIndex::SetDocument
//

void SourceIndex::SetDocument(const std::string& name, const std::string& text) {
	const auto key = Normalize(name);
	Tokenizer tokenizer(text);
	Parsed parsed;
	parsed.includes = std::move(tokenizer.includes);

	for (const auto& macro : tokenizer.macros) {
		parsed.symbols.push_back({macro.text, macro.text, Kind::macro, key, macro.line, macro.index, true});
	}

	Parser(tokenizer.tokens, key, parsed.symbols).run();
	std::stable_sort(parsed.symbols.begin(), parsed.symbols.end(), [](const Symbol& a, const Symbol& b) {
		return a.line != b.line ? a.line < b.line : a.index < b.index;
	});

	documents[key] = std::move(parsed);
	resolved = false;
}


//
//	SourceIndex::RemoveDocument
//

void SourceIndex::RemoveDocument(const std::string& name) {
	documents.erase(Normalize(name));
	resolved = false;
}


//
//	SourceIndex::HasDocument
//

bool SourceIndex::HasDocument(const std::string& name) const {
	return documents.find(Normalize(name)) != documents.end();
}


//
//	SourceIndex::GetDocuments
//

std::vector<std::string> SourceIndex::GetDocuments() const {
	std::vector<std::string> names;

	for (const auto& document : documents) {
		names.push_back(document.first);
	}

	return names;
}


//
//	SourceIndex::SetIncludeFolders
//

void SourceIndex::SetIncludeFolders(const std::vector<std::string>& folders) {
	includeFolders = folders;
	resolved = false;
}


//
//	SourceIndex::GetIncludes
//

std::vector<SourceIndex::Include> SourceIndex::GetIncludes(const std::string& name) const {
	const auto key = Normalize(name);
	const auto document = documents.find(key);

	if (document == documents.end()) {
		return {};
	}

	auto includes = document->second.includes;

	for (auto& include : includes) {
		include.document = resolve(key, include);
	}

	return includes;
}


//
//	SourceIndex::GetIncludedBy
//

std::vector<std::string> SourceIndex::GetIncludedBy(const std::string& name) const {
	resolve();
	const auto found = includedBy.find(Normalize(name));
	return found == includedBy.end() ? std::vector<std::string>() : std::vector<std::string>(found->second.begin(), found->second.end());
}


//
//	SourceIndex::GetIncludeClosure
//

std::vector<std::string> SourceIndex::GetIncludeClosure(const std::string& name) const {
	resolve();
	const auto root = Normalize(name);
	std::vector<std::string> closure;
	std::set<std::string> seen{root};

	std::function<void(const std::string&)> visit = [&](const std::string& document) {
		const auto found = includesOf.find(document);

		if (found != includesOf.end()) {
			for (const auto& included : found->second) {
				if (seen.insert(included).second) {
					closure.push_back(included);
					visit(included);
				}
			}
		}
	};

	visit(root);
	return closure;
}


//
//	SourceIndex::GetSymbols
//

std::vector<SourceIndex::Symbol> SourceIndex::GetSymbols(const std::string& name) const {
	const auto document = documents.find(Normalize(name));
	return document == documents.end() ? std::vector<Symbol>() : document->second.symbols;
}


//
//	SourceIndex::FindDefinitions
//

std::vector<SourceIndex::Symbol> SourceIndex::FindDefinitions(const std::string& identifier, const std::string& from) const {
	const auto origin = Normalize(from);

	// nearness: 0 the document, 1 what it includes, 2 the documents including it and what they include
	std::map<std::string, int> nearness{{origin, 0}};

	for (const auto& included : GetIncludeClosure(origin)) {
		nearness.emplace(included, 1);
	}

	std::vector<std::string> pending{origin};
	std::set<std::string> includers;

	while (!pending.empty()) {
		const auto document = pending.back();
		pending.pop_back();

		for (const auto& includer : GetIncludedBy(document)) {
			if (includers.insert(includer).second) {
				pending.push_back(includer);
			}
		}
	}

	for (const auto& includer : includers) {
		nearness.emplace(includer, 2);

		for (const auto& included : GetIncludeClosure(includer)) {
			nearness.emplace(included, 2);
		}
	}

	// the qualified name is the identifier, or ends with "::" and the identifier
	auto matches = [&](const Symbol& symbol) {
		const auto& q = symbol.qualified;

		return q == identifier || (q.size() > identifier.size() + 1 && q.compare(q.size() - identifier.size(), identifier.size(), identifier) == 0 &&
			q[q.size() - identifier.size() - 1] == ':');
	};

	std::vector<Symbol> found;

	for (const auto& document : documents) {
		for (const auto& symbol : document.second.symbols) {
			if (matches(symbol)) {
				found.push_back(symbol);
			}
		}
	}

	auto rank = [&](const Symbol& symbol) {
		const auto near = nearness.find(symbol.document);
		return near == nearness.end() ? 3 : near->second;
	};

	std::stable_sort(found.begin(), found.end(), [&](const Symbol& a, const Symbol& b) {
		if (rank(a) != rank(b)) { return rank(a) < rank(b); }
		if (a.definition != b.definition) { return a.definition; }
		if (a.document != b.document) { return a.document < b.document; }
		return a.line < b.line;
	});

	return found;
}


//
//	SourceIndex::resolve
//

void SourceIndex::resolve() const {
	if (resolved) {
		return;
	}

	includesOf.clear();
	includedBy.clear();

	for (const auto& [name, parsed] : documents) {
		auto& list = includesOf[name];

		for (const auto& include : parsed.includes) {
			const auto document = resolve(name, include);

			if (!document.empty()) {
				list.push_back(document);
				includedBy[document].insert(name);
			}
		}
	}

	resolved = true;
}

std::string SourceIndex::resolve(const std::string& includer, const Include& include) const {
	std::vector<std::string> candidates;

	if (!include.angled) {
		const auto folder = std::filesystem::path(includer).parent_path().generic_string();
		candidates.push_back(folder.empty() ? include.text : folder + "/" + include.text);
	}

	for (const auto& folder : includeFolders) {
		candidates.push_back(folder + "/" + include.text);
	}

	candidates.push_back(include.text);

	for (const auto& candidate : candidates) {
		const auto name = Normalize(candidate);

		if (documents.find(name) != documents.end()) {
			return name;
		}
	}

	return "";
}
