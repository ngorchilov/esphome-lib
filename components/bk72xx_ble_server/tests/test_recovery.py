"""Compile the real server and ESPHome queues against a deterministic SDK double.

Run with the Python interpreter containing the ESPHome package. Requires clang++.
No radios, controllers, or network connections are used.
"""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import esphome
import esphome.config_validation as cv

files={
'esphome/core/defines.h': '#pragma once\n#define ESPHOME_THREAD_MULTI_NO_ATOMICS\n',
'esphome/core/helpers.h': '''#pragma once
#include <cstdlib>
#include <new>
namespace esphome { template<class T> struct RAMAllocator { enum {ALLOC_INTERNAL}; RAMAllocator(int){} T* allocate(size_t n){return static_cast<T*>(std::malloc(n*sizeof(T)));} void deallocate(T*p,size_t){std::free(p);} }; }
''',
'esphome/core/hal.h': '''#pragma once
#include <cstdint>
extern uint32_t fake_now;
inline uint32_t millis(){ return fake_now; }
inline void delay(uint32_t ms){fake_now+=ms;}
''',
'esphome/core/component.h': '''#pragma once
#include <cstdint>
#include <string>
#include <functional>
#include <map>
#include "esphome/core/hal.h"
namespace esphome { namespace setup_priority {constexpr float AFTER_WIFI=200;}
class Component {public: virtual ~Component(){}; virtual void setup(){}; virtual void loop(){}; virtual void dump_config(){}; virtual float get_setup_priority()const{return 0;}
 bool failed=false; bool is_failed() const{return failed;} void mark_failed(){failed=true;}
 struct Timer {uint32_t at; std::function<void()> f;}; std::map<std::string,Timer> timers;
 void set_timeout(const char*n,uint32_t ms,std::function<void()> f){timers[n]={millis()+ms,f};}
 void cancel_timeout(const char*n){timers.erase(n);}
 void run_timers(){for(;;){auto it=timers.begin();for(;it!=timers.end();++it)if(int32_t(millis()-it->second.at)>=0)break;if(it==timers.end())break;auto f=it->second.f;timers.erase(it);f();}}
}; }
''',
'esphome/core/automation.h': '#pragma once\n#include <cstdint>\nnamespace esphome {template<class T> struct Trigger {unsigned calls=0; T last{}; void trigger(T value){calls++;last=value;}};}\n',
'esphome/core/log.h': '''#pragma once
#define ESP_LOGE(...) ((void)0)
void test_warning(const char *format);
#define ESP_LOGW(tag,format,...) test_warning(format)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGCONFIG(...) ((void)0)
#define YESNO(x) ((x)?"YES":"NO")
''',
'esphome/components/bk72xx_ble/bk72xx_ble.h': '''#pragma once
#include <cstdint>
#include <cassert>
extern bool sleep_enabled;
namespace esphome::bk72xx_ble {struct BK72xxBLE {void enable(){assert(!sleep_enabled);};void get_mac_lsb_first(uint8_t*p){for(int i=0;i<6;i++)p[i]=i;}};}
''',
'esphome/components/ota/ota_backend.h': '''#pragma once
#include <cstdint>
namespace esphome::ota {enum OTAState {OTA_STARTED,OTA_ERROR,OTA_ABORT,OTA_COMPLETED};class OTAComponent{};struct OTAGlobalStateListener{virtual void on_ota_global_state(OTAState,float,uint8_t,OTAComponent*)=0;}; struct Callback{void add_global_state_listener(OTAGlobalStateListener*){}};inline Callback*get_global_ota_callback(){static Callback x;return &x;}}
''',
'ble_api.h': '''#pragma once
#include <stdint.h>
#define CFG_BLE_VERSION 2
#define BLE_VERSION_5_1 2
#define BK_PERM_SET(a,b) 0
#define ERR_SUCCESS 0
#define ERR_UNKNOW_IDX 9
typedef int ble_err_t;
typedef enum {BLE_CREATE_ADV=1,BLE_SET_ADV_DATA,BLE_SET_RSP_DATA,BLE_START_ADV,BLE_STOP_ADV} ble_cmd_t;
typedef struct {uint8_t status;} ble_cmd_param_t;
typedef void(*ble_cmd_cb_t)(ble_cmd_t,ble_cmd_param_t*);
typedef enum {BLE_5_CREATE_DB,BLE_5_WRITE_EVENT,BLE_5_ATT_INFO_REQ,BLE_5_READ_EVENT,BLE_5_CONNECT_EVENT,BLE_5_DISCONNECT_EVENT} ble_notice_t;
typedef void(*notice_t)(ble_notice_t,void*);
typedef struct {uint16_t prf_id;uint8_t status;} create_db_t;
typedef struct {uint16_t prf_id,att_idx,len;uint8_t*value;uint8_t conn_idx;} write_req_t;
typedef struct {uint16_t prf_id,att_idx,length;uint8_t status;} att_info_req_t;
typedef struct {uint16_t length;} read_req_t;
typedef struct {uint8_t conn_idx;} conn_ind_t;
typedef struct {uint8_t conn_idx,reason;} discon_ind_t;
typedef struct {uint8_t uuid[16];uint16_t perm,ext_perm,len;} bk_attm_desc_t;
typedef struct {uint16_t prf_task_id;uint8_t uuid[16];uint16_t att_db_nb;bk_attm_desc_t*att_db;uint16_t svc_perm;} bk_ble_db_cfg;
void ble_ps_enable_clear(void);
void ble_set_notice_cb(notice_t);
void ble_appm_set_dev_name(uint8_t,uint8_t*);
ble_err_t bk_ble_create_db(bk_ble_db_cfg*);
ble_err_t bk_ble_create_advertising(uint8_t,uint8_t,uint16_t,uint16_t,ble_cmd_cb_t);
ble_err_t bk_ble_set_adv_data(uint8_t,uint8_t*,uint8_t,ble_cmd_cb_t);
ble_err_t bk_ble_set_scan_rsp_data(uint8_t,uint8_t*,uint8_t,ble_cmd_cb_t);
ble_err_t bk_ble_start_advertising(uint8_t,uint16_t,ble_cmd_cb_t);
ble_err_t bk_ble_stop_advertising(uint8_t,ble_cmd_cb_t);
ble_err_t bk_ble_disconnect(uint8_t);
''',
'app_ble.h': '''#pragma once
#define APP_BLE_READY 1
#define ACTV_IDLE 0
#define ACTV_ADV_CREATED 1
#define ACTV_ADV_STARTED 2
#define ADV_ACTV 0
#define BLE_CONNECTION_MAX 1
#define UNKNOW_CONN_HDL 0xff
#define USED_CONN_HDL 0xfe
uint8_t app_ble_get_connhdl(int);
int app_ble_env_state_get(void);
int app_ble_actv_state_get(uint8_t);
uint8_t app_ble_get_idle_actv_idx_handle(int);
''',
}

