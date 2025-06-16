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

namespace fs = std::filesystem;

pid_t media_pid = -1;
bool is_paused = false;
fs::path current_media;

void list_directory(const fs::path& path, std::vector<std::string>& entries, std::vector<fs::path>& paths) {
    entries.clear();
    paths.clear();

    entries.push_back("[Back]");
    paths.push_back(path.parent_path());

    entries.push_back("[Quit]");
    paths.push_back("");

    for (const auto& entry : fs::directory_iterator(path)) {
        entries.push_back(entry.path().filename().string());
        paths.push_back(entry.path());
    }
}

bool is_media_file(const fs::path& path) {
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
    }
}

void play_media(const fs::path& path) {
    stop_media(); // Stop existing

    media_pid = fork();
    if (media_pid == 0) {
        int devnull = open("/dev/null", O_RDWR);
        if (devnull != -1) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > 2) close(devnull);
        }

        execlp("mpv", "mpv", "--quiet", "--audio-display=no", "--terminal=no", path.c_str(), NULL);
        _exit(1);
    }

    current_media = path;
    is_paused = false;
}

int main() {
    initscr();
    noecho();
    cbreak();
    keypad(stdscr, TRUE);
    curs_set(0);

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
            mvprintw(1, 0, "Now playing: %s [%s] (p: pause/resume, s: stop)",
                     current_media.filename().string().c_str(),
                     is_paused ? "paused" : "playing");
        }

        for (int i = 0; i < entries.size(); ++i) {
            if (i == selected) {
                attron(A_REVERSE);
                mvprintw(i + 2, 0, "%s", entries[i].c_str());
                attroff(A_REVERSE);
            } else {
                mvprintw(i + 2, 0, "%s", entries[i].c_str());
            }
        }

        refresh();
        ch = getch();

        if (ch == KEY_UP && selected > 0) {
            selected--;
        } else if (ch == KEY_DOWN && selected < entries.size() - 1) {
            selected++;
        } else if (ch == '\n' || ch == KEY_ENTER) {
            const fs::path& chosen = paths[selected];
            if (selected == 1) { // Quit
                break;
            } else if (selected == 0) { // Back
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
