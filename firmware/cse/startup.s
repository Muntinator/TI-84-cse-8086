;; Munt386-CSE -- Z80 bare-metal bootstrap (SDCC asdcc/asdc syntax).
;;
;; Performs the hardware bring-up that the C startup cannot do itself:
;;   DI, IM 1, memory-timer speed, high bank bits clear, bank layout,
;;   stack in RAM, CPU speed 15 MHz, ON-key interrupt enable, then call
;;   cse_startup() (C).
;;
;; Register values follow the sequence documented in docs/CSE_HARDWARE.md
;; and firmware/cse/cse_hardware.h (verified against the KnightOS kernel
;; sources; VERIFY items are annotated inline).
;;
;; Assembled by SDCC (asdc) as part of `make cse` and linked ahead of the
;; C runtime so this code runs from the reset vector.

	.module munt386_boot

	;; C entry point (defined in firmware/cse/startup.c).  ASxxxx requires
	;; used-but-not-defined symbols to be declared .globl so they become
	;; external references resolved at link time.
	.globl _cse_startup

	;; ports (duplicate of cse_hardware.h for the assembler)
	PORT_KEYPAD	= 0x01
	PORT_INT_TRIG	= 0x04
	PORT_BANKB	= 0x07
	PORT_MEMA_HIGH	= 0x0E
	PORT_MEMB_HIGH	= 0x0F
	PORT_CPUSPEED	= 0x20
	PORT_INT_MASK	= 0x03
	INT_ON		= 0x01
	BANKB_ISRAM_CPU15 = 0x80
	;; Port 0x20 takes the speed INDEX (0 = 6 MHz, 1 = 15 MHz), not a bitmask.
	;; 15 MHz is the highest stable software-selectable speed: register values
	;; 2/3 were meant for 20/25 MHz but left unimplemented before production
	;; (~15.0 MHz measured, different delay states, ignored by newer ASICs).
	CPUSPEED_6MHZ	= 0x00
	CPUSPEED_15MHZ	= 0x01

	.area _HEADER (ABS)
	.org 0x0000
startup_reset:
	di
	im 1
	jp boot_c

	;; The SDCC C runtime lives in the normal code area; the reset entry
	;; simply falls through to it after the Z80 bring-up.

boot_c:
	;; memory-timer speed (mask index 1)
	ld a, #0x04
	out (PORT_INT_TRIG), a

	;; clear high bank bits
	xor a
	out (PORT_MEMA_HIGH), a
	out (PORT_MEMB_HIGH), a

	;; bank layout: 0x0000 flash p0, 0x4000 flash p1, RAM pages above
	ld a, #0x01 | BANKB_ISRAM_CPU15
	out (PORT_BANKB), a

	;; stack in the RAM window (top of the SDCC heap arena)
	ld sp, #0xDA00

	;; CPU to 15 MHz (highest stable speed: port 0x20 takes the speed INDEX,
	;; 1 = 15 MHz; values 2/3 are undocumented, unimplemented 20/25 MHz modes
	;; that do not reach 20 MHz on production silicon).  The C startup
	;; self-test re-reads the port and confirms the speed on the diagnostic
	;; screen.
	ld a, #CPUSPEED_15MHZ
	out (PORT_CPUSPEED), a

	;; enable the ON-key interrupt line only
	ld a, #INT_ON
	out (PORT_INT_MASK), a

	;; run the C entry point
	call _cse_startup

	;; never returns; park safely (no flash writes anywhere)
idle_loop:
	halt
	jr idle_loop
