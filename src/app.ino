#include <Arduino.h>
#include <BLEDevice.h>
#include "remote_scales.h"
#include "scales/acaia.h"

struct Scales
{
    std::string name;
    std::string address;
};

void onWeightReceived(float weight)
{
    Serial.println("Weight received: " + String(weight));
}

namespace blescales
{
    RemoteScalesScanner remoteScalesScanner;
    std::unique_ptr<RemoteScales> bleScales;

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
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }

    void handleBleDevice()
    {
        if (!BLEDevice::getInitialized())
        {
            Serial.println("Initializing BLE device...");
            BLEDevice::init("Gaggiuino");
        }

        if (BLEDevice::getInitialized())
        {
            // Serial.println("BLE device initialized.");
        }
    }

    void maintainConnection()
    {
        if (bleScales.get() == nullptr)
        { // No scale discovered yet. Keep checking scan results to find scales.
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
        { // Scale discovered but not connected. Make sure it's still reachable.
            Serial.println("Connection failed. Will retry.");
            remoteScalesScanner.stopAsyncScan();
            bleScales.release();
        }
        else if (bleScales->isConnected())
        { // Scale stil connected. Invoke update to keep alive.
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
    // Nothing to do here. All the work is done in the task.

    //Tare if "ta" is received from serial
    if (Serial.available() > 0)
    {
        String input = Serial.readStringUntil('\n');
        if (input == "tare")
        {
            blescales::tare();
        }
    }
}