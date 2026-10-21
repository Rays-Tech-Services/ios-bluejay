/* Exact format/state observed on this Pixel in IMG_5925. Fail closed on
 * another layout. Only bit 1 of slot A's flags byte may change. */
static int slot_a_success_word(u32 magic, u32 version, u32 a, u32 b, u32 *out)
{
	if (magic != 0x49564544 || version != 0x000f0003 || b != 0x00000800 ||
	    (a & ~0x00000203u) != 0x00000c00 || (a & 0xff) > 3)
		return 0;
	*out = a | 0x00000200;
	return 1;
}
