/* Read-only GPU clock/power diagnostic, explicit shimgpuq boot only.
 * Question: is the Mali-G78 shader clock (ACPM DVFS domain G3D) running?
 * Google gs101-gpu.dtsi: gpu0_cmu_cal_id = ACPM_DVFS_G3DL2 (L2/top),
 * gpu1_cmu_cal_id = ACPM_DVFS_G3D (shader cores). Domain numbers follow
 * mainline's gs101 ACPM clock list (MIF 0 ... BO 13; CPUCL0 = 2 matches
 * cpu_clocks()). Only FREQ_GET requests, PMU status reads and PMIC
 * register reads: nothing is written to a clock, rail or power domain.
 * Results stay on screen. */
#define GQ_PD_G3D		0x17461e00UL	/* pd-g3d ("top"): +0 config, +4 status */
#define GQ_PD_EMBEDDED_G3D	0x17462000UL	/* pd-embedded_g3d ("cores") */

static void gq_dec(u32 v)
{
	char b[10];
	int n = 0;
	do
		b[n++] = (char)('0' + v % 10);
	while ((v /= 10) && n < 10);
	while (n)
		putch(b[--n]);
}

/*
 * GPU rails on S2MPG11 (ACPM channel 2, speedy 1; mainline sec-acpm.c,
 * s2mps11.c, s2mpg11.h). CTRL[7:6] (LDO1S: [5:4]): 3 on, 1 external
 * (pin chosen by PCTRLSELx: 7 = G3D_EN), 0 off. Bucks: 200 mV + 6.25 mV * OUT1.
 * Reads only (func 0).
 */
static int gq_pmic_read(u8 reg, u8 *out)
{
	u32 cmd[4], w1 = 0;
	cmd[0] = (1u << 12) | (1u << 8) | reg;	/* speedy 1, type PMIC */
	cmd[1] = 0;				/* ACPM_PMIC_READ */
	cmd[2] = 0;
	cmd[3] = (u32)(now() / (timer_hz / 1000));
	int e = acpm_xfer(ACPM_PMIC_CHAN, cmd, &w1);
	if (e)
		return e;
	*out = (u8)(w1 >> 8);
	return (w1 >> 24) ? -(int)(w1 >> 24) - 10 : 0;
}

static void gq_rails(void)
{
	static const struct { char name[16]; u8 ctrl, out, shift; } rails[3] = {
		{ "BUCK2S VDD_G3D ", 0x15, 0x16, 6 },
		{ "BUCK8S G3D_L2  ", 0x25, 0x26, 6 },
		{ "LDO1S  G3D_M   ", 0x41, 0, 4 },
	};
	static const struct { char name[12]; u8 reg; } sels[4] = {
		{ "PCTRLSEL1 ", 0x8d }, { "PCTRLSEL3 ", 0x8f },
		{ "PCTRLSEL5 ", 0x91 }, { "STATUS1   ", 0x0c },
	};
	say(C_DIM, "S2MPG11 GPU rails (ctrl / mode / out / mV):");
	for (int i = 0; i < 3; i++) {
		u8 c = 0, o = 0;
		int e = gq_pmic_read(rails[i].ctrl, &c);
		if (!e && rails[i].out)
			e = gq_pmic_read(rails[i].out, &o);
		color = i == 0 ? C_WARN : C_FG;
		puts_("  ");
		puts_(rails[i].name);
		if (e) {
			puts_("error ");
			puthex((u32)-e, 2);
		} else {
			u32 mode = (c >> rails[i].shift) & 3;
			puthex(c, 2);
			puts_(mode == 3 ? " ON  " : mode == 1 ? " EXT " : mode == 0 ? " OFF " : " ?2  ");
			if (rails[i].out) {
				puthex(o, 2);
				puts_(" ");
				gq_dec(200 + o * 25 / 4);
				puts_(" mV");
			}
		}
		color = C_FG;
		newline();
	}
	for (int i = 0; i < 4; i++) {
		u8 v = 0;
		int e = gq_pmic_read(sels[i].reg, &v);
		puts_("  ");
		puts_(sels[i].name);
		if (e) {
			puts_("error ");
			puthex((u32)-e, 2);
		} else {
			puthex(v, 2);
		}
		newline();
	}
	say(C_DIM, "BUCK2S pin = PCTRLSEL1[7:4], 7 = G3D_EN");
}

static void __attribute__((noreturn)) diag_gpu_query(u64 dtb)
{
	static const char names[14][8] = {
		"MIF   ", "INT   ", "CPUCL0", "CPUCL1", "CPUCL2", "G3D   ", "G3DL2 ",
		"TPU   ", "INTCAM", "TNR   ", "CAM   ", "MFC   ", "DISP  ", "BO    ",
	};
	watchdogs_off(dtb, 0);
	say(C_WARN, "GPU CLOCK QUERY (read-only, shimgpuq)");
	say(C_DIM, "power domains (config / status):");
	kv2("  pd-g3d          ", rd32(GQ_PD_G3D), " / ", rd32(GQ_PD_G3D + 4));
	kv2("  pd-embedded_g3d ", rd32(GQ_PD_EMBEDDED_G3D), " / ", rd32(GQ_PD_EMBEDDED_G3D + 4));
	say(C_DIM, "ACPM DVFS FREQ_GET (kHz):");
	for (u32 d = 0; d < 14; d++) {
		u32 khz = 0;
		int e = acpm_dvfs(d, 0, &khz);
		color = d == 5 || d == 6 ? C_WARN : C_FG;
		puts_("  ");
		gq_dec(d);
		puts_(d < 10 ? "  " : " ");
		puts_(names[d]);
		puts_(" ");
		if (e) {
			puts_("error ");
			puthex((u32)-e, 2);
		} else {
			gq_dec(khz);
		}
		color = C_FG;
		newline();
	}
	say(C_DIM, "G3D expected 151000-848000, G3DL2 302000-996000");
	gq_rails();
	say(C_DIM, "photo this screen; nothing was changed");
	halt("done");
}
