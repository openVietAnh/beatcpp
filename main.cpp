#include <ncurses.h>
#include <filesystem>
#include <vector>
#include <string>
#include <unistd.h>
#include <sys/wait.h>
#include <csignal>
#include <iostream>
#include <chrono>
#include <thread>
#include <fstream>
#include <sys/socket.h>
#include <sys/un.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

const int MAX_VIEW = 10;

bool is_media_file(const fs::path& path) {
    return path.extension() == ".mp3";
}

void list_directory(const fs::path& dir, std::vector<std::string>& entries, std::vector<fs::path>& paths) {
    entries.clear();
    paths.clear();
    entries.emplace_back("Back");
    paths.emplace_back("..");
    entries.emplace_back("Quit");
    paths.emplace_back("");

    for (const auto& entry : fs::directory_iterator(dir)) {
        entries.push_back(entry.path().filename().string());
        paths.push_back(entry.path());
    }
}

pid_t media_pid = -1;
fs::path current_media;
bool is_paused = false;

void stop_media() {
    if (media_pid > 0) {
        kill(media_pid, SIGTERM);
        waitpid(media_pid, nullptr, 0);
        media_pid = -1;
        current_media.clear();
    }
}


void play_media(const fs::path& path) {
    stop_media();
    pid_t pid = fork();
    if (pid == 0) {
        execlp("mpv", "mpv", "--no-terminal", "--input-ipc-server=/tmp/mpv-socket", path.c_str(), nullptr);
        _exit(1);
    } else if (pid > 0) {
        media_pid = pid;
        current_media = path;
        is_paused = false;
    }
}

std::optional<float> get_playback_property(const std::string& socket_path, const std::string& property) {
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) return std::nullopt;

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) == -1) {
        close(fd);
        return std::nullopt;
    }

    const json request = {
        {"command", {"get_property", property}}
    };

    std::string payload = request.dump() + "\n";
    send(fd, payload.c_str(), payload.size(), 0);

    char buffer[512] = {};
    const int len = recv(fd, buffer, sizeof(buffer), 0);
    close(fd);

    if (len <= 0) return std::nullopt;

    try {
        auto response = json::parse(buffer);
        if (response.contains("error") && response["error"] == "success" && response.contains("data")) {
            return response["data"].get<float>();
        }
    } catch (...) {}

    return std::nullopt;
}

int main() {
    initscr();
    noecho();
    cbreak();
    keypad(stdscr, TRUE);
    curs_set(0);
    nodelay(stdscr, TRUE); // getch() won't block

    fs::path current_path = fs::current_path();
    std::vector<std::string> entries;
    std::vector<fs::path> paths;
    list_directory(current_path, entries, paths);

    int selected = 0;
    int scroll_offset = 0;

    while (true) {
        clear();
        mvprintw(0, 0, "Directory: %s", current_path.c_str());

        if (media_pid > 0) {
            auto position = get_playback_property("/tmp/mpv-socket", "time-pos");
            auto duration = get_playback_property("/tmp/mpv-socket", "duration");

            if (position && duration && *duration > 0.0f) {
                int bar_width = 50;
                float ratio = *position / *duration;
                int filled = static_cast<int>(ratio * bar_width);

                // std::string bar = "[" + std::string(filled, '#') + std::string(bar_width - filled, '-') + "]";

                int pos_sec = static_cast<int>(*position);
                int dur_sec = static_cast<int>(*duration);
                char time_buf[32];
                std::snprintf(time_buf, sizeof(time_buf), "%02d:%02d / %02d:%02d",
                              pos_sec / 60, pos_sec % 60, dur_sec / 60, dur_sec % 60);

                mvprintw(1, 0, "Now playing: %s %s", current_media.filename().string().c_str(), time_buf);
            } else {
                mvprintw(1, 0, "Now playing: %s (loading...)", current_media.filename().string().c_str());
            }
        }

        if (scroll_offset > std::max(0, (int)entries.size() - MAX_VIEW)) {
            scroll_offset = std::max(0, (int)entries.size() - MAX_VIEW);
        }
        int visible_count = std::min<int>(MAX_VIEW, entries.size());
        int list_start = scroll_offset;
        int list_end = std::min<int>(scroll_offset + visible_count, entries.size());

        bool show_scroll_up = scroll_offset > 0;
        bool show_scroll_down = (entries.size() > MAX_VIEW) && (scroll_offset + visible_count < entries.size());

        int base_row = 2;

        if (show_scroll_up) {
            mvprintw(base_row - 1, 0, "More above");
        }

        for (int i = list_start; i < list_end; ++i) {
            int screen_row = i - scroll_offset + base_row;
            if (i == selected) {
                attron(A_REVERSE);
                mvprintw(screen_row, 0, "%s", entries[i].c_str());
                attroff(A_REVERSE);
            } else {
                mvprintw(screen_row, 0, "%s", entries[i].c_str());
            }
        }

        if (show_scroll_down) {
            mvprintw(list_end - scroll_offset + base_row, 0, "More below");
        }

        refresh();

        int ch = getch();

        if (ch == KEY_UP) {
            if (selected > 0) {
                selected--;
                if (selected < scroll_offset) {
                    scroll_offset--;
                }
            }
        } else if (ch == KEY_DOWN) {
            if (selected < (int)entries.size() - 1) {
                selected++;
                if (selected >= scroll_offset + MAX_VIEW) {
                    scroll_offset++;
                }
            }
        } else if (ch == '\n' || ch == KEY_ENTER) {
            const fs::path& chosen = paths[selected];
            if (selected == 1) {
                break;
            } else if (selected == 0) {
                if (current_path.has_parent_path()) {
                    current_path = current_path.parent_path();
                    list_directory(current_path, entries, paths);
                    selected = 0;
                }
            } else if (fs::is_directory(chosen)) {
                current_path = chosen;
                list_directory(current_path, entries, paths);
                selected = 0;
            } else if (fs::is_regular_file(chosen) && is_media_file(chosen)) {
                play_media(chosen);
            }
        } else if (ch == 'q') {
            break;
        } else if (ch == 'p' && media_pid > 0) {
            kill(media_pid, is_paused ? SIGCONT : SIGSTOP);
            is_paused = !is_paused;
        } else if (ch == 's' && media_pid > 0) {
            stop_media();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    stop_media();
    endwin();
    return 0;
}
