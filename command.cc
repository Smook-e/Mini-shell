#include "command.h"

#include <algorithm>
#include <cctype>
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
#include <vector>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
int logFd = -1;
pid_t shellPgid = -1;
bool interactiveShell = false;
volatile sig_atomic_t childStatusPending = 0;

enum class JobState
{
    Running,
    Stopped,
    Done
};

struct Job
{
    int id;
    pid_t pgid;
    std::string command;
    JobState state = JobState::Running;
    std::vector<pid_t> pids;
};

std::vector<Job> jobs;
int nextJobId = 1;

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
    else if (WIFSIGNALED(status))
    {
        length = std::snprintf(
            buffer, sizeof(buffer), "child %d killed by signal %d\n",
            static_cast<int>(pid), WTERMSIG(status));
    }
    else
    {
        return;
    }

    if (logFd >= 0 && length > 0)
        write(logFd, buffer, length);
}

#ifndef COMMAND_TEST_BUILD
void childHandler(int)
{
    childStatusPending = 1;
}

void setupSignals()
{
    logFd = open("shell.log", O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (logFd < 0)
        perror("shell.log");

    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);

    if (isatty(STDIN_FILENO))
    {
        interactiveShell = true;
        shellPgid = getpid();
        if (setpgid(shellPgid, shellPgid) < 0 && errno != EACCES)
            perror("setpgid");
        if (tcsetpgrp(STDIN_FILENO, shellPgid) < 0)
            perror("tcsetpgrp");
    }

    struct sigaction action{};
    action.sa_handler = childHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &action, nullptr);
    rl_catch_signals = 0;
}
#endif

void resetChildSignals()
{
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
}

Job *findJob(int id)
{
    for (Job &job : jobs)
        if (job.id == id)
            return &job;
    return nullptr;
}

Job *findJobByPid(pid_t pid)
{
    for (Job &job : jobs)
        for (pid_t jobPid : job.pids)
            if (jobPid == pid)
                return &job;
    return nullptr;
}

void removeDoneJobs()
{
    jobs.erase(std::remove_if(jobs.begin(), jobs.end(),
                              [](const Job &job)
                              { return job.state == JobState::Done; }),
               jobs.end());
}

void reapChildren()
{
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status,
                          WNOHANG | WUNTRACED | WCONTINUED)) > 0)
    {
        Job *job = findJobByPid(pid);
        if (!job)
            continue;

        if (WIFSTOPPED(status))
            job->state = JobState::Stopped;
        else if (WIFCONTINUED(status))
            job->state = JobState::Running;
        else if (WIFEXITED(status) || WIFSIGNALED(status))
        {
            logChild(pid, status);
            job->pids.erase(std::remove(job->pids.begin(), job->pids.end(), pid),
                            job->pids.end());
            if (job->pids.empty())
                job->state = JobState::Done;
        }
    }
    childStatusPending = 0;
}

int parseJobId(const std::string &argument)
{
    if (argument.size() < 2 || argument[0] != '%' ||
        !std::all_of(argument.begin() + 1, argument.end(),
                     [](unsigned char character)
                     { return std::isdigit(character); }))
        return -1;
    return std::stoi(argument.substr(1));
}

int waitForJob(Job &job, bool foreground)
{
    if (foreground && interactiveShell &&
        tcsetpgrp(STDIN_FILENO, job.pgid) < 0)
        perror("tcsetpgrp");

    int status = 0;
    while (!job.pids.empty())
    {
        const pid_t pid = waitpid(-job.pgid, &status, WUNTRACED);
        if (pid < 0)
        {
            if (errno == EINTR)
                continue;
            if (errno == ECHILD)
                break;
            perror("waitpid");
            break;
        }

        if (WIFSTOPPED(status))
        {
            job.state = JobState::Stopped;
            std::printf("[%d]+ Stopped %s\n", job.id, job.command.c_str());
            break;
        }

        logChild(pid, status);
        job.pids.erase(std::remove(job.pids.begin(), job.pids.end(), pid),
                       job.pids.end());
    }

    if (foreground && interactiveShell &&
        tcsetpgrp(STDIN_FILENO, shellPgid) < 0)
        perror("tcsetpgrp");

    if (job.pids.empty())
    {
        job.state = JobState::Done;
        return WIFEXITED(status) ? WEXITSTATUS(status)
                                 : 128 + WTERMSIG(status);
    }
    return 128 + (WIFSTOPPED(status) ? WSTOPSIG(status) : SIGTSTP);
}

