#include <Arduino.h>
#include <BLEDevice.h>
#include "remote_scales.h"
#include "scales/acaia.h"
#include <ctime>
#include "oled/oled.h"

struct Scales
{
    std::string name;
    std::string address;
};
static uint64_t cptWeightReceived = 0;
static uint64_t oldCptWeightReceived = cptWeightReceived;
static float flowRate = 0;
void onWeightReceived(float weight)
{
    unsigned long currentTime = millis(); // Get the current time in milliseconds
    Serial.println(String(currentTime) + ";" + String(weight));

    cptWeightReceived++;
    if (cptWeightReceived > 1000000)
    {
        cptWeightReceived = 0;
    }
    // calculate flow rate every 2 seconds

    flowRate = calculateFlowRate(weight, currentTime);
    
}

float calculateFlowRate(float weight, unsigned long newTime)
{
    Serial.println(String("New time: ") + newTime + "s");
    static unsigned long oldTime = 0;
    static float oldWeight = 0;
    if (newTime == oldTime)
    {
        return 0;
    }
    // Calculate flow rate using delta weight and delta time 
    float rate = (weight - oldWeight) / (newTime - oldTime) * 1000;
    
    oldWeight = weight;
    oldTime = newTime;
    Serial.println(String("Flow rate: ") + rate + "g/s");
    return rate;
}

namespace blescales
{
    RemoteScalesScanner remoteScalesScanner;
    std::unique_ptr<RemoteScales> bleScales;
    bool bMaintainConnection = true;

    void bleScalesTask(void *params);

    // ----------------------------------------------------------------
    // ------------------------- PUBLIC METHODS -----------------------
    // ----------------------------------------------------------------

    void init()
    {
        AcaiaScalesPlugin::apply();
    }

    void tare()
    {
        if (bleScales.get() != nullptr)
        {
            bleScales->tare();
        }
    }

    void disconnect()
    {
        if (bleScales.get() != nullptr)
        {
            bleScales->disconnect();
        }
    }

    void connect()
    {
        if (bleScales.get() != nullptr)
        {
            bleScales->connect();
        }
    }

    void DisplayBattery()
    {
        if (bleScales.get() != nullptr)
        {

            uint8_t batt = bleScales->getBattery();
            Serial.println(String("Battery: ") + batt);
        }
    }

    void startTimer()
    {
        if (bleScales.get() != nullptr)
        {
            bleScales->startTimer();
        }
    }

    void stopTimer()
    {
        if (bleScales.get() != nullptr)
        {
            bleScales->stopTimer();
        }
    }

    void resetTimer()
    {
        if (bleScales.get() != nullptr)
        {
            bleScales->resetTimer();
        }
    }

    unsigned char getSeconds()
    {
        if (bleScales.get() != nullptr)
        {
            return bleScales->getSeconds();
        }
        return 0;
    }

    float getWeight()
    {
        if (bleScales.get() != nullptr)
        {
            return bleScales->getWeight();
        }
        return 0;
    }


    float getFlowRate()
    {
        return flowRate;
    }
    

    float getBattery()
    {
        if (bleScales.get() != nullptr)
        {
            return bleScales->getBattery();
        }
        return 0;
    }

    std::pair<std::string, std::string> getDeviceCharacteristic()
    {
        std::string _deviceName;
        std::string _deviceAddress;
        if (bleScales.get() != nullptr)
        {
            _deviceName = bleScales->getDeviceName();
            _deviceAddress = bleScales->getDeviceAddress();
        }
        return {_deviceName, _deviceAddress};
    }

    std::vector<Scales> getAvailableScales()
    {
        auto discoveredScales = remoteScalesScanner.getDiscoveredScales();
        std::vector<Scales> result(discoveredScales.size());

        std::transform(discoveredScales.begin(), discoveredScales.end(), result.begin(), [](RemoteScales *input)
                       { return Scales{.name = input->getDeviceName(), .address = input->getDeviceAddress()}; });

        return result;
    }

    Scales getConnectedScales()
    {
        if (bleScales.get() != nullptr && bleScales->isConnected())
        {
            return {.name = bleScales->getDeviceName(), .address = bleScales->getDeviceAddress()};
        }
        return {.name = "", .address = ""};
    }

