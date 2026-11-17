# Go to Definition, Include Trees and File Watching

The text editor edits one document; an application editing several (an IDE, a shader
editor) usually wants to know how they relate and to jump between them. The editor
provides the keyboard side and two optional companion classes in the **extras** folder
provide the rest. Neither needs a compiler, a language server or anything beyond C++17.

### Go to Definition

Alt+G calls the application with the identifier under the main cursor and where it
starts. An identifier following "::" comes with its qualifiers: the cursor on **load** in
`normal::load(pixel)` gives "normal::load". The editor does not look anything up; the
application does (SourceIndex below can) and shows the result, in this editor or another
one. As other callbacks, it runs while the editor handles input, so a callback that opens
another document is best deferred until Render returns.

```c++
std::string requested;
editor.SetGoToDefinitionCallback([&](const std::string& identifier, TextEditor::DocPos start) {
	requested = identifier;
});

...
editor.Render("Shader");

if (!requested.empty()) {
	auto found = index.FindDefinitions(requested, currentDocument);
	requested.clear();

	if (!found.empty()) {
		openDocument(found[0].document);
		editor.SetCursor(TextEditor::DocPos(found[0].line, found[0].index));
		editor.ScrollToLine(found[0].line);
	}
}
```

**GetIdentifierAt** gives the same identifier for any position.

### SourceIndex

SourceIndex keeps what a set of C-like documents (HLSL, GLSL, C, C++) include and declare,
as the application sets them (**SetDocument** again after an edit, **RemoveDocument** when
one goes). Documents are named by their paths, or any names, compared lexically normalized.

- The include tree: **GetIncludes** (each include resolved to a document when one matches:
  relative to the including document's folder, then the folders given to
  **SetIncludeFolders**, then the name as written), **GetIncludedBy** and
  **GetIncludeClosure**.
- The symbol table: **GetSymbols** lists a document's functions, structs and classes
  (cbuffers and tbuffers too, whose members are globals), namespaces, enums and their
  values, typedefs and using aliases, macros, global variables and members, each with its
  qualified name (a::b::name), its position (line and code point, as DocPos) and whether
  it is a definition or only a declaration.
- **FindDefinitions** finds the symbols an identifier names, nearest first: in the
  document, in what it includes, in the documents including it (the translation units it
  is part of) and what those include, then anywhere; definitions before declarations.

A tokenizer and rules for declarations as they are usually written stand in for a
compiler: function bodies are skipped (locals are not symbols), every branch of
conditional compilation is read, and macros are not expanded.

### FileWatcher

FileWatcher reports the files added, modified or removed under watched folders, with an
extension filter. It compares write times and sizes through std::filesystem between looks,
so it runs on any platform C++17 does. **Start** looks on a thread of its own at an
interval, **Scan** looks on the calling thread, and **Poll** hands the changes found since
its last call to the application's thread.

```c++
FileWatcher watcher;
watcher.Watch("shaders", {".hlsl", ".hlsli"});
watcher.Start(std::chrono::milliseconds(250));

...
for (auto& event : watcher.Poll()) {
	if (event.change == FileWatcher::Change::removed) {
		index.RemoveDocument(event.path.generic_string());

	} else {
		index.SetDocument(event.path.generic_string(), readFile(event.path));
	}
}
```
