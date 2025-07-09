#include "parse.h"

#include "lexer.h"
#include "runtime.h"
#include "statement.h"

using namespace std;

namespace TokenType = parse::token_type;

namespace {
bool operator==(const parse::Token& token, char c) {
    const TokenType::Char* p = token.TryAs<TokenType::Char>();
    return p != nullptr && p->value == c;
}

bool operator!=(const parse::Token& token, char c) {
    return !(token == c);
}

class Parser {
public:
    explicit Parser(parse::Lexer& lexer) : m_lexer(lexer) {}

    // Program -> eps
    //          | Statement \n Program
    unique_ptr<runtime::Executable> ParseProgram() {
        std::unique_ptr<ast::Compound> result = make_unique<ast::Compound>();
        while (!m_lexer.CurrentToken().Is<TokenType::Eof>()) {
            if(m_lexer.CurrentToken().Is<TokenType::Newline>()) {
                m_lexer.NextToken();
                continue;
            }
            result->AddStatement(ParseStatement());
        }

        return result;
    }

private:
    // Suite -> NEWLINE INDENT (Statement)+ DEDENT
    unique_ptr<runtime::Executable> ParseSuite() {
        m_lexer.Expect<TokenType::Newline>();
        m_lexer.ExpectNext<TokenType::Indent>();

        m_lexer.NextToken();

        std::unique_ptr<ast::Compound> result = make_unique<ast::Compound>();
        while (!m_lexer.CurrentToken().Is<TokenType::Dedent>()) {
            result->AddStatement(ParseStatement());
        }

        m_lexer.Expect<TokenType::Dedent>();
        m_lexer.NextToken();

        return result;
    }

    // Methods -> [def id(Params) : Suite]*
    vector<runtime::Method> ParseMethods() {
        vector<runtime::Method> result;

        while (m_lexer.CurrentToken().Is<TokenType::Def>()) {
            runtime::Method m;

            m.name = m_lexer.ExpectNext<TokenType::Id>().value;
            m_lexer.ExpectNext<TokenType::Char>('(');

            if (m_lexer.NextToken().Is<TokenType::Id>()) {
                m.formal_params.push_back(m_lexer.Expect<TokenType::Id>().value);
                while (m_lexer.NextToken() == ',') {
                    m.formal_params.push_back(m_lexer.ExpectNext<TokenType::Id>().value);
                }
            }

            m_lexer.Expect<TokenType::Char>(')');
            m_lexer.ExpectNext<TokenType::Char>(':');
            m_lexer.NextToken();

            m.body = std::make_unique<ast::MethodBody>(ParseSuite());

            result.push_back(std::move(m));
        }
        return result;
    }

    // ClassDefinition -> Id ['(' Id ')'] : new_line indent MethodList dedent
    unique_ptr<runtime::Executable> ParseClassDefinition() {
        string class_name = m_lexer.Expect<TokenType::Id>().value;

        m_lexer.NextToken();

        const runtime::Class* base_class = nullptr;
        if (m_lexer.CurrentToken() == '(') {
            std::string name = m_lexer.ExpectNext<TokenType::Id>().value;
            m_lexer.ExpectNext<TokenType::Char>(')');
            m_lexer.NextToken();

            runtime::Closure::iterator it = m_declared_classes.find(name);
            if (it == m_declared_classes.end()) {
                throw ParseError("Base class "s + name + " not found for class "s + class_name);
            }
            base_class = static_cast<const runtime::Class*>(it->second.Get());
        }

        m_lexer.Expect<TokenType::Char>(':');
        m_lexer.ExpectNext<TokenType::Newline>();
        m_lexer.ExpectNext<TokenType::Indent>();
        m_lexer.ExpectNext<TokenType::Def>();
        vector<runtime::Method> methods = ParseMethods();

        m_lexer.Expect<TokenType::Dedent>();
        m_lexer.NextToken();

        auto [it, inserted] = m_declared_classes.insert({class_name, runtime::ObjectHolder::Own(runtime::Class(class_name, std::move(methods), base_class))});

        if (!inserted) {
            throw ParseError("Class "s + class_name + " already exists"s);
        }

        return make_unique<ast::ClassDefinition>(it->second);
    }

