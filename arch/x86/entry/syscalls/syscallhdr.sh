#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

in="$1"
out="$2"
my_abis=`echo "($3)" | tr ',' '|'`
prefix="$4"
offset="$5"

syno_syscalls()
{
cat << SYNO_SYSTEM_CALLS

#ifndef __KERNEL__
#include <bits/wordsize.h>

#define syno_utime(arg1, arg2)                          syscall(__NR_syno_utime, arg1, arg2)

#define syno_archive_bit(arg1, arg2)                    syscall(__NR_syno_archive_bit, arg1, arg2)

#define syno_recv_file(arg1, arg2, arg3, arg4, arg5)    syscall(__NR_syno_recv_file, arg1, arg2, arg3, arg4, arg5)

#define syno_mtd_alloc(arg1)                            syscall(__NR_syno_mtd_alloc, arg1)

#define syno_ecrypt_name(arg1, arg2)                    syscall(__NR_syno_ecrypt_name, arg1, arg2)
#define syno_decrypt_name(arg1, arg2, arg3)             syscall(__NR_syno_decrypt_name, arg1, arg2, arg3)

#define syno_acl_check_perm(arg1, arg2)                 syscall(__NR_syno_acl_check_perm, arg1, arg2)
#define syno_acl_is_support(arg1, arg2, arg3)           syscall(__NR_syno_acl_is_support, arg1, arg2, arg3)
#define syno_acl_get_perm(arg1, arg2)                   syscall(__NR_syno_acl_get_perm, arg1, arg2)

#define syno_flush_aggregate(arg1)                      syscall(__NR_syno_flush_aggregate, arg1)

#if __WORDSIZE == 64
#define syno_stat(arg1, arg2, arg3)                     syscall(__NR_syno_stat, arg1, arg2, arg3)
#define syno_fstat(arg1, arg2, arg3)                    syscall(__NR_syno_fstat, arg1, arg2, arg3)
#define syno_lstat(arg1, arg2, arg3)                    syscall(__NR_syno_lstat, arg1, arg2, arg3)
#define syno_caseless_stat(arg1, arg2)                  syscall(__NR_syno_caseless_stat, arg1, arg2)
#define syno_caseless_lstat(arg1, arg2)                 syscall(__NR_syno_caseless_lstat, arg1, arg2)
#endif /* __WORDSIZE == 64 */

#define syno_notify_init(arg1)                          syscall(__NR_syno_notify_init, arg1)
#define syno_notify_add_watch(arg1, arg2, arg3)         syscall(__NR_syno_notify_add_watch, arg1, arg2, arg3)
#define syno_notify_remove_watch(arg1, arg2, arg3)      syscall(__NR_syno_notify_remove_watch, arg1, arg2, arg3)
#define syno_notify_add_watch32(arg1, arg2, arg3)       syscall(__NR_syno_notify_add_watch32, arg1, arg2, arg3)
#define syno_notify_remove_watch32(arg1, arg2, arg3)    syscall(__NR_syno_notify_remove_watch32, arg1, arg2, arg3)

#define syno_archive_overwrite(arg1, arg2)              syscall(__NR_syno_archive_overwrite, arg1, arg2)
#endif

SYNO_SYSTEM_CALLS
}

fileguard=_ASM_X86_`basename "$out" | sed \
    -e 'y/abcdefghijklmnopqrstuvwxyz/ABCDEFGHIJKLMNOPQRSTUVWXYZ/' \
    -e 's/[^A-Z0-9_]/_/g' -e 's/__/_/g'`
grep -E "^[0-9A-Fa-fXx]+[[:space:]]+${my_abis}" "$in" | sort -n | (
    echo "#ifndef ${fileguard}"
    echo "#define ${fileguard} 1"
    echo ""

    max=0
    while read nr abi name entry ; do
	if [ -z "$offset" ]; then
	    echo "#define __NR_${prefix}${name} $nr"
	else
	    echo "#define __NR_${prefix}${name} ($offset + $nr)"
        fi

	max=$nr
    done

    syno_syscalls

    echo ""
    echo "#ifdef __KERNEL__"
    echo "#define __NR_${prefix}syscall_max $max"
    echo "#endif"
    echo ""
    echo "#endif /* ${fileguard} */"
) > "$out"
