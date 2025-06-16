#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <ncurses.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <signal.h>
#include <algorithm>
#include <sys/socket.h>
#include <sys/un.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

const int MAX_VIEW = 10;
pid_t media_pid = -1;
bool is_paused = false;
fs::path current_media;

bool wait_for_socket(const std::string& socket_path, int timeout_ms = 1000) {
    int waited = 0;
    while (waited < timeout_ms) {
        if (fs::exists(socket_path)) {
            return true;
        }
        usleep(100 * 1000);
        waited += 100;
    }
    return false;
}

std::optional<float> get_playback_position(const std::string& socket_path, const std::string& property) {
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) return std::nullopt;

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        close(fd);
        return std::nullopt;
    }

    json request = {
        {"command", {"get_property", property}}
    };

    std::string payload = request.dump() + "\n";
    send(fd, payload.c_str(), payload.size(), 0);

    char buffer[512] = {};
    int len = recv(fd, buffer, sizeof(buffer), 0);
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

void list_directory(const fs::path &path, std::vector<std::string> &entries, std::vector<fs::path> &paths) {
    entries.clear();
    paths.clear();

    entries.push_back("[Back]");
    paths.push_back(path.parent_path());

    entries.push_back("[Quit]");
    paths.push_back("");

    for (const auto &entry: fs::directory_iterator(path)) {
        entries.push_back(entry.path().filename().string());
        paths.push_back(entry.path());
    }
}

bool is_media_file(const fs::path &path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".mp3" || ext == ".mp4" || ext == ".mkv" || ext == ".flac" || ext == ".wav" || ext == ".ogg";
}

void stop_media() {
    if (media_pid > 0) {
        kill(media_pid, SIGTERM);
        media_pid = -1;
        is_paused = false;
        current_media.clear();
        fs::remove("/tmp/mpv-socket"); // Clean up stale socket
    }
}

void play_media(const fs::path &path) {
    std::string socket_path = "/tmp/mpv-socket";
    media_pid = fork();
    if (media_pid == 0) {
        fclose(stdin);
        fclose(stdout);
        fclose(stderr);

        execlp("mpv", "mpv",
               "--quiet",
               "--audio-display=no",
               "--terminal=no",
               "--input-ipc-server=/tmp/mpv-socket",
               path.c_str(),
               NULL);
        _exit(1);
    }
    wait_for_socket(socket_path);
}

int main() {
    initscr();
    noecho();
    cbreak();
    keypad(stdscr, TRUE);
    curs_set(0);
    int scroll_offset = 0;

    fs::path current_path = fs::current_path();
    std::vector<std::string> entries;
    std::vector<fs::path> paths;
    list_directory(current_path, entries, paths);

    int selected = 0;
    int ch;

    while (true) {
        clear();
        mvprintw(0, 0, "Directory: %s", current_path.c_str());
        if (media_pid > 0) {
            auto position = get_playback_position("/tmp/mpv-socket", "time-pos");
            auto duration = get_playback_position("/tmp/mpv-socket", "duration");

            if (position && duration && *duration > 0.0f) {
                int bar_width = 50;
                float ratio = *position / *duration;
                int filled = static_cast<int>(ratio * bar_width);

                std::string bar = "[" + std::string(filled, '#') + std::string(bar_width - filled, '-') + "]";

                // Format time as mm:ss
                int pos_sec = static_cast<int>(*position);
                int dur_sec = static_cast<int>(*duration);

                char time_buf[32];
                std::snprintf(time_buf, sizeof(time_buf), "%02d:%02d / %02d:%02d",
                              pos_sec / 60, pos_sec % 60, dur_sec / 60, dur_sec % 60);

                mvprintw(1, 0, "Now playing: %s", current_media.filename().string().c_str());
                mvprintw(2, 0, "%s %s", bar.c_str(), time_buf);
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
        ch = getch();

        if (ch == KEY_UP) {
            if (selected > 0) {
                selected--;
                if (selected < scroll_offset) {
                    scroll_offset--;
                }
            }
        } else if (ch == KEY_DOWN) {
            if (selected < entries.size() - 1) {
                selected++;
                if (selected >= scroll_offset + MAX_VIEW) {
                    scroll_offset++;
                }
            }
        } else if (ch == '\n' || ch == KEY_ENTER) {
            const fs::path &chosen = paths[selected];
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
    }

    stop_media();
    endwin();
    return 0;
}
