#ifndef TESTS_H
#define TESTS_H

/* Runs the VM test cases and returns the number of failed checks. */
int test_vm(void);
/* Runs the assembly parser test cases and returns the number of failed checks. */
int test_asm(void);
/* Runs the VMIM image/loader test cases and returns the number of failed checks. */
int test_rom(void);

#endif /* TESTS_H */
