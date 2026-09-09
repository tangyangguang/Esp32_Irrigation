#!/usr/bin/env python3
"""Exercise actual Base MQTT at this application's configured transport capacities."""
from pathlib import Path
import configparser
import re
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
base = root.parents[2] / "foundation/Esp32Base"
config = configparser.ConfigParser(interpolation=None)
config.read(root / "platformio.ini")
environment = config['platformio']['default_envs'].strip()
flags = config['env:' + environment]['build_flags']
macros = ['ESP32BASE_MQTT_MAX_PAYLOAD_BYTES', 'ESP32BASE_MQTT_MAX_INCOMING_PAYLOAD_BYTES', 'ESP32BASE_MQTT_MAX_OUTBOX_BYTES']
capacity_flags = ['-D' + name + '=' + re.search(r'\b' + name + r'=(\d+)', flags).group(1) for name in macros]
unity = base / '.pio/libdeps/arduino2/native_mqtt_harness/Unity/src'
source = r'''
#define main base_harness_main
#include "test/test_native_mqtt_harness/test_main.cpp"
#undef main
void full_command() {
    Esp32BaseMqtt::setMessageCallback(onMessage);
    startAndConnect();
    char topic[] = "device/command";
    static char payload[4096];
    memset(payload, 'x', sizeof(payload));
    esp_mqtt_event_t event={};
    event.client=&g_fakeClient; event.event_id=MQTT_EVENT_DATA; event.msg_id=55;
    event.total_data_len=sizeof(payload); event.qos=1;
    for (size_t offset=0;offset<sizeof(payload);offset+=1024) {
        event.topic=offset ? nullptr : topic; event.topic_len=offset ? 0 : strlen(topic);
        event.data=payload+offset; event.data_len=1024; event.current_data_offset=offset;
        g_fakeEventHandler(nullptr,nullptr,MQTT_EVENT_DATA,&event);
    }
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(1,g_messageCount);
    TEST_ASSERT_EQUAL(sizeof(payload),g_lastPayloadLength);
}
void full_publish() {
    startAndConnect();
    static uint8_t payload[4096];
    char topic[ESP32BASE_MQTT_MAX_TOPIC_BYTES+1];
    memset(topic,'t',sizeof(topic)-1);topic[sizeof(topic)-1]=0;
    Esp32BaseMqtt::PublishRequest request;
    request.topic=topic;request.payload=payload;request.payloadLength=sizeof(payload);
    request.qos=Esp32BaseMqtt::QOS_1;
    TEST_ASSERT_TRUE(Esp32BaseMqtt::publish(request).accepted());
}
int main() {
    UNITY_BEGIN();
    RUN_TEST(full_command);
    RUN_TEST(test_fragment_assembly_and_oversize_drop);
    RUN_TEST(full_publish);
    return UNITY_END();
}
'''
with tempfile.TemporaryDirectory(prefix='irrigation-mqtt-') as directory:
    work=Path(directory); (work/'test.cpp').write_text(source)
    subprocess.run(['cc','-c',str(unity/'unity.c'),'-I',str(unity),'-o',str(work/'unity.o')],check=True)
    subprocess.run(['c++','-std=c++11','-fsanitize=address,undefined','-fno-omit-frame-pointer',
                    '-I',str(base),'-I',str(base/'src'),'-I',str(base/'test/test_native_mqtt_harness/stubs'),
                    '-I',str(unity),'-DESP32BASE_PROFILE=ESP32BASE_PROFILE_IOT','-DCONFIG_MBEDTLS_HAVE_TIME_DATE=1',
                    *capacity_flags,str(work/'test.cpp'),str(work/'unity.o'),'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')],check=True)
