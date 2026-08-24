#pragma once

#include <wolfmqtt/mqtt_client.h>

void mqtt_transport_init(void);
MqttNet *mqtt_transport_net(void);
int mqtt_transport_tls_callback(MqttClient *client);
void mqtt_transport_force_close(void);


