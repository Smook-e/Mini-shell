
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <glob.h>
#include <wordexp.h>
#include <vector>
#include <string>
#include <cctype>
#include <iostream>
#include <sstream>
#include "command.h"
#include "tokenizer.h"
#include <errno.h> 

static int logFd = -1;



Command *fail(const std::string &msg, Command *cmd, SimpleCommand *sc)
{
    fprintf(stderr, "myshell: %s\n", msg.c_str());
    cmd->insertSimpleCommand(sc);
    delete cmd;
    return nullptr;
}

Command *parse(std::vector<Token> &tokens)
{
    if (tokens.size() <= 1) 
        return nullptr;

    Command *cmd = new Command();
    SimpleCommand *sc = new SimpleCommand();

    for (size_t i = 0; i < tokens.size(); i++)
    {
        const Token &tok = tokens[i];
        if (tok.type == TOKEN_EOF)
            break;

        if (cmd->_background)
            return fail("nothing may follow '&'", cmd, sc);

        switch (tok.type)
        {
        case TOKEN_COMMAND:
        case TOKEN_ARGUMENT:
            sc->insertArgument(strdup(tok.value.c_str()));
            break;

        case TOKEN_PIPE:
            if (sc->_numberOfArguments == 0)
                return fail("missing command before '|'", cmd, sc);
            cmd->insertSimpleCommand(sc);
            sc = new SimpleCommand();
            break;

        case TOKEN_BACKGROUND:
            if (sc->_numberOfArguments == 0)
                return fail("'&' with no command", cmd, sc);
            cmd->_background = 1;
            break;

        default: 
        {
            if (i + 1 >= tokens.size() || tokens[i + 1].type != TOKEN_ARGUMENT)
                return fail("missing file name after '" + tok.value + "'", cmd, sc);

            const std::string &file = tokens[++i].value;

            if (tok.type == TOKEN_INPUT)
            {
                free(cmd->_inputFile);
                cmd->_inputFile = strdup(file.c_str());
            }
            else if (tok.type == TOKEN_ERROR)
            {
                free(cmd->_errFile);
                cmd->_errFile = strdup(file.c_str());
            }
            else
            {
                free(cmd->_outFile);
                cmd->_outFile = strdup(file.c_str());
                cmd->_append = (tok.type == TOKEN_APPEND ||
                                tok.type == TOKEN_REDIRECT_AND_ERROR);
                if (tok.type == TOKEN_REDIRECT_AND_ERROR)
                {
                    free(cmd->_errFile);
                    cmd->_errFile = strdup(file.c_str());
                    cmd->_out_error = 1;
                }
            }
            break;
        }
        }
    }

    if (sc->_numberOfArguments == 0)
        return fail(cmd->_numberOfSimpleCommands > 0
                        ? "missing command after '|'"
                        : "missing command",
                    cmd, sc);

    cmd->insertSimpleCommand(sc);
    return cmd;
}

SimpleCommand::SimpleCommand()
{
    _numberOfAvailableArguments = 5;
    _numberOfArguments = 0;
    _arguments = (char **)malloc(_numberOfAvailableArguments * sizeof(char *));
}

static void insertLiteralArgument(SimpleCommand *command, char *argument)
{
    if (command->_numberOfAvailableArguments == command->_numberOfArguments + 1)
    {
        command->_numberOfAvailableArguments *= 2;
        command->_arguments = (char **)realloc(
            command->_arguments,
            command->_numberOfAvailableArguments * sizeof(char *));
    }

    command->_arguments[command->_numberOfArguments] = argument;
    command->_arguments[command->_numberOfArguments + 1] = NULL;
    command->_numberOfArguments++;
}

