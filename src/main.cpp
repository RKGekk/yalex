#include <iostream>

#include "lexer.h"
#include "runtime.h"
#include "test_runner_p.h"

namespace parse {
    void RunOpenLexerTests(TestRunner& tr);
}

namespace runtime {
    void RunObjectHolderTests(TestRunner& tr);
    void RunObjectsTests(TestRunner& tr);
}

namespace {
    void TestAll() {
        TestRunner tr;
        parse::RunOpenLexerTests(tr);
        runtime::RunObjectHolderTests(tr);
        runtime::RunObjectsTests(tr);
    }
}  // namespace

int main() {
    try {
        TestAll();
        parse::Lexer lexer(std::cin);
        parse::Token t;
        while ((t = lexer.CurrentToken()) != parse::token_type::Eof{}) {
            std::cout << t << std::endl;
            lexer.NextToken();
        }
    }
    catch (const std::exception& e) {
        std::cerr << e.what();
        return 1;
    }

    return 0;
}