    vector<string> ParseDottedIds() {
        vector<string> result(1, m_lexer.Expect<TokenType::Id>().value);

        while (m_lexer.NextToken() == '.') {
            result.push_back(m_lexer.ExpectNext<TokenType::Id>().value);
        }

        return result;
    }

    //  AssignmentOrCall -> DottedIds = Expr
    //                   | DottedIds '(' ExprList ')'
    unique_ptr<runtime::Executable> ParseAssignmentOrCall() {
        m_lexer.Expect<TokenType::Id>();

        vector<string> id_list = ParseDottedIds();
        string last_name = id_list.back();
        id_list.pop_back();

        if (m_lexer.CurrentToken() == '=') {
            m_lexer.NextToken();

            if (id_list.empty()) {
                return make_unique<ast::Assignment>(std::move(last_name), ParseTest());
            }
            return make_unique<ast::FieldAssignment>(ast::VariableValue{std::move(id_list)}, std::move(last_name), ParseTest());
        }
        m_lexer.Expect<TokenType::Char>('(');
        m_lexer.NextToken();

        if (id_list.empty()) {
            throw ParseError("Mython doesn't support functions, only methods: "s + last_name);
        }

        vector<unique_ptr<runtime::Executable>> args;
        if (m_lexer.CurrentToken() != ')') {
            args = ParseTestList();
        }
        m_lexer.Expect<TokenType::Char>(')');
        m_lexer.NextToken();

        return make_unique<ast::MethodCall>(make_unique<ast::VariableValue>(std::move(id_list)), std::move(last_name), std::move(args));
    }

    // Expr -> Adder ['+'/'-' Adder]*
    unique_ptr<runtime::Executable> ParseExpression() {

        unique_ptr<runtime::Executable> result = ParseAdder();
        while (m_lexer.CurrentToken() == '+' || m_lexer.CurrentToken() == '-') {
            char op = m_lexer.CurrentToken().As<TokenType::Char>().value;
            m_lexer.NextToken();

            if (op == '+') {
                result = make_unique<ast::Add>(std::move(result), ParseAdder());
            } else {
                result = make_unique<ast::Sub>(std::move(result), ParseAdder());
            }
        }
        return result;
    }

    // Adder -> Mult ['*'/'/' Mult]*
    unique_ptr<runtime::Executable> ParseAdder() {
        unique_ptr<runtime::Executable> result = ParseMult();
        while (m_lexer.CurrentToken() == '*' || m_lexer.CurrentToken() == '/') {
            char op = m_lexer.CurrentToken().As<TokenType::Char>().value;
            m_lexer.NextToken();

            if (op == '*') {
                result = make_unique<ast::Mult>(std::move(result), ParseMult());
            } else {
                result = make_unique<ast::Div>(std::move(result), ParseMult());
            }
        }
        return result;
    }