void SimpleCommand::insertArgument(char *argument)
{
    if (strpbrk(argument, "*?") != nullptr)
    {
        glob_t matches = {};
        int result = glob(argument, 0, nullptr, &matches);

        if (result == 0)
        {
            int inserted = 0;
            for (size_t i = 0; i < matches.gl_pathc; i++)
            {
                    insertLiteralArgument(this, strdup(matches.gl_pathv[i]));
                    inserted++;
            }

            if (inserted > 0)
            {
                globfree(&matches);
                free(argument);
                return;
            }
        }
        else if (result != GLOB_NOMATCH)
        {
            fprintf(stderr, "myshell: wildcard expansion failed for '%s'\n", argument);
        }

        globfree(&matches);
    }

    insertLiteralArgument(this, argument);
}

Command::Command()
{
    _numberOfAvailableSimpleCommands = 1;
    _simpleCommands = (SimpleCommand **)
        malloc(_numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));

    _numberOfSimpleCommands = 0;
    _out_error = 0;
    _append = 0;
    _outFile = 0;
    _inputFile = 0;
    _errFile = 0;
    _background = 0;
}

Command::~Command()
{
    clear();
    free(_simpleCommands);
}

void Command::insertSimpleCommand(SimpleCommand *simpleCommand)
{
    if (_numberOfAvailableSimpleCommands == _numberOfSimpleCommands)
    {
        _numberOfAvailableSimpleCommands *= 2;
        _simpleCommands = (SimpleCommand **)realloc(_simpleCommands,
                                                    _numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));
    }

    _simpleCommands[_numberOfSimpleCommands] = simpleCommand;
    _numberOfSimpleCommands++;
}

void Command::clear()
{
    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        for (int j = 0; j < _simpleCommands[i]->_numberOfArguments; j++)
        {
            free(_simpleCommands[i]->_arguments[j]);
        }

        free(_simpleCommands[i]->_arguments);
        delete _simpleCommands[i];
    }

    if (_outFile)
    {
        free(_outFile);
    }

    if (_inputFile)
    {
        free(_inputFile);
    }

    if (_errFile)
    {
        free(_errFile);
    }

    _numberOfSimpleCommands = 0;
    _out_error = 0;
    _append = 0;
    _outFile = 0;
    _inputFile = 0;
    _errFile = 0;
    _background = 0;
}

void Command::print()
{
    printf("\n\n");
    printf("              COMMAND TABLE                \n");
    printf("\n");
    printf("  #   Simple Commands\n");
    printf("  --- ----------------------------------------------------------\n");

    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        printf("  %-3d ", i);
        for (int j = 0; j < _simpleCommands[i]->_numberOfArguments; j++)
        {
            printf("\"%s\" \t", _simpleCommands[i]->_arguments[j]);
        }
    }

    printf("\n\n");
    printf("  Output       Input        Error        Err&Out       Background\n");
    printf("  ------------ ------------ ------------ ------------ ------------\n");
    printf("  %-12s %-12s %-12s %-12s %-12s\n", _outFile ? _outFile : "default",
           _inputFile ? _inputFile : "default", _errFile ? _errFile : "default", _out_error == 1 ? _errFile : "default", _background ? "YES" : "NO");
    printf("\n\n");
}

bool builtin(SimpleCommand *sc)
{
    char **a = sc->_arguments;

    if (strcmp(a[0], "exit") == 0)
    {
        printf("Good bye!!\n");
        exit(0);
    }
    if (strcmp(a[0], "cd") == 0)
    {
        if (sc->_numberOfArguments > 2)
        {
            fprintf(stderr, "cd: too many arguments\n");
            return true;
        }
        const char *dir = a[1] ? a[1] : getenv("HOME");
        if (!dir)
            fprintf(stderr, "cd: HOME not set\n");
        else if (chdir(dir) != 0)
            fprintf(stderr, "cd: %s: %s\n", dir, strerror(errno));
        return true;
    }
    return false;
}


