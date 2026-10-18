"""Exercise actual timing/touch policy without hardware or the ESP SDK."""
import subprocess
from pathlib import Path


def test_power_policy(tmp_path):
    include = Path(__file__).resolve().parents[1] / 'firmware/esp32/main'
    source = tmp_path / 'power.c'
    source.write_text(r'''
#include <assert.h>
#include "power_policy.h"
int main(void) {
    power_activity_t p={.last_touch=100};
    assert(power_timeout(30,0,0)==30); assert(power_timeout(30,0,1)==30);
    assert(power_timeout(30,0,2)==0);
    assert(!power_expired(&p,30099,30)); assert(power_expired(&p,30100,30));
    assert(!power_expired(&p,999999,0));
    assert(!power_touch(&p,30100,true)); assert(!power_expired(&p,30101,30));
    assert(!power_suppress(&p,true));
    p.asleep=true;
    assert(power_touch(&p,40000,true)); assert(power_suppress(&p,true));
    p.asleep=false;
    assert(!power_touch(&p,40010,true)); assert(power_suppress(&p,true));
    assert(power_suppress(&p,false)); assert(!power_suppress(&p,true));
    /* Holding a finger continually renews inactivity. */
    power_touch(&p,60000,true); assert(!power_expired(&p,60000,30));
    /* Unsigned elapsed time survives the LVGL tick counter wrapping. */
    p.last_touch=UINT32_MAX-100;
    assert(!power_expired(&p,100,1)); assert(power_expired(&p,1000,1));
    /* Splash dwell begins after presentation; no early backlight off. */
    p=(power_activity_t){.last_touch=100};
    assert(power_expired(&p,30100,30));
    power_begin_sleep(&p,30200);
    assert(!power_expired(&p,99999,30));
    assert(!power_sleep_ready(&p,32199)); assert(power_sleep_ready(&p,32200));
    /* Touch during the splash cancels sleep and consumes the whole gesture. */
    assert(power_touch(&p,31000,true)); assert(!p.sleep_pending);
    assert(power_suppress(&p,true)); assert(power_suppress(&p,false));
    assert(!power_suppress(&p,false)); assert(!power_sleep_ready(&p,40000));
    power_begin_sleep(&p,UINT32_MAX-1000);
    assert(!power_sleep_ready(&p,998)); assert(power_sleep_ready(&p,999));
    /* Offline time, including a long clock sync, never consumes idle time. */
    p=(power_activity_t){.last_touch=0};
    power_connection(&p,29000,false);
    assert(p.waiting); assert(!power_expired(&p,900000,30));
    power_connection(&p,900000,true);
    assert(!p.waiting); assert(!power_expired(&p,929999,30));
    assert(power_expired(&p,930000,30));
    /* Healthy metadata/readiness checks must not continually reset idle. */
    power_connection(&p,930000,true); assert(power_expired(&p,930000,30));
    /* Connection loss during pre-sleep cancels the pending blank. */
    power_begin_sleep(&p,930000);
    power_connection(&p,931999,false);
    assert(!p.sleep_pending); assert(!power_sleep_ready(&p,932000));
    power_connection(&p,1000000,true);
    assert(!power_expired(&p,1029999,30)); assert(power_expired(&p,1030000,30));
    /* Background reconnection cannot illuminate a screen already asleep. */
    p.asleep=true; power_connection(&p,1100000,false);
    power_connection(&p,1200000,true); assert(p.asleep);
    /* Wake while offline remains awake until ready, then gets full timeout. */
    power_connection(&p,1300000,false);
    assert(power_touch(&p,1300100,true)); p.asleep=false;
    assert(power_suppress(&p,true)); assert(power_suppress(&p,false));
    assert(!power_expired(&p,1500000,30));
    power_connection(&p,1500000,true);
    assert(!power_expired(&p,1529999,30)); assert(power_expired(&p,1530000,30));
    /* Recovery at tick wrap also starts a complete idle interval. */
    power_connection(&p,UINT32_MAX-500,false);
    power_connection(&p,UINT32_MAX-100,true);
    assert(!power_expired(&p,898,1)); assert(power_expired(&p,899,1));
    return 0;
}
''')
    binary = tmp_path / 'power'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(include),str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
