
/*
 * This file was automatically generated by sdbuscpp-xml2cpp; DO NOT EDIT!
 */

#ifndef __sdbuscpp__concatenator_adaptor_h__adaptor__H__
#define __sdbuscpp__concatenator_adaptor_h__adaptor__H__

#include <sdbus-c++/sdbus-c++.h>
#include <string>
#include <tuple>

namespace org {
namespace sdbuscpp {
namespace stresstest {

class concatenator_adaptor
{
public:
    static constexpr const char* interfaceName = "org.sdbuscpp.stresstest.concatenator";

protected:
    concatenator_adaptor(sdbus::IObject& object)
        : object_(object)
    {
        object_.registerMethod("concatenate").onInterface(interfaceName).implementedAs([this](sdbus::Result<std::string>&& result, std::map<std::string, sdbus::Variant> params){ this->concatenate(std::move(result), std::move(params)); });
        object_.registerSignal("concatenatedSignal").onInterface(interfaceName).withParameters<std::string>();
    }

public:
    void concatenatedSignal(const std::string& concatenatedString)
    {
        object_.emitSignal("concatenatedSignal").onInterface(interfaceName).withArguments(concatenatedString);
    }

private:
    virtual void concatenate(sdbus::Result<std::string>&& result, std::map<std::string, sdbus::Variant> params) = 0;

private:
    sdbus::IObject& object_;
};

}}} // namespaces

#endif
