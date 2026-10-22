#!/usr/bin/env python3
"""Sanitized host checks of the actual policy functions in the exported patch."""
import errno
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


def added_source(path):
    patch = (ROOT / 'kernel/0001-bluejay-port.patch').read_text()
    section = patch.split(f'diff --git a/{path} b/{path}\n', 1)[1].split('\ndiff --git ', 1)[0]
    return '\n'.join(line[1:] for line in section.splitlines()
                      if line.startswith('+') and not line.startswith('+++'))


def added_function(path, name):
    added = added_source(path)
    start = added.index(f'static int\n{name}(')
    brace = added.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (added[end] == '{') - (added[end] == '}')
        end += 1
    return added[start:end]


COMMON = r'''
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <stdlib.h>
'''

ENV_PRE = r'''
#undef CAST_USER_ADDR_T
#define CAST_USER_ADDR_T(x) ((uintptr_t)(x))
#define UIO_SYSSPACE 0
#define TRUE 1
struct image_params { char *ip_strings; int ip_argspace; int ip_envc; };
static const char *spec_input;
static char values[8][128];
static int count, fail_string;
static int PE_parse_boot_argn(const char *key, char *dst, size_t size) {
    (void)key;
    snprintf(dst, size, "%s", spec_input);
    return spec_input[0] != 0;
}
static int exec_add_user_string(struct image_params *p, uintptr_t v, int a, int b) {
    (void)p; (void)a; (void)b;
    if (fail_string) return EFAULT;
    assert(count < 8);
    snprintf(values[count++], 128, "%s", (char *)v);
    return 0;
}
'''

ENV_MAIN = r'''
int main(int argc, char **argv) {
    assert(argc == 8);
    spec_input = argv[1];
    int space = atoi(argv[5]);
    fail_string = atoi(argv[7]);
    struct image_params p = {argv[2], space, 0};
    int result = sweet_exec_env(&p, 8);
    assert(result == atoi(argv[6]));
    assert(count == atoi(argv[3]));
    if (result == 0) {
        assert(p.ip_envc == count);
        assert(p.ip_argspace == space - 8 * count);
    }
    if (count) assert(strcmp(values[0], argv[4]) == 0);
    return 0;
}
'''

AMFI_PRE = r'''
#ifndef ENOPOLICY
#define ENOPOLICY 103
#endif
#ifndef __unused
#define __unused __attribute__((unused))
#endif
#ifndef __APPLE__
typedef uintptr_t user_addr_t;
#endif
struct proc { int unused; };
static uintptr_t input_ptr, output_ptr;
static int fail_input, fail_output;
static int copyin(uintptr_t a, void *b, size_t n) {
    if (fail_input || a != input_ptr) return EFAULT;
    memcpy(b, (void *)a, n); return 0;
}
static int copyout(const void *a, uintptr_t b, size_t n) {
    if (fail_output || b != output_ptr) return EFAULT;
    memcpy((void *)b, a, n); return 0;
}
'''

AMFI_MAIN = r'''
int main(void) {
    uint64_t result = 0, args[2] = {0, (uintptr_t)&result};
    input_ptr = (uintptr_t)args; output_ptr = (uintptr_t)&result;
    assert(sweet_amfi_syscall(0, 0x5a, input_ptr) == 0);
    assert(result == 0x7b);
    assert(sweet_amfi_syscall(0, 0x59, input_ptr) == ENOPOLICY);
    fail_input = 1;
    assert(sweet_amfi_syscall(0, 0x5a, input_ptr) == EFAULT);
    fail_input = 0; args[1] = 0;
    assert(sweet_amfi_syscall(0, 0x5a, input_ptr) == EFAULT);
    args[1] = output_ptr; fail_output = 1;
    assert(sweet_amfi_syscall(0, 0x5a, input_ptr) == EFAULT);
    return 0;
}
'''


class KernelPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        (ROOT / 'out').mkdir(exist_ok=True)
        cls.temp = tempfile.TemporaryDirectory(prefix='policy-test-', dir=ROOT / 'out')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binaries = {}
        for key, path, name, pre, main in [
            ('env', 'bsd/kern/kern_exec.c', 'sweet_exec_env', ENV_PRE, ENV_MAIN),
            ('amfi', 'bsd/kern/sweet_amfi.c', 'sweet_amfi_syscall', AMFI_PRE, AMFI_MAIN),
        ]:
            source = Path(cls.temp.name) / (key + '.c')
            if key == 'amfi':
                pre += next(line for line in added_source(path).splitlines()
                            if line.startswith('#define SWEET_AMFI_DYLD_FLAGS')) + '\n'
            source.write_text(COMMON + pre + added_function(path, name) + main)
            binary = source.with_suffix('')
            subprocess.run([os.environ.get('CC', 'clang'), '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', str(source), '-o', str(binary)], check=True)
            cls.binaries[key] = str(binary)

    def test_environment_selection_and_bounds(self):
        cases = [
            ('backboardd:A=1,B=2;SpringBoard:C=3', '/usr/libexec/backboardd', 2, 'A=1'),
            ('backboardd:A=1;SpringBoard:C=3', '/SpringBoard', 1, 'C=3'),
            ('backboardd:A=1', '/backboardd-helper', 0, ''),
            ('bad;SpringBoard:C=3', '/SpringBoard', 1, 'C=3'),
            ('backboardd:;SpringBoard:C=3', '/backboardd', 0, ''),
            ('backboardd:' + 'X' * 128 + ',B=2', '/backboardd', 1, 'B=2'),
            ('', '/backboardd', 0, ''),
        ]
        for spec, path, count, first in cases:
            with self.subTest(spec=spec, path=path):
                subprocess.run([self.binaries['env'], spec, 'executable_path=' + path,
                                str(count), first, '4096', '0', '0'], check=True,
                               capture_output=True)

    def test_environment_errors(self):
        for space, error, fail, count in [('0', str(errno.E2BIG), '0', '1'),
                                          ('4096', str(errno.EFAULT), '1', '0')]:
            with self.subTest(space=space, fail=fail):
                subprocess.run([self.binaries['env'], 'backboardd:A=1',
                                'executable_path=/backboardd', count, 'A=1',
                                space, error, fail], check=True, capture_output=True)

    def test_amfi_policy_and_copy_errors(self):
        subprocess.run([self.binaries['amfi']], check=True, capture_output=True)


if __name__ == '__main__':
    unittest.main()
