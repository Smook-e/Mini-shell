#ifndef COMMAND_H
#define COMMAND_H

#include "tokenizer.h"

#include <string>
#include <vector>

class SimpleCommand
{
public:
    void insertArgument(const std::string &argument);
    const std::vector<std::string> &arguments() const;
    std::size_t argumentCount() const;

private:
    std::vector<std::string> _arguments;
};

class Command
{
public:
    void insertSimpleCommand(SimpleCommand command);
    int execute();
    void print() const;
    void clear();

    std::vector<SimpleCommand> _simpleCommands;
    std::string _outFile;
    std::string _inputFile;
    std::string _errFile;
    bool _outError = false;
    bool _background = false;
    bool _append = false;
};

Command *parse(const std::vector<Token> &tokens);
bool builtin(const SimpleCommand &command);
int executeLine(const std::string &input);

#endif