    // ----------------------------------------------------------------
    // ---------------------- PRIVATE HELPER METHODS ------------------
    // ----------------------------------------------------------------

    void handleBleDevice();
    void maintainConnection();

    void bleScalesTask(void *params)
    {
        Serial.println("Remote scales and bluetooth initialized");

        for (;;)
        {
            handleBleDevice();
            maintainConnection();
            vTaskDelay(500 / portTICK_PERIOD_MS);

            // Check if we need to maintain a connection to a scale.
            if (bMaintainConnection)
            {
            }
        }
    }

    void handleBleDevice()
    {
        if (!BLEDevice::getInitialized())
        {
            Serial.println("Initializing BLE device...");
            BLEDevice::init("ESP32 old-ACAIA LUNAR");
        }

        if (BLEDevice::getInitialized())
        {
            Serial.println("BLE device initialized.");
        }
    }

    void maintainConnection()
    {
        if (bleScales.get() == nullptr)
        {
            displayLogo();
            remoteScalesScanner.initializeAsyncScan();

            std::vector<RemoteScales *> scales = remoteScalesScanner.getDiscoveredScales();

            if (scales.size() > 0)
            {
                Serial.println("We have discovered " + String(scales.size()) + " matching scales.");
                remoteScalesScanner.stopAsyncScan();
                bleScales.reset(scales[0]);
                bleScales->setWeightUpdatedCallback(onWeightReceived);
                bleScales->setLogCallback([](std::string message)
                                          { Serial.println(message.c_str()); });
                bleScales->connect();
                Serial.println("Connected to scale.");
                Serial.println("Device name: " + String(bleScales->getDeviceName().c_str()));
                Serial.println("Device address: " + String(bleScales->getDeviceAddress().c_str()));
            }
        }
        else if (!bleScales->isConnected())
        {
            Serial.println("Connection failed. Will retry.");
            remoteScalesScanner.stopAsyncScan();
            bleScales.release();
        }
        else if (bleScales->isConnected())
        {
            remoteScalesScanner.stopAsyncScan();
            bleScales->update();
        }
    }
}

void setup()
{
    Serial.begin(250000);
    Serial.println("Starting up...");

    init_display();
    blescales::init();

    xTaskCreatePinnedToCore(
        blescales::bleScalesTask, /* Function to implement the task */
        "blescalesTask",          /* Name of the task */
        10000,                    /* Stack size in words */
        NULL,                     /* Task input parameter */
        1,                        /* Priority of the task */
        NULL,                     /* Task handle. */
        0);                       /* Core where the task should run */
}

void loop()
{

    if (Serial.available() > 0)
    {
        String input = Serial.readStringUntil('\n');
        switch (input[0])
        {
        case 't':
            blescales::tare();
            break;
        case 'c':
            blescales::connect();
            blescales::bMaintainConnection = true;
            break;
        case 'd':
            blescales::disconnect();
            blescales::bMaintainConnection = false;
            break;
        case 'r':
            break;
        case 'b':
            blescales::DisplayBattery();
            break;
        case 's':
            blescales::startTimer();
            break;
        case 'p':
            blescales::stopTimer();
            break;
        case 'e':
            blescales::resetTimer();
            break;
        case 'g':
            if (input == "getDeviceCharacteristic")
            {
                std::pair<std::string, std::string> deviceCharacteristic = blescales::getDeviceCharacteristic();
                std::string name = deviceCharacteristic.first;
                std::string address = deviceCharacteristic.second;
                Serial.println(String("Name: ") + name.c_str());
                Serial.println(String("Address: ") + address.c_str());
            }
            else if (input == "getAvailableScales")
            {
                auto scales = blescales::getAvailableScales();
                for (auto scale : scales)
                {
                    Serial.println(String("Name: ") + scale.name.c_str());
                    Serial.println(String("Address: ") + scale.address.c_str());
                }
            }
            break;
        default:
            break;
        }
    }
    
    if (oldCptWeightReceived != cptWeightReceived)
    {
        mainDisplay(blescales::getWeight(), blescales::getBattery(), blescales::getFlowRate());
    }

    oldCptWeightReceived = cptWeightReceived;
}

