/*
**  File:	3c503.c		Dec. 20, 1996
**
**  Author:	Giovanni Falzoni <gfalzoni@inwind.it>
**
**  Driver for the Etherlink II boards.  Works in shared memory mode.
**  Programmed I/O could be used as well but would result in poor
**  performances. This file contains only the board specific code,
**  the rest is in 8390.c        Code specific for ISA bus only
*/

#include <minix/drivers.h>
#include <net/gen/ether.h>
#include <net/gen/eth_io.h>
#include "dp.h"

#if (ENABLE_3C503 == 1)

#include "8390.h"
#include "3c503.h"

/*
**  Name:	void el2_init(dpeth_t *dep);
**  Function:	Initalize hardware and data structures.
*/
static void el2_init(dpeth_t * dep)
{
  int ix, irq;
  int sendq_nr;
  int cntr;

  /* Map the address PROM to lower I/O address range */
  cntr = inb_el2(dep, EL2_CNTR);
  outb_el2(dep, EL2_CNTR, cntr | ECNTR_SAPROM);

  /* Read station address from PROM */
  for (ix = EL2_EA0; ix <= EL2_EA5; ix += 1)
	dep->de_address.ea_addr[ix] = inb_el2(dep, ix);

  /* Map the 8390 back to lower I/O address range */
  outb_el2(dep, EL2_CNTR, cntr);

  /* Enable memory, but turn off interrupts until we are ready */
  outb_el2(dep, EL2_CFGR, ECFGR_IRQOFF);

  dep->de_data_port = dep->de_dp8390_port = dep->de_base_port;
  dep->de_prog_IO = FALSE;	/* Programmed I/O not yet available */

  /* Check width of data bus */
  outb_el2(dep, DP_CR, CR_PS_P0 | CR_NO_DMA | CR_STP);
  outb_el2(dep, DP_DCR, 0);
  outb_el2(dep, DP_CR, CR_PS_P2 | CR_NO_DMA | CR_STP);
  dep->de_16bit = (inb_el2(dep, DP_DCR) & DCR_WTS) != 0;
  outb_el2(dep, DP_CR, CR_PS_P0 | CR_NO_DMA | CR_STP);

  /* Allocate one send buffer (1.5kb) per 8kb of on board memory. */
  /* Only 8kb of 3c503/16 boards are used to avoid specific routines */
  sendq_nr = dep->de_ramsize / 0x2000;
  if (sendq_nr < 1)
	sendq_nr = 1;
  else if (sendq_nr > SENDQ_NR)
	sendq_nr = SENDQ_NR;

  dep->de_sendq_nr = sendq_nr;
  for (ix = 0; ix < sendq_nr; ix++)
	dep->de_sendq[ix].sq_sendpage = (ix * SENDQ_PAGES) + EL2_SM_START_PG;

  dep->de_startpage = (ix * SENDQ_PAGES) + EL2_SM_START_PG;
  dep->de_stoppage = EL2_SM_STOP_PG;

  outb_el2(dep, EL2_STARTPG, dep->de_startpage);
  outb_el2(dep, EL2_STOPPG, dep->de_stoppage);

  /* Point the vector pointer registers somewhere ?harmless?. */
  outb_el2(dep, EL2_VP2, 0xFF);	/* Point at the ROM restart location    */
  outb_el2(dep, EL2_VP1, 0xFF);	/* 0xFFFF:0000  (from original sources) */
  outb_el2(dep, EL2_VP0, 0x00);	/* - What for protected mode? */

  /* Set interrupt level for 3c503 */
  irq = (dep->de_irq &= ~DEI_DEFAULT);	/* Strip the default flag. */
  if (irq == 9) irq = 2;
  if (irq < 2 || irq > 5) panic("bad 3c503 irq configuration: %d", irq);
  outb_el2(dep, EL2_IDCFG, (0x04 << irq));

  outb_el2(dep, EL2_DRQCNT, 0x08);	/* Set burst size to 8 */
  outb_el2(dep, EL2_DMAAH, EL2_SM_START_PG);	/* Put start of TX  */
  outb_el2(dep, EL2_DMAAL, 0x00);	/* buffer in the GA DMA reg */

  outb_el2(dep, EL2_CFGR, ECFGR_NORM);	/* Enable shared memory */

  ns_init(dep);			/* Initialize DP controller */

  printf("%s: Etherlink II%s (%s) at %X:%d:%05lX - ",
	 dep->de_name, dep->de_16bit ? "/16" : "", "3c503",
	 dep->de_base_port, dep->de_irq,
         dep->de_linmem + dep->de_offset_page);
  for (ix = 0; ix < SA_ADDR_LEN; ix += 1)
	printf("%02X%c", dep->de_address.ea_addr[ix],
	       ix < SA_ADDR_LEN - 1 ? ':' : '\n');
  return;
}

/*
**  Name:	void el2_stop(dpeth_t *dep);
**  Function:	Stops board by disabling interrupts.
*/
static void el2_stop(dpeth_t * dep)
{

  outb_el2(dep, EL2_CFGR, ECFGR_IRQOFF);
  sys_irqdisable(&dep->de_hook);	/* disable interrupts */
  return;
}

/*
**  Name:	void el2_probe(dpeth_t *dep);
**  Function:	Probe for the presence of an EtherLink II card.
**  		Initialize memory addressing if card detected.
*/
int el2_probe(dpeth_t * dep)
{
  int iobase, membase;
  int thin;

  /* Thin ethernet or AUI? */
  thin = (dep->de_linmem & 1) ? ECNTR_AUI : ECNTR_THIN;

  /* Location registers should have 1 bit set */
  if (!(iobase = inb_el2(dep, EL2_IOBASE))) return FALSE;
  if (!((membase = inb_el2(dep, EL2_MEMBASE)) & 0xF0)) return FALSE;
  if ((iobase & (iobase - 1)) || (membase & (membase - 1))) return FALSE;

  /* Resets board */
  outb_el2(dep, EL2_CNTR, ECNTR_RESET | thin);
  milli_delay(1);
  outb_el2(dep, EL2_CNTR, thin);
  milli_delay(5);

  /* Map the address PROM to lower I/O address range */
  outb_el2(dep, EL2_CNTR, ECNTR_SAPROM | thin);
  if (inb_el2(dep, EL2_EA0) != 0x02 ||	/* Etherlink II Station address */
      inb_el2(dep, EL2_EA1) != 0x60 ||	/* MUST be 02:60:8c:xx:xx:xx */
      inb_el2(dep, EL2_EA2) != 0x8C)
	return FALSE;		/* No Etherlink board at this address */

  /* Map the 8390 back to lower I/O address range */
  outb_el2(dep, EL2_CNTR, thin);

  /* Setup shared memory addressing for 3c503 */
  dep->de_linmem = ((membase & 0xC0) ? EL2_BASE_0D8000 : EL2_BASE_0C8000) +
	((membase & 0xA0) ? (EL2_BASE_0CC000 - EL2_BASE_0C8000) : 0x0000);

  /* Shared memory starts at 0x2000 (8kb window) */
  dep->de_offset_page = (EL2_SM_START_PG * DP_PAGESIZE);
  dep->de_linmem -= dep->de_offset_page;
  dep->de_ramsize = (EL2_SM_STOP_PG - EL2_SM_START_PG) * DP_PAGESIZE;

  /* Board initialization and stop functions */
  dep->de_initf = el2_init;
  dep->de_stopf = el2_stop;
  return TRUE;
}
#endif				/* ENABLE_3C503 */

/** 3c503.c **/
