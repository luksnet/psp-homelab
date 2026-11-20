	.set noreorder

#include "pspstub.s"

	STUB_START "Backlight",0x40090000,0x00020005
	STUB_FUNC  0xE6A17C4F,backlight_set
	STUB_FUNC  0x4827917D,backlight_get
	STUB_END
