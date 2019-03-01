/**
 * (C) 2019 KISTLER INSTRUMENTE AG, Winterthur, Switzerland
 *
 * @file client.cpp
 *
 * Created on: Jan 25, 2019
 * Project: sdbus-c++
 * Description: High-level D-Bus IPC C++ library based on sd-bus
 *
 * This file is part of sdbus-c++.
 *
 * sdbus-c++ is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * sdbus-c++ is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with sdbus-c++. If not, see <http://www.gnu.org/licenses/>.
 */

#include "celsius-thermometer-adaptor.h"
#include "celsius-thermometer-proxy.h"
#include "fahrenheit-thermometer-adaptor.h"
#include "fahrenheit-thermometer-proxy.h"
#include "concatenator-adaptor.h"
#include "concatenator-proxy.h"
#include <sdbus-c++/sdbus-c++.h>
#include <vector>
#include <string>
#include <iostream>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <cassert>

using namespace std::chrono_literals;

class CelsiusThermometerAdaptor : public sdbus::Interfaces<org::sdbuscpp::stresstest::celsius::thermometer_adaptor>
{
public:
    using sdbus::Interfaces<org::sdbuscpp::stresstest::celsius::thermometer_adaptor>::Interfaces;

protected:
    virtual uint32_t getCurrentTemperature() override
    {
        return m_currentTemperature++;
    }

private:
    uint32_t m_currentTemperature{};
};

class CelsiusThermometerProxy : public sdbus::ProxyInterfaces<org::sdbuscpp::stresstest::celsius::thermometer_proxy>
{
public:
    using sdbus::ProxyInterfaces<org::sdbuscpp::stresstest::celsius::thermometer_proxy>::ProxyInterfaces;
};

class FahrenheitThermometerAdaptor : public sdbus::Interfaces<org::sdbuscpp::stresstest::fahrenheit::thermometer_adaptor>
{
public:
    FahrenheitThermometerAdaptor(sdbus::IConnection& connection, std::string objectPath)
        : sdbus::Interfaces<org::sdbuscpp::stresstest::fahrenheit::thermometer_adaptor>(connection, std::move(objectPath))
        , celsiusProxy(connection, "org.sdbuscpp.stresstest.service2", "/org/sdbuscpp/stresstest/celsius/thermometer")
    {
    }

protected:
    virtual uint32_t getCurrentTemperature() override
    {
        return static_cast<uint32_t>(celsiusProxy.getCurrentTemperature() * 1.8 + 32.);
    }

private:
    CelsiusThermometerProxy celsiusProxy;
};

class FahrenheitThermometerProxy : public sdbus::ProxyInterfaces<org::sdbuscpp::stresstest::fahrenheit::thermometer_proxy>
{
public:
    using sdbus::ProxyInterfaces<org::sdbuscpp::stresstest::fahrenheit::thermometer_proxy>::ProxyInterfaces;
};

class ConcatenatorAdaptor : public sdbus::Interfaces<org::sdbuscpp::stresstest::concatenator_adaptor>
{
public:
    using sdbus::Interfaces<org::sdbuscpp::stresstest::concatenator_adaptor>::Interfaces;

protected:
    virtual std::string concatenate(const std::map<std::string, sdbus::Variant>& params) override
    {
        auto aString = params.at("key1").get<std::string>();
        auto aNumber = params.at("key2").get<int>();
        auto result = aString + std::to_string(aNumber);
        concatenatedSignal(result);
        return result;
    }

private:
    uint32_t m_currentTemperature{};
};

class CelsiusThermometerProxy : public sdbus::ProxyInterfaces<org::sdbuscpp::stresstest::celsius::thermometer_proxy>
{
public:
    using sdbus::ProxyInterfaces<org::sdbuscpp::stresstest::celsius::thermometer_proxy>::ProxyInterfaces;
};




