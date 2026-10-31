#include "tokenizer.h"
#include <sstream>
#include <iostream>


std::vector<Token> tokenize(const std::string &input)
{
    std::vector<Token> tokens;
    std::string word;
    char quote = '\0';

    auto addToken = [&]()
    {
        if (word.empty())
            return;

        Token token;

        if (word == "|")
            token.type = TOKEN_PIPE;
        else if (word == ">")
            token.type = TOKEN_REDIRECT;
        else if (word == ">>")
            token.type = TOKEN_APPEND;
        else if (word == "<")
            token.type = TOKEN_INPUT;
        else if (word == "&")
            token.type = TOKEN_BACKGROUND;
        else if (word == "2>")
            token.type = TOKEN_ERROR;
        else if (word == ">>&")
            token.type = TOKEN_REDIRECT_AND_ERROR;
        else if (word == ";")
            token.type = TOKEN_SEQUENCE;
        else if (word == "&&")
            token.type = TOKEN_AND;
        else if (word == "||")
            token.type = TOKEN_OR;
        else if (tokens.empty() || tokens.back().type == TOKEN_PIPE)
            token.type = TOKEN_COMMAND;
        else
            token.type = TOKEN_ARGUMENT;

        token.value = word;
        tokens.push_back(token);
        word.clear();
    };

    for (size_t i = 0; i < input.size(); ++i)
    {
        char c = input[i];

        if (quote != '\0')
        {
            if (c == quote)
                quote = '\0';
            else
                word += c;
        }
        else if (c == '\'' || c == '"')
        {
            quote = c;
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
        else
        {
            word += c;
        }
    }

    addToken();
    tokens.push_back({TOKEN_EOF, ""});

    return tokens;
}