files["esphome/core/application.h"] = """#pragma once
namespace esphome {
struct RebootRequested {};
struct Application {void safe_reboot(){throw RebootRequested{};}};
inline Application App;
}
"""

if __name__ == "__main__":
    component = Path(__file__).resolve().parents[1]
    spec = importlib.util.spec_from_file_location("tested_ble_server", component / "__init__.py")
    schema_module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(schema_module)
    for value in (0, 0xA1B2C3, 0xFFFFFF, "0xa1b2c3"):
        config = schema_module.CONFIG_SCHEMA({"command": value})
        assert config["command"] == (int(value, 16) if isinstance(value, str) else value)
    for config in ({}, {"command": -1}, {"command": 0x1000000}, {"command": "a1b2c3"}):
        try:
            schema_module.CONFIG_SCHEMA(config)
        except cv.Invalid:
            continue
        raise AssertionError(f"Accepted invalid command configuration: {config}")
    print("PASS: command configuration requires an explicit unsigned 24-bit number", flush=True)
    with tempfile.TemporaryDirectory(prefix="bk72xx-ble-test-") as temporary:
        root = Path(temporary)
        for name, text in files.items():
            target = root / "stubs" / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(text)
        binary = root / "recovery"
        subprocess.run([
            os.environ.get("CXX", "clang++"), "-std=c++20", "-DUSE_HOST",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-I" + str(root / "stubs"), "-I" + str(component),
            "-I" + str(Path(esphome.__file__).resolve().parent.parent),
            str(component / "tests" / "recovery.cpp"),
            str(component / "bk72xx_ble_server.cpp"), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
