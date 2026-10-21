/* Read-only MAX77759 sample conversion; units from Google's maxfg_common.h
 * and charger state definitions. No hardware access or charging control. */
#ifndef BLUEJAY_BATTERY_DATA_H
#define BLUEJAY_BATTERY_DATA_H
struct BluejayBatterySample {
 unsigned percent, mv; int temperature; bool external, charging, full;
};
static bool bluejay_battery_decode(unsigned id, unsigned soc, unsigned volts,
 unsigned temp, unsigned intok, unsigned details1, unsigned details2,
 BluejayBatterySample *s, bool charger_valid = true) {
 if (id==0 || id==0xffff || id>0xffff || soc>100u*256u || volts>0xffff || temp>0xffff) return false;
 unsigned mv=volts*5u/64u;
 int tc=((temp&0x8000)?(int)temp-65536:(int)temp)*100/256;
 if (mv<2800 || mv>4600 || tc < -2000 || tc>8000) return false;
 s->percent=(soc+128)/256; s->mv=mv; s->temperature=tc;
 /* Charger failure must not discard valid fuel-gauge data or invent unplugging. */
 if (!charger_valid) return true;
 s->external=(intok&0x40)!=0;
 unsigned state=details1&15;
 s->charging=s->external && (details2&0x20) && state<=3;
 s->full=s->external && state==4;
 return true;
}
#endif
