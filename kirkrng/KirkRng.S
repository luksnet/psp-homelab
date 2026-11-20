	.set noreorder

#include "pspstub.s"

	STUB_START "KirkRng",0x40090000,0x00010005
	STUB_FUNC  0x27738106,kirkrng_get
	STUB_END
