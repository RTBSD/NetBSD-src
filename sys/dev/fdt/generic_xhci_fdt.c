/* $NetBSD: generic_xhci_fdt.c,v 1.20 2022/06/12 08:04:07 skrll Exp $ */

/*-
 * Copyright (c) 2018 Jared McNeill <jmcneill@invisible.ca>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: generic_xhci_fdt.c,v 1.20 2022/06/12 08:04:07 skrll Exp $");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/intr.h>
#include <sys/systm.h>
#include <sys/kernel.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usb_mem.h>
#include <dev/usb/xhcireg.h>
#include <dev/usb/xhcivar.h>

#include <dev/fdt/fdtvar.h>

static int	generic_xhci_fdt_match(device_t, cfdata_t, void *);
static void	generic_xhci_fdt_attach(device_t, device_t, void *);

CFATTACH_DECL2_NEW(xhci_fdt, sizeof(struct xhci_softc),
	generic_xhci_fdt_match, generic_xhci_fdt_attach, NULL,
	xhci_activate, NULL, xhci_childdet);

#define	RD4(sc, reg)				\
	bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg))
#define	WR4(sc, reg, val)			\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define	SET4(sc, reg, mask)			\
	WR4((sc), (reg), RD4((sc), (reg)) | (mask))
#define	CLR4(sc, reg, mask)			\
	WR4((sc), (reg), RD4((sc), (reg)) & ~(mask))

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "generic-xhci" },
	DEVICE_COMPAT_EOL
};

static int
generic_xhci_fdt_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
generic_xhci_fdt_attach(device_t parent, device_t self, void *aux)
{
	struct xhci_softc * const sc = device_private(self);
	struct fdt_attach_args * const faa = aux;
	const int phandle = faa->faa_phandle;
#if 0
	struct fdtbus_reset *rst;
	struct fdtbus_phy *phy;
	struct clk *clk;
#endif
	char intrstr[128];
	bus_addr_t addr;
	bus_size_t size;
	uint32_t hccparams;
	int error;
	void *ih;
#if 0
	u_int n;
#endif

	if (fdtbus_get_reg(phandle, 0, &addr, &size) != 0) {
		aprint_error(": couldn't get registers\n");
		return;
	}

#if 0
	/* Enable clocks */
	for (n = 0; (clk = fdtbus_clock_get_index(phandle, n)) != NULL; n++)
		if (clk_enable(clk) != 0) {
			aprint_error(": couldn't enable clock #%d\n", n);
			return;
		}
	/* De-assert resets */
	for (n = 0; (rst = fdtbus_reset_get_index(phandle, n)) != NULL; n++)
		if (fdtbus_reset_deassert(rst) != 0) {
			aprint_error(": couldn't de-assert reset #%d\n", n);
			return;
		}

	/* Enable optional phy */
	phy = fdtbus_phy_get(phandle, "usb");
	if (phy && fdtbus_phy_enable(phy, true) != 0) {
		aprint_error(": couldn't enable phy\n");
		return;
	}
#endif

	sc->sc_dev = self;
	sc->sc_bus.ub_hcpriv = sc;
	sc->sc_bus.ub_revision = USBREV_3_0;
	sc->sc_ios = size;
	sc->sc_iot = faa->faa_bst;
	if (bus_space_map(sc->sc_iot, addr, size, 0, &sc->sc_ioh) != 0) {
		aprint_error(": couldn't map registers\n");
		return;
	}

	hccparams = bus_space_read_4(sc->sc_iot, sc->sc_ioh, XHCI_HCCPARAMS);
	if (XHCI_HCC_AC64(hccparams)) {
		aprint_verbose_dev(self, "64-bit DMA");
	} else {
		aprint_verbose_dev(self, "32-bit DMA\n");
	}
	sc->sc_bus.ub_dmatag = faa->faa_dmat;

	if (!fdtbus_intr_str(phandle, 0, intrstr, sizeof(intrstr))) {
		aprint_error_dev(self, "failed to decode interrupt\n");
		return;
	}

	ih = fdtbus_intr_establish_xname(phandle, 0, IPL_USB,
	    FDT_INTR_MPSAFE, xhci_intr, sc, device_xname(self));
	if (ih == NULL) {
		aprint_error_dev(self, "couldn't establish interrupt on %s\n",
		    intrstr);
		return;
	}
	aprint_normal_dev(self, "interrupting on %s\n", intrstr);

	error = xhci_init(sc);
	if (error) {
		aprint_error_dev(self, "init failed, error = %d\n", error);
		return;
	}

	pmf_device_register1(self, NULL, NULL, xhci_shutdown);

	sc->sc_child = config_found(self, &sc->sc_bus, usbctlprint, CFARGS_NONE);
	sc->sc_child2 = config_found(self, &sc->sc_bus2, usbctlprint,
	    CFARGS_NONE);
}
