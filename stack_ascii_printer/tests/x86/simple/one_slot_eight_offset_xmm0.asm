# RUN: print_ascii_stack.py -i %s -f main -c 1 -a x86 | filecheck %s

# CHECK:      0x0  : ||xmm0-----|| : -0x8
# CHECK-NEXT: 0x8  : ||rbp------|| : 0x0
# CHECK-NEXT: 0x10 : ||---------|| : 0x8
# CHECK-NEXT:
# CHECK-NEXT: rsp: 0x0
# CHECK-NEXT: rbp: 0x8

0000000000501040 main:
  501020:	55                   	push   rbp
  501041:	48 89 e5             	mov    rbp,rsp
  501057:	48 81 ec 90 00 00 00 	sub    rsp,0x8
  501068:	48 89 75 d8          	movsd  XMMWORD PTR [rbp-0x8],xmm0
