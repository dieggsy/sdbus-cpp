/**
 * (C) 2019 KISTLER INSTRUMENTE AG, Winterthur, Switzerland
 *
 * @file stresstests.cpp
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
#include <atomic>
#include <sstream>
#include <mutex>
#include <condition_variable>
#include <queue>

using namespace std::chrono_literals;
using namespace std::string_literals;

#define SERVICE_1_BUS_NAME "org.sdbuscpp.stresstest.service1"
#define SERVICE_2_BUS_NAME "org.sdbuscpp.stresstest.service2"
#define CELSIUS_THERMOMETER_OBJECT_PATH "/org/sdbuscpp/stresstest/celsius/thermometer"
#define FAHRENHEIT_THERMOMETER_OBJECT_PATH "/org/sdbuscpp/stresstest/fahrenheit/thermometer"
#define CONCATENATOR_OBJECT_PATH "/org/sdbuscpp/stresstest/concatenator"

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
        , celsiusProxy_(connection, SERVICE_2_BUS_NAME, CELSIUS_THERMOMETER_OBJECT_PATH)
    {
    }

protected:
    virtual uint32_t getCurrentTemperature() override
    {
        // In this D-Bus call, make yet another D-Bus call to another service over the same connection
        return static_cast<uint32_t>(celsiusProxy_.getCurrentTemperature() * 1.8 + 32.);
    }

private:
    CelsiusThermometerProxy celsiusProxy_;
};

class FahrenheitThermometerProxy : public sdbus::ProxyInterfaces<org::sdbuscpp::stresstest::fahrenheit::thermometer_proxy>
{
public:
    using sdbus::ProxyInterfaces<org::sdbuscpp::stresstest::fahrenheit::thermometer_proxy>::ProxyInterfaces;
};

class ConcatenatorAdaptor : public sdbus::Interfaces<org::sdbuscpp::stresstest::concatenator_adaptor>
{
public:
    ConcatenatorAdaptor(sdbus::IConnection& connection, std::string objectPath)
        : sdbus::Interfaces<org::sdbuscpp::stresstest::concatenator_adaptor>(connection, std::move(objectPath))
    {
        unsigned int workers = std::thread::hardware_concurrency();
        if (workers < 4)
            workers = 4;

        for (unsigned int i = 0; i < workers; ++i)
            workers_.emplace_back([this]()
            {
                //std::cout << "Created worker thread 0x" << std::hex << std::this_thread::get_id() << std::dec << std::endl;

                while(!exit_)
                {
                    // Pop a work item from the queue
                    std::unique_lock<std::mutex> lock(mutex_);
                    cond_.wait(lock, [this]{return !requests_.empty() || exit_;});
                    if (exit_)
                        break;
                    auto request = std::move(requests_.front());
                    requests_.pop();
                    lock.unlock();

                    // Do concatenation work, return results and fire signal
                    auto aString = request.input.at("key1").get<std::string>();
                    auto aNumber = request.input.at("key2").get<uint32_t>();
                    auto resultString = aString + " " + std::to_string(aNumber);

                    request.result.returnResults(resultString);

                    concatenatedSignal(resultString);
                }
            });
    }

    ~ConcatenatorAdaptor()
    {
        exit_ = true;
        cond_.notify_all();
        for (auto& worker : workers_)
            worker.join();
    }

protected:
    virtual void concatenate(sdbus::Result<std::string>&& result, std::map<std::string, sdbus::Variant> params) override
    {
        std::unique_lock<std::mutex> lock(mutex_);
        requests_.push(WorkItem{std::move(params), std::move(result)});
        lock.unlock();
        cond_.notify_one();
    }

private:
    struct WorkItem
    {
        std::map<std::string, sdbus::Variant> input;
        sdbus::Result<std::string> result;
    };
    std::mutex mutex_;
    std::condition_variable cond_;
    std::queue<WorkItem> requests_;
    std::vector<std::thread> workers_;
    std::atomic<bool> exit_{};
};

class ConcatenatorProxy : public sdbus::ProxyInterfaces<org::sdbuscpp::stresstest::concatenator_proxy>
{
public:
    using sdbus::ProxyInterfaces<org::sdbuscpp::stresstest::concatenator_proxy>::ProxyInterfaces;

private:
    virtual void onConcatenateReply(const std::string& result, const sdbus::Error* error) override
    {
        assert(error == nullptr);

        std::stringstream str(result);
        std::string aString;
        str >> aString;
        assert(aString == "sdbus-c++-stress-tests");

        uint32_t aNumber;
        str >> aNumber;
        assert(aNumber >= 0);

        ++repliesReceived_;
    }

    virtual void onConcatenatedSignal(const std::string& concatenatedString) override
    {
        std::stringstream str(concatenatedString);
        std::string aString;
        str >> aString;
        assert(aString == "sdbus-c++-stress-tests");

        uint32_t aNumber;
        str >> aNumber;
        assert(aNumber >= 0);

        ++signalsReceived_;
    }

public:
    std::atomic<uint32_t> repliesReceived_;
    std::atomic<uint32_t> signalsReceived_;
};

//-----------------------------------------
int main(int /*argc*/, char */*argv*/[])
{
    auto service2Connection = sdbus::createSystemBusConnection(SERVICE_2_BUS_NAME);
    std::thread service2Thread([&con = *service2Connection]()
    {
        CelsiusThermometerAdaptor thermometer(con, CELSIUS_THERMOMETER_OBJECT_PATH);
        con.enterProcessingLoop();
    });

    auto service1Connection = sdbus::createSystemBusConnection(SERVICE_1_BUS_NAME);
    std::thread service1Thread([&con = *service1Connection]()
    {
        ConcatenatorAdaptor concatenator(con, CONCATENATOR_OBJECT_PATH);
        FahrenheitThermometerAdaptor thermometer(con, FAHRENHEIT_THERMOMETER_OBJECT_PATH);
        con.enterProcessingLoop();
    });

    std::this_thread::sleep_for(100ms);

    std::atomic<uint32_t> concatenationCallsMade{0};
    std::atomic<uint32_t> concatenationRepliesReceived{0};
    std::atomic<uint32_t> concatenationSignalsReceived{0};
    std::atomic<uint32_t> thermometerCallsMade{0};

    auto clientConnection = sdbus::createSystemBusConnection();
    std::thread clientThread([&, &con = *clientConnection]()
    {
        std::atomic<bool> stopClients{false};

        std::thread concatenatorThread([&]()
        {
            ConcatenatorProxy concatenator(con, SERVICE_1_BUS_NAME, CONCATENATOR_OBJECT_PATH);

            uint32_t localCounter{};

            // Issue async concatenate calls densely one after another
            while (!stopClients)
            {
                std::map<std::string, sdbus::Variant> param;
                param["key1"] = "sdbus-c++-stress-tests";
                param["key2"] = localCounter++;

                concatenator.concatenate(param);

                if ((localCounter % 10) == 0)
                {
                    // Make sure the system is catching up with our async requests,
                    // otherwise sleep a bit to slow down flooding the server.
                    assert(localCounter >= concatenator.repliesReceived_);
                    while ((localCounter - concatenator.repliesReceived_) > 20 && !stopClients)
                        std::this_thread::sleep_for(2ms);

                    // Update statistics
                    concatenationCallsMade = localCounter;
                    concatenationRepliesReceived = (uint32_t)concatenator.repliesReceived_;
                    concatenationSignalsReceived = (uint32_t)concatenator.signalsReceived_;
                }
            }
        });

        std::thread thermometerThread([&]()
        {
            FahrenheitThermometerProxy thermometer(con, SERVICE_1_BUS_NAME, FAHRENHEIT_THERMOMETER_OBJECT_PATH);
            uint32_t localCounter{};
            uint32_t previousTemperature{};

            while (!stopClients)
            {
                localCounter++;
                auto temperature = thermometer.getCurrentTemperature();
                assert(temperature >= previousTemperature); // The temperature shall rise continually
                previousTemperature = temperature;
                std::this_thread::sleep_for(5ms);

                if ((localCounter % 10) == 0)
                    thermometerCallsMade = localCounter;
            }
        });

        con.enterProcessingLoop();

        stopClients = true;
        concatenatorThread.join();
        thermometerThread.join();
    });

    std::atomic<bool> exitLogger{};
    std::thread loggerThread([&]()
    {
        while (!exitLogger)
        {
            std::this_thread::sleep_for(1s);

            std::cout << "Made " << concatenationCallsMade << " concatenation calls, received " << concatenationRepliesReceived << " replies and " << concatenationSignalsReceived << " signals so far." << std::endl;
            std::cout << "Made " << thermometerCallsMade << " thermometer calls so far." << std::endl << std::endl;
        }
    });

    getchar();

    exitLogger = true;
    loggerThread.join();
    clientConnection->leaveProcessingLoop();
    clientThread.join();
    service1Connection->leaveProcessingLoop();
    service1Thread.join();
    service2Connection->leaveProcessingLoop();
    service2Thread.join();
    
    return 0;
}
