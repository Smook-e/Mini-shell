#include "command.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
Command *parseCommand(const std::string &input)
{
    std::vector<Token> tokens = tokenize(input);
    return parse(tokens);
}

void testTokenizer()
{
    std::vector<Token> tokens = tokenize("echo hello | wc -l >> output.txt");
    assert(tokens.size() == 8);
    assert(tokens[0].type == TOKEN_COMMAND && tokens[0].value == "echo");
    assert(tokens[1].type == TOKEN_ARGUMENT && tokens[1].value == "hello");
    assert(tokens[2].type == TOKEN_PIPE);
    assert(tokens[3].type == TOKEN_COMMAND && tokens[3].value == "wc");
    assert(tokens[4].type == TOKEN_ARGUMENT && tokens[4].value == "-l");
    assert(tokens[5].type == TOKEN_APPEND);
    assert(tokens[6].type == TOKEN_ARGUMENT && tokens[6].value == "output.txt");
    assert(tokens[7].type == TOKEN_EOF);

    tokens = tokenize("false && echo no || echo yes; echo done");
    assert(tokens.size() == 11);
    assert(tokens[1].type == TOKEN_AND);
    assert(tokens[4].type == TOKEN_OR);
    assert(tokens[7].type == TOKEN_SEQUENCE);
    assert(tokens[10].type == TOKEN_EOF);
}

void testParsing()
{
    Command *command = parseCommand("cat input.txt | grep shell > output.txt");
    assert(command != nullptr);
    assert(command->_simpleCommands.size() == 2);
    assert(command->_simpleCommands[0].arguments()[0] == "cat");
    assert(command->_simpleCommands[0].arguments()[1] == "input.txt");
    assert(command->_simpleCommands[1].arguments()[0] == "grep");
    assert(command->_outFile == "output.txt");
    delete command;

    assert(parseCommand("") == nullptr);
    assert(parseCommand("| echo invalid") == nullptr);
    assert(parseCommand("echo invalid |") == nullptr);
    assert(parseCommand("echo missing >") == nullptr);
    assert(parseCommand("echo one & echo two") == nullptr);
}

void testWildcardExpansion()
{
    char directory[] = "/tmp/myshell-command-tests-XXXXXX";
    assert(mkdtemp(directory) != nullptr);
    const std::string first = std::string(directory) + "/alpha.cc";
    const std::string second = std::string(directory) + "/beta.cc";
    std::ofstream(first).close();
    std::ofstream(second).close();

    const std::string original = getcwd(nullptr, 0);
    assert(chdir(directory) == 0);
    SimpleCommand command;
    command.insertArgument("*.cc");
    assert(command.argumentCount() == 2);
    assert(command.arguments()[0] == "alpha.cc");
    assert(command.arguments()[1] == "beta.cc");
    command.insertArgument("*.missing");
    assert(command.argumentCount() == 3);
    assert(command.arguments()[2] == "*.missing");
    assert(chdir(original.c_str()) == 0);

    std::remove(first.c_str());
    std::remove(second.c_str());
    assert(rmdir(directory) == 0);
}

void testBuiltinCd()
{
    const std::string original = getcwd(nullptr, 0);
    SimpleCommand command;
    command.insertArgument("cd");
    command.insertArgument("/tmp");
    assert(builtin(command));
    assert(std::string(getcwd(nullptr, 0)) == "/tmp");
    assert(chdir(original.c_str()) == 0);
}

void testExecutionAndRedirection()
{
    char filename[] = "/tmp/myshell-command-output-XXXXXX";
    const int fd = mkstemp(filename);
    assert(fd >= 0);
    close(fd);
    std::remove(filename);

    Command *command = parseCommand(
        std::string("printf test > ") + filename);
    assert(command != nullptr);
    command->execute();
    delete command;

    std::ifstream output(filename);
    std::string contents;
    std::getline(output, contents);
    assert(contents == "test");
    output.close();
    std::remove(filename);

    command = parseCommand("printf 'alpha\\nbeta\\n' | wc -l");
    assert(command != nullptr);
    command->execute();
    delete command;
}

void testCommandLists()
{
    char filename[] = "/tmp/myshell-command-list-XXXXXX";
    const int fd = mkstemp(filename);
    assert(fd >= 0);
    close(fd);
    std::remove(filename);

    assert(executeLine(std::string("false && printf skipped > ") + filename) != 0);
    assert(access(filename, F_OK) != 0);

    assert(executeLine(std::string("true || printf skipped > ") + filename) == 0);
    assert(access(filename, F_OK) != 0);

    assert(executeLine(std::string("false || printf fallback > ") + filename) == 0);
    std::ifstream output(filename);
    std::string contents;
    std::getline(output, contents);
    assert(contents == "fallback");
    output.close();
    std::remove(filename);

    assert(executeLine("true; false; true") == 0);
    assert(executeLine("false; true") == 0);
    assert(executeLine("true &&") != 0);
}
}

int main()
{
    testTokenizer();
    testParsing();
    testWildcardExpansion();
    testBuiltinCd();
    testExecutionAndRedirection();
    testCommandLists();
    std::cout << "All command tests passed\n";
    return 0;
}
