#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <sstream>
#include <vector>
#include <filesystem>
#include <cctype>
#include <unistd.h>
#include <sys/wait.h>
#include <fstream>

bool fileExecutableExists(std::string file_path);
void splitString(std::string text, char d, std::vector<std::string>& result);
void vectorOfStringsToArrayOfCharArrays(std::vector<std::string>& list, char ***result);
void freeArrayOfCharArrays(char **array, size_t array_length);

static const char* HISTORY_FILE = ".osshell_history";

static bool isWhitespaceOnly(const std::string& s) {
    for (char c : s) {
        if (!std::isspace(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

static void trimHistoryTo128(std::vector<std::string>& history) {
    while (history.size() > 128) {
        history.erase(history.begin());
    }
}

static void loadHistory(std::vector<std::string>& history) {
    history.clear();
    std::ifstream in(HISTORY_FILE);
    if (!in.is_open()) return;

    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && !isWhitespaceOnly(line)) {
            history.push_back(line);
        }
    }
    in.close();
    trimHistoryTo128(history);
}

static void saveHistory(const std::vector<std::string>& history) {
    std::ofstream out(HISTORY_FILE, std::ios::trunc);
    if (!out.is_open()) return;

    for (const auto& cmd : history) {
        out << cmd << "\n";
    }
    out.close();
}

static void historyAdd(std::vector<std::string>& history, const std::string& cmdLine) {
    history.push_back(cmdLine);
    trimHistoryTo128(history);
    saveHistory(history);
}

static void historyClear(std::vector<std::string>& history) {
    history.clear();
    saveHistory(history);
}

// Print history with EXACT spacing: "  1: cmd" (two leading spaces for 1-digit)
static void historyPrint(const std::vector<std::string>& history, int lastN = -1) {
    int startIndex = 0;
    if (lastN > 0 && lastN < (int)history.size()) {
        startIndex = (int)history.size() - lastN;
    }
    for (int i = startIndex; i < (int)history.size(); i++) {
        // width 3 gives: "  1", " 10", "128"
        printf("%3d: %s\n", i + 1, history[i].c_str());
    }
}

static bool parsePositiveIntStrict(const std::string& s, int& out) {
    // must be digits only
    if (s.empty()) return false;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    try {
        long long v = std::stoll(s);
        if (v <= 0 || v > 2147483647LL) return false;
        out = (int)v;
        return true;
    } catch (...) {
        return false;
    }
}

static bool resolveExecutablePath(
    const std::string& cmd,
    const std::vector<std::string>& os_path_list,
    std::string& exec_path
) {
    if (cmd.empty()) return false;

    // Advanced C behavior: direct execution for . or / commands.
    if (cmd[0] == '.' || cmd[0] == '/') {
        if (fileExecutableExists(cmd)) {
            exec_path = cmd;
            return true;
        }
        return false;
    }

    // Core A behavior: search PATH for the first executable match.
    for (const auto& dir : os_path_list) {
        std::string candidate = dir + "/" + cmd;
        if (fileExecutableExists(candidate)) {
            exec_path = candidate;
            return true;
        }
    }
    return false;
}

static void executeResolvedCommand(const std::string& exec_path, std::vector<std::string>& command_list) {
    char **command_list_exec = nullptr;
    vectorOfStringsToArrayOfCharArrays(command_list, &command_list_exec);

    pid_t pid = fork();
    if (pid == 0) {
        execv(exec_path.c_str(), command_list_exec);
        _exit(127);
    }

    if (pid > 0) {
        waitpid(pid, nullptr, 0);
    }

    freeArrayOfCharArrays(command_list_exec, command_list.size() + 1);
}

int main (int argc, char **argv)
{
    // Get list of paths to binary executables
    std::vector<std::string> os_path_list;
    char* os_path = getenv("PATH");
    if (os_path != nullptr) {
        splitString(os_path, ':', os_path_list);
    }

    // Create list to store history (load persisted history)
    std::vector<std::string> history;
    loadHistory(history);

    // Create variables for storing command user types
    std::string user_command;               // full line user typed
    std::vector<std::string> command_list;  // tokenized command

    // Welcome message (must match exactly)
    std::cout << "Welcome to OSShell! Please enter your commands ('exit' to quit)." << std::endl;

    while (true)
    {
        // Prompt (must match exactly, no newline)
        std::cout << "osshell> " << std::flush;

        if (!std::getline(std::cin, user_command)) {
            // EOF -> exit shell
            break;
        }

        // Blank input: just reprompt (no error)
        if (user_command.empty() || isWhitespaceOnly(user_command)) {
            continue;
        }

        // Tokenize the command line (supports double quotes via splitString)
        splitString(user_command, ' ', command_list);
        if (command_list.empty()) {
            continue;
        }

        std::string cmd = command_list[0];

        // Special command: exit (log it, then quit)
        if (cmd == "exit") {
            historyAdd(history, user_command);
            break;
        }

        // Special command: history (advanced behavior)
        if (cmd == "history") {
            // history
            if (command_list.size() == 1) {
                historyPrint(history);
                historyAdd(history, user_command);  // log normal history
                continue;
            }

            // history <arg>
            if (command_list.size() == 2) {
                const std::string& arg = command_list[1];

                if (arg == "clear") {
                    // Do NOT log "history clear"
                    historyClear(history);
                    continue;
                }

                int n = 0;
                if (!parsePositiveIntStrict(arg, n)) {
                    std::cout << "Error: history expects an integer > 0 (or 'clear')" << std::endl;
                    historyAdd(history, user_command); // invalid history should be logged
                    continue;
                }

                historyPrint(history, n);
                historyAdd(history, user_command); // log history N
                continue;
            }

            // Too many args
            std::cout << "Error: history expects an integer > 0 (or 'clear')" << std::endl;
            historyAdd(history, user_command);
            continue;
        }

        // Log normal commands
        historyAdd(history, user_command);

        std::string exec_path;
        if (!resolveExecutablePath(cmd, os_path_list, exec_path)) {
            std::cout << cmd << ": Error command not found" << std::endl;
            continue;
        }

        executeResolvedCommand(exec_path, command_list);
    }

    // Ensure file ends with newline for diff-based tests
    std::cout << std::endl;
    return 0;
}

/*
   file_path: path to a file
   RETURN: true/false - whether or not that file exists and is executable
*/
bool fileExecutableExists(std::string file_path)
{
    namespace fs = std::filesystem;

    std::error_code ec;
    if (!fs::exists(file_path, ec)) return false;
    if (fs::is_directory(file_path, ec)) return false;

    if (access(file_path.c_str(), X_OK) != 0) return false;

    return true;
}

void splitString(std::string text, char d, std::vector<std::string>& result)
{
    enum states { NONE, IN_WORD, IN_STRING } state = NONE;

    int i;
    std::string token;
    result.clear();
    for (i = 0; i < (int)text.length(); i++)
    {
        char c = text[i];
        switch (state) {
            case NONE:
                if (c != d)
                {
                    if (c == '\"')
                    {
                        state = IN_STRING;
                        token = "";
                    }
                    else
                    {
                        state = IN_WORD;
                        token = c;
                    }
                }
                break;
            case IN_WORD:
                if (c == d)
                {
                    result.push_back(token);
                    state = NONE;
                }
                else
                {
                    token += c;
                }
                break;
            case IN_STRING:
                if (c == '\"')
                {
                    result.push_back(token);
                    state = NONE;
                }
                else
                {
                    token += c;
                }
                break;
        }
    }
    if (state != NONE)
    {
        result.push_back(token);
    }
}

void vectorOfStringsToArrayOfCharArrays(std::vector<std::string>& list, char ***result)
{
    int i;
    int result_length = (int)list.size() + 1;
    *result = new char*[result_length];
    for (i = 0; i < (int)list.size(); i++)
    {
        (*result)[i] = new char[list[i].length() + 1];
        strcpy((*result)[i], list[i].c_str());
    }
    (*result)[list.size()] = NULL;
}

void freeArrayOfCharArrays(char **array, size_t array_length)
{
    for (size_t i = 0; i < array_length; i++)
    {
        if (array[i] != NULL)
        {
            delete[] array[i];
        }
    }
    delete[] array;
}
