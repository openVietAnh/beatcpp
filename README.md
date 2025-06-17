# 🔧 CLI Media Browser – Dependency Installation Guide

This guide helps you install all required dependencies to build and run the CLI Media Browser on **Linux** (openSUSE, Ubuntu/Debian, Arch, Fedora).

---

## 🧱 Required Dependencies

- `ncurses` – Terminal UI rendering
- `mpv` – Media playback engine
- `nlohmann/json` – C++ JSON parsing (header-only)
- `cmake` – Build system
- `g++` – C++ compiler
- `make` – Required by CMake

---

## 🐧 openSUSE

```bash
sudo zypper install \
  ncurses-devel \
  mpv \
  nlohmann_json-devel \
  cmake \
  gcc-c++ \
  make
```

## Ubuntu / Debian
```bash
sudo apt update
sudo apt install \
  libncurses-dev \
  mpv \
  nlohmann-json3-dev \
  cmake \
  g++ \
  make
```

## Arch Linux / Manjaro
```bash
sudo pacman -S --needed \
  ncurses \
  mpv \
  nlohmann-json \
  cmake \
  gcc \
  make
```

## Fedora
```bash
sudo dnf install \
  ncurses-devel \
  mpv \
  json-devel \
  cmake \
  gcc-c++ \
  make
```