class PerftestClient : public sdbus::ProxyInterfaces<org::sdbuscpp::perftest_proxy>
{
public:
    PerftestClient(std::string destination, std::string objectPath)
        : sdbus::ProxyInterfaces<org::sdbuscpp::perftest_proxy>(std::move(destination), std::move(objectPath))
    {
    }

protected:
    virtual void onDataSignal(const std::string& data) override
    {
        static unsigned int counter = 0;
        static std::chrono::time_point<std::chrono::steady_clock> startTime;
        
        assert(data.size() == m_msgSize);
        
        ++counter;
        
        if (counter == 1)
            startTime = std::chrono::steady_clock::now();
        else if (counter == m_msgCount)
        {
            auto stopTime = std::chrono::steady_clock::now();
            std::cout << "Received " << m_msgCount << " signals in: " << std::chrono::duration_cast<std::chrono::milliseconds>(stopTime - startTime).count() << " ms" << std::endl;
            counter = 0;
        }
    }
    
public:
    unsigned int m_msgSize{};
    unsigned int m_msgCount{};
};

std::string createRandomString(size_t length)
{
    auto randchar = []() -> char
    {
        const char charset[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
        const size_t max_index = (sizeof(charset) - 1);
        return charset[ rand() % max_index ];
    };
    std::string str(length, 0);
    std::generate_n(str.begin(), length, randchar);
    return str;
}


//-----------------------------------------
int main(int /*argc*/, char */*argv*/[])
{
    const char* destinationName = "org.sdbuscpp.perftest";
    const char* objectPath = "/org/sdbuscpp/perftest";
    //PerftestClient client(destinationName, objectPath);

    const unsigned int repetitions{2};
    unsigned int msgCount = 1000;
    unsigned int msgSize{};
    
    {
        PerftestClient client(destinationName, objectPath);
        msgSize = 20;
        std::cout << "** Measuring signals of size " << msgSize << " bytes (" << repetitions << " repetitions)..." << std::endl << std::endl;
        client.m_msgCount = msgCount; client.m_msgSize = msgSize;
        for (unsigned int r = 0; r < repetitions; ++r)
        {
            client.sendDataSignals(msgCount, msgSize);

            std::this_thread::sleep_for(1000ms);
        }
    }
    
    {
        PerftestClient client(destinationName, objectPath);
        msgSize = 1000;
        std::cout << std::endl << "** Measuring signals of size " << msgSize << " bytes (" << repetitions << " repetitions)..." << std::endl << std::endl;
        client.m_msgCount = msgCount; client.m_msgSize = msgSize;
        for (unsigned int r = 0; r < repetitions; ++r)
        {
            client.sendDataSignals(msgCount, msgSize);

            std::this_thread::sleep_for(1000ms);
        }
    }
    
    {
        PerftestClient client(destinationName, objectPath);
        msgSize = 20;
        std::cout << std::endl << "** Measuring method calls of size " << msgSize << " bytes (" << repetitions << " repetitions)..." << std::endl << std::endl;
        for (unsigned int r = 0; r < repetitions; ++r)
        {
            auto str1 = createRandomString(msgSize/2);
            auto str2 = createRandomString(msgSize/2);

            auto startTime = std::chrono::steady_clock::now();
            for (unsigned int i = 0; i < msgCount; i++)
            {
                auto result = client.concatenateTwoStrings(str1, str2);

                assert(result.size() == str1.size() + str2.size());
                assert(result.size() == msgSize);
            }
            auto stopTime = std::chrono::steady_clock::now();
            std::cout << "Called " << msgCount << " methods in: " << std::chrono::duration_cast<std::chrono::milliseconds>(stopTime - startTime).count() << " ms" << std::endl;

            std::this_thread::sleep_for(1000ms);
        }
    }
    
    {
        PerftestClient client(destinationName, objectPath);
        msgSize = 1000;
        std::cout << std::endl << "** Measuring method calls of size " << msgSize << " bytes (" << repetitions << " repetitions)..." << std::endl << std::endl;
        for (unsigned int r = 0; r < repetitions; ++r)
        {
            auto str1 = createRandomString(msgSize/2);
            auto str2 = createRandomString(msgSize/2);

            auto startTime = std::chrono::steady_clock::now();
            for (unsigned int i = 0; i < msgCount; i++)
            {
                auto result = client.concatenateTwoStrings(str1, str2);

                assert(result.size() == str1.size() + str2.size());
                assert(result.size() == msgSize);
            }
            auto stopTime = std::chrono::steady_clock::now();
            std::cout << "Called " << msgCount << " methods in: " << std::chrono::duration_cast<std::chrono::milliseconds>(stopTime - startTime).count() << " ms" << std::endl;

            std::this_thread::sleep_for(1000ms);
        }
    }
    
    return 0;
}
