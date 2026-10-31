#include "command.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <glob.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <iostream>
#include <signal.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
int logFd = -1;

Command *parseError(const std::string &message, Command *command)
{
    std::fprintf(stderr, "myshell: %s\n", message.c_str());
    delete command;
    return nullptr;
}

int openInput(const std::string &file)
{
    const int fd = open(file.c_str(), O_RDONLY);
    if (fd < 0)
        perror(file.c_str());
    return fd;
}

int openOutput(const std::string &file, bool append)
{
    const int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
    const int fd = open(file.c_str(), flags, 0644);
    if (fd < 0)
        perror(file.c_str());
    return fd;
}

void logChild(pid_t pid, int status)
{
    char buffer[128];
    int length;

    if (WIFEXITED(status))
    {
        length = std::snprintf(
            buffer, sizeof(buffer), "child %d terminated, exit status %d\n",
            static_cast<int>(pid), WEXITSTATUS(status));
    }
    else
    {
        length = std::snprintf(
            buffer, sizeof(buffer), "child %d killed by signal %d\n",
            static_cast<int>(pid), WTERMSIG(status));
    }

    if (logFd >= 0 && length > 0)
        write(logFd, buffer, length);
}

#ifndef COMMAND_TEST_BUILD
void childHandler(int)
{
    const int savedErrno = errno;
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
        logChild(pid, status);

    errno = savedErrno;
}

void setupSignals()
{
    logFd = open("shell.log", O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (logFd < 0)
        perror("shell.log");

    signal(SIGINT, SIG_IGN);

    struct sigaction action{};
    action.sa_handler = childHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &action, nullptr);
}
#endif

void redirectInput(const Command &command)
{
    if (command._inputFile.empty())
        return;

    const int fd = openInput(command._inputFile);
    if (fd < 0)
        _exit(1);
    dup2(fd, STDIN_FILENO);
    close(fd);
}

void redirectOutput(const Command &command)
{
    if (!command._outFile.empty())
    {
        const int fd = openOutput(command._outFile, command._append);
        if (fd < 0)
            _exit(1);
        dup2(fd, STDOUT_FILENO);
        if (command._outError)
            dup2(fd, STDERR_FILENO);
        close(fd);
    }

    if (!command._errFile.empty() && !command._outError)
    {
        const int fd = openOutput(command._errFile, false);
        if (fd < 0)
            _exit(1);
        dup2(fd, STDERR_FILENO);
        close(fd);
    }
}
}

Command *parse(const std::vector<Token> &tokens)
{
    if (tokens.size() <= 1)
        return nullptr;

    auto *command = new Command();
    SimpleCommand current;

    for (std::size_t i = 0; i < tokens.size(); ++i)
    {
        const Token &token = tokens[i];
        if (token.type == TOKEN_EOF)
            break;

        if (command->_background)
            return parseError("nothing may follow '&'", command);

        switch (token.type)
        {
        case TOKEN_COMMAND:
        case TOKEN_ARGUMENT:
            current.insertArgument(token.value);
            break;

        case TOKEN_PIPE:
            if (current.argumentCount() == 0)
                return parseError("missing command before '|'", command);
            command->insertSimpleCommand(std::move(current));
            current = SimpleCommand();
            break;

        case TOKEN_BACKGROUND:
            if (current.argumentCount() == 0)
                return parseError("'&' with no command", command);
            command->_background = true;
            break;

        default:
            if (i + 1 >= tokens.size() ||
                tokens[i + 1].type != TOKEN_ARGUMENT)
                return parseError("missing file name after '" + token.value + "'",
                                  command);

            const std::string &file = tokens[++i].value;
            if (token.type == TOKEN_INPUT)
                command->_inputFile = file;
            else if (token.type == TOKEN_ERROR)
                command->_errFile = file;
            else
            {
                command->_outFile = file;
                command->_append = token.type == TOKEN_APPEND ||
                                   token.type == TOKEN_REDIRECT_AND_ERROR;
                if (token.type == TOKEN_REDIRECT_AND_ERROR)
                {
                    command->_errFile = file;
                    command->_outError = true;
                }
            }
            break;
        }
    }

    if (current.argumentCount() == 0)
        return parseError(command->_simpleCommands.empty()
                              ? "missing command"
                              : "missing command after '|'",
                          command);

    command->insertSimpleCommand(std::move(current));
    return command;
}

void SimpleCommand::insertArgument(const std::string &argument)
{
    if (argument.find_first_of("*?") == std::string::npos)
    {
        _arguments.push_back(argument);
        return;
    }

    glob_t matches{};
    const int result = glob(argument.c_str(), 0, nullptr, &matches);
    if (result == 0 && matches.gl_pathc > 0)
    {
        for (std::size_t i = 0; i < matches.gl_pathc; ++i)
            _arguments.emplace_back(matches.gl_pathv[i]);
        globfree(&matches);
        return;
    }

    if (result != 0 && result != GLOB_NOMATCH)
        std::fprintf(stderr, "myshell: wildcard expansion failed for '%s'\n",
                     argument.c_str());

    globfree(&matches);
    _arguments.push_back(argument);
}

