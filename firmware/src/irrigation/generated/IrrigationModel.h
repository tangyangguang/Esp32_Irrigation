#pragma once
#include <ModelContract.h>
#include <string.h>
namespace iot_device { namespace model_irrigation_controller_6_zone {
static bool validate_1(JsonVariantConst v) {
    (void)v;
    if (!((IOT_DEVICE_TEXT_EQUAL(v,"idle")) || (IOT_DEVICE_TEXT_EQUAL(v,"active")))) return false;
    return true;
}
static bool validate_2(JsonVariantConst v) {
    (void)v;
    if (!((IOT_DEVICE_TEXT_EQUAL(v,"normal")) || (IOT_DEVICE_TEXT_EQUAL(v,"warning")) || (IOT_DEVICE_TEXT_EQUAL(v,"critical")))) return false;
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_0(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("activity")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("health")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("activity")) || validate_1(v[IOT_DEVICE_JSON_KEY("activity")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("health")) || validate_2(v[IOT_DEVICE_JSON_KEY("health")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==8 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("activity"),8)) || (p.key().size()==6 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("health"),6)))) return false;
    }
    }
    return true;
}
static bool validate_4(JsonVariantConst v) {
    (void)v;
    if (!(v.is<const char*>())) return false;
    if(v.is<const char*>()) { size_t length=0; if(!schema::textLength(v,length)) return false;
    if (!(length >= 1)) return false;
    if (!(length <= 24)) return false;
    }
    return true;
}
static bool validate_5(JsonVariantConst v) {
    (void)v;
    if (!(v.is<const char*>())) return false;
    if(v.is<const char*>()) { size_t length=0; if(!schema::textLength(v,length)) return false;
    if (!(length >= 1)) return false;
    if (!(length <= 16)) return false;
    }
    return true;
}
static bool validate_6(JsonVariantConst v) {
    (void)v;
    if (!(v.is<const char*>())) return false;
    if(v.is<const char*>()) { size_t length=0; if(!schema::textLength(v,length)) return false;
    if (!(length >= 1)) return false;
    if (!(length <= 32)) return false;
    }
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_7(JsonVariantConst v) {
    (void)v;
    if (!(v.is<const char*>())) return false;
    if(v.is<const char*>()) { size_t length=0; if(!schema::textLength(v,length)) return false;
    if (!(length >= 1)) return false;
    if (!(length <= 64)) return false;
    }
    if (!(schema::pattern(v,3))) return false;
    return true;
}
static bool validate_8(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 0)) return false;
    return true;
}
static bool validate_9(JsonVariantConst v) {
    (void)v;
    if (!(v.is<const char*>() || v.isNull())) return false;
    if(v.is<const char*>()) { size_t length=0; if(!schema::textLength(v,length)) return false;
    if (!(length <= 32)) return false;
    }
    return true;
}
static bool validate_10(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true) || v.isNull())) return false;
    if (!(!v.is<double>() || v.as<double>() >= -128)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 0)) return false;
    return true;
}
static bool validate_11(JsonVariantConst v) {
    (void)v;
    if (!(v.is<const char*>() || v.isNull())) return false;
    if(v.is<const char*>()) { size_t length=0; if(!schema::textLength(v,length)) return false;
    if (!(length <= 45)) return false;
    }
    return true;
}
static bool validate_12(JsonVariantConst v) {
    (void)v;
    if (!(v.is<bool>())) return false;
    return true;
}
static bool validate_13(JsonVariantConst v) {
    (void)v;
    if (!(v.is<const char*>() || v.isNull())) return false;
    if(v.is<const char*>()) { size_t length=0; if(!schema::textLength(v,length)) return false;
    if (!(length <= 64)) return false;
    }
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_3(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("fwName")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("fwVer")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("host")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("bootId")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("bootNo")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("reset")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("ssid")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("rssi")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("ip")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("uptime")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("heap")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("heapLow")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("ntp")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("wifiAtt")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("mqttConn")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("mqttAtt")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("mqttErr")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("wdt")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("fwName")) || validate_4(v[IOT_DEVICE_JSON_KEY("fwName")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("fwVer")) || validate_5(v[IOT_DEVICE_JSON_KEY("fwVer")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("host")) || validate_6(v[IOT_DEVICE_JSON_KEY("host")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("bootId")) || validate_7(v[IOT_DEVICE_JSON_KEY("bootId")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("bootNo")) || validate_8(v[IOT_DEVICE_JSON_KEY("bootNo")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("reset")) || validate_7(v[IOT_DEVICE_JSON_KEY("reset")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("ssid")) || validate_9(v[IOT_DEVICE_JSON_KEY("ssid")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("rssi")) || validate_10(v[IOT_DEVICE_JSON_KEY("rssi")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("ip")) || validate_11(v[IOT_DEVICE_JSON_KEY("ip")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("uptime")) || validate_8(v[IOT_DEVICE_JSON_KEY("uptime")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("heap")) || validate_8(v[IOT_DEVICE_JSON_KEY("heap")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("heapLow")) || validate_8(v[IOT_DEVICE_JSON_KEY("heapLow")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("ntp")) || validate_12(v[IOT_DEVICE_JSON_KEY("ntp")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("wifiAtt")) || validate_8(v[IOT_DEVICE_JSON_KEY("wifiAtt")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("mqttConn")) || validate_12(v[IOT_DEVICE_JSON_KEY("mqttConn")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("mqttAtt")) || validate_8(v[IOT_DEVICE_JSON_KEY("mqttAtt")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("mqttErr")) || validate_13(v[IOT_DEVICE_JSON_KEY("mqttErr")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("wdt")) || validate_8(v[IOT_DEVICE_JSON_KEY("wdt")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==6 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("fwName"),6)) || (p.key().size()==5 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("fwVer"),5)) || (p.key().size()==4 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("host"),4)) || (p.key().size()==6 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("bootId"),6)) || (p.key().size()==6 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("bootNo"),6)) || (p.key().size()==5 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("reset"),5)) || (p.key().size()==4 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("ssid"),4)) || (p.key().size()==4 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("rssi"),4)) || (p.key().size()==2 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("ip"),2)) || (p.key().size()==6 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("uptime"),6)) || (p.key().size()==4 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("heap"),4)) || (p.key().size()==7 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("heapLow"),7)) || (p.key().size()==3 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("ntp"),3)) || (p.key().size()==7 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("wifiAtt"),7)) || (p.key().size()==8 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("mqttConn"),8)) || (p.key().size()==7 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("mqttAtt"),7)) || (p.key().size()==7 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("mqttErr"),7)) || (p.key().size()==3 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("wdt"),3)))) return false;
    }
    }
    return true;
}
static bool validate_15(JsonVariantConst v) {
    (void)v;
    if (!((IOT_DEVICE_TEXT_EQUAL(v,"none")) || (IOT_DEVICE_TEXT_EQUAL(v,"configuration_unavailable")) || (IOT_DEVICE_TEXT_EQUAL(v,"scheduler_storage_unavailable")) || (IOT_DEVICE_TEXT_EQUAL(v,"startup_check_failed")))) return false;
    return true;
}
static bool validate_17(JsonVariantConst v) {
    (void)v;
    if (!(v.is<bool>())) return false;
    return true;
}
static bool validate_18(JsonVariantConst v) {
    (void)v;
    if (!((IOT_DEVICE_TEXT_EQUAL(v,"rtc")) || (IOT_DEVICE_TEXT_EQUAL(v,"ntp")) || (IOT_DEVICE_TEXT_EQUAL(v,"none")))) return false;
    return true;
}
static bool validate_19(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true) || v.isNull())) return false;
    if (!(!v.is<double>() || v.as<double>() >= 0)) return false;
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_16(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("trusted")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("source")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("epoch")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("rtcAvailable")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("rtcRollback")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("trusted")) || validate_17(v[IOT_DEVICE_JSON_KEY("trusted")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("source")) || validate_18(v[IOT_DEVICE_JSON_KEY("source")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("epoch")) || validate_19(v[IOT_DEVICE_JSON_KEY("epoch")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("rtcAvailable")) || validate_17(v[IOT_DEVICE_JSON_KEY("rtcAvailable")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("rtcRollback")) || validate_17(v[IOT_DEVICE_JSON_KEY("rtcRollback")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==7 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("trusted"),7)) || (p.key().size()==6 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("source"),6)) || (p.key().size()==5 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("epoch"),5)) || (p.key().size()==12 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("rtcAvailable"),12)) || (p.key().size()==11 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("rtcRollback"),11)))) return false;
    }
    }
    return true;
}
static bool validate_21(JsonVariantConst v) {
    (void)v;
    if (!((IOT_DEVICE_TEXT_EQUAL(v,"available")) || (IOT_DEVICE_TEXT_EQUAL(v,"no-enabled-plans")) || (IOT_DEVICE_TEXT_EQUAL(v,"time-unavailable")) || (IOT_DEVICE_TEXT_EQUAL(v,"rtc-rollback")) || (IOT_DEVICE_TEXT_EQUAL(v,"paused-indefinitely")) || (IOT_DEVICE_TEXT_EQUAL(v,"paused-until")))) return false;
    return true;
}
static bool validate_22(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true) || v.isNull())) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 8)) return false;
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_20(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("status")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("planId")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("scheduledAtEpoch")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("status")) || validate_21(v[IOT_DEVICE_JSON_KEY("status")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("planId")) || validate_22(v[IOT_DEVICE_JSON_KEY("planId")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("scheduledAtEpoch")) || validate_19(v[IOT_DEVICE_JSON_KEY("scheduledAtEpoch")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==6 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("status"),6)) || (p.key().size()==6 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("planId"),6)) || (p.key().size()==16 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("scheduledAtEpoch"),16)))) return false;
    }
    }
    return true;
}
static bool validate_24(JsonVariantConst v) {
    (void)v;
    if (!((IOT_DEVICE_TEXT_EQUAL(v,"idle")) || (IOT_DEVICE_TEXT_EQUAL(v,"manual")) || (IOT_DEVICE_TEXT_EQUAL(v,"automatic")) || (IOT_DEVICE_TEXT_EQUAL(v,"single-output")) || (IOT_DEVICE_TEXT_EQUAL(v,"calibration")) || (IOT_DEVICE_TEXT_EQUAL(v,"learning")))) return false;
    return true;
}
static bool validate_25(JsonVariantConst v) {
    (void)v;
    if (!(v.is<const char*>() || v.isNull())) return false;
    if (!(schema::pattern(v,1))) return false;
    return true;
}
static bool validate_26(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true) || v.isNull())) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 6)) return false;
    return true;
}
static bool validate_27(JsonVariantConst v) {
    (void)v;
    if (!((IOT_DEVICE_TEXT_EQUAL(v,"idle")) || (IOT_DEVICE_TEXT_EQUAL(v,"starting")) || (IOT_DEVICE_TEXT_EQUAL(v,"waiting-flow")) || (IOT_DEVICE_TEXT_EQUAL(v,"watering")) || (IOT_DEVICE_TEXT_EQUAL(v,"switching")) || (IOT_DEVICE_TEXT_EQUAL(v,"stopping")))) return false;
    return true;
}
static bool validate_28(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true) || v.isNull())) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    return true;
}
static bool validate_29(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 0)) return false;
    return true;
}
static bool validate_32(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 6)) return false;
    return true;
}
static bool validate_33(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true) || v.isNull())) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    return true;
}
static bool validate_34(JsonVariantConst v) {
    (void)v;
    if (!((IOT_DEVICE_TEXT_EQUAL(v,"current")) || (IOT_DEVICE_TEXT_EQUAL(v,"pending")) || (IOT_DEVICE_TEXT_EQUAL(v,"completed")))) return false;
    return true;
}
static bool validate_35(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1000)) return false;
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_31(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("zoneId")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("targetDurationMs")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("targetWaterMl")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("status")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("zoneId")) || validate_32(v[IOT_DEVICE_JSON_KEY("zoneId")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("targetWaterMl")) || validate_33(v[IOT_DEVICE_JSON_KEY("targetWaterMl")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("status")) || validate_34(v[IOT_DEVICE_JSON_KEY("status")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("targetDurationMs")) || validate_35(v[IOT_DEVICE_JSON_KEY("targetDurationMs")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==6 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("zoneId"),6)) || (p.key().size()==13 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("targetWaterMl"),13)) || (p.key().size()==6 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("status"),6)) || (p.key().size()==16 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("targetDurationMs"),16)))) return false;
    }
    }
    return true;
}
static bool validate_30(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonArrayConst>())) return false;
    if(v.is<JsonArrayConst>()) {
    if (!(v.size()<=6)) return false;
    for(JsonVariantConst item:v.as<JsonArrayConst>()) if(!validate_31(item)) return false;
    }
    return true;
}
static bool validate_36(JsonVariantConst v) {
    (void)v;
    if (!(v.is<const char*>() || v.isNull())) return false;
    if (!(schema::pattern(v,0))) return false;
    return true;
}
static bool validate_37(JsonVariantConst v) {
    (void)v;
    if (!(v.is<const char*>() || v.isNull())) return false;
    if (!(!v.is<const char*>() || validUtcTimestamp(v.as<JsonString>().c_str(),v.as<JsonString>().size()))) return false;
    return true;
}
static bool validate_38(JsonVariantConst v) {
    (void)v;
    if (!((IOT_DEVICE_TEXT_EQUAL(v,"synchronized")) || (IOT_DEVICE_TEXT_EQUAL(v,"uncertain")))) return false;
    return true;
}
static bool validate_39(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true) || v.isNull())) return false;
    if (!(!v.is<double>() || v.as<double>() >= 0)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 60000)) return false;
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_23(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("kind")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("commandId")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("zoneId")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("phase")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("activityId")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("requestedDurationMs")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("elapsedMs")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("remainingMs")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("deadlineAt")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("timeQuality")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("clockUncertaintyMs")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("targetWaterMl")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("pulseCount")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("estimatedWaterMl")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("flowMlPerMinute")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("lowFlowActive")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("highFlowActive")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("steps")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("kind")) || validate_24(v[IOT_DEVICE_JSON_KEY("kind")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("commandId")) || validate_25(v[IOT_DEVICE_JSON_KEY("commandId")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("zoneId")) || validate_26(v[IOT_DEVICE_JSON_KEY("zoneId")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("phase")) || validate_27(v[IOT_DEVICE_JSON_KEY("phase")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("targetWaterMl")) || validate_28(v[IOT_DEVICE_JSON_KEY("targetWaterMl")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("pulseCount")) || validate_29(v[IOT_DEVICE_JSON_KEY("pulseCount")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("estimatedWaterMl")) || validate_29(v[IOT_DEVICE_JSON_KEY("estimatedWaterMl")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("flowMlPerMinute")) || validate_19(v[IOT_DEVICE_JSON_KEY("flowMlPerMinute")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("lowFlowActive")) || validate_17(v[IOT_DEVICE_JSON_KEY("lowFlowActive")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("highFlowActive")) || validate_17(v[IOT_DEVICE_JSON_KEY("highFlowActive")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("steps")) || validate_30(v[IOT_DEVICE_JSON_KEY("steps")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("activityId")) || validate_36(v[IOT_DEVICE_JSON_KEY("activityId")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("requestedDurationMs")) || validate_28(v[IOT_DEVICE_JSON_KEY("requestedDurationMs")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("elapsedMs")) || validate_29(v[IOT_DEVICE_JSON_KEY("elapsedMs")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("remainingMs")) || validate_29(v[IOT_DEVICE_JSON_KEY("remainingMs")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("deadlineAt")) || validate_37(v[IOT_DEVICE_JSON_KEY("deadlineAt")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("timeQuality")) || validate_38(v[IOT_DEVICE_JSON_KEY("timeQuality")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("clockUncertaintyMs")) || validate_39(v[IOT_DEVICE_JSON_KEY("clockUncertaintyMs")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==4 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("kind"),4)) || (p.key().size()==9 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("commandId"),9)) || (p.key().size()==6 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("zoneId"),6)) || (p.key().size()==5 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("phase"),5)) || (p.key().size()==13 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("targetWaterMl"),13)) || (p.key().size()==10 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("pulseCount"),10)) || (p.key().size()==16 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("estimatedWaterMl"),16)) || (p.key().size()==15 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("flowMlPerMinute"),15)) || (p.key().size()==13 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("lowFlowActive"),13)) || (p.key().size()==14 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("highFlowActive"),14)) || (p.key().size()==5 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("steps"),5)) || (p.key().size()==10 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("activityId"),10)) || (p.key().size()==19 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("requestedDurationMs"),19)) || (p.key().size()==9 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("elapsedMs"),9)) || (p.key().size()==11 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("remainingMs"),11)) || (p.key().size()==10 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("deadlineAt"),10)) || (p.key().size()==11 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("timeQuality"),11)) || (p.key().size()==18 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("clockUncertaintyMs"),18)))) return false;
    }
    }
    return true;
}
static bool validate_41(JsonVariantConst v) {
    (void)v;
    if (!((IOT_DEVICE_TEXT_EQUAL(v,"rtc_unavailable")) || (IOT_DEVICE_TEXT_EQUAL(v,"time_unavailable")) || (IOT_DEVICE_TEXT_EQUAL(v,"rtc_rollback")) || (IOT_DEVICE_TEXT_EQUAL(v,"unexpected_flow")) || (IOT_DEVICE_TEXT_EQUAL(v,"configuration_storage")) || (IOT_DEVICE_TEXT_EQUAL(v,"scheduler_storage")) || (IOT_DEVICE_TEXT_EQUAL(v,"record_storage")) || (IOT_DEVICE_TEXT_EQUAL(v,"event_storage")))) return false;
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_40(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonArrayConst>())) return false;
    if(v.is<JsonArrayConst>()) {
    if (!(v.size()<=8)) return false;
    for(JsonVariantConst item:v.as<JsonArrayConst>()) if(!validate_41(item)) return false;
    for(size_t i=0;i<v.size();++i) for(size_t j=0;j<i;++j) if(schema::equal(v[i],v[j])) return false;
    }
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_14(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("ready")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("readyReason")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("time")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("nextAutomatic")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("activity")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("faults")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("ready")) || validate_12(v[IOT_DEVICE_JSON_KEY("ready")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("readyReason")) || validate_15(v[IOT_DEVICE_JSON_KEY("readyReason")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("time")) || validate_16(v[IOT_DEVICE_JSON_KEY("time")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("nextAutomatic")) || validate_20(v[IOT_DEVICE_JSON_KEY("nextAutomatic")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("activity")) || validate_23(v[IOT_DEVICE_JSON_KEY("activity")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("faults")) || validate_40(v[IOT_DEVICE_JSON_KEY("faults")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==5 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("ready"),5)) || (p.key().size()==11 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("readyReason"),11)) || (p.key().size()==4 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("time"),4)) || (p.key().size()==13 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("nextAutomatic"),13)) || (p.key().size()==8 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("activity"),8)) || (p.key().size()==6 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("faults"),6)))) return false;
    }
    }
    return true;
}
static bool validate_45(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 8)) return false;
    return true;
}
static bool validate_46(JsonVariantConst v) {
    (void)v;
    if (!(v.is<const char*>())) return false;
    if(v.is<const char*>()) { size_t length=0; if(!schema::textLength(v,length)) return false;
    if (!(length >= 1)) return false;
    if (!(length <= 20)) return false;
    }
    return true;
}
static bool validate_47(JsonVariantConst v) {
    (void)v;
    if (!(v.is<bool>())) return false;
    return true;
}
static bool validate_49(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 0)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 1439)) return false;
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_48(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonArrayConst>())) return false;
    if(v.is<JsonArrayConst>()) {
    if (!(v.size()<=4)) return false;
    for(JsonVariantConst item:v.as<JsonArrayConst>()) if(!validate_49(item)) return false;
    for(size_t i=0;i<v.size();++i) for(size_t j=0;j<i;++j) if(schema::equal(v[i],v[j])) return false;
    }
    return true;
}
static bool validate_52(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 6)) return false;
    return true;
}
static bool validate_53(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 720)) return false;
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_51(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("zoneId")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("durationMinutes")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("zoneId")) || validate_52(v[IOT_DEVICE_JSON_KEY("zoneId")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("durationMinutes")) || validate_53(v[IOT_DEVICE_JSON_KEY("durationMinutes")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==6 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("zoneId"),6)) || (p.key().size()==15 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("durationMinutes"),15)))) return false;
    }
    }
    return true;
}
static bool validate_50(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonArrayConst>())) return false;
    if(v.is<JsonArrayConst>()) {
    if (!(v.size()<=6)) return false;
    for(JsonVariantConst item:v.as<JsonArrayConst>()) if(!validate_51(item)) return false;
    }
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_44(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("id")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("name")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("automaticEnabled")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("startMinutes")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("zones")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("id")) || validate_45(v[IOT_DEVICE_JSON_KEY("id")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("name")) || validate_46(v[IOT_DEVICE_JSON_KEY("name")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("automaticEnabled")) || validate_47(v[IOT_DEVICE_JSON_KEY("automaticEnabled")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("startMinutes")) || validate_48(v[IOT_DEVICE_JSON_KEY("startMinutes")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("zones")) || validate_50(v[IOT_DEVICE_JSON_KEY("zones")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==2 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("id"),2)) || (p.key().size()==4 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("name"),4)) || (p.key().size()==16 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("automaticEnabled"),16)) || (p.key().size()==12 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("startMinutes"),12)) || (p.key().size()==5 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("zones"),5)))) return false;
    }
    }
    return true;
}
static bool validate_43(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonArrayConst>())) return false;
    if(v.is<JsonArrayConst>()) {
    if (!(v.size()<=8)) return false;
    for(JsonVariantConst item:v.as<JsonArrayConst>()) if(!validate_44(item)) return false;
    }
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_42(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("revision")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("plans")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("revision")) || validate_8(v[IOT_DEVICE_JSON_KEY("revision")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("plans")) || validate_43(v[IOT_DEVICE_JSON_KEY("plans")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==8 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("revision"),8)) || (p.key().size()==5 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("plans"),5)))) return false;
    }
    }
    return true;
}
static bool validate_56(JsonVariantConst v) {
    (void)v;
    if (!(IOT_DEVICE_TEXT_EQUAL(v,"enabled"))) return false;
    return true;
}
static bool validate_57(JsonVariantConst v) {
    (void)v;
    if (!(v.isNull())) return false;
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_55(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("mode")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("resumeAtEpoch")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("mode")) || validate_56(v[IOT_DEVICE_JSON_KEY("mode")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("resumeAtEpoch")) || validate_57(v[IOT_DEVICE_JSON_KEY("resumeAtEpoch")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==4 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("mode"),4)) || (p.key().size()==13 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("resumeAtEpoch"),13)))) return false;
    }
    }
    return true;
}
static bool validate_59(JsonVariantConst v) {
    (void)v;
    if (!(IOT_DEVICE_TEXT_EQUAL(v,"paused-indefinitely"))) return false;
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_58(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("mode")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("resumeAtEpoch")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("mode")) || validate_59(v[IOT_DEVICE_JSON_KEY("mode")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("resumeAtEpoch")) || validate_57(v[IOT_DEVICE_JSON_KEY("resumeAtEpoch")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==4 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("mode"),4)) || (p.key().size()==13 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("resumeAtEpoch"),13)))) return false;
    }
    }
    return true;
}
static bool validate_61(JsonVariantConst v) {
    (void)v;
    if (!(IOT_DEVICE_TEXT_EQUAL(v,"paused-until"))) return false;
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_60(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("mode")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("resumeAtEpoch")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("mode")) || validate_61(v[IOT_DEVICE_JSON_KEY("mode")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("resumeAtEpoch")) || validate_29(v[IOT_DEVICE_JSON_KEY("resumeAtEpoch")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==4 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("mode"),4)) || (p.key().size()==13 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("resumeAtEpoch"),13)))) return false;
    }
    }
    return true;
}
static bool validate_54(JsonVariantConst v) {
    (void)v;
    if (!(int(validate_55(v)) + int(validate_58(v)) + int(validate_60(v)) == 1)) return false;
    return true;
}
static bool validate_65(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 6)) return false;
    return true;
}
static bool validate_66(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 720)) return false;
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_64(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("zoneId")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("durationMinutes")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("zoneId")) || validate_65(v[IOT_DEVICE_JSON_KEY("zoneId")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("durationMinutes")) || validate_66(v[IOT_DEVICE_JSON_KEY("durationMinutes")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==6 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("zoneId"),6)) || (p.key().size()==15 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("durationMinutes"),15)))) return false;
    }
    }
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_63(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonArrayConst>())) return false;
    if(v.is<JsonArrayConst>()) {
    if (!(v.size()>=1)) return false;
    if (!(v.size()<=6)) return false;
    for(JsonVariantConst item:v.as<JsonArrayConst>()) if(!validate_64(item)) return false;
    }
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_62(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("zones")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("zones")) || validate_63(v[IOT_DEVICE_JSON_KEY("zones")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==5 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("zones"),5)))) return false;
    }
    }
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_67(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.size()<=0)) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!(false)) return false;
    }
    }
    return true;
}
static bool validate_70(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 6)) return false;
    return true;
}
static bool validate_71(JsonVariantConst v) {
    (void)v;
    if (!(IOT_DEVICE_TEXT_EQUAL(v,"duration"))) return false;
    return true;
}
static bool validate_72(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 43200)) return false;
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_69(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("zoneId")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("mode")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("durationSeconds")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("zoneId")) || validate_70(v[IOT_DEVICE_JSON_KEY("zoneId")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("mode")) || validate_71(v[IOT_DEVICE_JSON_KEY("mode")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("durationSeconds")) || validate_72(v[IOT_DEVICE_JSON_KEY("durationSeconds")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==6 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("zoneId"),6)) || (p.key().size()==4 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("mode"),4)) || (p.key().size()==15 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("durationSeconds"),15)))) return false;
    }
    }
    return true;
}
static bool validate_74(JsonVariantConst v) {
    (void)v;
    if (!(IOT_DEVICE_TEXT_EQUAL(v,"volume"))) return false;
    return true;
}
static bool validate_75(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 100)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 1000000)) return false;
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_73(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("zoneId")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("mode")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("targetWaterMl")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("zoneId")) || validate_70(v[IOT_DEVICE_JSON_KEY("zoneId")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("mode")) || validate_74(v[IOT_DEVICE_JSON_KEY("mode")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("targetWaterMl")) || validate_75(v[IOT_DEVICE_JSON_KEY("targetWaterMl")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==6 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("zoneId"),6)) || (p.key().size()==4 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("mode"),4)) || (p.key().size()==13 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("targetWaterMl"),13)))) return false;
    }
    }
    return true;
}
static bool validate_68(JsonVariantConst v) {
    (void)v;
    if (!(int(validate_69(v)) + int(validate_73(v)) == 1)) return false;
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_78(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("zoneId")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("name")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("zoneId")) || validate_65(v[IOT_DEVICE_JSON_KEY("zoneId")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("name")) || validate_46(v[IOT_DEVICE_JSON_KEY("name")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==6 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("zoneId"),6)) || (p.key().size()==4 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("name"),4)))) return false;
    }
    }
    return true;
}
static bool validate_77(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonArrayConst>())) return false;
    if(v.is<JsonArrayConst>()) {
    if (!(v.size()<=6)) return false;
    for(JsonVariantConst item:v.as<JsonArrayConst>()) if(!validate_78(item)) return false;
    }
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_76(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("zones")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("zones")) || validate_77(v[IOT_DEVICE_JSON_KEY("zones")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==5 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("zones"),5)))) return false;
    }
    }
    return true;
}
static bool validate_82(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true) || v.isNull())) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_81(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("zoneId")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("baselinePulseRateX10000")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("baselineFlowMlPerMinute")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("zoneId")) || validate_65(v[IOT_DEVICE_JSON_KEY("zoneId")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("baselinePulseRateX10000")) || validate_82(v[IOT_DEVICE_JSON_KEY("baselinePulseRateX10000")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("baselineFlowMlPerMinute")) || validate_82(v[IOT_DEVICE_JSON_KEY("baselineFlowMlPerMinute")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==6 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("zoneId"),6)) || (p.key().size()==23 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("baselinePulseRateX10000"),23)) || (p.key().size()==23 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("baselineFlowMlPerMinute"),23)))) return false;
    }
    }
    return true;
}
static bool validate_80(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonArrayConst>())) return false;
    if(v.is<JsonArrayConst>()) {
    if (!(v.size()<=6)) return false;
    for(JsonVariantConst item:v.as<JsonArrayConst>()) if(!validate_81(item)) return false;
    }
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_79(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("zones")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("zones")) || validate_80(v[IOT_DEVICE_JSON_KEY("zones")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==5 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("zones"),5)))) return false;
    }
    }
    return true;
}
static bool validate_84(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 10000000)) return false;
    return true;
}
static bool validate_85(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 0)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 10000000)) return false;
    return true;
}
static bool validate_86(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 0)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 1000000)) return false;
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_83(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("coefficientPulsesPerLiterX100")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("startupPulseCount")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("startupWaterMl")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("coefficientPulsesPerLiterX100")) || validate_84(v[IOT_DEVICE_JSON_KEY("coefficientPulsesPerLiterX100")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("startupPulseCount")) || validate_85(v[IOT_DEVICE_JSON_KEY("startupPulseCount")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("startupWaterMl")) || validate_86(v[IOT_DEVICE_JSON_KEY("startupWaterMl")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==29 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("coefficientPulsesPerLiterX100"),29)) || (p.key().size()==17 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("startupPulseCount"),17)) || (p.key().size()==14 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("startupWaterMl"),14)))) return false;
    }
    }
    return true;
}
static bool validate_89(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 100)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 10000)) return false;
    return true;
}
static bool validate_90(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1000)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 25000)) return false;
    return true;
}
static bool validate_91(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 100)) return false;
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_88(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("pullInTimeMs")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("switchDelayMs")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("pwmFrequencyHz")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("holdDutyPercent")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("pullInTimeMs")) || validate_89(v[IOT_DEVICE_JSON_KEY("pullInTimeMs")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("switchDelayMs")) || validate_89(v[IOT_DEVICE_JSON_KEY("switchDelayMs")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("pwmFrequencyHz")) || validate_90(v[IOT_DEVICE_JSON_KEY("pwmFrequencyHz")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("holdDutyPercent")) || validate_91(v[IOT_DEVICE_JSON_KEY("holdDutyPercent")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==12 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("pullInTimeMs"),12)) || (p.key().size()==13 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("switchDelayMs"),13)) || (p.key().size()==14 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("pwmFrequencyHz"),14)) || (p.key().size()==15 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("holdDutyPercent"),15)))) return false;
    }
    }
    return true;
}
static bool validate_93(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 0)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 60000)) return false;
    return true;
}
static bool validate_94(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 0)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 10000)) return false;
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_92(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("enabled")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("startDelayMs")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("stopToValveCloseDelayMs")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("enabled")) || validate_17(v[IOT_DEVICE_JSON_KEY("enabled")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("startDelayMs")) || validate_93(v[IOT_DEVICE_JSON_KEY("startDelayMs")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("stopToValveCloseDelayMs")) || validate_94(v[IOT_DEVICE_JSON_KEY("stopToValveCloseDelayMs")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==7 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("enabled"),7)) || (p.key().size()==12 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("startDelayMs"),12)) || (p.key().size()==23 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("stopToValveCloseDelayMs"),23)))) return false;
    }
    }
    return true;
}
static bool validate_96(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 10000000)) return false;
    return true;
}
static bool validate_97(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 10)) return false;
    return true;
}
static bool validate_98(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 2)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 10)) return false;
    return true;
}
static bool validate_99(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 30)) return false;
    return true;
}
static bool validate_100(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 120)) return false;
    return true;
}
static bool validate_101(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 60)) return false;
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_95(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("pulsesPerLiterX100")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("calibrationWindowSeconds")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("calibrationRequiredWindows")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("calibrationAllowedVariationPercent")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("flowStartTimeoutSeconds")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("noFlowTimeoutSeconds")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("pulsesPerLiterX100")) || validate_96(v[IOT_DEVICE_JSON_KEY("pulsesPerLiterX100")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("calibrationWindowSeconds")) || validate_97(v[IOT_DEVICE_JSON_KEY("calibrationWindowSeconds")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("calibrationRequiredWindows")) || validate_98(v[IOT_DEVICE_JSON_KEY("calibrationRequiredWindows")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("calibrationAllowedVariationPercent")) || validate_99(v[IOT_DEVICE_JSON_KEY("calibrationAllowedVariationPercent")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("flowStartTimeoutSeconds")) || validate_100(v[IOT_DEVICE_JSON_KEY("flowStartTimeoutSeconds")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("noFlowTimeoutSeconds")) || validate_101(v[IOT_DEVICE_JSON_KEY("noFlowTimeoutSeconds")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==18 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("pulsesPerLiterX100"),18)) || (p.key().size()==24 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("calibrationWindowSeconds"),24)) || (p.key().size()==26 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("calibrationRequiredWindows"),26)) || (p.key().size()==34 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("calibrationAllowedVariationPercent"),34)) || (p.key().size()==23 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("flowStartTimeoutSeconds"),23)) || (p.key().size()==20 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("noFlowTimeoutSeconds"),20)))) return false;
    }
    }
    return true;
}
static bool validate_103(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 0)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 300)) return false;
    return true;
}
static bool validate_104(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 300)) return false;
    return true;
}
static bool validate_105(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 65535)) return false;
    return true;
}
static bool validate_106(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 99)) return false;
    return true;
}
static bool validate_107(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 101)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 1000)) return false;
    return true;
}
static bool validate_108(JsonVariantConst v) {
    (void)v;
    if (!((IOT_DEVICE_TEXT_EQUAL(v,"alert")) || (IOT_DEVICE_TEXT_EQUAL(v,"stop")))) return false;
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_102(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("unexpectedFlowDelaySeconds")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("unexpectedFlowWindowSeconds")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("unexpectedFlowPulseCount")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("deviationConfirmSeconds")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("lowFlowPercent")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("highFlowPercent")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("lowFlowAction")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("highFlowAction")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("unexpectedFlowDelaySeconds")) || validate_103(v[IOT_DEVICE_JSON_KEY("unexpectedFlowDelaySeconds")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("unexpectedFlowWindowSeconds")) || validate_104(v[IOT_DEVICE_JSON_KEY("unexpectedFlowWindowSeconds")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("unexpectedFlowPulseCount")) || validate_105(v[IOT_DEVICE_JSON_KEY("unexpectedFlowPulseCount")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("deviationConfirmSeconds")) || validate_104(v[IOT_DEVICE_JSON_KEY("deviationConfirmSeconds")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("lowFlowPercent")) || validate_106(v[IOT_DEVICE_JSON_KEY("lowFlowPercent")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("highFlowPercent")) || validate_107(v[IOT_DEVICE_JSON_KEY("highFlowPercent")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("lowFlowAction")) || validate_108(v[IOT_DEVICE_JSON_KEY("lowFlowAction")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("highFlowAction")) || validate_108(v[IOT_DEVICE_JSON_KEY("highFlowAction")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==26 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("unexpectedFlowDelaySeconds"),26)) || (p.key().size()==27 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("unexpectedFlowWindowSeconds"),27)) || (p.key().size()==24 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("unexpectedFlowPulseCount"),24)) || (p.key().size()==23 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("deviationConfirmSeconds"),23)) || (p.key().size()==14 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("lowFlowPercent"),14)) || (p.key().size()==15 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("highFlowPercent"),15)) || (p.key().size()==13 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("lowFlowAction"),13)) || (p.key().size()==14 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("highFlowAction"),14)))) return false;
    }
    }
    return true;
}
static bool validate_110(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 720)) return false;
    return true;
}
static bool validate_111(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 1000)) return false;
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_109(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("maximumZoneDurationMinutes")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("maximumSingleOutputLiters")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("maximumZoneDurationMinutes")) || validate_110(v[IOT_DEVICE_JSON_KEY("maximumZoneDurationMinutes")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("maximumSingleOutputLiters")) || validate_111(v[IOT_DEVICE_JSON_KEY("maximumSingleOutputLiters")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==26 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("maximumZoneDurationMinutes"),26)) || (p.key().size()==25 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("maximumSingleOutputLiters"),25)))) return false;
    }
    }
    return true;
}
static bool validate_113(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 0)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 168)) return false;
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_112(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("rtcRollbackThresholdMinutes")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("aliveCheckpointHours")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("rtcRollbackThresholdMinutes")) || validate_101(v[IOT_DEVICE_JSON_KEY("rtcRollbackThresholdMinutes")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("aliveCheckpointHours")) || validate_113(v[IOT_DEVICE_JSON_KEY("aliveCheckpointHours")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==27 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("rtcRollbackThresholdMinutes"),27)) || (p.key().size()==20 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("aliveCheckpointHours"),20)))) return false;
    }
    }
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_87(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("valve")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("pump")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("meter")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("flow")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("limits")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("system")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("valve")) || validate_88(v[IOT_DEVICE_JSON_KEY("valve")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("pump")) || validate_92(v[IOT_DEVICE_JSON_KEY("pump")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("meter")) || validate_95(v[IOT_DEVICE_JSON_KEY("meter")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("flow")) || validate_102(v[IOT_DEVICE_JSON_KEY("flow")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("limits")) || validate_109(v[IOT_DEVICE_JSON_KEY("limits")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("system")) || validate_112(v[IOT_DEVICE_JSON_KEY("system")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==5 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("valve"),5)) || (p.key().size()==4 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("pump"),4)) || (p.key().size()==5 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("meter"),5)) || (p.key().size()==4 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("flow"),4)) || (p.key().size()==6 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("limits"),6)) || (p.key().size()==6 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("system"),6)))) return false;
    }
    }
    return true;
}
static bool validate_115(JsonVariantConst v) {
    (void)v;
    if (!(IOT_DEVICE_TEXT_EQUAL(v,"automatic.plan-run"))) return false;
    return true;
}
static bool validate_116(JsonVariantConst v) {
    (void)v;
    if (!(IOT_DEVICE_TEXT_EQUAL(v,"device_schedule"))) return false;
    return true;
}
static bool validate_117(JsonVariantConst v) {
    (void)v;
    if (!((IOT_DEVICE_TEXT_EQUAL(v,"succeeded")) || (IOT_DEVICE_TEXT_EQUAL(v,"canceled")) || (IOT_DEVICE_TEXT_EQUAL(v,"failed")) || (IOT_DEVICE_TEXT_EQUAL(v,"skipped")))) return false;
    return true;
}
static bool validate_118(JsonVariantConst v) {
    (void)v;
    if (!(v.is<const char*>())) return false;
    if(v.is<const char*>()) { size_t length=0; if(!schema::textLength(v,length)) return false;
    if (!(length >= 1)) return false;
    if (!(length <= 128)) return false;
    }
    return true;
}
static bool validate_119(JsonVariantConst v) {
    (void)v;
    if (!(v.is<const char*>() || v.isNull())) return false;
    if (!(!v.is<const char*>() || validUtcTimestamp(v.as<JsonString>().c_str(),v.as<JsonString>().size()))) return false;
    return true;
}
static bool validate_120(JsonVariantConst v) {
    (void)v;
    if (!(v.is<const char*>())) return false;
    if (!(!v.is<const char*>() || validUtcTimestamp(v.as<JsonString>().c_str(),v.as<JsonString>().size()))) return false;
    return true;
}
static bool validate_121(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true) || v.isNull())) return false;
    if (!(!v.is<double>() || v.as<double>() >= 0)) return false;
    return true;
}
static bool validate_123(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 8)) return false;
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_122(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("planId")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("planId")) || validate_123(v[IOT_DEVICE_JSON_KEY("planId")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==6 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("planId"),6)))) return false;
    }
    }
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_114(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("actionKey")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("sourceKey")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("status")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("reason")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("startedAt")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("endedAt")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("durationSeconds")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("parameters")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("actionKey")) || validate_115(v[IOT_DEVICE_JSON_KEY("actionKey")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("sourceKey")) || validate_116(v[IOT_DEVICE_JSON_KEY("sourceKey")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("status")) || validate_117(v[IOT_DEVICE_JSON_KEY("status")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("reason")) || validate_118(v[IOT_DEVICE_JSON_KEY("reason")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("startedAt")) || validate_119(v[IOT_DEVICE_JSON_KEY("startedAt")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("endedAt")) || validate_120(v[IOT_DEVICE_JSON_KEY("endedAt")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("durationSeconds")) || validate_121(v[IOT_DEVICE_JSON_KEY("durationSeconds")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("parameters")) || validate_122(v[IOT_DEVICE_JSON_KEY("parameters")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==9 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("actionKey"),9)) || (p.key().size()==9 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("sourceKey"),9)) || (p.key().size()==6 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("status"),6)) || (p.key().size()==6 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("reason"),6)) || (p.key().size()==9 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("startedAt"),9)) || (p.key().size()==7 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("endedAt"),7)) || (p.key().size()==15 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("durationSeconds"),15)) || (p.key().size()==10 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("parameters"),10)))) return false;
    }
    }
    return true;
}
static bool validate_125(JsonVariantConst v) {
    (void)v;
    if (!((IOT_DEVICE_TEXT_EQUAL(v,"wechat_miniprogram")) || (IOT_DEVICE_TEXT_EQUAL(v,"device_schedule")))) return false;
    return true;
}
static bool validate_126(JsonVariantConst v) {
    (void)v;
    if (!(v.is<const char*>() || v.isNull())) return false;
    if (!(schema::pattern(v,0))) return false;
    return true;
}
static bool validate_127(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true) || v.isNull())) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 8)) return false;
    return true;
}
static bool validate_128(JsonVariantConst v) {
    (void)v;
    if (!(IOT_DEVICE_TEXT_EQUAL(v,"trusted"))) return false;
    return true;
}
static bool validate_129(JsonVariantConst v) {
    (void)v;
    if (!((IOT_DEVICE_TEXT_EQUAL(v,"completed")) || (IOT_DEVICE_TEXT_EQUAL(v,"stopped")) || (IOT_DEVICE_TEXT_EQUAL(v,"failed")))) return false;
    return true;
}
static bool validate_132(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    return true;
}
static bool validate_133(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 0)) return false;
    return true;
}
static bool validate_134(JsonVariantConst v) {
    (void)v;
    if (!((IOT_DEVICE_TEXT_EQUAL(v,"completed")) || (IOT_DEVICE_TEXT_EQUAL(v,"stopped")) || (IOT_DEVICE_TEXT_EQUAL(v,"failed")) || (IOT_DEVICE_TEXT_EQUAL(v,"not-started")))) return false;
    return true;
}
static bool validate_135(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true) || v.isNull())) return false;
    if (!(!v.is<double>() || v.as<double>() >= 0)) return false;
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_131(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("zoneId")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("targetSeconds")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("targetWaterMl")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("actualSeconds")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("zoneResult")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("pulseCount")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("estimatedWaterMl")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("baselinePulseRateX10000")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("baselineFlowMlPerMinute")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("averageFlowMlPerMinute")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("lowFlowDetected")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("highFlowDetected")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("zoneId")) || validate_65(v[IOT_DEVICE_JSON_KEY("zoneId")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("targetSeconds")) || validate_132(v[IOT_DEVICE_JSON_KEY("targetSeconds")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("targetWaterMl")) || validate_82(v[IOT_DEVICE_JSON_KEY("targetWaterMl")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("actualSeconds")) || validate_133(v[IOT_DEVICE_JSON_KEY("actualSeconds")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("zoneResult")) || validate_134(v[IOT_DEVICE_JSON_KEY("zoneResult")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("pulseCount")) || validate_133(v[IOT_DEVICE_JSON_KEY("pulseCount")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("estimatedWaterMl")) || validate_133(v[IOT_DEVICE_JSON_KEY("estimatedWaterMl")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("baselinePulseRateX10000")) || validate_82(v[IOT_DEVICE_JSON_KEY("baselinePulseRateX10000")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("baselineFlowMlPerMinute")) || validate_82(v[IOT_DEVICE_JSON_KEY("baselineFlowMlPerMinute")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("averageFlowMlPerMinute")) || validate_135(v[IOT_DEVICE_JSON_KEY("averageFlowMlPerMinute")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("lowFlowDetected")) || validate_47(v[IOT_DEVICE_JSON_KEY("lowFlowDetected")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("highFlowDetected")) || validate_47(v[IOT_DEVICE_JSON_KEY("highFlowDetected")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==6 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("zoneId"),6)) || (p.key().size()==13 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("targetSeconds"),13)) || (p.key().size()==13 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("targetWaterMl"),13)) || (p.key().size()==13 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("actualSeconds"),13)) || (p.key().size()==10 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("zoneResult"),10)) || (p.key().size()==10 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("pulseCount"),10)) || (p.key().size()==16 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("estimatedWaterMl"),16)) || (p.key().size()==23 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("baselinePulseRateX10000"),23)) || (p.key().size()==23 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("baselineFlowMlPerMinute"),23)) || (p.key().size()==22 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("averageFlowMlPerMinute"),22)) || (p.key().size()==15 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("lowFlowDetected"),15)) || (p.key().size()==16 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("highFlowDetected"),16)))) return false;
    }
    }
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_130(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonArrayConst>())) return false;
    if(v.is<JsonArrayConst>()) {
    if (!(v.size()>=1)) return false;
    if (!(v.size()<=6)) return false;
    for(JsonVariantConst item:v.as<JsonArrayConst>()) if(!validate_131(item)) return false;
    }
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_124(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("sourceKey")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("relatedCommandId")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("planId")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("startedAt")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("completedAt")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("durationSeconds")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("timeQuality")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("result")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("reason")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("zones")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("sourceKey")) || validate_125(v[IOT_DEVICE_JSON_KEY("sourceKey")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("relatedCommandId")) || validate_126(v[IOT_DEVICE_JSON_KEY("relatedCommandId")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("planId")) || validate_127(v[IOT_DEVICE_JSON_KEY("planId")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("startedAt")) || validate_120(v[IOT_DEVICE_JSON_KEY("startedAt")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("completedAt")) || validate_120(v[IOT_DEVICE_JSON_KEY("completedAt")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("durationSeconds")) || validate_8(v[IOT_DEVICE_JSON_KEY("durationSeconds")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("timeQuality")) || validate_128(v[IOT_DEVICE_JSON_KEY("timeQuality")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("result")) || validate_129(v[IOT_DEVICE_JSON_KEY("result")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("reason")) || validate_118(v[IOT_DEVICE_JSON_KEY("reason")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("zones")) || validate_130(v[IOT_DEVICE_JSON_KEY("zones")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==9 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("sourceKey"),9)) || (p.key().size()==16 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("relatedCommandId"),16)) || (p.key().size()==6 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("planId"),6)) || (p.key().size()==9 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("startedAt"),9)) || (p.key().size()==11 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("completedAt"),11)) || (p.key().size()==15 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("durationSeconds"),15)) || (p.key().size()==11 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("timeQuality"),11)) || (p.key().size()==6 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("result"),6)) || (p.key().size()==6 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("reason"),6)) || (p.key().size()==5 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("zones"),5)))) return false;
    }
    }
    return true;
}
static bool validate_136(JsonVariantConst v) {
    (void)v;
    if (!(int(validate_58(v)) + int(validate_60(v)) == 1)) return false;
    return true;
}
static bool validate_139(JsonVariantConst v) {
    (void)v;
    if (!(IOT_DEVICE_TEXT_EQUAL(v,"expired"))) return false;
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_138(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("mode")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("mode")) || validate_139(v[IOT_DEVICE_JSON_KEY("mode")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==4 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("mode"),4)))) return false;
    }
    }
    return true;
}
static bool validate_137(JsonVariantConst v) {
    (void)v;
    if (!(int(validate_55(v)) + int(validate_138(v)) == 1)) return false;
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_141(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonArrayConst>())) return false;
    if(v.is<JsonArrayConst>()) {
    if (!(v.size()<=8)) return false;
    for(JsonVariantConst item:v.as<JsonArrayConst>()) if(!validate_123(item)) return false;
    for(size_t i=0;i<v.size();++i) for(size_t j=0;j<i;++j) if(schema::equal(v[i],v[j])) return false;
    }
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_140(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("revision")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("planIds")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("revision")) || validate_8(v[IOT_DEVICE_JSON_KEY("revision")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("planIds")) || validate_141(v[IOT_DEVICE_JSON_KEY("planIds")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==8 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("revision"),8)) || (p.key().size()==7 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("planIds"),7)))) return false;
    }
    }
    return true;
}
static bool validate_143(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_142(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("coefficientPulsesPerLiterX100")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("pulseCount")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("waterMl")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("coefficientPulsesPerLiterX100")) || validate_143(v[IOT_DEVICE_JSON_KEY("coefficientPulsesPerLiterX100")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("pulseCount")) || validate_8(v[IOT_DEVICE_JSON_KEY("pulseCount")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("waterMl")) || validate_143(v[IOT_DEVICE_JSON_KEY("waterMl")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==29 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("coefficientPulsesPerLiterX100"),29)) || (p.key().size()==10 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("pulseCount"),10)) || (p.key().size()==7 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("waterMl"),7)))) return false;
    }
    }
    return true;
}
static bool validate_145(JsonVariantConst v) {
    (void)v;
    if (!(schema::number(v,true))) return false;
    if (!(!v.is<double>() || v.as<double>() >= 1)) return false;
    if (!(!v.is<double>() || v.as<double>() <= 6)) return false;
    return true;
}
static IOT_DEVICE_NOINLINE bool validate_144(JsonVariantConst v) {
    (void)v;
    if (!(v.is<JsonObjectConst>())) return false;
    if(v.is<JsonObjectConst>()) {
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("zoneId")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("baselinePulseRateX10000")))) return false;
    if (!(v.containsKey(IOT_DEVICE_JSON_KEY("baselineFlowMlPerMinute")))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("zoneId")) || validate_145(v[IOT_DEVICE_JSON_KEY("zoneId")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("baselinePulseRateX10000")) || validate_143(v[IOT_DEVICE_JSON_KEY("baselinePulseRateX10000")]))) return false;
    if (!(!v.containsKey(IOT_DEVICE_JSON_KEY("baselineFlowMlPerMinute")) || validate_143(v[IOT_DEVICE_JSON_KEY("baselineFlowMlPerMinute")]))) return false;
    for(JsonPairConst p:v.as<JsonObjectConst>()) { (void)p;
    if (!((p.key().size()==6 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("zoneId"),6)) || (p.key().size()==23 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("baselinePulseRateX10000"),23)) || (p.key().size()==23 && !IOT_DEVICE_MEMCMP(p.key().c_str(),IOT_DEVICE_LITERAL("baselineFlowMlPerMinute"),23)))) return false;
    }
    }
    return true;
}
static const CapabilityContract capabilities[]={{"state.overview",CapabilityKind::State,0,validate_0},{"state.diagnostics",CapabilityKind::State,0,validate_3},{"state.runtime",CapabilityKind::State,0,validate_14},{"parameter.plans",CapabilityKind::Parameter,10000,validate_42},{"parameter.automatic-watering",CapabilityKind::Parameter,10000,validate_54},{"operation.start-manual",CapabilityKind::Process,30000,validate_62},{"operation.stop",CapabilityKind::Instant,10000,validate_67},{"operation.single-output",CapabilityKind::Process,30000,validate_68},{"state.zones",CapabilityKind::State,0,validate_76},{"state.zone-maintenance",CapabilityKind::State,0,validate_79},{"state.calibration",CapabilityKind::State,0,validate_83},{"state.system-parameters",CapabilityKind::State,0,validate_87}};
static const RecordContract records[]={{"operation.automatic-run.completed",validate_114},{"watering.completed",validate_124},{"watering.stopped",validate_124},{"watering.failed",validate_124},{"automatic.paused",validate_136},{"automatic.resumed",validate_137},{"configuration.plans-changed",validate_140},{"calibration.result-saved",validate_142},{"zone.baseline-saved",validate_144}};
static const ModelContract contract={"irrigation-controller","irrigation-controller",1,"irrigation-controller-6-zone",30,"b2f24fe51b7cb470c89d8638b298b2ec80aa8f070bf0f071663c50a4ffd533be",capabilities,12,records,9};
}}
