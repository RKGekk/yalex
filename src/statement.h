#pragma once

#include "runtime.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ast {

class ReturnException : std::runtime_error {
public:
    explicit ReturnException(runtime::ObjectHolder result) : std::runtime_error("result"), m_result(std::move(result)) {}
    
    runtime::ObjectHolder GetResult() const {
        return m_result;
    }

private:
    runtime::ObjectHolder m_result{};
};

// Выражение, возвращающее значение типа T,
// используется как основа для создания констант
template <typename T>
class ValueStatement : public runtime::Executable {
public:
    explicit ValueStatement(T v) : m_value(std::move(v)) {}

    runtime::ObjectHolder Execute(runtime::Closure& closure, runtime::Context& context) override {
        return runtime::ObjectHolder::Share(m_value);
    }

private:
    T m_value;
};

using NumericConst = ValueStatement<runtime::Number>;
using StringConst = ValueStatement<runtime::String>;
using BoolConst = ValueStatement<runtime::Bool>;

/*
Вычисляет значение переменной либо цепочки вызовов полей объектов id1.id2.id3.
Например, выражение circle.center.x - цепочка вызовов полей объектов в инструкции:
x = circle.center.x
*/
class VariableValue : public runtime::Executable {
public:
    explicit VariableValue(const std::string& var_name);
    explicit VariableValue(std::vector<std::string> dotted_ids);

    runtime::ObjectHolder Execute(runtime::Closure& closure, runtime::Context& context) override;

private:
    std::vector<std::string> m_id_seq;
};

// Присваивает переменной, имя которой задано в параметре var, значение выражения rv
class Assignment : public runtime::Executable {
public:
    Assignment(std::string var, std::unique_ptr<runtime::Executable> rv);

    runtime::ObjectHolder Execute(runtime::Closure& closure, runtime::Context& context) override;

private:
    std::string m_var_to_assign;
    std::unique_ptr<runtime::Executable> m_stm_to_execute;
};

// Присваивает полю object.field_name значение выражения rv
class FieldAssignment : public runtime::Executable {
public:
    FieldAssignment(VariableValue object, std::string field_name, std::unique_ptr<runtime::Executable> rv);

    runtime::ObjectHolder Execute(runtime::Closure& closure, runtime::Context& context) override;

private:
    VariableValue m_object_to_store;
    std::string m_field_name;
    std::unique_ptr<runtime::Executable> m_stm_to_execute;
};

// Значение None
class None : public runtime::Executable {
public:
    runtime::ObjectHolder Execute([[maybe_unused]] runtime::Closure& closure, [[maybe_unused]] runtime::Context& context) override {
        return runtime::ObjectHolder::None();
    }
};

// Команда print
class Print : public runtime::Executable {
public:
    // Инициализирует команду print для вывода значения выражения argument
    explicit Print(std::unique_ptr<runtime::Executable> argument);
    // Инициализирует команду print для вывода списка значений args
    explicit Print(std::vector<std::unique_ptr<runtime::Executable>> args);

    // Инициализирует команду print для вывода значения переменной name
    static std::unique_ptr<Print> Variable(const std::string& name);

    // Во время выполнения команды print вывод должен осуществляться в поток, возвращаемый из
    // context.GetOutputStream()
    runtime::ObjectHolder Execute(runtime::Closure& closure, runtime::Context& context) override;

private:
    std::vector<std::unique_ptr<runtime::Executable>> m_args;
};

// Вызывает метод object.method со списком параметров args
class MethodCall : public runtime::Executable {
public:
    MethodCall(std::unique_ptr<runtime::Executable> object, std::string method, std::vector<std::unique_ptr<runtime::Executable>> args);

    runtime::ObjectHolder Execute(runtime::Closure& closure, runtime::Context& context) override;

private:
    std::unique_ptr<runtime::Executable> m_object;
    std::string m_method;
    std::vector<std::unique_ptr<runtime::Executable>> m_args;
};

