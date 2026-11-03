# C++ Custom Multiplayer Networking Architecture

## 📌 Project Overview
A custom client-server multiplayer architecture developed in **native C++** using **SFML** for a 2D shooter. This project demonstrates low-level network protocol implementation, custom data serialization, and a decoupled server architecture handling both reliable authentication and real-time gameplay.

## 🏗️ Architecture & Core Modules
The repository is structured into three main independent modules to separate secure connection logic from high-performance real-time networking:

* **`AA3_Server_TCP` (Authentication & Matchmaking):** A dedicated server handling reliable connections. It manages the secure login pipeline, credential validation, and the matchmaking logic to pair players before the session starts.
* **`AA3_Server_Gameplay_UDP` (Real-Time Game State):** A high-performance server dedicated to gameplay loop synchronization. It utilizes non-blocking UDP sockets to broadcast player positions, inputs, and game state updates, prioritizing speed and low latency over absolute reliability.
* **`AA3_Juego` (SFML Client):** The interactive game client. It connects to the TCP server for menus and matchmaking, and seamlessly transitions to the UDP server for in-game action.

## 🚀 Key Technical Features
* **Raw Sockets Implementation:** Built entirely with native TCP and UDP sockets without relying on high-level networking APIs (like Photon or Steamworks).
* **Custom Serialization:** Manual byte-packing and unpacking to minimize network payload size and optimize bandwidth usage.
* **Concurrency & Multithreading:** Server-side management of multiple simultaneous client connections.
* **Decoupled Systems:** Strict separation of concerns between backend services (login) and the real-time simulation loop.

## 🛠️ Tech Stack
* **Language:** C++
* **Graphics & Network Library:** SFML (Simple and Fast Multimedia Library)
* **Version Control:** Git / GitHub
