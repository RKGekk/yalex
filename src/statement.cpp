#include "statement.h"
#include "lexer.h"
#include "test_runner_p.h"

#include <iostream>
#include <sstream>
#include <utility>

using namespace std;

namespace ast {

using runtime::Closure;
using runtime::Context;
using runtime::ObjectHolder;

VariableValue::VariableValue(const std::string& var_name) : m_id_seq{var_name} {
    ASSERT_EQUAL(var_name.size() > 0u, true);
}

VariableValue::VariableValue(std::vector<std::string> dotted_ids) : m_id_seq(std::move(dotted_ids)) {
    ASSERT_EQUAL(m_id_seq.size() > 0u, true);
}

ObjectHolder VariableValue::Execute(Closure& closure, Context& context) {
    if(!closure.count(m_id_seq[0])) {
        throw std::runtime_error("Closure doesn't have variable with name: "s + m_id_seq[0]);
    }

    const runtime::Closure* closure_ptr = &closure;
    size_t i = 1u;
    size_t sz = m_id_seq.size();
    for(; i < sz; ++i) {
        const std::string& id_name = m_id_seq[i - 1u];
        runtime::ClassInstance* instance_ptr = closure_ptr->at(id_name).TryAs<runtime::ClassInstance>();
        if(instance_ptr) {
            closure_ptr = &instance_ptr->Fields();
            continue;
        }
        ++i;
        break;
    }
    if(i != sz) {
        throw std::runtime_error("Closure doesn't have variable with name: "s + m_id_seq[i - 1u]);
    }
    return closure_ptr->at(m_id_seq[i - 1u]);
}

Assignment::Assignment(std::string var, std::unique_ptr<runtime::Executable> rv) : m_var_to_assign(std::move(var)), m_stm_to_execute(std::move(rv)) {}

ObjectHolder Assignment::Execute(Closure& closure, Context& context) {
    closure[m_var_to_assign] = m_stm_to_execute->Execute(closure, context);
    return closure[m_var_to_assign];
}

FieldAssignment::FieldAssignment(VariableValue object, std::string field_name, std::unique_ptr<runtime::Executable> rv) : m_object_to_store(std::move(object)), m_field_name(std::move(field_name)), m_stm_to_execute(std::move(rv)) {
    ASSERT_EQUAL(m_field_name.size() > 0, true);
}

ObjectHolder FieldAssignment::Execute(Closure& closure, Context& context) {
    runtime::ObjectHolder var_to_store = m_object_to_store.Execute(closure, context);
    if(runtime::ClassInstance* instance_ptr = var_to_store.TryAs<runtime::ClassInstance>()) {
        instance_ptr->Fields()[m_field_name] = m_stm_to_execute->Execute(closure, context);
        return instance_ptr->Fields()[m_field_name];
    }
    return runtime::ObjectHolder::None();
}

Print::Print(unique_ptr<runtime::Executable> argument) {
    m_args.push_back(std::move(argument));
}

Print::Print(vector<unique_ptr<runtime::Executable>> args) : m_args(std::move(args))  {}

unique_ptr<Print> Print::Variable(const std::string& name) {
    return std::make_unique<Print>(std::make_unique<VariableValue>(name));
}

ObjectHolder Print::Execute(Closure& closure, Context& context) {
    ostream& out = context.GetOutputStream();
    size_t sz = m_args.size();
    for (size_t i = 0u; i < sz; ++i) {
        const std::unique_ptr<runtime::Executable>& statement_ptr = m_args[i];
        if (i) {
            out << ' ';
        }
        
        ObjectHolder exec_res = statement_ptr->Execute(closure, context);
        if (exec_res) {
            exec_res->Print(out, context);
        }
        else {
            out << "None"sv;
        }
    }
    out << '\n';
    return {};
}

MethodCall::MethodCall(std::unique_ptr<runtime::Executable> object, std::string method, std::vector<std::unique_ptr<runtime::Executable>> args) : m_object(std::move(object)), m_method(std::move(method)), m_args(std::move(args)) {}

ObjectHolder MethodCall::Execute(Closure& closure, Context& context) {
    runtime::ObjectHolder class_instance_holder = m_object->Execute(closure, context);
    if(runtime::ClassInstance* class_instance_ptr = class_instance_holder.TryAs<runtime::ClassInstance>()) {
        std::vector<ObjectHolder> args_values;
        args_values.reserve(m_args.size());
        for (const std::unique_ptr<runtime::Executable>& arg_smt : m_args) {
            args_values.push_back(arg_smt->Execute(closure, context));
        }
        class_instance_ptr->Call(m_method, args_values, context);
    }
    return {};
}

NewInstance::NewInstance(const runtime::Class& class_) : m_instance(class_) {}

NewInstance::NewInstance(const runtime::Class& class_, std::vector<std::unique_ptr<runtime::Executable>> args) : m_instance(class_), m_ctx_args(std::move(args)) {}

ObjectHolder NewInstance::Execute(Closure& closure, Context& context) {
    if(m_instance.HasMethod(parse::token_const::INIT_METHOD, m_ctx_args.size())) {
        std::vector<ObjectHolder> args_values;
        args_values.reserve(m_ctx_args.size());
        for (const std::unique_ptr<runtime::Executable>& arg_smt : m_ctx_args) {
            args_values.push_back(arg_smt->Execute(closure, context));
        }
        m_instance.Call(parse::token_const::INIT_METHOD, args_values, context);
    }
    return runtime::ObjectHolder::Share(m_instance);
}


ObjectHolder Stringify::Execute(Closure& closure, Context& context) {
    ObjectHolder value_holder = m_arg->Execute(closure, context);
    if (auto* instance_ptr = value_holder.TryAs<runtime::ClassInstance>()) {
        if (instance_ptr->HasMethod(parse::token_const::STR_METHOD, 0)) {
            value_holder = instance_ptr->Call(parse::token_const::STR_METHOD, {}, context);
        }
    }
    std::string value("None"s);
    if (value_holder) {
        static runtime::DummyContext empty;
        std::stringstream ss;
        value_holder->Print(ss, empty);
        value = ss.str();
    }
    return ObjectHolder::Own(runtime::String(value));
}

ObjectHolder Add::Execute(Closure& closure, Context& context) {
    ObjectHolder lhs_value_holder = m_lhs_stm->Execute(closure, context);
    ObjectHolder rhs_value_holder = m_rhs_stm->Execute(closure, context);

    if (runtime::Number* lhs_num_ptr = lhs_value_holder.TryAs<runtime::Number>()) {
        if(runtime::Number* rhs_num_ptr = rhs_value_holder.TryAs<runtime::Number>()) {
            return ObjectHolder::Own(runtime::Number(lhs_num_ptr->GetValue() + rhs_num_ptr->GetValue()));
        }
    }

    if (runtime::String* lhs_str_ptr = lhs_value_holder.TryAs<runtime::String>()) {
        if(runtime::String* rhs_str_ptr = rhs_value_holder.TryAs<runtime::String>()) {
            return ObjectHolder::Own(runtime::String(lhs_str_ptr->GetValue() + rhs_str_ptr->GetValue()));
        }
    }
    if (runtime::ClassInstance* lhs_instance = lhs_value_holder.TryAs<runtime::ClassInstance>()) {
        return lhs_instance->Call(parse::token_const::ADD_METHOD, { rhs_value_holder }, context);
    }
    throw std::runtime_error("Couldn't add this objects."s);
}

ObjectHolder Sub::Execute(Closure& closure, Context& context) {
    ObjectHolder lhs_value_holder = m_lhs_stm->Execute(closure, context);
    ObjectHolder rhs_value_holder = m_rhs_stm->Execute(closure, context);

    if (runtime::Number* lhs_num_ptr = lhs_value_holder.TryAs<runtime::Number>()) {
        if(runtime::Number* rhs_num_ptr = rhs_value_holder.TryAs<runtime::Number>()) {
            return ObjectHolder::Own(runtime::Number(lhs_num_ptr->GetValue() - rhs_num_ptr->GetValue()));
        }
    }

    if (runtime::ClassInstance* lhs_instance = lhs_value_holder.TryAs<runtime::ClassInstance>()) {
        return lhs_instance->Call(parse::token_const::SUB_METHOD, { rhs_value_holder }, context);
    }
    throw std::runtime_error("Couldn't subtract this objects."s);
}

ObjectHolder Mult::Execute(Closure& closure, Context& context) {
    ObjectHolder lhs_value_holder = m_lhs_stm->Execute(closure, context);
    ObjectHolder rhs_value_holder = m_rhs_stm->Execute(closure, context);

    if (runtime::Number* lhs_num_ptr = lhs_value_holder.TryAs<runtime::Number>()) {
        if(runtime::Number* rhs_num_ptr = rhs_value_holder.TryAs<runtime::Number>()) {
            return ObjectHolder::Own(runtime::Number(lhs_num_ptr->GetValue() * rhs_num_ptr->GetValue()));
        }
    }

    if (runtime::ClassInstance* lhs_instance = lhs_value_holder.TryAs<runtime::ClassInstance>()) {
        return lhs_instance->Call(parse::token_const::MUL_METHOD, { rhs_value_holder }, context);
    }
    throw std::runtime_error("Couldn't multiply this objects."s);
}

ObjectHolder Div::Execute(Closure& closure, Context& context) {
    ObjectHolder lhs_value_holder = m_lhs_stm->Execute(closure, context);
    ObjectHolder rhs_value_holder = m_rhs_stm->Execute(closure, context);

    if (runtime::Number* lhs_num_ptr = lhs_value_holder.TryAs<runtime::Number>()) {
        if(runtime::Number* rhs_num_ptr = rhs_value_holder.TryAs<runtime::Number>()) {
            return ObjectHolder::Own(runtime::Number(lhs_num_ptr->GetValue() / rhs_num_ptr->GetValue()));
        }
    }

    if (runtime::ClassInstance* lhs_instance = lhs_value_holder.TryAs<runtime::ClassInstance>()) {
        return lhs_instance->Call(parse::token_const::DIV_METHOD, { rhs_value_holder }, context);
    }
    throw std::runtime_error("Couldn't divide this objects."s);
}

ObjectHolder Or::Execute(Closure& closure, Context& context) {
    ObjectHolder lhs_value_holder = m_lhs_stm->Execute(closure, context);
    if (runtime::ClassInstance* lhs_instance = lhs_value_holder.TryAs<runtime::ClassInstance>()) {
        lhs_value_holder = lhs_instance->Call(parse::token_const::BOOL_METHOD, {}, context);
    }
    if(runtime::IsTrue(lhs_value_holder)) {
        return runtime::obj_const::OBJECT_HOLDER_TRUE;
    }

    ObjectHolder rhs_value_holder = m_rhs_stm->Execute(closure, context);
    if (runtime::ClassInstance* rhs_instance = rhs_value_holder.TryAs<runtime::ClassInstance>()) {
        rhs_value_holder = rhs_instance->Call(parse::token_const::BOOL_METHOD, {}, context);
    }
    if(runtime::IsTrue(rhs_value_holder)) {
        return runtime::obj_const::OBJECT_HOLDER_TRUE;
    }

    return runtime::obj_const::OBJECT_HOLDER_FALSE;
}

ObjectHolder And::Execute(Closure& closure, Context& context) {
    ObjectHolder lhs_value_holder = m_lhs_stm->Execute(closure, context);
    if (runtime::ClassInstance* lhs_instance = lhs_value_holder.TryAs<runtime::ClassInstance>()) {
        lhs_value_holder = lhs_instance->Call(parse::token_const::BOOL_METHOD, {}, context);
    }
    if(!runtime::IsTrue(lhs_value_holder)) {
        return runtime::obj_const::OBJECT_HOLDER_FALSE;
    }

    ObjectHolder rhs_value_holder = m_rhs_stm->Execute(closure, context);
    if (runtime::ClassInstance* rhs_instance = rhs_value_holder.TryAs<runtime::ClassInstance>()) {
        rhs_value_holder = rhs_instance->Call(parse::token_const::BOOL_METHOD, {}, context);
    }
    if(!runtime::IsTrue(rhs_value_holder)) {
        return runtime::obj_const::OBJECT_HOLDER_FALSE;
    }

    return runtime::obj_const::OBJECT_HOLDER_TRUE;
}

ObjectHolder Not::Execute(Closure& closure, Context& context) {
    ObjectHolder value_holder = m_arg->Execute(closure, context);
    if (auto* instance_ptr = value_holder.TryAs<runtime::ClassInstance>()) {
        if (instance_ptr->HasMethod(parse::token_const::BOOL_METHOD, 0)) {
            value_holder = instance_ptr->Call(parse::token_const::BOOL_METHOD, {}, context);
        }
    }
    if(runtime::IsTrue(value_holder)) {
        return runtime::obj_const::OBJECT_HOLDER_FALSE;
    }
    return runtime::obj_const::OBJECT_HOLDER_TRUE;
}

ObjectHolder Compound::Execute(Closure& closure, Context& context) {
    for (auto& op : m_operations) {
        op->Execute(closure, context);
    }
    return {};
}

MethodBody::MethodBody(std::unique_ptr<runtime::Executable> body) : m_body(std::move(body)) {}

ObjectHolder MethodBody::Execute(Closure& closure, Context& context) {
    try {
        m_body->Execute(closure, context);
    }
    catch (ReturnException& ret) {
        return ret.GetResult();
    }
    return {};
}

ObjectHolder Return::Execute(Closure& closure, Context& context) {
    ObjectHolder res_holder = m_statement->Execute(closure, context);
    throw ReturnException(std::move(res_holder));
    return {};
}

ClassDefinition::ClassDefinition(ObjectHolder cls) : m_class(cls) {}

ObjectHolder ClassDefinition::Execute(Closure& closure, Context& context) {
    runtime::Class* ptr_cls = m_class.TryAs<runtime::Class>();
    closure.emplace(ptr_cls->GetName(), m_class);
    return {};
}

IfElse::IfElse(std::unique_ptr<runtime::Executable> condition,
               std::unique_ptr<runtime::Executable> if_body,
               std::unique_ptr<runtime::Executable> else_body) : m_condition(std::move(condition))
                                                               , m_if_body(std::move(if_body))
                                                               , m_else_body(std::move(else_body)) {}

ObjectHolder IfElse::Execute(Closure& closure, Context& context) {
    ObjectHolder result_holder = m_condition->Execute(closure, context);
    if (runtime::IsTrue(result_holder)) {
        return m_if_body->Execute(closure, context);
    }
    
    if (m_else_body) {
        return m_else_body->Execute(closure, context);
    }

    return {};
}

Comparison::Comparison(Comparator cmp,
                       unique_ptr<runtime::Executable> lhs,
                       unique_ptr<runtime::Executable> rhs) : BinaryOperation(std::move(lhs), std::move(rhs))
                                                            , m_comparator(std::move(cmp)) {}

ObjectHolder Comparison::Execute(Closure& closure, Context& context) {
    ObjectHolder lhs_value_holder = m_lhs_stm->Execute(closure, context);
    ObjectHolder rhs_value_holder = m_rhs_stm->Execute(closure, context);
    if(m_comparator(lhs_value_holder, rhs_value_holder, context)) {
        return runtime::obj_const::OBJECT_HOLDER_TRUE;
    }
    return runtime::obj_const::OBJECT_HOLDER_FALSE;
}

}  // namespace ast