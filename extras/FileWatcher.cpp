//	TextEditor - A syntax highlighting text editor for Dear ImGui.
//	FileWatcher, an addon of the soufianekhiat/ImGuiColorTextEdit fork.
//
//	This work is licensed under the terms of the MIT license.
//	For a copy, see <https://opensource.org/licenses/MIT>.


//
//	Include files
//

#include <algorithm>

#include "FileWatcher.h"


//
//	FileWatcher::Watch
//

void FileWatcher::Watch(const std::filesystem::path& folder, const std::vector<std::string>& extensions) {
	Folder added{folder, extensions};
	const auto found = look({added});
	std::lock_guard<std::mutex> lock(mutex);
	folders.push_back(added);
	known.insert(found.begin(), found.end());
}


//
//	FileWatcher::Start
//

void FileWatcher::Start(std::chrono::milliseconds interval) {
	if (running.exchange(true)) {
		return;
	}

	thread = std::thread([this, interval]() {
		std::mutex pause;
		std::unique_lock<std::mutex> lock(pause);

		while (running.load()) {
			Scan();
			wake.wait_for(lock, interval, [this]() { return !running.load(); });
		}
	});
}


//
//	FileWatcher::Stop
//

void FileWatcher::Stop() {
	if (running.exchange(false)) {
		wake.notify_all();
	}

	if (thread.joinable()) {
		thread.join();
	}
}


//
//	FileWatcher::Scan
//

void FileWatcher::Scan() {
	std::lock_guard<std::mutex> one(scanning);
	std::vector<Folder> watched;

	{
		std::lock_guard<std::mutex> lock(mutex);
		watched = folders;
	}

	// the file system is read without the lock, so Poll never waits for it
	auto found = look(watched);
	std::lock_guard<std::mutex> lock(mutex);

	for (const auto& [path, stamp] : found) {
		const auto previous = known.find(path);

		if (previous == known.end()) {
			events.push_back({path, Change::added});

		} else if (previous->second != stamp) {
			events.push_back({path, Change::modified});
		}
	}

	for (const auto& [path, stamp] : known) {
		if (found.find(path) == found.end()) {
			events.push_back({path, Change::removed});
		}
	}

	known = std::move(found);
}


//
//	FileWatcher::Poll
//

std::vector<FileWatcher::Event> FileWatcher::Poll() {
	std::lock_guard<std::mutex> lock(mutex);
	std::vector<Event> taken;
	taken.swap(events);
	return taken;
}


//
//	FileWatcher::look
//

std::map<std::filesystem::path, FileWatcher::Stamp> FileWatcher::look(const std::vector<Folder>& watched) const {
	std::map<std::filesystem::path, Stamp> found;

	for (const auto& folder : watched) {
		std::error_code ec;

		for (auto i = std::filesystem::recursive_directory_iterator(folder.path, ec); !ec && i != std::filesystem::recursive_directory_iterator(); i.increment(ec)) {
			if (!i->is_regular_file(ec)) {
				continue;
			}

			const auto extension = i->path().extension().string();

			if (!folder.extensions.empty() && std::find(folder.extensions.begin(), folder.extensions.end(), extension) == folder.extensions.end()) {
				continue;
			}

			// a file written while it is looked at shows up at the next look
			std::error_code timeError, sizeError;
			const auto time = i->last_write_time(timeError);
			const auto size = i->file_size(sizeError);

			if (!timeError && !sizeError) {
				found[i->path().lexically_normal()] = Stamp{time, size};
			}
		}
	}

	return found;
}
