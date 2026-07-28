Import("env")

from pathlib import Path


def cpp_string(value):
    return (
        value.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\r", "\\r")
        .replace("\n", "\\n")
    )


def option(name, default=""):
    return env.GetProjectOption(name, default).strip()


project_dir = Path(env.subst("$PROJECT_DIR"))
generated_dir = Path(env.subst("$BUILD_DIR")) / "generated"
generated_path = generated_dir / "IrrigationPrivateConfig.generated.h"

host = option("custom_irrigation_mqtt_host")
port = option("custom_irrigation_mqtt_port", "8883")
username = option("custom_irrigation_mqtt_username")
password = option("custom_irrigation_mqtt_password")
topic_prefix = option("custom_irrigation_mqtt_topic_prefix", "irrigation")
ca_name = option("custom_irrigation_mqtt_ca_file", "mqtt-ca.local.crt")
ca_path = project_dir / ca_name

configured = bool(host and port and username and password and topic_prefix and ca_path.is_file())
ca_pem = ca_path.read_text(encoding="utf-8") if configured else ""
if configured and not ca_pem.endswith("\n"):
    ca_pem += "\n"

try:
    port_number = int(port)
except ValueError:
    port_number = 0
    configured = False
if port_number < 1 or port_number > 65535:
    configured = False

content = f"""#pragma once

namespace IrrigationPrivateConfig {{
constexpr bool MQTT_CONFIGURED = {"true" if configured else "false"};
constexpr char MQTT_HOST[] = "{cpp_string(host)}";
constexpr unsigned MQTT_PORT = {port_number};
constexpr char MQTT_USERNAME[] = "{cpp_string(username)}";
constexpr char MQTT_PASSWORD[] = "{cpp_string(password)}";
constexpr char MQTT_TOPIC_PREFIX[] = "{cpp_string(topic_prefix.rstrip("/"))}";
constexpr char MQTT_CA_PEM[] = "{cpp_string(ca_pem)}";
}}  // namespace IrrigationPrivateConfig
"""

generated_dir.mkdir(parents=True, exist_ok=True)
if not generated_path.exists() or generated_path.read_text(encoding="utf-8") != content:
    generated_path.write_text(content, encoding="utf-8")

env.AppendUnique(CPPPATH=[str(generated_dir)])
print(
    "Irrigation MQTT private config: "
    + ("configured" if configured else "not configured")
)
