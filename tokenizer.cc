#include "tokenizer.h"
#include <cctype>
#include <cstdlib>

namespace
{
bool expandVariable(const std::string &input, std::size_t &index,
                    std::string &word)
{
    if (input[index] != '$')
        return false;

    std::size_t nameStart = index + 1;
    std::size_t nameEnd = nameStart;
    if (nameStart < input.size() && input[nameStart] == '{')
    {
        nameStart++;
        nameEnd = nameStart;
        while (nameEnd < input.size() &&
               (std::isalnum(static_cast<unsigned char>(input[nameEnd])) ||
                input[nameEnd] == '_'))
            nameEnd++;
        if (nameEnd == nameStart || nameEnd >= input.size() ||
            input[nameEnd] != '}')
            return false;

        const std::string name = input.substr(nameStart, nameEnd - nameStart);
        const char *value = std::getenv(name.c_str());
        if (value)
            word += value;
        index = nameEnd;
        return true;
    }

    while (nameEnd < input.size() &&
           (std::isalnum(static_cast<unsigned char>(input[nameEnd])) ||
            input[nameEnd] == '_'))
        nameEnd++;
    if (nameEnd == nameStart)
        return false;

    const std::string name = input.substr(nameStart, nameEnd - nameStart);
    const char *value = std::getenv(name.c_str());
    if (value)
        word += value;
    index = nameEnd - 1;
    return true;
}
}

std::vector<Token> tokenize(const std::string &input)
{
    std::vector<Token> tokens;
    std::string word;
    char quote = '\0';
    bool wordWasQuoted = false;

    auto addToken = [&]()
    {
        if (word.empty())
            return;

        Token token;

        if (!wordWasQuoted && word == "|")
            token.type = TOKEN_PIPE;
        else if (!wordWasQuoted && word == ">")
            token.type = TOKEN_REDIRECT;
        else if (!wordWasQuoted && word == ">>")
            token.type = TOKEN_APPEND;
        else if (!wordWasQuoted && word == "<")
            token.type = TOKEN_INPUT;
        else if (!wordWasQuoted && word == "&")
            token.type = TOKEN_BACKGROUND;
        else if (!wordWasQuoted && word == "2>")
            token.type = TOKEN_ERROR;
        else if (!wordWasQuoted && word == ">>&")
            token.type = TOKEN_REDIRECT_AND_ERROR;
        else if (!wordWasQuoted && word == ";")
            token.type = TOKEN_SEQUENCE;
        else if (!wordWasQuoted && word == "&&")
            token.type = TOKEN_AND;
        else if (!wordWasQuoted && word == "||")
            token.type = TOKEN_OR;
        else if (tokens.empty() || tokens.back().type == TOKEN_PIPE)
            token.type = TOKEN_COMMAND;
        else
            token.type = TOKEN_ARGUMENT;

        token.value = word;
        token.quoted = wordWasQuoted;
        tokens.push_back(token);
        word.clear();
        wordWasQuoted = false;
    };

    for (size_t i = 0; i < input.size(); ++i)
    {
        char c = input[i];

        if (quote != '\0')
        {
            if (c == quote)
                quote = '\0';
            else if (quote != '\'' && c == '$' &&
                     expandVariable(input, i, word))
            {
            }
            else
                word += c;
        }
        else if (c == '\'' || c == '"')
        {
            quote = c;
            wordWasQuoted = true;
        }
        else if (std::isspace(static_cast<unsigned char>(c)))
        {
            addToken();
        }
        else if (c == '2' && word.empty() &&
                 i + 1 < input.size() && input[i + 1] == '>')
        {
            addToken();
            word = "2>";
            addToken();
        }
        else if (c == '>')
        {
            addToken();

            if (i + 1 < input.size() && input[i + 1] == '>')
            {
                if (i + 2 < input.size() && input[i + 2] == '&')
                {
                    word = ">>&";
                    i += 2;
                }
                else
                {
                    word = ">>";
                    ++i;
                }
            }
            else
            {
                word = ">";
            }

            addToken();
        }
        else if (c == ';')
        {
            addToken();
            word = ";";
            addToken();
        }
        else if (c == '|')
        {
            addToken();
            if (i + 1 < input.size() && input[i + 1] == '|')
            {
                word = "||";
                ++i;
            }
            else
            {
                word = "|";
            }
            addToken();
        }
        else if (c == '&')
        {
            addToken();
            if (i + 1 < input.size() && input[i + 1] == '&')
            {
                word = "&&";
                ++i;
            }
            else
            {
                word = "&";
            }
            addToken();
        }
        else if (c == '$' && expandVariable(input, i, word))
        {
        }
        else
        {
            word += c;
        }
    }

    addToken();
    tokens.push_back({TOKEN_EOF, ""});

    return tokens;
}
