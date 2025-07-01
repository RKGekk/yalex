#pragma once

#include <algorithm>
#include <functional>
#include <cassert>
#include <cctype>
#include <deque>
#include <iosfwd>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace parse {

namespace token_type {
    struct Number {  // Лексема «число»
        int value;   // число
    };

    struct Id {             // Лексема «идентификатор»
        std::string value;  // Имя идентификатора
    };

    struct Char {    // Лексема «символ»
        char value;  // код символа
    };

    struct String {  // Лексема «строковая константа»
        std::string value;
    };

    struct Class {};    // Лексема «class»
    struct Return {};   // Лексема «return»
    struct If {};       // Лексема «if»
    struct Else {};     // Лексема «else»
    struct Def {};      // Лексема «def»
    struct Newline {};  // Лексема «конец строки»
    struct Print {};    // Лексема «print»
    struct Indent {};  // Лексема «увеличение отступа», соответствует двум пробелам
    struct Dedent {};  // Лексема «уменьшение отступа»
    struct Eof {};     // Лексема «конец файла»
    struct And {};     // Лексема «and»
    struct Or {};      // Лексема «or»
    struct Not {};     // Лексема «not»
    struct Eq {};      // Лексема «==»
    struct NotEq {};   // Лексема «!=»
    struct LessOrEq {};     // Лексема «<=»
    struct GreaterOrEq {};  // Лексема «>=»
    struct None {};         // Лексема «None»
    struct True {};         // Лексема «True»
    struct False {};        // Лексема «False»
}  // namespace token_type

using TokenBase = std::variant<
    token_type::Number,     // 0
    token_type::Id,         // 1
    token_type::Char,       // 2
    token_type::String,     // 3
    token_type::Class,      // 4
    token_type::Return,     // 5
    token_type::If,         // 6
    token_type::Else,       // 7
    token_type::Def,        // 8
    token_type::Newline,    // 9
    token_type::Print,      // 10
    token_type::Indent,     // 11
    token_type::Dedent,     // 12
    token_type::And,        // 13
    token_type::Or,         // 14
    token_type::Not,        // 15
    token_type::Eq,         // 16
    token_type::NotEq,      // 17
    token_type::LessOrEq,   // 18
    token_type::GreaterOrEq,// 19
    token_type::None,       // 20
    token_type::True,       // 21
    token_type::False,      // 22
    token_type::Eof         // 23
>;

struct Token : TokenBase {
    using TokenBase::TokenBase;

    template <typename T>
    [[nodiscard]] bool Is() const {
        return std::holds_alternative<T>(*this);
    }

    template <typename T>
    [[nodiscard]] const T& As() const {
        return std::get<T>(*this);
    }

    template <typename T>
    [[nodiscard]] const T* TryAs() const {
        return std::get_if<T>(this);
    }
};

template<typename T>
using QueueContainer = std::deque<T>;
using TokenQueue = QueueContainer<Token>;

namespace token_const {
    static const size_t INDENT_STEP = 2;

    static const std::unordered_set<char> OPERATOR_CHAR = {':', '(', ')', ',', '.', '+', '-', '*', '/', '!', '>', '<', '='};

    static const std::unordered_map<std::string, Token> SPECIAL_WORDS = {
        { std::string("=="),     token_type::Eq{}          },
        { std::string("!="),     token_type::NotEq{}       },
        { std::string("<="),     token_type::LessOrEq{}    },
        { std::string(">="),     token_type::GreaterOrEq{} },
        { std::string("class"),  token_type::Class{}       },
        { std::string("return"), token_type::Return{}      },
        { std::string("if"),     token_type::If{}          },
        { std::string("else"),   token_type::Else{}        },
        { std::string("def"),    token_type::Def{}         },
        { std::string("print"),  token_type::Print{}       },
        { std::string("and"),    token_type::And{}         },
        { std::string("or"),     token_type::Or{}          },
        { std::string("not"),    token_type::Not{}         },
        { std::string("None"),   token_type::None{}        },
        { std::string("True"),   token_type::True{}        },
        { std::string("False"),  token_type::False{}       }
    };
} // namespace lexer_consts

bool operator==(const Token& lhs, const Token& rhs);
bool operator!=(const Token& lhs, const Token& rhs);

std::ostream& operator<<(std::ostream& os, const Token& rhs);

class LexerError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

namespace utility {

    char GetChar(std::istream& input);
    char PeekChar(std::istream& input);

    bool IsNextCharCorrect(std::istream& input);

    size_t ReadWhile(std::istream& input, std::function<bool(char)> fn);
    std::string SaveCharWhile(std::istream& input, std::function<bool(char)> fn);

    class TokenParser {
    public:
        static TokenQueue Tokenise(std::istream& input);
    private:
        struct State {
            size_t current_intent_nesting;
            std::istream& input;
            TokenQueue& token_queue;
        };

        static bool PushIntents(State& state);
        static bool SkipComments(State& state);
        static bool SkipSpaces(State& state);
        static bool ProcessNewLine(State& state);
        static bool ProcessStringLiteral(State& state);
        static bool ProcessIntLiteral(State& state);
        static bool ProcessOperator(State& state);
        static bool ProcessSpecialWords(State& state);
        static bool ProcessId(State& state);

        static void Optimize(TokenQueue& token_queue);
    };

    TokenQueue Tokenise(std::istream& input);
}

class Lexer {
public:
    explicit Lexer(std::istream& input);

    // Возвращает ссылку на текущий токен или token_type::Eof, если поток токенов закончился
    [[nodiscard]] const Token& CurrentToken() const;

    // Возвращает следующий токен, либо token_type::Eof, если поток токенов закончился
    Token NextToken();

    // Если текущий токен имеет тип T, метод возвращает ссылку на него.
    // В противном случае метод выбрасывает исключение LexerError
    template <typename T>
    const T& Expect() const {
        using namespace std::literals;
        if (!CurrentToken().Is<T>()) {
            throw LexerError("Expect token type error"s);
        }
        return CurrentToken().As<T>();
    }

    // Метод проверяет, что текущий токен имеет тип T, а сам токен содержит значение value.
    // В противном случае метод выбрасывает исключение LexerError
    template <typename T, typename U>
    void Expect(const U& value) const {
        using namespace std::literals;
        const auto& token = Expect<T>();
        if (token.value != value) {
            throw LexerError("Expect token value error"s);
        }
    }

    // Если следующий токен имеет тип T, метод возвращает ссылку на него.
    // В противном случае метод выбрасывает исключение LexerError
    template <typename T>
    const T& ExpectNext() {
        ++m_current_index;
        return Expect<T>();
    }

    // Метод проверяет, что следующий токен имеет тип T, а сам токен содержит значение value.
    // В противном случае метод выбрасывает исключение LexerError
    template <typename T, typename U>
    void ExpectNext(const U& value) {
        using namespace std::literals;
        ++m_current_index;
        return Expect<T>(value);
    }

private:
    std::deque<Token> m_tokens;
    size_t m_current_index = 0;
};

}  // namespace parse