    // Mult -> '(' Expr ')'
    //       | NUMBER
    //       | '-' Mult
    //       | STRING
    //       | NONE
    //       | TRUE
    //       | FALSE
    //       | DottedIds '(' ExprList ')'
    //       | DottedIds
    unique_ptr<runtime::Executable> ParseMult() {
        if (m_lexer.CurrentToken() == '(') {
            m_lexer.NextToken();
            unique_ptr<runtime::Executable> result = ParseTest();
            m_lexer.Expect<TokenType::Char>(')');
            m_lexer.NextToken();
            return result;
        }
        if (m_lexer.CurrentToken() == '-') {
            m_lexer.NextToken();
            return make_unique<ast::Mult>(ParseMult(), make_unique<ast::NumericConst>(-1));
        }
        if (const TokenType::Number* num = m_lexer.CurrentToken().TryAs<TokenType::Number>()) {
            int result = num->value;
            m_lexer.NextToken();
            return make_unique<ast::NumericConst>(result);
        }
        if (const TokenType::String* str = m_lexer.CurrentToken().TryAs<TokenType::String>()) {
            string result = str->value;
            m_lexer.NextToken();
            return make_unique<ast::StringConst>(std::move(result));
        }
        if (m_lexer.CurrentToken().Is<TokenType::True>()) {
            m_lexer.NextToken();
            return make_unique<ast::BoolConst>(runtime::Bool(true));
        }
        if (m_lexer.CurrentToken().Is<TokenType::False>()) {
            m_lexer.NextToken();
            return make_unique<ast::BoolConst>(runtime::Bool(false));
        }
        if (m_lexer.CurrentToken().Is<TokenType::None>()) {
            m_lexer.NextToken();
            return make_unique<ast::None>();
        }

        return ParseDottedIdsInMultExpr();
    }

    std::unique_ptr<runtime::Executable> ParseDottedIdsInMultExpr() {
        vector<string> names = ParseDottedIds();

        if (m_lexer.CurrentToken() == '(') {
            // various calls
            vector<unique_ptr<runtime::Executable>> args;
            if (m_lexer.NextToken() != ')') {
                args = ParseTestList();
            }
            m_lexer.Expect<TokenType::Char>(')');
            m_lexer.NextToken();

            std::string method_name = names.back();
            names.pop_back();

            if (!names.empty()) {
                return make_unique<ast::MethodCall>(make_unique<ast::VariableValue>(std::move(names)), std::move(method_name), std::move(args));
            }
            if (runtime::Closure::iterator it = m_declared_classes.find(method_name); it != m_declared_classes.end()) {
                return make_unique<ast::NewInstance>(static_cast<const runtime::Class&>(*it->second), std::move(args));
            }
            if (method_name == "str"sv) {
                if (args.size() != 1) {
                    throw ParseError("Function str takes exactly one argument"s);
                }
                return make_unique<ast::Stringify>(std::move(args.front()));
            }
            throw ParseError("Unknown call to "s + method_name + "()"s);
        }
        return make_unique<ast::VariableValue>(std::move(names));
    }

    vector<unique_ptr<runtime::Executable>> ParseTestList() {
        vector<unique_ptr<runtime::Executable>> result;
        result.push_back(ParseTest());

        while (m_lexer.CurrentToken() == ',') {
            m_lexer.NextToken();
            result.push_back(ParseTest());
        }
        return result;
    }

    // Condition -> if LogicalExpr: Suite [else: Suite]
    unique_ptr<runtime::Executable> ParseCondition() {
        m_lexer.Expect<TokenType::If>();
        m_lexer.NextToken();

        unique_ptr<runtime::Executable> condition = ParseTest();

        m_lexer.Expect<TokenType::Char>(':');
        m_lexer.NextToken();

        unique_ptr<runtime::Executable> if_body = ParseSuite();

        unique_ptr<runtime::Executable> else_body;
        if (m_lexer.CurrentToken().Is<TokenType::Else>()) {
            m_lexer.ExpectNext<TokenType::Char>(':');
            m_lexer.NextToken();
            else_body = ParseSuite();
        }

        return make_unique<ast::IfElse>(std::move(condition), std::move(if_body), std::move(else_body));
    }

    // LogicalExpr -> AndTest [OR AndTest]
    // AndTest -> NotTest [AND NotTest]
    // NotTest -> [NOT] NotTest
    //          | Comparison
    unique_ptr<runtime::Executable> ParseTest() {
        unique_ptr<runtime::Executable> result = ParseAndTest();
        while (m_lexer.CurrentToken().Is<TokenType::Or>()) {
            m_lexer.NextToken();
            result = make_unique<ast::Or>(std::move(result), ParseAndTest());
        }
        return result;
    }

