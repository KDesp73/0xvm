// SPDX-License-Identifier: MIT
#ifndef TESTS_H
#define TESTS_H

/* Runs the VM test cases and returns the number of failed checks. */
int test_vm(void);
/* Runs the assembly parser test cases and returns the number of failed checks. */
int test_asm(void);
/* Runs the VMIM image/loader test cases and returns the number of failed checks. */
int test_rom(void);
/* Runs the device-layer test cases and returns the number of failed checks. */
int test_device(void);
/* Runs the executable conformance ROM and returns the number of failed checks. */
int test_conformance(void);
/* Runs the disassembler test cases and returns the number of failed checks. */
int test_dis(void);
/* Runs the shared host-device test cases and returns the number of failed checks. */
int test_host(void);
/* Runs the typography demo editing checks and returns the number of failed checks. */
int test_typography(void);

#endif /* TESTS_H */
