# Go to Definition, Back and Forward, Include Trees and File Watching

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

### Back and Forward

Jumps (to a definition, an error, another document) leave places behind; Back and Forward
go through them as undo and redo go through edits. Alt+Left and Alt+Right (Control+Minus and
Control+Shift+Minus on macOS, where Alt with the arrows moves by word) and the mouse's back
and forward buttons over the editor call the application, which keeps the places:
NavigationHistory (header only, in the **extras** folder) does. Before a jump, **Record**
where the cursor was; **Back** and **Forward** take where it is and give the place to go
to. A new jump drops the way forward, as a new edit drops redo, a step never stays where the
cursor already is, and **Forget** drops the places of a document that is gone.

```c++
NavigationHistory history;
std::optional<TextEditor::Navigation> navigate;
editor.SetNavigationCallback([&](TextEditor::Navigation direction) { navigate = direction; });

auto here = [&]() {
	auto pos = editor.GetMainCursorPosition();
	return NavigationHistory::Location{currentDocument, pos.line, pos.index};
};

...
// a jump
history.Record(here());
openDocument(found[0].document);

...
editor.Render("Shader");

if (navigate) {
	NavigationHistory::Location to;

	if (*navigate == TextEditor::Navigation::back ? history.Back(here(), to) : history.Forward(here(), to)) {
		openDocument(to.document);
		editor.SetCursor(TextEditor::DocPos(to.line, to.index));
	}

	navigate.reset();
}
```

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
