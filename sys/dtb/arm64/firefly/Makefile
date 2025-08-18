#	$NetBSD: Makefile,v 1.3 2021/11/07 17:10:46 jmcneill Exp $

DTSSUBDIR=	firefly
DTSMAKEVARS=	CONFIG_ARCH_FIREFLY=y
DTSFILESCMD=	${MAKE} -C ${ARCHDTSDIR}/${DTSSUBDIR} ${DTSMAKEVARS} -v dtb-y
DTS=		${DTSFILESCMD:sh}

.include <bsd.dtb.mk>
