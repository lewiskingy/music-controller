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
    return 0;
}
''')
    binary = tmp_path / 'power'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(include),str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
