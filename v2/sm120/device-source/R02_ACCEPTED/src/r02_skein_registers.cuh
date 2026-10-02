// Derived mechanically from qualified upstream_skein.cuh.
// Round helpers already defined by upstream_skein.cuh; only IO is changed.
#pragma once
__device__ __forceinline__ void r02_skein_registers(uint2 hio[4]) {
const uint2 skein_ks_parity = { 0xA9FC1A22, 0x1BD11BDA };
		const uint2 t12[6] = {
			{ 0x20, 0 },
			{ 0,    0xf0000000 },
			{ 0x20, 0xf0000000 },
			{ 0x08, 0 },
			{ 0,    0xff000000 },
			{ 0x08, 0xff000000 }
		};

		uint2 h[9] = {
			{ 0x2FDB3E13, 0xCCD044A1 },
			{ 0x1A79A9EB, 0xE8359030 },
			{ 0x4F816E6F, 0x55AEA061 },
			{ 0xAE9B94DB, 0x2A2767A4 },
			{ 0x74DD7683, 0xEC06025E },
			{ 0xC4746251, 0xE7A436CD },
			{ 0x393AD185, 0xC36FBAF9 },
			{ 0x33EDFC13, 0x3EEDBA18 },
			{ 0xC73A4E2A, 0xB69D3CFC }
		};
		uint2 dt0,dt1,dt2,dt3;
		uint2 p0, p1, p2, p3, p4, p5, p6, p7;

		dt0=hio[0];
		dt1=hio[1];
		dt2=hio[2];
		dt3=hio[3];

		p0 = h[0] + dt0;
		p1 = h[1] + dt1;
		p2 = h[2] + dt2;
		p3 = h[3] + dt3;
		p4 = h[4];
		p5 = h[5] + t12[0];
		p6 = h[6] + t12[1];
		p7 = h[7];

		// forced unroll required
		Round_8_512v35(h, t12, p0, p1, p2, p3, p4, p5, p6, p7, 1);
		Round_8_512v35(h, t12, p0, p1, p2, p3, p4, p5, p6, p7, 3);
		Round_8_512v35(h, t12, p0, p1, p2, p3, p4, p5, p6, p7, 5);
		Round_8_512v35(h, t12, p0, p1, p2, p3, p4, p5, p6, p7, 7);
		Round_8_512v35(h, t12, p0, p1, p2, p3, p4, p5, p6, p7, 9);
		Round_8_512v35(h, t12, p0, p1, p2, p3, p4, p5, p6, p7, 11);
		Round_8_512v35(h, t12, p0, p1, p2, p3, p4, p5, p6, p7, 13);
		Round_8_512v35(h, t12, p0, p1, p2, p3, p4, p5, p6, p7, 15);
		Round_8_512v35(h, t12, p0, p1, p2, p3, p4, p5, p6, p7, 17);

		p0 ^= dt0;
		p1 ^= dt1;
		p2 ^= dt2;
		p3 ^= dt3;

		h[0] = p0;
		h[1] = p1;
		h[2] = p2;
		h[3] = p3;
		h[4] = p4;
		h[5] = p5;
		h[6] = p6;
		h[7] = p7;
		h[8] = skein_ks_parity ^ h[0] ^ h[1] ^ h[2] ^ h[3] ^ h[4] ^ h[5] ^ h[6] ^ h[7];

		const uint2 *t = t12+3;
		p5 += t12[3];  //p5 already equal h[5]
		p6 += t12[4];

		// forced unroll
		Round_8_512v35(h, t, p0, p1, p2, p3, p4, p5, p6, p7, 1);
		Round_8_512v35(h, t, p0, p1, p2, p3, p4, p5, p6, p7, 3);
		Round_8_512v35(h, t, p0, p1, p2, p3, p4, p5, p6, p7, 5);
		Round_8_512v35(h, t, p0, p1, p2, p3, p4, p5, p6, p7, 7);
		Round_8_512v35(h, t, p0, p1, p2, p3, p4, p5, p6, p7, 9);
		Round_8_512v35(h, t, p0, p1, p2, p3, p4, p5, p6, p7, 11);
		Round_8_512v35(h, t, p0, p1, p2, p3, p4, p5, p6, p7, 13);
		Round_8_512v35(h, t, p0, p1, p2, p3, p4, p5, p6, p7, 15);
		Round_8_512v35_final(h, t, p0, p1, p2, p3, p4, p5, p6, p7);

		
 hio[0]=p0;hio[1]=p1;hio[2]=p2;hio[3]=p3;
}
