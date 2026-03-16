//  ./server <port> prender la moto

#include <iostream>
#include <string>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <chrono>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <atomic>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// Protobuf
#include "common.pb.h"
#include "register.pb.h"
#include "message_general.pb.h"
#include "message_dm.pb.h"
#include "change_status.pb.h"
#include "list_users.pb.h"
#include "get_user_info.pb.h"
#include "quit.pb.h"
#include "all_users.pb.h"
#include "for_dm.pb.h"
#include "broadcast_messages.pb.h"
#include "get_user_info_response.pb.h"
#include "server_response.pb.h"

#include "proto_framing.h"


static const int    INACTIVITY_TIMEOUT = 60; // inactividad
static const int    BACKLOG            = 10;

//json de la sesión del usuario
struct UserSession {
    std::string  username;
    std::string  ip;
    int          fd;
    chat::StatusEnum status;
    std::chrono::steady_clock::time_point last_activity;
};

//estado
static std::unordered_map<std::string, UserSession> g_users; //llave=username
static std::mutex g_mutex;
static std::atomic<bool> g_running{true};


static void send_server_response(int fd, int32_t code, const std::string& msg, bool ok) {
    chat::ServerResponse resp;
    resp.set_status_code(code);
    resp.set_message(msg);
    resp.set_is_successful(ok);
    std::string payload;
    resp.SerializeToString(&payload);
    send_message(fd, MSG_SERVER_RESPONSE, payload);
}


static void broadcast_to_all(uint8_t type, const std::string& payload,
                              const std::string& exclude_username = "") {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& [uname, session] : g_users) {
        if (uname == exclude_username) continue;
        // Don't send to INVISIBLE users (they still receive, just not listed)
        send_message(session.fd, type, payload);
    }
}


static bool handle_register(int fd, const std::string& ip, const std::string& payload) {
    chat::Register msg;
    if (!msg.ParseFromString(payload)) {
        send_server_response(fd, SC_ERROR_GENERAL, "Mensaje fallido WOMP WOMP", false);
        return false;
    }

    std::string username = msg.username();
    if (username.empty()) {
        send_server_response(fd, SC_ERROR_GENERAL, "Sin usuario", false);
        return false;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    // no usuario repetido
    if (g_users.count(username)) {
        send_server_response(fd, SC_ERROR_USERNAME_TAKEN,
                             "No pueden haber 2: '" + username + "' en resumen cambia tu nombre ", false);
        return false;
    }
    // no ip
    for (auto& [u, s] : g_users) {
        if (s.ip == ip) {
            send_server_response(fd, SC_ERROR_IP_TAKEN,
                                 "Misma IP '" + u + "'", false);
            return false;
        }
    }

    // registro
    UserSession session;
    session.username      = username;
    session.ip            = ip;
    session.fd            = fd;
    session.status        = chat::ACTIVE;
    session.last_activity = std::chrono::steady_clock::now();
    g_users[username]     = session;

    std::cout << "SERVER: Se registrado " << username << " @ " << ip << "\n";
    send_server_response(fd, SC_REGISTERED, "Bienvenido, " + username + ", sin luz", true);
    return true;
}

static void handle_message_general(const std::string& sender_username, const std::string& payload) {
    chat::MessageGeneral msg;
    if (!msg.ParseFromString(payload)) return;

    // actividad
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_users.count(sender_username))
            g_users[sender_username].last_activity = std::chrono::steady_clock::now();
    }

    // pedidos ya
    chat::BroadcastDelivery bcast;
    bcast.set_message(msg.message());
    bcast.set_username_origin(msg.username_origin());
    std::string bcast_payload;
    bcast.SerializeToString(&bcast_payload);

    // mandar a todos
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& [uname, session] : g_users) {
        send_message(session.fd, MSG_BROADCAST, bcast_payload);
    }
}

static void handle_message_dm(const std::string& sender_username, int sender_fd,
                               const std::string& payload) {
    chat::MessageDM msg;
    if (!msg.ParseFromString(payload)) return;

    // actividad
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_users.count(sender_username))
            g_users[sender_username].last_activity = std::chrono::steady_clock::now();
    }

    std::string dest = msg.username_des();

    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_users.find(dest);
    if (it == g_users.end()) {
        // unlock
        lock.~lock_guard();
        send_server_response(sender_fd, SC_ERROR_USER_NOT_FOUND,
                             "No se encontro a '" + dest + "' no esta conectado", false);
        return;
    }

    chat::ForDm dm;
    dm.set_username_des(sender_username);
    dm.set_message(msg.message());
    std::string dm_payload;
    dm.SerializeToString(&dm_payload);

    // destino
    send_message(it->second.fd, MSG_FOR_DM, dm_payload);

    // condifrmar
    send_server_response(sender_fd, SC_OK, "Alabado sea mensaje " + dest, true);
}