    unique_ptr<runtime::Executable> ParseAndTest() {
        unique_ptr<runtime::Executable> result = ParseNotTest();
        while (m_lexer.CurrentToken().Is<TokenType::And>()) {
            m_lexer.NextToken();
            result = make_unique<ast::And>(std::move(result), ParseNotTest());
        }
        return result;
    }

    unique_ptr<runtime::Executable> ParseNotTest() {
        if (m_lexer.CurrentToken().Is<TokenType::Not>()) {
            m_lexer.NextToken();
            return make_unique<ast::Not>(ParseNotTest());
        }
        return ParseComparison();
    }

    // Comparison -> Expr [COMP_OP Expr]
    unique_ptr<runtime::Executable> ParseComparison() {
        unique_ptr<runtime::Executable> result = ParseExpression();

        const parse::Token tok = m_lexer.CurrentToken();

        if (tok == '<') {
            m_lexer.NextToken();
            return make_unique<ast::Comparison>(runtime::Less, std::move(result), ParseExpression());
        }
        if (tok == '>') {
            m_lexer.NextToken();
            return make_unique<ast::Comparison>(runtime::Greater, std::move(result), ParseExpression());
        }
        if (tok.Is<TokenType::Eq>()) {
            m_lexer.NextToken();
            return make_unique<ast::Comparison>(runtime::Equal, std::move(result), ParseExpression());
        }
        if (tok.Is<TokenType::NotEq>()) {
            m_lexer.NextToken();
            return make_unique<ast::Comparison>(runtime::NotEqual, std::move(result), ParseExpression());
        }
        if (tok.Is<TokenType::LessOrEq>()) {
            m_lexer.NextToken();
            return make_unique<ast::Comparison>(runtime::LessOrEqual, std::move(result), ParseExpression());
        }
        if (tok.Is<TokenType::GreaterOrEq>()) {
            m_lexer.NextToken();
            return make_unique<ast::Comparison>(runtime::GreaterOrEqual, std::move(result), ParseExpression());
        }
        return result;
    }

    // Statement -> SimpleStatement Newline
    //           | class ClassDefinition
    //           | if Condition
    unique_ptr<runtime::Executable> ParseStatement() {
        const parse::Token& tok = m_lexer.CurrentToken();

        if (tok.Is<TokenType::Class>()) {
            m_lexer.NextToken();
            return ParseClassDefinition();
        }
        if (tok.Is<TokenType::If>()) {
            return ParseCondition();
        }
        unique_ptr<runtime::Executable> result = ParseSimpleStatement();
        if(m_lexer.CurrentToken().Is<TokenType::Eof>()) {
            return result;
        }
        m_lexer.Expect<TokenType::Newline>();
        m_lexer.NextToken();
        return result;
    }

    // SimpleStatement -> return Expression
    //                 | print ExpressionList
    //                 | AssignmentOrCall
    unique_ptr<runtime::Executable> ParseSimpleStatement() {
        const parse::Token& tok = m_lexer.CurrentToken();

        if (tok.Is<TokenType::Return>()) {
            m_lexer.NextToken();
            return make_unique<ast::Return>(ParseTest());
        }
        if (tok.Is<TokenType::Print>()) {
            m_lexer.NextToken();
            vector<unique_ptr<runtime::Executable>> args;
            if (!m_lexer.CurrentToken().Is<TokenType::Newline>()) {
                args = ParseTestList();
            }
            return make_unique<ast::Print>(std::move(args));
        }
        return ParseAssignmentOrCall();
    }

    parse::Lexer& m_lexer;
    runtime::Closure m_declared_classes;
};

}  // namespace

unique_ptr<runtime::Executable> ParseProgram(parse::Lexer& lexer) {
    return Parser{lexer}.ParseProgram();
}