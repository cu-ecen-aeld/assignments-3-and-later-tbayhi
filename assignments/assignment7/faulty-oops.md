# Kernel Oops in Faulty Module
Executing the command `echo "hello_world" > /dev/faulty` produces the following error message in the `buildroot` environment:

```
Welcome to Buildroot
buildroot login: root
Password: 
# echo "hello_world" > /dev/faulty
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
Mem abort info:
  ESR = 0x96000045
  EC = 0x25: DABT (current EL), IL = 32 bits
  SET = 0, FnV = 0
  EA = 0, S1PTW = 0
  FSC = 0x05: level 1 translation fault
Data abort info:
  ISV = 0, ISS = 0x00000045
  CM = 0, WnR = 1
user pgtable: 4k pages, 39-bit VAs, pgdp=000000004205b000
[0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
Internal error: Oops: 96000045 [#1] SMP
Modules linked in: hello(O) faulty(O) scull(O)
CPU: 0 PID: 156 Comm: sh Tainted: G           O      5.15.18 #1
Hardware name: linux,dummy-virt (DT)
pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
pc : faulty_write+0x14/0x20 [faulty]
lr : vfs_write+0xa8/0x2b0
sp : ffffffc008d03d80
x29: ffffffc008d03d80 x28: ffffff80020df2c0 x27: 0000000000000000
x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
x23: 0000000040001000 x22: 000000000000000c x21: 000000558d6a2a70
x20: 000000558d6a2a70 x19: ffffff80020c2900 x18: 0000000000000000
x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
x5 : 0000000000000001 x4 : ffffffc0006f7000 x3 : ffffffc008d03df0
x2 : 000000000000000c x1 : 0000000000000000 x0 : 0000000000000000
Call trace:
 faulty_write+0x14/0x20 [faulty]
 ksys_write+0x68/0x100
 __arm64_sys_write+0x20/0x30
 invoke_syscall+0x54/0x130
 el0_svc_common.constprop.0+0x44/0xf0
 do_el0_svc+0x40/0xa0
 el0_svc+0x20/0x60
 el0t_64_sync_handler+0xe8/0xf0
 el0t_64_sync+0x1a0/0x1a4
Code: d2800001 d2800000 d503233f d50323bf (b900003f) 
---[ end trace 81b99b0b9d806543 ]---
```

## Analysis of the Oops Message

### What Happened?
The first line of the oops message describes the error that occurred.

`Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000`

The invalid instruction was an attempt to dereference a NULL pointer.

### Who Caused It?
The kernel oops message contains the value of the CPU registers at the time of the exception.  The Program Counter (PC) register will hold the memory address of the instruction that caused the error.  It can be seen here that the memory address in the PC register is within the `faulty_write` function:

```
pc : faulty_write+0x14/0x20 [faulty]
```

The call trace also points to the `faulty_write` function at the top of the stack:
```
Call trace:
 faulty_write+0x14/0x20 [faulty]
 ksys_write+0x68/0x100
 __arm64_sys_write+0x20/0x30
 invoke_syscall+0x54/0x130
...
```

### What is the Offending Instruction?
Both the Program Counter and the call trace point us to the answer.  The string `faulty_write+0x14/0x20` tells us that the instruction within `faulty_write` that was executing was the instruction that is `0x14` (20 in decimal) bytes into the function.

We can use `objdump` to find the instruction at that location within `faulty_write`:


```
$ aarch64-none-linux-gnu-objdump -d faulty.ko

faulty.ko:     file format elf64-littleaarch64


Disassembly of section .text:

0000000000000000 <faulty_write>:
   0:	d503245f 	bti	c
   4:	d2800001 	mov	x1, #0x0                   	// #0
   8:	d2800000 	mov	x0, #0x0                   	// #0
   c:	d503233f 	paciasp
  10:	d50323bf 	autiasp
  14:	b900003f 	str	wzr, [x1]
  18:	d65f03c0 	ret
  1c:	d503201f 	nop
...
```
At offset `0x14` is the instruction `str wzr, [x1]`.  This instruction is attempting to copy the contents of the memory address stored in `x1` into the `wxr` register; however, at offset `0x4` we can see that all zeros were copied into `x1`.  Fixing the code that generated the instruction at `0x4` should resolve the issue.