static void handle_change_status(const std::string& username, int fd, const std::string& payload) {
    chat::ChangeStatus msg;
    if (!msg.ParseFromString(payload)) return;

    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_users.find(username);
    if (it == g_users.end()) return;

    it->second.status        = msg.status();
    it->second.last_activity = std::chrono::steady_clock::now();

    std::string status_str;
    switch (msg.status()) {
        case chat::ACTIVE:         status_str = "ACTIVE";          break;
        case chat::DO_NOT_DISTURB: status_str = "DO_NOT_DISTURB";  break;
        case chat::INVISIBLE:      status_str = "INVISIBLE";        break;
        default:                   status_str = "UNKNOWN";
    }
    std::cout << "SERVER: " << username << " cambio su estado " << status_str << "\n";
    send_server_response(fd, SC_OK, "estado cambiado a " + status_str, true);
}

static void handle_list_users(const std::string& username, int fd, const std::string& payload) {
    (void)payload;

    // actualizar actividad
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_users.count(username))
            g_users[username].last_activity = std::chrono::steady_clock::now();
    }

    chat::AllUsers all;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto& [uname, session] : g_users) {
            // No mostrar usuarios invisibles
            if (session.status == chat::INVISIBLE && uname != username) continue;
            all.add_usernames(uname);
            all.add_status(session.status);
        }
    }

    std::string out;
    all.SerializeToString(&out);
    send_message(fd, MSG_ALL_USERS, out);
}

static void handle_get_user_info(const std::string& username, int fd, const std::string& payload) {
    chat::GetUserInfo msg;
    if (!msg.ParseFromString(payload)) return;

    std::string target = msg.username_des();

    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_users.find(target);
    if (it == g_users.end()) {
        lock.~lock_guard();
        send_server_response(fd, SC_ERROR_USER_NOT_FOUND,
                             "User '" + target + "' not found", false);
        return;
    }

    chat::GetUserInfoResponse resp;
    resp.set_ip_address(it->second.ip);
    resp.set_username(it->second.username);
    resp.set_status(it->second.status);

    std::string out;
    resp.SerializeToString(&out);
    send_message(fd, MSG_GET_USER_INFO_RESP, out);
}

static void handle_quit(const std::string& username) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_users.erase(username);
    std::cout << "SERVER: Invasor desconectado: " << username << "\n";
}


// client thread

static void client_thread(int fd, std::string ip) {
    std::string username; // set after successful registration
    bool registered = false;

    uint8_t type;
    std::string payload;

    while (g_running) {
        if (!recv_message(fd, type, payload)) {
            // muerte inmediata
            break;
        }

        if (!registered) {
            if (type == MSG_REGISTER) {
                registered = handle_register(fd, ip, payload);
                if (registered) {
                    // Read the username back
                    chat::Register r;
                    r.ParseFromString(payload);
                    username = r.username();
                }
            } else {
                send_server_response(fd, SC_ERROR_GENERAL,
                                     "Registrate manin", false);
            }
            continue;
        }

        // registrar
        switch (type) {
            case MSG_GENERAL:       handle_message_general(username, payload);        break;
            case MSG_DM:            handle_message_dm(username, fd, payload);         break;
            case MSG_CHANGE_STATUS: handle_change_status(username, fd, payload);      break;
            case MSG_LIST_USERS:    handle_list_users(username, fd, payload);         break;
            case MSG_GET_USER_INFO: handle_get_user_info(username, fd, payload);      break;
            case MSG_QUIT:          handle_quit(username); goto done;
            default:
                send_server_response(fd, SC_ERROR_GENERAL,
                                     "?????", false);
        }
    }

done:
    if (registered) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_users.erase(username);
        std::cout << "SERVER: Sesión limpia: " << username << "\n";
    }
    close(fd);
}

//Hilo de inactividad
static void inactivity_monitor() {
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(10));

        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto& [uname, session] : g_users) {
            if (session.status == chat::ACTIVE) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    now - session.last_activity).count();
                if (elapsed >= INACTIVITY_TIMEOUT) {
                    session.status = chat::DO_NOT_DISTURB; // Proxy
                    std::cout << "SERVER: " << uname << "Inactivo\n";

                    // Notificar
                    chat::ServerResponse resp;
                    resp.set_status_code(SC_OK);
                    resp.set_message("Te volviste inactivo por inactivo por así decirlo inactivo");
                    resp.set_is_successful(true);
                    std::string out;
                    resp.SerializeToString(&out);
                    send_message(session.fd, MSG_SERVER_RESPONSE, out);
                }
            }
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <port>\n";
        return 1;
    }

    int port = std::atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        std::cerr << "Invalid port\n";
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    // Create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen"); return 1;
    }

    std::cout << "[SERVER] Listening on port " << port << "\n";
    std::cout << "[SERVER] Inactivity timeout: " << INACTIVITY_TIMEOUT << "s\n";

    // Start inactivity monitor
    std::thread(inactivity_monitor).detach();

    // Accept loop
    while (g_running) {
        sockaddr_in client_addr{};
        socklen_t   client_len = sizeof(client_addr);
        int client_fd = accept(server_fd,
                               reinterpret_cast<sockaddr*>(&client_addr),
                               &client_len);
        if (client_fd < 0) {
            if (g_running) perror("accept");
            continue;
        }

        std::string client_ip = inet_ntoa(client_addr.sin_addr);
        std::cout << "[SERVER] New connection from " << client_ip << "\n";

        // Detach a thread per client
        std::thread(client_thread, client_fd, client_ip).detach();
    }

    close(server_fd);
    return 0;
}
