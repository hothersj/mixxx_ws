# Mixxx (Custom Build – WebSocket Integration)

This is a custom fork of Mixxx with additional functionality for **programmatic control via WebSockets**, enabling external scripts and services to interact with Mixxx (e.g. track loading, automation, AI DJ integration, etc.).

> ⚠️ Note: This fork introduces additional dependencies that are **not present in upstream Mixxx**, so default build workflows may fail without installing them.

---

## 🔧 Additional Dependency (Required)

Before building, you must install:

```bash
sudo apt install libwebsocketpp-dev
```

This is required for the WebSocket functionality added in this fork.
Without it, the build will fail due to missing headers.

---

## 📦 Build Environment Setup (Linux)

Follow the standard Mixxx setup for Debian/Ubuntu:

```bash
tools/debian_buildenv.sh setup
```

> This installs most dependencies required by Mixxx itself.
> You still need to install `libwebsocketpp-dev` separately (see above).

---

## 🛠️ Build Instructions

Once dependencies are installed:

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

After building, the `mixxx` executable will be available in the `build` directory.

---

## 🚀 What’s Different in This Fork

This fork includes:

* WebSocket-based control layer
* Programmatic track loading / importing
* Integration potential for external systems (e.g. AI DJ pipelines, automation scripts)

---

## ⚠️ Notes on CI / GitHub Actions

Upstream Mixxx CI workflows will likely fail on this fork due to the missing websocket dependency.

---

## 📌 Upstream Mixxx

This project is based on:

* https://github.com/mixxxdj/mixxx

All credit for the core application goes to the Mixxx developers.

---

## 📄 License

This project follows the same license as Mixxx (GPL).

---

If you're using this as part of another project, this repo is intended to act as a **drop-in modified Mixxx build** with extended control capabilities for my AI DJ Transition repo.