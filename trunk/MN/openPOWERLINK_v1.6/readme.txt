  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
        www.systec-electronic.com
	openPOWERLINK.sourceforge.net


	openPOWERLINK - Quick Start Guide
	==================================


1. Documentation
-----------------

* The documentation of the openPOWERLINK protocol stack can be found in the
  subdirectory "Documenation".

* Further documentation can be downloaded from

    http://www.systec-electronic.com/html/index.pl/en_download_OpenPOWERLINK

    It contains an introduction and a reference manual. A free registration
    is required for downloading.

* The openPOWERLINK LiveCD with a ready-to-run Managing Node reference application
  is available as pre-configured ISO image from

    http://www.systec-electronic.com/openpowerlink_livecd

* Update Guide for necessary changes to your application project, if you update
  from a previous version of openPOWERLINK, can be found in file update.txt.

* License: Please refer to the file "license.txt" for information about
  the license of the source code.


2. Generic Requirements for all demo applications
--------------------------------------------------

- POWERLINK network:
    * one or more POWERLINK I/O devices according device profile CiA-401
      (i.e. Controlled Nodes) with the following PDO mapping:

    RPDO (PollRequest from MN): length = 1 or 2 Bytes
	                            containing the values for the digital outputs
	                            PDO version = 0
	TPDO (PollResponse): length = min. 1 Byte
	                     containing the values from the digital inputs
	                     PDO version = 0

    * Node-IDs of the Controlled Nodes (CN): 1, 32 or 110

    * CAT5 cables to connect the POWERLINK devices with the demo application

- openPOWERLINK demo application with node-ID 240/0xF0.
  When the demo application runs as MN (node-ID 240/0xF0) it drives a running light
  on the CNs. Otherwise it just behaves as CiA-401 I/O device.


3. Available demo applications
-------------------------------

X86 PC with Linux operating system (see section 3.1 for requirements)
  * Qt demo:
    = Examples\X86\Linux\gnu\demo_mn_8139_qt
  * simple MN demo with application running together with stack in kernel:
    = Examples\X86\Linux\gnu\demo_mn_8139_kernel
  * MN demo with Configuration Manager (CFM):
    = Examples\X86\Linux\gnu\demo_cfm_8139_kernel

X86 PC with Microsoft Windows operating system (2000, XP or newer)
and WinPcap driver installed:
  * simple MN demo for Microsoft Visual C 2005:
    = Examples\X86\Windows\VC8\demo_pcap
  * simple MN demo for Microsoft Visual C 2008:
    = Examples\X86\Windows\VC9\demo_pcap
  * MN demo with Configuration Manager (CFM) for Microsoft Visual C 2005:
    = Examples\X86\Windows\VC8\demo_pcap_cfm

Freescale ColdFire MCF5484 (SYSTEC Development Board for ECUcore-5484)
with Linux operating system (see section 3.2 for requirements)
  * simple CN demo which controls the LEDs and reads the pushbuttons on the devboard:
    = Examples\PLCcore-CF54\Linux\gnu\demo_cn_kernel
  * simple CN and MN demo which controls the LEDs and reads the pushbuttons on the devboard
    and drives a running light on other CNs if running as MN (node-ID 240/0xF0):
    = Examples\PLCcore-CF54\Linux\gnu\demo_mn_kernel

Hilscher netX-500 (Evaluation board Hilscher NXEB 500-HMI)
  = GPL-Addon Package: Examples\netx500\Linux\gnu\demo_mn_kernel

Atmel AT91RM9200 with Davicom DM9003 under Linux
  = Examples\at91rm9200\Linux\gnu\demo_mn_dm9003_kernel

Altera Cyclone III on EBV DBC3C40 Development Board with Nios II Soft-CPU and openMAC
  * simple CN demo which controls the LEDs and reads the pushbuttons on the devboard:
    = Examples\altera_nios2\no_os\gnu\demo_cn_openmac
  * extended CN demo which controls the LEDs and reads the pushbuttons on the devboard:
    = Examples\altera_nios2\no_os\gnu\demo_cn_3r1tpdo


3.1. Requirements for X86 demo under Linux
-------------------------------------------

- Linux kernel version 2.6.23 or later (last tested version 2.6.31)
  with CONFIG_HIGH_RES_TIMERS enabled
    * this needs ACPI support, maybe you need to append "highres=on" or
      "acpi=force" to kernel command line for older BIOSes)
    * check /proc/timer_list if .hres_active is 1
      $ cat /proc/timer_list | grep 'hres_active'

- Network controller card with Realtek RTL8139 Rev C or D chip onboard
  PCI network cards:
    * Zyxel FN312 (tested)
    * Netgear FA311 v2 Rev-D1 (tested)
    * D-Link DFE-528TX
    * LevelOne FNC-0109TX
    * Typhoon Speednet Card 10/100 PCI (P/N 70035)
  Cardbus network cards (PCMCIA):
    * Longshine LCS-8539TXR
    * Micronet SP160T V3
    * Micronet SP160TA V3


3.2. Requirements for ColdFire MCF5484 demo
--------------------------------------------

- Linux-BSP and toolchain for ColdFire MCF5484
- SYSTEC Developmentboard for ECUcore-5484
- Host PC with Linux


3.3. Steps to build and execute the demo application
-----------------------------------------------------

1.  Setup build environment on the host computer
    (e.g. install Linux-BSP and toolchain for ColdFire MCF5484
     or install Linux kernel sources for X86)

2.  Compile the sample application,
    e.g. for ColdFire MCF5484 with Linux execute the following commands
            $ cd Examples/PLCcore-CF54/Linux/gnu/demo_mn_kernel
            $ make

    for X86 with RTL8139 network controller try the following
	        $ cd Examples/X86/Linux/gnu/demo_mn_8139_kernel
	        $ make

3.  Unload an existing 8139 driver under Linux (if necessary):
	        $ rmmod 8139too.ko

    Unload the running application before:
            $ rmmod epl.ko

4.  Copy the built sample application (i.e. the Linux kernel object epl.ko) to
    the target (e.g. via FTP or NFS) and run it.
            $ insmod epl.ko
    With an additional parameter 'nodeid' the node-ID can be set manually.
    It overwrites any hardware settings.
            $ insmod epl.ko nodeid=240

5.  Now you may modify the sources to your needs and restart from 2.
    (e.g. change the cycle length and the network configuration in demo_main.c)



4. It does not work
--------------------

1.  Check the kernel log
            $ dmesg

2.  Make a trace with Wireshark on another PC that is connected to the
    POWERLINK network (www.wireshark.org)

3.  Study the output of
            $ cat /proc/epl

4.  Try to reset the NMT state machine with
            $ echo > /proc/epl

    (Hint: /proc/epl executes the NMT events defined in enum tEplNmtEvent in
           file Include/EplNmt.h like
            $ echo 0x13 > /proc/epl
           for NMT Reset Configuration)

5.  If TCP/IP communication over the POWERLINK network does not work
    check the configuration of the virtual network interface and the routing
            $ ifconfig epl
            $ netstat -r

