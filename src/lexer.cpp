#include "lexer.h"

#include <algorithm>
#include <charconv>
#include <unordered_map>

using namespace std;

namespace parse {

bool operator==(const Token& lhs, const Token& rhs) {
    using namespace token_type;

    if (lhs.index() != rhs.index()) {
        return false;
    }
    if (lhs.Is<Char>()) {
        return lhs.As<Char>().value == rhs.As<Char>().value;
    }
    if (lhs.Is<Number>()) {
        return lhs.As<Number>().value == rhs.As<Number>().value;
    }
    if (lhs.Is<String>()) {
        return lhs.As<String>().value == rhs.As<String>().value;
    }
    if (lhs.Is<Id>()) {
        return lhs.As<Id>().value == rhs.As<Id>().value;
    }
    return true;
}

bool operator!=(const Token& lhs, const Token& rhs) {
    return !(lhs == rhs);
}

std::ostream& operator<<(std::ostream& os, const Token& rhs) {
    using namespace token_type;

#define VALUED_OUTPUT(type) if (auto p = rhs.TryAs<type>()) return os << #type << '{' << p->value << '}';

    VALUED_OUTPUT(Number);
    VALUED_OUTPUT(Id);
    VALUED_OUTPUT(String);
    VALUED_OUTPUT(Char);

#undef VALUED_OUTPUT

#define UNVALUED_OUTPUT(type) if (rhs.Is<type>()) return os << #type;

    UNVALUED_OUTPUT(Class);
    UNVALUED_OUTPUT(Return);
    UNVALUED_OUTPUT(If);
    UNVALUED_OUTPUT(Else);
    UNVALUED_OUTPUT(Def);
    UNVALUED_OUTPUT(Newline);
    UNVALUED_OUTPUT(Print);
    UNVALUED_OUTPUT(Indent);
    UNVALUED_OUTPUT(Dedent);
    UNVALUED_OUTPUT(And);
    UNVALUED_OUTPUT(Or);
    UNVALUED_OUTPUT(Not);
    UNVALUED_OUTPUT(Eq);
    UNVALUED_OUTPUT(NotEq);
    UNVALUED_OUTPUT(LessOrEq);
    UNVALUED_OUTPUT(GreaterOrEq);
    UNVALUED_OUTPUT(None);
    UNVALUED_OUTPUT(True);
    UNVALUED_OUTPUT(False);
    UNVALUED_OUTPUT(Eof);

#undef UNVALUED_OUTPUT

    return os << "Unknown token :("sv;
}

namespace utility {
    char GetChar(std::istream& input) {
        return static_cast<char>(input.get());
    }

    char PeekChar(std::istream& input) {
        return static_cast<char>(input.peek());
    }

    bool IsNextCharCorrect(std::istream& input) {
        const char next_char = PeekChar(input);
        if(std::iscntrl(next_char) && !std::isspace(next_char)) {
            return false;
        }
        return true;
    }

    size_t ReadWhile(std::istream& input, std::function<bool(char)> fn) {
        size_t ct = 0u;
        if(!input) return ct;
        char ch = GetChar(input);
        while(ch != std::char_traits<char>::eof()) {
            if(fn(ch)) {
                ++ct;
                ch = GetChar(input);
            }
            else {
                input.unget();
                break;
            }
        }
        return ct;
    }

    std::string SaveCharWhile(std::istream& input, std::function<bool(char)> fn) {
        std::string str;
        utility::ReadWhile(
            input,
            [&str, &fn](const char ch){
                const bool is_right = fn(ch);
                if(is_right) {
                    str += ch;
                }
                return is_right;
            }
        );
        return str;
    }

    TokenQueue Tokenise(std::istream& input) {
        return TokenParser::Tokenise(input);
    }

    bool TokenParser::PushIntents(State& state) {
        const size_t spaces_counter = utility::ReadWhile(state.input, [](const char ch){ return ch == ' ';});
        const size_t intents = spaces_counter / token_const::INDENT_STEP;
        const bool gt = intents > state.current_intent_nesting;
        const size_t intent_diff = gt ? intents - state.current_intent_nesting : state.current_intent_nesting - intents;
        if(intent_diff) {
            if(gt) {
                state.current_intent_nesting += intent_diff;
                state.token_queue.insert(state.token_queue.end(), intent_diff, token_type::Indent{});
            }
            else {
                state.current_intent_nesting -= intent_diff;
                state.token_queue.insert(state.token_queue.end(), intent_diff, token_type::Dedent{});
            }
            return true;
        }
        return false;
    }