void printJobs()
{
    reapChildren();
    for (const Job &job : jobs)
    {
        const char *state = job.state == JobState::Running
                                ? "Running"
                                : job.state == JobState::Stopped ? "Stopped"
                                                                 : "Done";
        std::printf("[%d] %-7s %s\n", job.id, state, job.command.c_str());
    }
    removeDoneJobs();
}

bool jobControlBuiltin(const std::vector<std::string> &arguments, int &status)
{
    if (arguments[0] == "jobs")
    {
        printJobs();
        status = 0;
        return true;
    }

    if (arguments[0] != "fg" && arguments[0] != "bg")
        return false;
    if (arguments.size() != 2)
    {
        std::fprintf(stderr, "%s: usage: %s %%job\n", arguments[0].c_str(),
                     arguments[0].c_str());
        status = 2;
        return true;
    }

    Job *job = findJob(parseJobId(arguments[1]));
    if (!job)
    {
        std::fprintf(stderr, "%s: job not found: %s\n", arguments[0].c_str(),
                     arguments[1].c_str());
        status = 1;
        return true;
    }

    if (kill(-job->pgid, SIGCONT) < 0)
    {
        perror("SIGCONT");
        status = 1;
        return true;
    }
    job->state = JobState::Running;
    if (arguments[0] == "fg")
        status = waitForJob(*job, true);
    else
        status = 0;
    return true;
}

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
            current.insertArgument(token.value, !token.quoted);
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

void SimpleCommand::insertArgument(const std::string &argument,
                                   bool expandWildcards)
{
    if (!expandWildcards || argument.find_first_of("*?") == std::string::npos)
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

int runBuiltin(const SimpleCommand &command)
{
    if (command.argumentCount() == 0)
        return -1;

    const std::vector<std::string> &arguments = command.arguments();
    int jobStatus = 0;
    if (jobControlBuiltin(arguments, jobStatus))
        return jobStatus;

    if (arguments[0] == "exit")
    {
        std::printf("Good bye!!\n");
        std::exit(0);
    }

    if (arguments[0] != "cd")
        return -1;

    if (arguments.size() > 2)
    {
        std::fprintf(stderr, "cd: too many arguments\n");
        return 1;
    }

    const char *directory = arguments.size() == 1
                                ? std::getenv("HOME")
                                : arguments[1].c_str();
    if (!directory)
    {
        std::fprintf(stderr, "cd: HOME not set\n");
        return 1;
    }
    else if (chdir(directory) != 0)
    {
        std::fprintf(stderr, "cd: %s: %s\n", directory, std::strerror(errno));
        return 1;
    }
    return 0;
}

bool builtin(const SimpleCommand &command)
{
    return runBuiltin(command) >= 0;
}

int Command::execute(const std::string &jobText)
{
    if (_simpleCommands.size() == 1)
    {
        const int status = runBuiltin(_simpleCommands.front());
        if (status >= 0)
        {
            clear();
            return status;
        }
    }

    int previousRead = -1;
    pid_t processGroup = 0;
    std::vector<pid_t> childPids;
    int launchStatus = 0;

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
            launchStatus = 1;
            if (pipeEnds[0] >= 0)
            {
                close(pipeEnds[0]);
                close(pipeEnds[1]);
            }
            break;
        }

        if (pid == 0)
        {
            resetChildSignals();
            const pid_t childPgid = processGroup == 0 ? getpid() : processGroup;
            if (setpgid(0, childPgid) < 0)
                _exit(1);

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

        if (processGroup == 0)
            processGroup = pid;
        if (setpgid(pid, processGroup) < 0 && errno != EACCES)
            perror("setpgid");
        childPids.push_back(pid);

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
    }

    if (previousRead >= 0)
        close(previousRead);

    if (launchStatus != 0 || processGroup == 0)
    {
        clear();
        return 1;
    }

    Job job{nextJobId++, processGroup, jobText, JobState::Running, childPids};
    jobs.push_back(job);
    Job &storedJob = jobs.back();

    int exitStatus = 0;
    if (_background)
    {
        std::printf("[%d] %d\n", storedJob.id, static_cast<int>(processGroup));
    }
    else
    {
        const int foregroundJobId = storedJob.id;
        exitStatus = waitForJob(storedJob, true);
        if (storedJob.state == JobState::Done)
            jobs.erase(std::remove_if(jobs.begin(), jobs.end(),
                                      [foregroundJobId](const Job &candidate)
                                      { return candidate.id == foregroundJobId; }),
                       jobs.end());
    }

    clear();
    return exitStatus;
}

