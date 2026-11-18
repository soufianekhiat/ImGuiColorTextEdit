//	TextEditor - A syntax highlighting text editor for Dear ImGui.
//	NavigationHistory, an addon of the soufianekhiat/ImGuiColorTextEdit fork.
//
//	This work is licensed under the terms of the MIT license.
//	For a copy, see <https://opensource.org/licenses/MIT>.


#pragma once


//
//	Include files
//

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>


//
//	NavigationHistory
//
//	The places an application went, to go back and forward through them as undo and redo
//	do with edits (TextEditor::SetNavigationCallback asks for it). Before a jump (to a
//	definition, an error, another document), Record where the cursor was; Back and Forward
//	take where it is and give the place to go to. Places are a document's name, a line and
//	a code point on it, like TextEditor::DocPos.
//

class NavigationHistory {
public:
	struct Location {
		std::string document;
		size_t line = 0;
		size_t index = 0;

		// one place per line
		inline bool operator==(const Location& other) const { return document == other.document && line == other.line; }
		inline bool operator!=(const Location& other) const { return !(*this == other); }
	};

	// before a jump: the place it leaves (what lay forward goes, as a new edit drops redo)
	inline void Record(const Location& from) {
		forwardPlaces.clear();

		if (backPlaces.empty() || backPlaces.back() != from) {
			backPlaces.push_back(from);

			if (backPlaces.size() > limit) {
				backPlaces.erase(backPlaces.begin());
			}
		}
	}

	// the place to go back (or forward) to from `current`, which becomes the way forward (or back)
	inline bool Back(const Location& current, Location& to) { return step(backPlaces, forwardPlaces, current, to); }
	inline bool Forward(const Location& current, Location& to) { return step(forwardPlaces, backPlaces, current, to); }

	inline bool CanGoBack() const { return !backPlaces.empty(); }
	inline bool CanGoForward() const { return !forwardPlaces.empty(); }

	// a document gone: its places go too
	inline void Forget(const std::string& document) {
		for (auto* places : {&backPlaces, &forwardPlaces}) {
			places->erase(std::remove_if(places->begin(), places->end(), [&](const Location& l) { return l.document == document; }), places->end());
		}
	}

	inline void Clear() { backPlaces.clear(); forwardPlaces.clear(); }

	// places kept back (100 by default)
	inline void SetLimit(size_t places) { limit = places; }

private:
	static inline bool step(std::vector<Location>& from, std::vector<Location>& to, const Location& current, Location& place) {
		// a place where the cursor already is would be a step that does not move
		while (!from.empty() && from.back() == current) {
			from.pop_back();
		}

		if (from.empty()) {
			return false;
		}

		to.push_back(current);
		place = from.back();
		from.pop_back();
		return true;
	}

	std::vector<Location> backPlaces;
	std::vector<Location> forwardPlaces;
	size_t limit = 100;
};
