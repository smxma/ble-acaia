#include <Arduino.h>
#include <BLEDevice.h>
#include "remote_scales.h"
#include "scales/acaia.h"
#include <ctime>

struct Scales
{
    std::string name;
    std::string address;
};

void onWeightReceived(float weight)
{
    std::time_t t = std::time(nullptr);
    std::tm tm = *std::localtime(&t);
    char buffer[20];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
    Serial.println(String(buffer) + ";" + String(weight));
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
            // Check if we need to maintain a connection to a scale.
            if (bMaintainConnection)
            {
                handleBleDevice();
                maintainConnection();
            }
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }

    void handleBleDevice()
    {
        if (!BLEDevice::getInitialized())
        {
            Serial.println("Initializing BLE device...");
            BLEDevice::init("ESP32 old-ACAIA LUNAR Scales Client");
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

    blescales::init();

    xTaskCreatePinnedToCore(
        blescales::bleScalesTask, /* Function to implement the task */
        "blescalesTask",          /* Name of the task */
        10000,                    /* Stack size in words */
        NULL,                     /* Task input parameter */
        0,                        /* Priority of the task */
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
}