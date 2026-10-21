/* Pixel 6a observation-only battery/USB diagnostic.
 * Sources: Google gs101-usi.dtsi (I2C12, gpp23-4/5), pinctrl bank +0x60,
 * gs101-raviole-battery.dtsi (FG 0x36, charger 0x69), and MAX77759 regs.
 * Open-drain software I2C: output LOW or input/released, never drive HIGH.
 * Only register-pointer writes; never write charger/gauge configuration.
 */
#define PB_BANK 0x10c40060UL
#define PB_SCL 4
#define PB_SDA 5
static void pb_delay(void) { u64 t = now(); while (now()-t < timer_hz/100000+1) {} }
static void pb_line(int pin, int release) { gpio_func(PB_BANK, pin, release ? 0 : 1); pb_delay(); }
static int pb_get(int pin) { return (rd32(PB_BANK+4) >> pin) & 1; }
static int pb_clock(void) {
 pb_line(PB_SCL,1);
 for (int i=0;i<100;i++) { if (pb_get(PB_SCL)) return 1; pb_delay(); }
 return 0;
}
static int pb_start(void) {
 pb_line(PB_SDA,1); if (!pb_clock() || !pb_get(PB_SDA)) return 0;
 pb_line(PB_SDA,0); pb_line(PB_SCL,0); return 1;
}
static void pb_stop(void) { pb_line(PB_SCL,0); pb_line(PB_SDA,0); (void)pb_clock(); pb_line(PB_SDA,1); }
static int pb_send(u8 value) {
 for (int n=7;n>=0;n--) { pb_line(PB_SDA,(value>>n)&1); if (!pb_clock()) return 0; pb_line(PB_SCL,0); }
 pb_line(PB_SDA,1); if (!pb_clock()) return 0; int ack=!pb_get(PB_SDA); pb_line(PB_SCL,0); return ack;
}
static int pb_recv(u8 *value, int last) {
 u8 v=0; pb_line(PB_SDA,1);
 for (int n=0;n<8;n++) { if (!pb_clock()) return 0; v=(v<<1)|pb_get(PB_SDA); pb_line(PB_SCL,0); }
 pb_line(PB_SDA,last); if (!pb_clock()) return 0; pb_line(PB_SCL,0); pb_line(PB_SDA,1); *value=v; return 1;
}
static int pb_read(u8 chip,u8 reg,u8 *out,int len) {
 u32 con=rd32(PB_BANK), dat=rd32(PB_BANK+4);
 /* Release first, then preload LOW before enabling either output. */
 gpio_func(PB_BANK,PB_SCL,0); gpio_func(PB_BANK,PB_SDA,0);
 wr32(PB_BANK+4,dat & ~((1u<<PB_SCL)|(1u<<PB_SDA)));
 int ok=pb_start() && pb_send(chip<<1) && pb_send(reg) && pb_start() && pb_send((chip<<1)|1);
 for (int i=0;ok && i<len;i++) ok=pb_recv(out+i,i==len-1);
 pb_stop();
 /* Restore only these two pins, including latch before original mux. */
 u32 mask=(1u<<PB_SCL)|(1u<<PB_SDA), cmask=(0xfu<<(4*PB_SCL))|(0xfu<<(4*PB_SDA));
 wr32(PB_BANK+4,(rd32(PB_BANK+4)&~mask)|(dat&mask));
 wr32(PB_BANK,(rd32(PB_BANK)&~cmask)|(con&cmask)); return ok;
}
static void pb_word(const char *name,u8 reg) {
 u8 b[2]; puts_(name);
 if (pb_read(0x36,reg,b,2)) puthex(b[0]|((u32)b[1]<<8),4); else puts_("NO ACK / TIMEOUT"); newline();
}
static void __attribute__((noreturn)) diag_power_usb(u64 dtb) {
 say(C_OK,"PIXEL BATTERY + USB OBSERVER (no storage writes)");
 struct fdt f; int phone=0;
 fdt_begin(&f,dtb); while(fdt_next(&f)) if(prop_is(&f,"compatible") && contains((const char *)f.val,f.len,"google,bluejay")) phone=1;
 if (!phone) { say(C_WARN,"Not bluejay hardware: skipping all device access"); for (;;) __asm__ volatile("wfe"); }
 kv2("I2C12 GPIO mux ",rd32(PB_BANK)," data ",rd32(PB_BANK+4));
 pb_word("FG DevName 0x21 = ",0x21); pb_word("FG RepSOC 0x06 (8.8 percent) = ",0x06);
 pb_word("FG VCell 0x09 (raw) = ",0x09); pb_word("FG Temp 0x08 (signed 8.8 C) = ",0x08);
 pb_word("FG Current 0x0a (raw) = ",0x0a);
 for (u8 reg=0xb4;reg<=0xb7;reg++) { u8 v; puts_("Charger "); puthex(reg,2); puts_(" = "); if(pb_read(0x69,reg,&v,1)) puthex(v,2); else puts_("NO ACK / TIMEOUT"); newline(); }
 say(C_DIM,"USB DWC3 inherited from fastboot (register reads only)");
 u64 usb=0x11110000;
 kv2("GSNPSID ",rd32(usb+0xc120)," GCTL ",rd32(usb+0xc110));
 kv2("VER_NUMBER ",rd32(usb+0xc1a0)," GUSB2PHYCFG ",rd32(usb+0xc200));
 kv2("DCFG ",rd32(usb+0xc700)," DCTL ",rd32(usb+0xc704));
 kv2("DSTS ",rd32(usb+0xc70c)," DALEPENA ",rd32(usb+0xc720));
 kv2("EVENT PA low ",rd32(usb+0xc400)," high ",rd32(usb+0xc404));
 say(C_OK,"Observer complete. Photograph this screen.");
 say(C_DIM,"Battery registers can be rechecked after cable changes:");
 for (;;) { delay_ms(3000); u8 b[2],c; puts_("SOC "); if(pb_read(0x36,6,b,2)) puthex(b[0]|((u32)b[1]<<8),4); else puts_("ERR"); puts_(" USB/charge "); if(pb_read(0x69,0xb4,&c,1)) puthex(c,2); else puts_("ERR"); newline(); }
}