int executeLine(const std::string &input)
{
    const std::vector<Token> tokens = tokenize(input);

    bool validCommandList = true;
    bool previousWasSeparator = true;
    TokenType lastSeparator = TOKEN_SEQUENCE;
    for (const Token &token : tokens)
    {
        if (token.type == TOKEN_EOF)
            break;

        const bool isSeparator = token.type == TOKEN_SEQUENCE ||
                                 token.type == TOKEN_AND ||
                                 token.type == TOKEN_OR;
        if (!isSeparator)
        {
            previousWasSeparator = false;
            continue;
        }

        if (previousWasSeparator)
        {
            std::fprintf(stderr, "myshell: unexpected separator '%s'\n",
                         token.value.c_str());
            validCommandList = false;
            break;
        }
        previousWasSeparator = true;
        lastSeparator = token.type;
    }
    if (validCommandList && previousWasSeparator &&
        (lastSeparator == TOKEN_AND || lastSeparator == TOKEN_OR))
    {
        std::fprintf(stderr, "myshell: missing command after '%s'\n",
                     lastSeparator == TOKEN_AND ? "&&" : "||");
        validCommandList = false;
    }
    if (!validCommandList)
        return 2;

    std::size_t commandStart = 0;
    TokenType connector = TOKEN_SEQUENCE;
    int previousStatus = 0;
    bool havePreviousCommand = false;

    for (std::size_t i = 0; i < tokens.size(); ++i)
    {
        const TokenType type = tokens[i].type;
        const bool isSeparator = type == TOKEN_SEQUENCE ||
                                 type == TOKEN_AND || type == TOKEN_OR ||
                                 type == TOKEN_EOF;
        if (!isSeparator)
            continue;

        if (i == commandStart)
        {
            std::fprintf(stderr, "myshell: missing command near '%s'\n",
                         tokens[i].value.empty() ? "end of line"
                                                 : tokens[i].value.c_str());
            return 2;
        }

        std::vector<Token> commandTokens(tokens.begin() + commandStart,
                                         tokens.begin() + i);
        commandTokens.push_back({TOKEN_EOF, ""});
        Command *command = parse(commandTokens);
        if (!command)
            return 2;

        const bool shouldRun =
            !havePreviousCommand || connector == TOKEN_SEQUENCE ||
            (connector == TOKEN_AND && previousStatus == 0) ||
            (connector == TOKEN_OR && previousStatus != 0);
        if (shouldRun)
            previousStatus = command->execute(input);
        delete command;

        havePreviousCommand = true;
        connector = type;
        commandStart = i + 1;
    }
    return previousStatus;
}

void prompt()
{
    using_history();

    while (true)
    {
        reapChildren();
        char *currentDirectory = getcwd(nullptr, 0);
        std::string directory;
        if (currentDirectory)
        {
            directory = currentDirectory;
            std::free(currentDirectory);
        }
        else
        {
            perror("getcwd");
            directory = "?";
        }

        const std::string prompt =
            "\001\033[1;36m\002" + directory +
            "\001\033[0m\002\n$ ";
        char *line = readline(prompt.c_str());
        if (!line)
            break;

        if (*line != '\0')
            add_history(line);

        const std::string input(line);
        std::free(line);
        executeLine(input);
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
