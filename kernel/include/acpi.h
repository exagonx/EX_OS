/* =============================================================================
 * kernel/include/acpi.h — the ACPI pieces needed to power off and reboot
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * See kernel/arch/x86/acpi.c. Not a general ACPI implementation: the FADT
 * (power-management ports, the reset register) and the S5 values from the
 * DSDT, which is what turning a real PC off takes.
 * ============================================================================= */
#ifndef EXOS_ACPI_H
#define EXOS_ACPI_H

/* Reads the tables and logs what it found. Interrupts on (it reads through
 * the paging window). Harmless where there is no ACPI. */
void acpi_init(void);

/* S5 ("soft off"). Returns only if the machine is still on. */
void acpi_spegni(void);

/* The FADT reset register, when the firmware declares one. Returns only if
 * the machine did not reset. */
int  acpi_ha_reset(void);     /* the FADT declares a reset register */
void acpi_riavvia(void);

#endif /* EXOS_ACPI_H */
