#include "runtime.h"

#include <cassert>
#include <optional>
#include <sstream>

using namespace std;

namespace runtime {

ObjectHolder::ObjectHolder(std::shared_ptr<Object> data) : m_data(std::move(data)) {}

void ObjectHolder::AssertIsValid() const {
    assert(m_data != nullptr);
}

ObjectHolder ObjectHolder::Share(Object& object) {
    // Возвращаем невладеющий shared_ptr (его deleter ничего не делает)
    return ObjectHolder(std::shared_ptr<Object>(&object, [](auto* /*p*/) { /* do nothing */ }));
}

ObjectHolder ObjectHolder::None() {
    return ObjectHolder();
}

Object& ObjectHolder::operator*() const {
    AssertIsValid();
    return *Get();
}

Object* ObjectHolder::operator->() const {
    AssertIsValid();
    return Get();
}

Object* ObjectHolder::Get() const {
    return m_data.get();
}

ObjectHolder::operator bool() const {
    return Get() != nullptr;
}

bool IsTrue(const ObjectHolder& object) {
    if (!object) {
        return false;
    }
    if (auto p = object.TryAs<String>()) {
        return p->GetValue() != ""s;
    }
    if (auto p = object.TryAs<Number>()) {
        return p->GetValue() != 0;
    }
    if (auto p = object.TryAs<Bool>()) {
        return p->GetValue() != false;
    }
    return false;
}

void Bool::Print(std::ostream& os, [[maybe_unused]] Context& context) {
    os << (GetValue() ? consts::TRUE : consts::FALSE);
}

Class::Class(std::string name, std::vector<Method> methods, const Class* parent) : m_name(name)
                                                                                 , m_parent(parent)
{
    const size_t sz = methods.size();
    m_methods.reserve(sz);
    for(size_t i = 0u; i < sz; ++i) {
        m_methods[methods[i].name] = std::move(methods[i]);
    }
}

const Method* Class::GetMethod(const std::string& name) const {
    if(m_methods.count(name)) {
        return &m_methods.at(name);
    }
    else if(m_parent){
        return m_parent->GetMethod(name);
    }
    return nullptr;
}

void Class::Print(std::ostream& os, Context& context) {
    os << consts::CLASS << ' ' << m_name;
}

const std::string& Class::GetName() const {
    return m_name;
}

ClassInstance::ClassInstance(const Class& cls) : m_type(cls) {}

void ClassInstance::Print(std::ostream& os, Context& context) {
    if (HasMethod(consts::STR, 0)) {
        ObjectHolder result_holder = Call(consts::STR, NOPARAMS, context);
        result_holder.Get()->Print(os, context);
    }
    else {
        os << this;
    }
}

bool ClassInstance::HasMethod(const std::string& method_name, size_t argument_count) const {
    const Method* method_ptr = m_type.GetMethod(method_name);
    if(method_ptr && method_ptr->formal_params.size() == argument_count) {
        return true;
    }
    return false;
}

Closure& ClassInstance::Fields() {
    return m_closure;
}

const Closure& ClassInstance::Fields() const {
    return m_closure;
}

const std::vector<ObjectHolder> ClassInstance::NOPARAMS = {};

ObjectHolder ClassInstance::Call(const std::string& method_name, const std::vector<ObjectHolder>& actual_args, Context& context) {
    if (HasMethod(method_name, actual_args.size())) {
        const Method* method_ptr = m_type.GetMethod(method_name);
        Closure local_closure = MixinLocalClosure(method_ptr->formal_params, actual_args);
        return method_ptr->body->Execute(local_closure, context);
    }
    throw std::runtime_error("Class does not have a method named as "s + method_name);
}

Closure ClassInstance::MixinLocalClosure(const std::vector<std::string>& formal_params, const std::vector<ObjectHolder>& actual_args) {
    assert(formal_params.size() == actual_args.size());

    Closure closure;
    closure.emplace(consts::SELF, ObjectHolder::Share(*this));
    for (size_t i = 0; i < formal_params.size(); ++i) {
        closure.emplace(formal_params.at(i), actual_args.at(i));
    }
    return closure;
}

template <typename Compare>
bool MakeComparison(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context, const std::string& embedded_cmp, Compare alt_cmp) {
    if (!(lhs && rhs)) {
        throw std::runtime_error("Cannot compare objects"s);
    }
    
    if (auto* ptr_cls = lhs.TryAs<ClassInstance>()) {
        return IsTrue(ptr_cls->Call(embedded_cmp, { rhs }, context));
    }

    if (lhs.TryAs<String>() && rhs.TryAs<String>()) {
        return alt_cmp(lhs.TryAs<String>()->GetValue(), rhs.TryAs<String>()->GetValue());
    }

    if (lhs.TryAs<Number>() && rhs.TryAs<Number>()) {
        return alt_cmp(lhs.TryAs<Number>()->GetValue(), rhs.TryAs<Number>()->GetValue());
    }

    if (lhs.TryAs<Bool>() && rhs.TryAs<Bool>()) {
        return alt_cmp(lhs.TryAs<Bool>()->GetValue(), rhs.TryAs<Bool>()->GetValue());
    }
    
    throw std::runtime_error("Cannot compare objects"s);
}

bool Equal(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
    return MakeComparison(lhs, rhs, context, consts::EQ, std::equal_to{});
}

bool Less(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
    return MakeComparison(lhs, rhs, context, consts::LT, std::less{});
}

bool NotEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
    return !Equal(lhs, rhs, context);
}

bool Greater(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
    return !Less(lhs, rhs, context) && !Equal(lhs, rhs, context);
}

bool LessOrEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
    return !Greater(lhs, rhs, context);
}

bool GreaterOrEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
    return !Less(lhs, rhs, context);
}

}  // namespace runtime