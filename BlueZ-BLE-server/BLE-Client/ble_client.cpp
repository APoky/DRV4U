#include "ble-client.hpp"

#include <iostream>
#include <cstring>

BleClient::BleClient()
    : m_conn(nullptr)
{
}

BleClient::~BleClient()
{
    if (m_conn)
    {
        dbus_connection_unref(m_conn);
    }
}

bool BleClient::initialize()
{
    DBusError err;

    dbus_error_init(&err);

    m_conn =
        dbus_bus_get(
            DBUS_BUS_SYSTEM,
            &err);

    if (!m_conn)
    {
        std::cerr
            << "Failed to connect D-Bus\n";

        return false;
    }

    return true;
}

bool BleClient::callMethod(
    const std::string& service,
    const std::string& path,
    const std::string& interface,
    const std::string& method)
{
    DBusMessage* msg =
        dbus_message_new_method_call(
            service.c_str(),
            path.c_str(),
            interface.c_str(),
            method.c_str());

    if (!msg)
        return false;

    DBusError err;
    dbus_error_init(&err);

    DBusMessage* reply =
        dbus_connection_send_with_reply_and_block(
            m_conn,
            msg,
            -1,
            &err);

    dbus_message_unref(msg);

    if (!reply)
    {
        std::cerr
            << "Method call failed: "
            << method
            << "\n";

        return false;
    }

    dbus_message_unref(reply);

    return true;
}

bool BleClient::connectDevice(
    const std::string& devicePath)
{
    return callMethod(
        "org.bluez",
        devicePath,
        "org.bluez.Device1",
        "Connect");
}

bool BleClient::startNotify(
    const std::string& characteristicPath)
{
    return callMethod(
        "org.bluez",
        characteristicPath,
        "org.bluez.GattCharacteristic1",
        "StartNotify");
}

void BleClient::processEvents()
{
    while (true)
    {
        dbus_connection_read_write(
            m_conn,
            100);

        DBusMessage* msg =
            dbus_connection_pop_message(
                m_conn);

        if (!msg)
            continue;

        if (dbus_message_is_signal(
                msg,
                "org.freedesktop.DBus.Properties",
                "PropertiesChanged"))
        {
            std::cout
                << "Notification received\n";

            /*
             * Parse "Value" property here.
             *
             * Temperature format:
             * uint32_t milli-Celsius
             *
             * Example:
             * 25000 = 25.000 C
             */
            parseTemperatureNotification(msg);
        }

        dbus_message_unref(msg);
    }
}

void BleClient::parseTemperatureNotification(DBusMessage *msg)
{
    DBusMessageIter iter;

    if (!dbus_message_iter_init(msg, &iter))
        return;

    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING)
        return;

    const char *iface = nullptr;

    dbus_message_iter_get_basic(&iter, &iface);

    if (!iface)
        return;

    if (strcmp(iface,
               "org.bluez.GattCharacteristic1") != 0)
        return;

    dbus_message_iter_next(&iter);

    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY)
        return;

    DBusMessageIter dict;

    dbus_message_iter_recurse(&iter, &dict);

    while (dbus_message_iter_get_arg_type(&dict)
           != DBUS_TYPE_INVALID)
    {
        if (dbus_message_iter_get_arg_type(&dict)
            != DBUS_TYPE_DICT_ENTRY)
        {
            dbus_message_iter_next(&dict);
            continue;
        }

        DBusMessageIter entry;

        dbus_message_iter_recurse(&dict, &entry);

        const char *property = nullptr;

        dbus_message_iter_get_basic(&entry,
                                    &property);

        if (!property)
        {
            dbus_message_iter_next(&dict);
            continue;
        }

        dbus_message_iter_next(&entry);

        if (strcmp(property, "Value") == 0)
        {
            DBusMessageIter variant;

            dbus_message_iter_recurse(&entry,
                                      &variant);

            DBusMessageIter array;

            dbus_message_iter_recurse(&variant,
                                      &array);

            uint8_t value[4];
            int index = 0;

            while (dbus_message_iter_get_arg_type(&array)
                   == DBUS_TYPE_BYTE)
            {
                unsigned char byte;

                dbus_message_iter_get_basic(
                    &array,
                    &byte);

                if (index < 4)
                    value[index++] = byte;

                dbus_message_iter_next(&array);
            }

            if (index == 4)
            {
                uint32_t temp_mC =
                    value[0]
                    | (value[1] << 8)
                    | (value[2] << 16)
                    | (value[3] << 24);

                double temp_C =
                    static_cast<double>(temp_mC)
                    / 1000.0;

                std::cout
                    << "Temperature = "
                    << temp_C
                    << " C"
                    << std::endl;
            }
        }

        dbus_message_iter_next(&dict);
    }
}