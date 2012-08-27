/**
 * Enhanced Seccomp Architecture/Machine Specific Code
 *
 * Copyright (c) 2012 Red Hat <pmoore@redhat.com>
 * Author: Paul Moore <pmoore@redhat.com>
 */

/*
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of version 2.1 of the GNU Lesser General Public License as
 * published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses>.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <asm/bitsperlong.h>
#include <linux/audit.h>

#include <seccomp.h>

#include "arch.h"
#include "arch-i386.h"
#include "arch-x86_64.h"
#include "system.h"

const struct arch_def arch_def_native = {
#if __i386__
	.token = AUDIT_ARCH_I386,
#elif __x86_64__
	.token = AUDIT_ARCH_X86_64,
#else
#error the arch code needs to know about your machine type
#endif /* machine type guess */

#if __BITS_PER_LONG == 32
	.size = ARCH_SIZE_32,
#elif __BITS_PER_LONG == 64
	.size = ARCH_SIZE_64,
#else
	.size = ARCH_SIZE_UNSPEC,
#endif /* BITS_PER_LONG */

#if __BYTE_ORDER == __LITTLE_ENDIAN
	.endian = ARCH_ENDIAN_LITTLE,
#elif __BYTE_ORDER == __BIG_ENDIAN
	.endian = ARCH_ENDIAN_BIG,
#else
	.endian = ARCH_ENDIAN_UNSPEC,
#endif /* __BYTE_ORDER */
};

/**
 * Lookup the syscall table for an architecture
 * @param token the architecure token
 *
 * Return the architecture's syscall table, returns NULL on failure.
 *
 */
const struct arch_syscall_def *_arch_def_lookup(uint32_t token)
{
	switch (token) {
	case AUDIT_ARCH_I386:
		return i386_syscall_table;
		break;
	case AUDIT_ARCH_X86_64:
		return x86_64_syscall_table;
		break;
	}

	return NULL;
}

/**
 * Determine the maximum number of syscall arguments
 * @param arch the architecture definition
 *
 * Determine the maximum number of syscall arguments for the given architecture.
 * Returns the number of arguments on success, negative values on failure.
 *
 */
int arch_arg_count_max(const struct arch_def *arch)
{
	switch (arch->token) {
	case AUDIT_ARCH_I386:
		return i386_arg_count_max;
	case AUDIT_ARCH_X86_64:
		return x86_64_arg_count_max;
	default:
		return -EDOM;
	}
}

/**
 * Determine the argument offset for the lower 32 bits
 * @param arch the architecture definition
 * @param arg the argument number
 *
 * Determine the correct offset for the low 32 bits of the given argument based
 * on the architecture definition.  Returns the offset on success, negative
 * values on failure.
 *
 */
int arch_arg_offset_lo(const struct arch_def *arch, unsigned int arg)
{
	switch (arch->token) {
	case AUDIT_ARCH_X86_64:
		return x86_64_arg_offset_lo(arg);
	default:
		return -EDOM;
	}
}

/**
 * Determine the argument offset for the high 32 bits
 * @param arch the architecture definition
 * @param arg the argument number
 *
 * Determine the correct offset for the high 32 bits of the given argument
 * based on the architecture definition.  Returns the offset on success,
 * negative values on failure.
 *
 */
int arch_arg_offset_hi(const struct arch_def *arch, unsigned int arg)
{
	switch (arch->token) {
	case AUDIT_ARCH_X86_64:
		return x86_64_arg_offset_hi(arg);
	default:
		return -EDOM;
	}
}

/**
 * Resolve a syscall name to a number
 * @param arch the architecture definition
 * @param name the syscall name
 *
 * Resolve the given syscall name to the syscall number based on the given
 * architecture.  Returns the syscall number on success, including negative
 * pseudo syscall numbers; returns -1 on failure.
 *
 */
int arch_syscall_resolve_name(const struct arch_def *arch, const char *name)
{
	unsigned int iter;
	const struct arch_syscall_def *table;

	table = _arch_def_lookup(arch->token);
	if (table == NULL)
		return __NR_SCMP_ERROR;

	/* XXX - plenty of room for future improvement here */
	for (iter = 0; table[iter].name != NULL; iter++) {
		if (strcmp(name, table[iter].name) == 0)
			return table[iter].num;
	}

	return __NR_SCMP_ERROR;
}

/**
 * Resolve a syscall number to a name
 * @param arch the architecture definition
 * @param num the syscall number
 *
 * Resolve the given syscall number to the syscall name based on the given
 * architecture.  Returns a pointer to the syscall name string on success,
 * including pseudo syscall names; returns NULL on failure.
 *
 */
const char *arch_syscall_resolve_num(const struct arch_def *arch, int num)
{
	unsigned int iter;
	const struct arch_syscall_def *table;

	table = _arch_def_lookup(arch->token);
	if (table == NULL)
		return NULL;

	/* XXX - plenty of room for future improvement here */
	for (iter = 0; table[iter].num != __NR_SCMP_ERROR; iter++) {
		if (num == table[iter].num)
			return table[iter].name;
	}

	return NULL;
}

/**
 * Translate the syscall number
 * @param arch the architecture definition
 * @param syscall the syscall number
 *
 * Translate the syscall number, in the context of the native architecure, to
 * the provided architecure.  Returns zero on success, negative values on
 * failure.
 *
 */
int arch_syscall_translate(const struct arch_def *arch, int *syscall)
{
	int sc_num;
	const char *sc_name;

	if (arch->token != arch_def_native.token) {
		sc_name = arch_syscall_resolve_num(&arch_def_native, *syscall);
		if (sc_name == NULL)
			return -EFAULT;

		sc_num = arch_syscall_resolve_name(arch, sc_name);
		if (sc_num == __NR_SCMP_ERROR)
			return -EFAULT;

		*syscall = sc_num;
	}

	return 0;
}

/**
 * Rewrite a syscall value to match the architecture
 * @param arch the architecture definition
 * @param syscall the syscall number
 *
 * Syscalls can vary across different architectures so this function rewrites
 * the syscall into the correct value for the specified architecture.  Returns
 * zero on success, negative values on failure.
 *
 */
int arch_syscall_rewrite(const struct arch_def *arch, int *syscall)
{
	switch (arch->token) {
	case AUDIT_ARCH_I386:
		return i386_syscall_rewrite(arch, syscall);
	default:
		return -EDOM;
	}
}

/**
 * Rewrite a filter rule to match the architecture specifics
 * @param arch the architecture definition
 * @param strict strict flag
 * @param syscall the syscall number
 * @param chain the argument filter chain
 *
 * Syscalls can vary across different architectures so this function handles
 * the necessary seccomp rule rewrites to ensure the right thing is done
 * regardless of the rule or architecture.  If @strict is true then the
 * function will fail if the entire filter can not be preservered, however,
 * if @strict is false the function will do a "best effort" rewrite and not
 * fail.  Returns zero on success, negative values on failure.
 *
 */
int arch_filter_rewrite(const struct arch_def *arch,
			unsigned int strict,
			int *syscall, struct db_api_arg *chain)
{
	switch (arch->token) {
	case AUDIT_ARCH_I386:
		return i386_filter_rewrite(arch, strict, syscall, chain);
	default:
		return -EDOM;
	}
}