const std::vector<std::string> &SimpleCommand::arguments() const
{
    return _arguments;
}

std::size_t SimpleCommand::argumentCount() const
{
    return _arguments.size();
}

void Command::insertSimpleCommand(SimpleCommand command)
{
    _simpleCommands.push_back(std::move(command));
}

void Command::clear()
{
    _simpleCommands.clear();
    _outFile.clear();
    _inputFile.clear();
    _errFile.clear();
    _outError = false;
    _append = false;
    _background = false;
}

void Command::print() const
{
    std::printf("\n\n              COMMAND TABLE                \n\n");
    std::printf("  #   Simple Commands\n");
    std::printf("  --- ----------------------------------------------------------\n");

    for (std::size_t i = 0; i < _simpleCommands.size(); ++i)
    {
        std::printf("  %-3zu ", i);
        for (const std::string &argument : _simpleCommands[i].arguments())
            std::printf("\"%s\" \t", argument.c_str());
    }

    std::printf("\n\n  Output       Input        Error        Err&Out       Background\n");
    std::printf("  ------------ ------------ ------------ ------------ ------------\n");
    std::printf("  %-12s %-12s %-12s %-12s %-12s\n",
                _outFile.empty() ? "default" : _outFile.c_str(),
                _inputFile.empty() ? "default" : _inputFile.c_str(),
                _errFile.empty() ? "default" : _errFile.c_str(),
                _outError ? _errFile.c_str() : "default",
                _background ? "YES" : "NO");
}

bool builtin(const SimpleCommand &command)
{
    if (command.argumentCount() == 0)
        return false;

    const std::vector<std::string> &arguments = command.arguments();
    if (arguments[0] == "exit")
    {
        std::printf("Good bye!!\n");
        std::exit(0);
    }

    if (arguments[0] != "cd")
        return false;

    if (arguments.size() > 2)
    {
        std::fprintf(stderr, "cd: too many arguments\n");
        return true;
    }

    const char *directory = arguments.size() == 1
                                ? std::getenv("HOME")
                                : arguments[1].c_str();
    if (!directory)
        std::fprintf(stderr, "cd: HOME not set\n");
    else if (chdir(directory) != 0)
        std::fprintf(stderr, "cd: %s: %s\n", directory, std::strerror(errno));
    return true;
}

void Command::execute()
{
    if (_simpleCommands.size() == 1 && builtin(_simpleCommands.front()))
    {
        clear();
        return;
    }

    int previousRead = -1;
    pid_t lastPid = -1;

    for (std::size_t i = 0; i < _simpleCommands.size(); ++i)
    {
        int pipeEnds[2] = {-1, -1};
        if (i + 1 < _simpleCommands.size() && pipe(pipeEnds) < 0)
        {
            perror("pipe");
            break;
        }

        const pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork");
            if (pipeEnds[0] >= 0)
            {
                close(pipeEnds[0]);
                close(pipeEnds[1]);
            }
            break;
        }

        if (pid == 0)
        {
            signal(SIGINT, SIG_DFL);

            if (previousRead >= 0)
                dup2(previousRead, STDIN_FILENO);
            if (i + 1 < _simpleCommands.size())
                dup2(pipeEnds[1], STDOUT_FILENO);
            if (previousRead >= 0)
                close(previousRead);
            if (pipeEnds[0] >= 0)
            {
                close(pipeEnds[0]);
                close(pipeEnds[1]);
            }

            if (i == 0)
                redirectInput(*this);
            if (i + 1 == _simpleCommands.size())
                redirectOutput(*this);

            std::vector<char *> arguments;
            for (const std::string &argument : _simpleCommands[i].arguments())
                arguments.push_back(const_cast<char *>(argument.c_str()));
            arguments.push_back(nullptr);

            execvp(arguments[0], arguments.data());
            if (errno == ENOENT)
                std::fprintf(stderr, "%s: command not found\n", arguments[0]);
            else
                perror(arguments[0]);
            _exit(127);
        }

        if (previousRead >= 0)
            close(previousRead);
        if (i + 1 < _simpleCommands.size())
        {
            close(pipeEnds[1]);
            previousRead = pipeEnds[0];
        }
        else
        {
            previousRead = -1;
        }
        lastPid = pid;
    }

    if (previousRead >= 0)
        close(previousRead);

    if (!_background && lastPid > 0)
    {
        int status;
        pid_t result;
        while ((result = waitpid(lastPid, &status, 0)) < 0 && errno == EINTR)
        {
        }
        if (result > 0)
            logChild(result, status);
    }

    clear();
}

void prompt()
{
    using_history();

    while (true)
    {
        char *line = readline("myshell>");
        if (!line)
            break;

        if (*line != '\0')
            add_history(line);

        const std::string input(line);
        std::free(line);
        const std::vector<Token> tokens = tokenize(input);
        Command *command = parse(tokens);
        if (command)
        {
            command->execute();
            delete command;
        }
    }
}

#ifndef COMMAND_TEST_BUILD
int main()
{
    setupSignals();
    prompt();
    return 0;
}
#endif