    bool TokenParser::SkipComments(State& state) {
        if(PeekChar(state.input) == '#') {
            GetChar(state.input);
            utility::ReadWhile(state.input, [](const char ch){ return ch != '\n';});
            return true;
        }
        return false;
    }

    bool TokenParser::SkipSpaces(State& state) {
        const size_t spaces_counter = utility::ReadWhile(state.input, [](const char ch){ return ch == ' ';});
        if(spaces_counter) {
            return true;
        }
        return false;
    }

    bool TokenParser::ProcessNewLine(State& state) {
        if(PeekChar(state.input) == '\n') {
            GetChar(state.input);
            state.token_queue.push_back(token_type::Newline{});
            return true;
        }
        return false;
    }

    bool TokenParser::ProcessStringLiteral(State& state) {
        const char next_ch = PeekChar(state.input);
        if(next_ch == '\"' || next_ch == '\'') {
            const char quote_ch = GetChar(state.input);
            std::string str = SaveCharWhile(state.input, [&quote_ch](const char ch){return ch != quote_ch;});
            state.token_queue.push_back(token_type::String{str});
            GetChar(state.input);
            return true;
        }
        return false;
    }

    bool TokenParser::ProcessIntLiteral(State& state) {
        if(std::isdigit(PeekChar(state.input))) {
            std::string str = SaveCharWhile(state.input, [](const char ch){return std::isdigit(ch);});
            state.token_queue.push_back(token_type::Number{std::stoi(str)});
            return true;
        }
        return false;
    }

    bool TokenParser::ProcessOperator(State& state) {
        if(token_const::OPERATOR_CHAR.count(PeekChar(state.input))) {
            const char operator_char = GetChar(state.input);
            state.token_queue.push_back(token_type::Char{operator_char});
            return true;
        }
        return false;
    }

    bool TokenParser::ProcessSpecialWords(State& state) {
        static const std::function<bool(char)> is_in_special = [](const char ch) {return std::isalpha(ch) || ch == '=' || ch == '>' || ch == '<' || ch == '!';};
        if(is_in_special(PeekChar(state.input))) {
            const std::string str = SaveCharWhile(state.input, is_in_special);
            if(token_const::SPECIAL_WORDS.count(str)) {
                state.token_queue.push_back(token_const::SPECIAL_WORDS.at(str));
                return true;
            }
            else {
                if(state.input.eof()) {
                    state.input.clear();
                }
                for (std::string::const_reverse_iterator c = str.crbegin(); c != str.crend(); ++c) {
                    state.input.putback(*c);
                }
                return false;
            }
        }
        return false;
    }

    bool TokenParser::ProcessId(State& state) {
        const char next_ch = PeekChar(state.input);
        if(std::isalpha(next_ch) || next_ch == '_') {
            std::string str = SaveCharWhile(state.input, [](const char ch){return std::isalpha(ch) || std::isdigit(ch) || ch == '_';});
            state.token_queue.push_back(token_type::Id{str});
            return true;
        }
        return false;
    }

    void OptimizeStartCR(TokenQueue& token_queue) {
        const size_t sz = token_queue.size();
        if(!sz) {
            return;
        }

        if(sz == 1u && token_queue[0].Is<token_type::Newline>()) {
            token_queue.clear();
            return;
        }
        
        size_t cr_counter = 0u;
        for (size_t i = 0u; i < sz; ++i) {
            if(!token_queue[i].Is<token_type::Newline>()) {
                if(i) {
                    token_queue.erase(token_queue.cbegin(), token_queue.cbegin() + i);
                }
                return;
            }
            else {
                ++cr_counter;
            }
        }
        if(cr_counter == sz) {
            token_queue.clear();
        }
    }

    void OptimizeEndCR(TokenQueue& token_queue) {
        const int max_idx = static_cast<int>(token_queue.size()) - 1;
        for (int i = max_idx; i >= 0; --i) {
            if(!token_queue[i].Is<token_type::Newline>()) {
                if(i != max_idx) {
                    token_queue.erase(token_queue.cbegin() + i + 1, token_queue.cend());
                }
                break;
            }
        }
    }

    void OptimizeCRDoubles(TokenQueue& token_queue) {
        std::deque<std::pair<size_t, size_t>> to_remove;
        size_t last_cr = -1;
        const size_t sz = token_queue.size();
        for (size_t i = 0u; i < sz; ++i) {
            if(token_queue[i].Is<token_type::Newline>()) {
                if((last_cr + 1u) == i) {
                    to_remove.push_back({last_cr, i});
                }
                last_cr = i;
            }
        }

        for(auto it = to_remove.rbegin(); it != to_remove.rend(); ++it) {
            const auto&[start, end] = *it;
            token_queue.erase(token_queue.cbegin() + start, token_queue.cbegin() + end);
        }
    }