void logChild(pid_t pid, int status)
{
    char buf[128];
    int n;
    if (WIFEXITED(status))
        n = snprintf(buf, sizeof buf, "child %d terminated, exit status %d\n",
                     (int)pid, WEXITSTATUS(status));
    else
        n = snprintf(buf, sizeof buf, "child %d killed by signal %d\n",
                     (int)pid, WTERMSIG(status));
    if (logFd >= 0 && n > 0)
        write(logFd, buf, n);
}

void childHandler(int)
{
    int saved = errno; 
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
        logChild(pid, status);
    errno = saved;
}

void setupSignals()
{
    logFd = open("shell.log", O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (logFd < 0)
        perror("shell.log");

    signal(SIGINT, SIG_IGN); 

    struct sigaction sa = {};
    sa.sa_handler = childHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP; 
    sigaction(SIGCHLD, &sa, nullptr);
}

void Command::execute()
{
    int n = _numberOfSimpleCommands;

    if (n == 1 && builtin(_simpleCommands[0]))
    {
        clear();
        return;
    }

    int prevRead = -1; 
    pid_t lastPid = -1;

    for (int i = 0; i < n; i++)
    {
        int p[2] = {-1, -1};
        if (i < n - 1 && pipe(p) < 0)
        {
            perror("pipe");
            break;
        }

        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork");
            if (p[0] >= 0)
            {
                close(p[0]);
                close(p[1]);
            }
            break;
        }

        if (pid == 0) 
        {
            signal(SIGINT, SIG_DFL); 

            if (prevRead >= 0)
                dup2(prevRead, 0); 
            if (i < n - 1)
                dup2(p[1], 1); 
            if (prevRead >= 0)
                close(prevRead);
            if (p[0] >= 0)
            {
                close(p[0]);
                close(p[1]);
            }

            if (i == 0 && _inputFile)
            {
                int fd = open(_inputFile, O_RDONLY);
                if (fd < 0)
                {
                    perror(_inputFile);
                    _exit(1);
                }
                dup2(fd, 0);
                close(fd);
            }
            if (i == n - 1) 
            {
                if (_outFile)
                {
                    int flags = O_WRONLY | O_CREAT | (_append ? O_APPEND : O_TRUNC);
                    int fd = open(_outFile, flags, 0644);
                    if (fd < 0)
                    {
                        perror(_outFile);
                        _exit(1);
                    }
                    dup2(fd, 1);
                    if (_out_error)
                        dup2(fd, 2);
                    close(fd);
                }
                if (_errFile && !_out_error)
                {
                    int fd = open(_errFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (fd < 0)
                    {
                        perror(_errFile);
                        _exit(1);
                    }
                    dup2(fd, 2);
                    close(fd);
                }
            }

            char **a = _simpleCommands[i]->_arguments;
            execvp(a[0], a);
            if (errno == ENOENT)
                fprintf(stderr, "%s: command not found\n", a[0]);
            else
                perror(a[0]);
            _exit(127);
        }

        
        if (prevRead >= 0)
            close(prevRead);
        if (i < n - 1)
        {
            close(p[1]);
            prevRead = p[0];
        }
        else
        {
            prevRead = -1;
        }
        lastPid = pid;
    }

    if (prevRead >= 0)
        close(prevRead); 

    if (!_background && lastPid > 0)
    {
        int status;
        pid_t r;
        while ((r = waitpid(lastPid, &status, 0)) < 0 && errno == EINTR)
        {
        }
        if (r > 0)
            logChild(r, status); 
    }

    clear();
}

void prompt()
{
    std::string input;
    while (true)
    {
        printf("myshell>");
        fflush(stdout);
        if (!std::getline(std::cin, input)) 
            break;
        std::vector<Token> tokens = tokenize(input);
        Command *cmd = parse(tokens);
        if (cmd)
        {
            cmd->execute();
            delete cmd;
        }
    }
}

Command Command::_currentCommand;
SimpleCommand *Command::_currentSimpleCommand;

#ifndef COMMAND_TEST_BUILD
int main()
{
    setupSignals();
    prompt();
    return 0;
}
#endif