/*
Создаёт новый экземпляр класса class_, передавая его конструктору набор параметров args.
Если в классе отсутствует метод __init__ с заданным количеством аргументов,
то экземпляр класса создаётся без вызова конструктора (поля объекта не будут проинициализированы):

class Person:
  def set_name(name):
    self.name = name

p = Person()
# Поле name будет иметь значение только после вызова метода set_name
p.set_name("Ivan")
*/
class NewInstance : public runtime::Executable {
public:
    explicit NewInstance(const runtime::Class& class_);
    NewInstance(const runtime::Class& class_, std::vector<std::unique_ptr<runtime::Executable>> args);

    // Возвращает объект, содержащий значение типа ClassInstance
    runtime::ObjectHolder Execute(runtime::Closure& closure, runtime::Context& context) override;

private:
    runtime::ClassInstance m_instance;
    std::vector<std::unique_ptr<runtime::Executable>> m_ctx_args;
};

// Базовый класс для унарных операций
class UnaryOperation : public runtime::Executable {
public:
    explicit UnaryOperation(std::unique_ptr<runtime::Executable> argument) : m_arg(std::move(argument)) {}

protected:
    std::unique_ptr<runtime::Executable> m_arg;
};

// Операция str, возвращающая строковое значение своего аргумента
class Stringify : public UnaryOperation {
public:
    using UnaryOperation::UnaryOperation;
    runtime::ObjectHolder Execute(runtime::Closure& closure, runtime::Context& context) override;
};

// Родительский класс Бинарная операция с аргументами lhs и rhs
class BinaryOperation : public runtime::Executable {
public:
    BinaryOperation(std::unique_ptr<runtime::Executable> lhs, std::unique_ptr<runtime::Executable> rhs) : m_lhs_stm(std::move(lhs)), m_rhs_stm(std::move(rhs)) {}

protected:
    std::unique_ptr<runtime::Executable> m_lhs_stm;
    std::unique_ptr<runtime::Executable> m_rhs_stm;
};

// Возвращает результат операции + над аргументами lhs и rhs
class Add : public BinaryOperation {
public:
    using BinaryOperation::BinaryOperation;

    // Поддерживается сложение:
    //  число + число
    //  строка + строка
    //  объект1 + объект2, если у объект1 - пользовательский класс с методом _add__(rhs)
    // В противном случае при вычислении выбрасывается runtime_error
    runtime::ObjectHolder Execute(runtime::Closure& closure, runtime::Context& context) override;
};

// Возвращает результат вычитания аргументов lhs и rhs
class Sub : public BinaryOperation {
public:
    using BinaryOperation::BinaryOperation;

    // Поддерживается вычитание:
    //  число - число
    // Если lhs и rhs - не числа, выбрасывается исключение runtime_error
    runtime::ObjectHolder Execute(runtime::Closure& closure, runtime::Context& context) override;
};

// Возвращает результат умножения аргументов lhs и rhs
class Mult : public BinaryOperation {
public:
    using BinaryOperation::BinaryOperation;

    // Поддерживается умножение:
    //  число * число
    // Если lhs и rhs - не числа, выбрасывается исключение runtime_error
    runtime::ObjectHolder Execute(runtime::Closure& closure, runtime::Context& context) override;
};

// Возвращает результат деления lhs и rhs
class Div : public BinaryOperation {
public:
    using BinaryOperation::BinaryOperation;

    // Поддерживается деление:
    //  число / число
    // Если lhs и rhs - не числа, выбрасывается исключение runtime_error
    // Если rhs равен 0, выбрасывается исключение runtime_error
    runtime::ObjectHolder Execute(runtime::Closure& closure, runtime::Context& context) override;
};

// Возвращает результат вычисления логической операции or над lhs и rhs
class Or : public BinaryOperation {
public:
    using BinaryOperation::BinaryOperation;
    // Значение аргумента rhs вычисляется, только если значение lhs
    // после приведения к Bool равно False
    runtime::ObjectHolder Execute(runtime::Closure& closure, runtime::Context& context) override;
};

// Возвращает результат вычисления логической операции and над lhs и rhs
class And : public BinaryOperation {
public:
    using BinaryOperation::BinaryOperation;
    // Значение аргумента rhs вычисляется, только если значение lhs
    // после приведения к Bool равно True
    runtime::ObjectHolder Execute(runtime::Closure& closure, runtime::Context& context) override;
};

