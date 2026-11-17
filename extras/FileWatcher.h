//	TextEditor - A syntax highlighting text editor for Dear ImGui.
//	FileWatcher, an addon of the soufianekhiat/ImGuiColorTextEdit fork.
//
//	This work is licensed under the terms of the MIT license.
//	For a copy, see <https://opensource.org/licenses/MIT>.


#pragma once


//
//	Include files
//

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>


//
//	FileWatcher
//
//	Reports the files added, modified or removed under watched folders. It
//	compares what std::filesystem says (write time and size) between looks,
//	so it runs wherever C++17 does, with no platform API. Looks happen on a
//	thread of its own (Start) or when asked (Scan); the application takes the
//	changes on its own thread with Poll.
//

class FileWatcher {
public:
	enum class Change {
		added,
		modified,
		removed
	};

	struct Event {
		std::filesystem::path path;
		Change change;
	};

	// constructor/destructor
	FileWatcher() = default;
	~FileWatcher() { Stop(); }

	// watch the files under a folder, recursively, those with one of the extensions (".hlsl"),
	// or all of them when there are none; the files there now are known, not reported
	void Watch(const std::filesystem::path& folder, const std::vector<std::string>& extensions={});

	// look every interval on a thread of its own, until Stop
	void Start(std::chrono::milliseconds interval=std::chrono::milliseconds(250));
	void Stop();
	inline bool IsRunning() const { return running.load(); }

	// look now, on the calling thread
	void Scan();

	// the changes found since the last call, in the order they were found
	std::vector<Event> Poll();

private:
	struct Folder {
		std::filesystem::path path;
		std::vector<std::string> extensions;
	};

	struct Stamp {
		std::filesystem::file_time_type time;
		std::uintmax_t size;

		inline bool operator!=(const Stamp& other) const { return time != other.time || size != other.size; }
	};

	std::map<std::filesystem::path, Stamp> look(const std::vector<Folder>& folders) const;

	std::mutex mutex;   // guards folders, known and events
	std::vector<Folder> folders;
	std::map<std::filesystem::path, Stamp> known;
	std::vector<Event> events;

	std::mutex scanning;   // one look at a time
	std::atomic_bool running = false;
	std::condition_variable wake;
	std::thread thread;
};