    void OptimizeIntents(TokenQueue& token_queue) {
        size_t sz = token_queue.size();
        if(!sz) {
            return;
        }
        size_t max_idx = sz -1u;

        size_t last_not_intent_pos = 0u;

        size_t intent_ct = 0u;
        size_t detent_ct = 0u;

        for (size_t i = 0u; i < sz; ++i) {

            const Token& currnt_token = token_queue[i];
            if(currnt_token.Is<token_type::Indent>()) {
                ++intent_ct;
            }
            else if(currnt_token.Is<token_type::Dedent>()) {
                ++detent_ct;
            }
            else if(currnt_token.Is<token_type::Newline>()) {
                continue;
            }
            else {
                if(intent_ct > detent_ct) {
                    size_t intent_diff = intent_ct - detent_ct;
                    size_t cut_from_pos = last_not_intent_pos + 2u;
                    size_t cut_range = i - cut_from_pos;
                    size_t cut_to_pos = cut_from_pos + cut_range;
                    token_queue.erase(token_queue.cbegin() + cut_from_pos, token_queue.cbegin() + cut_to_pos);
                    token_queue.insert(token_queue.cbegin() + cut_from_pos, intent_diff, token_type::Indent{});
                    sz -= cut_range;
                    max_idx -= cut_range;
                }
                else if(intent_ct < detent_ct) {
                    size_t intent_diff = detent_ct - intent_ct;
                    size_t cut_from_pos = last_not_intent_pos + 2u;
                    size_t cut_range = i - cut_from_pos;
                    size_t cut_to_pos = cut_from_pos + cut_range;
                    token_queue.erase(token_queue.cbegin() + cut_from_pos, token_queue.cbegin() + cut_to_pos);
                    token_queue.insert(token_queue.cbegin() + cut_from_pos, intent_diff, token_type::Dedent{});
                    sz -= cut_range;
                    max_idx -= cut_range;
                }
                else if(intent_ct != 0u && detent_ct != 0u && intent_ct == detent_ct) {
                    size_t cut_from_pos = last_not_intent_pos + 2u;
                    size_t cut_range = i - cut_from_pos;
                    size_t cut_to_pos = cut_from_pos + cut_range;
                    token_queue.erase(token_queue.cbegin() + cut_from_pos, token_queue.cbegin() + cut_to_pos);
                    sz -= cut_range;
                    max_idx -= cut_range;
                }
                
                
                intent_ct = 0u;
                detent_ct = 0u;
                last_not_intent_pos = i;
            }
        }
        if(last_not_intent_pos != max_idx) {
            token_queue.erase(token_queue.cbegin() + last_not_intent_pos + 1u, token_queue.cbegin() + max_idx + 1u);
        }
    }

    void TokenParser::Optimize(TokenQueue& token_queue) {
        OptimizeStartCR(token_queue);
        OptimizeEndCR(token_queue);
        OptimizeCRDoubles(token_queue);
        OptimizeIntents(token_queue);
    }

    TokenQueue TokenParser::Tokenise(std::istream& input) {
        TokenQueue result_token_queue;
        size_t current_intent_nesting = 0u;
        State current_state {current_intent_nesting, input, result_token_queue};
        while(input.good() && !input.eof()) {
            //assert(IsNextCharCorrect(input), "Input stream char voilation");
            PushIntents(current_state);
            while(input.good() && !input.eof()) {
                if(SkipComments(current_state)) {
                    continue;
                }
                if(ProcessNewLine(current_state)) {
                    break;
                }

                if(ProcessStringLiteral(current_state)) {
                    SkipSpaces(current_state);
                }
                if(ProcessIntLiteral(current_state)) {
                    SkipSpaces(current_state);
                }
                if(ProcessSpecialWords(current_state)) {
                    SkipSpaces(current_state);
                    continue;
                }
                if(ProcessOperator(current_state)) {
                    SkipSpaces(current_state);
                }
                if(ProcessId(current_state)) {
                    SkipSpaces(current_state);
                }
            }
        }
        Optimize(current_state.token_queue);

        return result_token_queue;
    }
}

const Token& Lexer::CurrentToken() const {
    if (m_current_index < m_tokens.size()) {
        return m_tokens.at(m_current_index);
    }
    else {
        static const Token eof = token_type::Eof{};
        return eof;
    }
}

Token Lexer::NextToken() {
    ++m_current_index;
    return CurrentToken();
}

Lexer::Lexer(std::istream& _input) : m_tokens(utility::Tokenise(_input)) {}

}  // namespace parse