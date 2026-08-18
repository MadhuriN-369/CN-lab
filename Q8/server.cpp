#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <algorithm>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 65439
#define BUFFER_SIZE 2048

struct Question {
    std::string prompt;
    std::vector<std::string> options;
    char correct_option; // 'A', 'B', 'C', or 'D'
};

// Global MCQ Question Bank
const std::vector<Question> quiz_questions = {
    {
        "What layer in the OSI model does TCP operate on?",
        {"A. Network Layer", "B. Transport Layer", "C. Data Link Layer", "D. Application Layer"},
        'B'
    },
    {
        "Which system call is used by a TCP server to accept incoming connections?",
        {"A. connect()", "B. bind()", "C. accept()", "D. listen()"},
        'C'
    },
    {
        "What is the default subnet mask for a Class C IP address?",
        {"A. 255.0.0.0", "B. 255.255.0.0", "C. 255.255.255.0", "D. 255.255.255.255"},
        'C'
    },
    {
        "Which protocol is connectionless and does not guarantee packet delivery?",
        {"A. TCP", "B. UDP", "C. HTTP", "D. FTP"},
        'B'
    }
};

// Thread-safe Leaderboard Management
std::unordered_map<std::string, int> leaderboard; // {username: score}
std::mutex leaderboard_mutex;

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

// Generate formatted leaderboard string
std::string get_leaderboard_str() {
    std::lock_guard<std::mutex> lock(leaderboard_mutex);
    std::vector<std::pair<std::string, int>> scores(leaderboard.begin(), leaderboard.end());
    
    // Sort descending by score
    std::sort(scores.begin(), scores.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });

    std::stringstream ss;
    ss << "\n=========================================\n";
    ss << "            QUIZ LEADERBOARD             \n";
    ss << "=========================================\n";
    ss << "Rank  | Player Name          | Score     \n";
    ss << "-----------------------------------------\n";

    int rank = 1;
    for (const auto& entry : scores) {
        ss << rank++ << "     | " 
           << entry.first << std::string(std::max(0, 20 - (int)entry.first.length()), ' ')
           << "| " << entry.second << " / " << quiz_questions.size() << "\n";
    }
    ss << "=========================================\n";
    return ss.str();
}

void handle_client(int client_fd, sockaddr_in client_addr) {
    char buffer[BUFFER_SIZE];

    // 1. Get Username
    const char* user_prompt = "ENTER_USERNAME\n";
    send(client_fd, user_prompt, strlen(user_prompt), MSG_NOSIGNAL);

    memset(buffer, 0, BUFFER_SIZE);
    ssize_t b = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
    if (b <= 0) {
        close(client_fd);
        return;
    }
    std::string username = trim(std::string(buffer));
    if (username.empty()) username = "Anonymous";

    std::cout << "[+] Quiz participant started: " << username << std::endl;

    int score = 0;
    int total_questions = quiz_questions.size();

    // 2. Deliver Questions Sequentially
    for (size_t i = 0; i < quiz_questions.size(); ++i) {
        const auto& q = quiz_questions[i];

        std::stringstream qss;
        qss << "\n[Question " << (i + 1) << "/" << total_questions << "]: " << q.prompt << "\n";
        for (const auto& opt : q.options) {
            qss << "  " << opt << "\n";
        }
        qss << "Your Answer [A/B/C/D]: ";

        std::string q_str = qss.str();
        send(client_fd, q_str.c_str(), q_str.length(), MSG_NOSIGNAL);

        // Receive response
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t bytes_recv = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_recv <= 0) {
            std::cout << "[-] Participant dropped mid-quiz: " << username << std::endl;
            close(client_fd);
            return;
        }

        std::string ans = trim(std::string(buffer));
        char user_choice = ' ';
        if (!ans.empty()) {
            user_choice = toupper(ans[0]);
        }

        // Validate answer
        if (user_choice == q.correct_option) {
            score++;
            const char* correct_msg = ">> Correct!\n";
            send(client_fd, correct_msg, strlen(correct_msg), MSG_NOSIGNAL);
        } else {
            std::string wrong_msg = ">> Incorrect! The correct answer was: " + std::string(1, q.correct_option) + "\n";
            send(client_fd, wrong_msg.c_str(), wrong_msg.length(), MSG_NOSIGNAL);
        }
    }

    // 3. Save Score to Leaderboard
    {
        std::lock_guard<std::mutex> lock(leaderboard_mutex);
        leaderboard[username] = score;
    }

    // 4. Send Final Score & Global Leaderboard
    std::stringstream final_report;
    final_report << "\n=========================================\n";
    final_report << " QUIZ COMPLETED! Final Score: " << score << " / " << total_questions << "\n";
    final_report << "=========================================\n";
    final_report << get_leaderboard_str();

    std::string report_str = final_report.str();
    send(client_fd, report_str.c_str(), report_str.length(), MSG_NOSIGNAL);

    std::cout << "[*] Quiz completed for " << username << ". Score: " << score << "/" << total_questions << std::endl;
    close(client_fd);
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 20) < 0) {
        perror("Listen failed");
        close(server_fd);
        return 1;
    }

    std::cout << "[*] Online Quiz Server listening on port " << PORT << "..." << std::endl;

    while (true) {
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &len);
        if (client_fd >= 0) {
            std::thread(handle_client, client_fd, client_addr).detach();
        }
    }

    close(server_fd);
    return 0;
}