// Возвращает результат вычисления логической операции not над единственным аргументом операции
class Not : public UnaryOperation {
public:
    using UnaryOperation::UnaryOperation;
    runtime::ObjectHolder Execute(runtime::Closure& closure, runtime::Context& context) override;
};

// Составная инструкция (например: тело метода, содержимое ветки if, либо else)
class Compound : public runtime::Executable {
public:
    // Конструирует Compound из нескольких инструкций типа unique_ptr<runtime::Executable>
    template <typename... Args>
    explicit Compound(Args&&... args) {
        if constexpr ((sizeof...(args)) != 0) {
            FillOperations(std::forward<Args>(args)...);
        }
    }

    // Добавляет очередную инструкцию в конец составной инструкции
    void AddStatement(std::unique_ptr<runtime::Executable> stmt) {
        m_operations.push_back(std::move(stmt));
    }

    // Последовательно выполняет добавленные инструкции. Возвращает None
    runtime::ObjectHolder Execute(runtime::Closure& closure, runtime::Context& context) override;

private:
    template <typename... Args>
    void FillOperations(std::unique_ptr<runtime::Executable>&& stmt, Args&&... args) {
        m_operations.push_back(std::move(stmt));
        if constexpr (sizeof...(args) != 0) {
            FillOperations(std::forward<Args>(args)...);
        }
    }

    std::vector<std::unique_ptr<runtime::Executable>> m_operations;
};

// Тело метода. Как правило, содержит составную инструкцию
class MethodBody : public runtime::Executable {
public:
    explicit MethodBody(std::unique_ptr<runtime::Executable> body);

    // Вычисляет инструкцию, переданную в качестве body.
    // Если внутри body была выполнена инструкция return, возвращает результат return
    // В противном случае возвращает None
    runtime::ObjectHolder Execute(runtime::Closure& closure, runtime::Context& context) override;

private:
    std::unique_ptr<runtime::Executable> m_body;
};

// Выполняет инструкцию return с выражением statement
class Return : public runtime::Executable {
public:
    explicit Return(std::unique_ptr<runtime::Executable> statement) : m_statement(std::move(statement)) {}

    // Останавливает выполнение текущего метода. После выполнения инструкции return метод,
    // внутри которого она была исполнена, должен вернуть результат вычисления выражения statement.
    runtime::ObjectHolder Execute(runtime::Closure& closure, runtime::Context& context) override;

private:
    std::unique_ptr<runtime::Executable> m_statement;
};

// Объявляет класс
class ClassDefinition : public runtime::Executable {
public:
    // Гарантируется, что ObjectHolder содержит объект типа runtime::Class
    explicit ClassDefinition(runtime::ObjectHolder cls);

    // Создаёт внутри closure новый объект, совпадающий с именем класса и значением, переданным в
    // конструктор
    runtime::ObjectHolder Execute(runtime::Closure& closure, runtime::Context& context) override;

private:
    runtime::ObjectHolder m_class;
};

// Инструкция if <condition> <if_body> else <else_body>
class IfElse : public runtime::Executable {
public:
    // Параметр else_body может быть равен nullptr
    IfElse(std::unique_ptr<runtime::Executable> condition, std::unique_ptr<runtime::Executable> if_body, std::unique_ptr<runtime::Executable> else_body);

    runtime::ObjectHolder Execute(runtime::Closure& closure, runtime::Context& context) override;

private:
    std::unique_ptr<runtime::Executable> m_condition;
    std::unique_ptr<runtime::Executable> m_if_body;
    std::unique_ptr<runtime::Executable> m_else_body;
};

// Операция сравнения
class Comparison : public BinaryOperation {
public:
    // Comparator задаёт функцию, выполняющую сравнение значений аргументов
    using Comparator = std::function<bool(const runtime::ObjectHolder&, const runtime::ObjectHolder&, runtime::Context&)>;

    Comparison(Comparator cmp, std::unique_ptr<runtime::Executable> lhs, std::unique_ptr<runtime::Executable> rhs);

    // Вычисляет значение выражений lhs и rhs и возвращает результат работы comparator,
    // приведённый к типу runtime::Bool
    runtime::ObjectHolder Execute(runtime::Closure& closure, runtime::Context& context) override;

private:
    Comparator m_comparator;
};

}  // namespace ast