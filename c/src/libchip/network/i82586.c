/*  $NetBSD: i82586.c,v 1.38 2001/07/07 05:35:39 thorpej Exp $  */

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg and Charles M. Hannum.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 1997 Paul Kranenburg.
 * Copyright (c) 1992, 1993, University of Vermont and State
 *  Agricultural College.
 * Copyright (c) 1992, 1993, Garrett A. Wollman.
 *
 * Portions:
 * Copyright (c) 1994, 1995, Rafal K. Boni
 * Copyright (c) 1990, 1991, William F. Jolitz
 * Copyright (c) 1990, The Regents of the University of California
 *
 * RTEMS:
 * Copyright (c) 2001, Chris Johns, Cybertec Pty Ltd,
 *       http://www.cybertec.com.au/.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *  This product includes software developed by the University of Vermont
 *  and State Agricultural College and Garrett A. Wollman, by William F.
 *  Jolitz, and by the University of California, Berkeley, Lawrence
 *  Berkeley Laboratory, and its contributors.
 * 4. Neither the names of the Universities nor the names of the authors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR AUTHORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Intel 82586 Ethernet chip
 * Register, bit, and structure definitions.
 *
 * Original StarLAN driver written by Garrett Wollman with reference to the
 * Clarkson Packet Driver code for this chip written by Russ Nelson and others.
 *
 * BPF support code taken from hpdev/if_le.c, supplied with tcpdump.
 *
 * 3C507 support is loosely based on code donated to NetBSD by Rafal Boni.
 *
 * Majorly cleaned up and 3C507 code merged by Charles Hannum.
 *
 * Converted to SUN ie driver by Charles D. Cranor,
 *    October 1994, January 1995.
 * This sun version based on i386 version 1.30.
 */

/*
 * The i82586 is a very painful chip, found in sun3's, sun-4/100's
 * sun-4/200's, and VME based suns.  The byte order is all wrong for a
 * SUN, making life difficult.  Programming this chip is mostly the same,
 * but certain details differ from system to system.  This driver is
 * written so that different "ie" interfaces can be controled by the same
 * driver.
 */

/*
Mode of operation:

   We run the 82586 in a standard Ethernet mode.  We keep NFRAMES
   received frame descriptors around for the receiver to use, and
   NRXBUF associated receive buffer descriptors, both in a circular
   list.  Whenever a frame is received, we rotate both lists as
   necessary.  (The 586 treats both lists as a simple queue.)  We also
   keep a transmit command around so that packets can be sent off
   quickly.

   We configure the adapter in AL-LOC = 1 mode, which means that the
   Ethernet/802.3 MAC header is placed at the beginning of the receive
   buffer rather than being split off into various fields in the RFD.
   This also means that we must include this header in the transmit
   buffer as well.

   By convention, all transmit commands, and only transmit commands,
   shall have the I (IE_CMD_INTR) bit set in the command.  This way,
   when an interrupt arrives at i82586_intr(), it is immediately possible
   to tell what precisely caused it.  ANY OTHER command-sending
   routines should run at splnet(), and should post an acknowledgement
   to every interrupt they generate.

   To save the expense of shipping a command to 82586 every time we
   want to send a frame, we use a linked list of commands consisting
   of alternate XMIT and NOP commands. The links of these elements
   are manipulated (in iexmit()) such that the NOP command loops back
   to itself whenever the following XMIT command is not yet ready to
   go. Whenever an XMIT is ready, the preceding NOP link is pointed
   at it, while its own link field points to the following NOP command.
   Thus, a single transmit command sets off an interlocked traversal
   of the xmit command chain, with the host processor in control of
   the synchronization.
*/

#include <rtems.h>
#include <rtems/error.h>
#include <rtems/rtems_bsdnet.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/ioctl.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_types.h>
#include <net/if_dl.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include "i82586reg.h"
#include "i82586var.h"

/*
 * A global way to change all async cmd requests at once. For RTEMS and running
 * as tasks I wanted to see if the tx race condition is effected by this.
 */
#define ASYNC_OPTION (1)

void i82586_reset (struct ie_softc *, int);
void i82586_watchdog (struct ifnet *);
void i82586_init (void *);
int  i82586_ioctl (struct ifnet *, int cmd, caddr_t data);
void i82586_start (struct ifnet *);

void i82586_stop (struct ifnet *, int);
int  i82586_rint (struct ie_softc *, int);
int  i82586_tint (struct ie_softc *, int);

int  i82586_mediachange (struct ifnet *);
void i82586_mediastatus (struct ifnet *, struct ifmediareq *);

static void i82586_tx_task(void *arg);
static void i82586_start_tx(struct ie_softc *sc);

static int ie_readframe (struct ie_softc *, int);
static struct mbuf *ieget (struct ie_softc *, int, int);
static int i82586_get_rbd_list (struct ie_softc *, u_int16_t*,
                                          u_int16_t*, int *);
static void i82586_release_rbd_list (struct ie_softc *,
                                     u_int16_t, u_int16_t);
static int i82586_drop_frames (struct ie_softc *);
static int i82586_chk_rx_ring (struct ie_softc *);

static __inline__ void ie_ack (struct ie_softc *, u_int);
static __inline__ void iexmit (struct ie_softc *);
static void i82586_start_transceiver (struct ie_softc *);

static void i82586_count_errors (struct ie_softc *);
static void i82586_rx_errors (struct ie_softc *, int, int);
static void i82586_setup_bufs (struct ie_softc *);
static void setup_simple_command (struct ie_softc *, int, int);
static int  ie_cfg_setup (struct ie_softc *, int, int, int);
static int  ie_ia_setup (struct ie_softc *, int);
static void ie_run_tdr (struct ie_softc *, int);
static int  ie_mc_setup (struct ie_softc *, int);
static void ie_mc_reset (struct ie_softc *);
static int  i82586_start_cmd (struct ie_softc *, int, int, int, int);
static int  i82586_cmd_wait (struct ie_softc *);

#if I82586_DEBUG
static void print_softie(struct ie_softc *sc);
static void print_rbd (struct ie_softc *, int);
#endif

#define min(l,r) ((l) < (r) ? (l) : (r))
#define max(l,r) ((l) > (r) ? (l) : (r))

#define delay(p) rtems_task_wake_after (TOD_MICROSECONDS_TO_TICKS (p))

#define i82586_WAKE_EVENT RTEMS_EVENT_1
#define i82586_TX_EVENT   RTEMS_EVENT_2

char *bitmask_snprintf(unsigned long value, const char *format, char *buf, int blen)
{
  char *b  = buf;
  int  bit = 31;
  
  while (bit-- > *format)
    value <<= 1;

  format++;
  
  while (*format)
  {
    if (value & 0x80000000)
      while (isalnum(*format))
        *b++ = *format;
    else
      *b++ = '0';

    *b++ = ',';
    
    while (bit-- > *format)
      value <<= 1;
  
    format++;
  }
  
  *b = '\0';
  return buf;
}

char *ether_sprintf(unsigned char *addr)
{
  static char buf[32];
  char *b = buf;
  int i;
  
  for (i = 0; i < ETHER_ADDR_LEN; i++)
  {
    sprintf(b, "%02x:", *addr++);
    b += 3;
  }
  b--;
  b = "\0";
  return buf;
}

/*
 * Front-ends call this function to attach to the MI driver.
 *
 * The front-end has responsibility for managing the ICP and ISCP
 * structures. Both of these are opaque to us.  Also, the front-end
 * chooses a location for the SCB which is expected to be addressable
 * (through `sc->scb') as an offset against the shared-memory bus handle.
 *
 * The following MD interface function must be setup by the front-end
 * before calling here:
 *
 *  hwreset      - board dependent reset
 *  hwinit      - board dependent initialization
 *  chan_attn    - channel attention
 *  intrhook    - board dependent interrupt processing
 *  memcopyin    - shared memory copy: board to KVA
 *  memcopyout    - shared memory copy: KVA to board
 *  ie_bus_read16    - read a sixteen-bit i82586 pointer
 *  ie_bus_write16    - write a sixteen-bit i82586 pointer
 *  ie_bus_write24    - write a twenty-four-bit i82586 pointer
 *
 */
int
i82586_attach(struct rtems_bsdnet_ifconfig *config, int attaching)
{
  struct ie_softc *sc;
  struct ifnet    *ifp;
  char            *name;
  int             unit;
  int             mtu;
  
  /*
    * Parse driver name
   */
  
  if ((unit = rtems_bsdnet_parse_driver_name (config, &name)) < 0)
    return 0;
  
  sc  = config->drv_ctrl; 
  ifp = &sc->arpcom.ac_if;
  
#if I82586_DEBUG
  sc->sc_debug = 0; //IED_TINT | IED_XMIT;
#endif

  if (attaching)
  {
    if (ifp->if_softc)
    {
      printf ("Driver `%s' already in use.\n", config->name);
      return 0;
    }
    
    /*
     * Process options
     */
    
    memcpy (sc->arpcom.ac_enaddr, config->hardware_address, ETHER_ADDR_LEN);
  
    if (config->mtu)
      mtu = config->mtu;
    else
      mtu = ETHERMTU;
    
    ifp->if_softc  = sc;
    ifp->if_unit   = unit;
    ifp->if_name   = name;
    ifp->if_mtu    = mtu;
    ifp->if_init   = i82586_init;
    ifp->if_ioctl  = i82586_ioctl;
    ifp->if_start  = i82586_start;
    ifp->if_output = ether_output;
    ifp->if_flags  = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;

    if (ifp->if_snd.ifq_maxlen == 0)
      ifp->if_snd.ifq_maxlen = ifqmaxlen;

    /* Attach the interface. */
    if_attach(ifp);
    ether_ifattach(ifp);
    return 1;
  }
  return 0;
}


/*
 * Interrupt Acknowledge. Mask in native byte-order.
 */
static __inline__ void
ie_ack(struct ie_softc *sc, u_int mask)
{
  u_int status;

  IE_BUS_BARRIER(sc, 0, 0, BUS_SPACE_BARRIER_READ);
  status = sc->ie_bus_read16(sc, IE_SCB_STATUS(sc->scb));
  i82586_start_cmd(sc, status & mask, 0, 0, 0);
  if (sc->intrhook)
    sc->intrhook(sc, INTR_ACK);
}


/*
 * Read data off the interface, and turn it into an mbuf chain.
 *
 * This code is DRAMATICALLY different from the previous version; this
 * version tries to allocate the entire mbuf chain up front, given the
 * length of the data available.  This enables us to allocate mbuf
 * clusters in many situations where before we would have had a long
 * chain of partially-full mbufs.  This should help to speed up the
 * operation considerably.  (Provided that it works, of course.)
 */
static __inline struct mbuf *
ieget(struct ie_softc *sc, int head, int totlen)
{
  struct mbuf *m, *m0, *newm;
  int len, resid;
  int thisrboff, thismboff;
  struct ether_header eh;

  /*
   * Snarf the Ethernet header.
   */
  (sc->memcopyin)(sc, &eh, IE_RBUF_ADDR(sc, head),
                  sizeof(struct ether_header));

  resid = totlen;

  MGETHDR(m0, M_DONTWAIT, MT_DATA);
  if (m0 == 0)
    return (0);
  m0->m_pkthdr.rcvif = &sc->arpcom.ac_if;
  m0->m_pkthdr.len = totlen;
  len = MHLEN;
  m = m0;

  /*
   * This loop goes through and allocates mbufs for all the data we will
   * be copying in.  It does not actually do the copying yet.
   */
  while (totlen > 0) {
    if (totlen >= MINCLSIZE) {
      MCLGET(m, M_DONTWAIT);
      if ((m->m_flags & M_EXT) == 0)
        goto bad;
      len = MCLBYTES;
    }

    if (m == m0) {
      caddr_t newdata = (caddr_t)
        ALIGN(m->m_data + sizeof(struct ether_header)) -
        sizeof(struct ether_header);
      len -= newdata - m->m_data;
      m->m_data = newdata;
    }

    m->m_len = len = min(totlen, len);

    totlen -= len;
    if (totlen > 0) {
      MGET(newm, M_DONTWAIT, MT_DATA);
      if (newm == 0)
        goto bad;
      len = MLEN;
      m = m->m_next = newm;
    }
  }

  m = m0;
  thismboff = 0;

  /*
   * Copy the Ethernet header into the mbuf chain.
   */
  memcpy(mtod(m, caddr_t), &eh, sizeof(struct ether_header));
  thismboff = sizeof(struct ether_header);
  thisrboff = sizeof(struct ether_header);
  resid -= sizeof(struct ether_header);

  /*
   * Now we take the mbuf chain (hopefully only one mbuf most of the
   * time) and stuff the data into it.  There are no possible failures
   * at or after this point.
   */
  while (resid > 0) {
    int thisrblen = IE_RBUF_SIZE - thisrboff,
      thismblen = m->m_len - thismboff;
    len = min(thisrblen, thismblen);

    (sc->memcopyin)(sc, mtod(m, caddr_t) + thismboff,
                    IE_RBUF_ADDR(sc,head) + thisrboff,
                    (u_int)len);
    resid -= len;

    if (len == thismblen) {
      m = m->m_next;
      thismboff = 0;
    } else
      thismboff += len;

    if (len == thisrblen) {
      if (++head == sc->nrxbuf)
        head = 0;
      thisrboff = 0;
    } else
      thisrboff += len;
  }

  /*
   * Unless something changed strangely while we were doing the copy,
   * we have now copied everything in from the shared memory.
   * This means that we are done.
   */
  return (m0);

  bad:
  m_freem(m0);
  return (0);
}

/*
 * Setup all necessary artifacts for an XMIT command, and then pass the XMIT
 * command to the chip to be executed.
 */
static __inline__ void
iexmit(struct ie_softc *sc)
{
  int off;
  int cur, prev;

  cur = sc->xctail;

#if I82586_DEBUG
    I82586_TRACE(sc, I82586_TX_EMIT, cur);
#endif
    
#if I82586_DEBUG
  if (sc->sc_debug & IED_XMIT)
    printf("%s: xmit buffer %d\n", sc->arpcom.ac_if.if_name, cur);
#endif

  /*
   * Setup the transmit command.
   */
  sc->ie_bus_write16(sc, IE_CMD_XMIT_DESC(sc->xmit_cmds, cur),
                     IE_XBD_ADDR(sc->xbds, cur));

  sc->ie_bus_write16(sc, IE_CMD_XMIT_STATUS(sc->xmit_cmds, cur), 0);

  if (sc->do_xmitnopchain) {
    /*
     * Gate this XMIT command to the following NOP
     */
    sc->ie_bus_write16(sc, IE_CMD_XMIT_LINK(sc->xmit_cmds, cur),
                       IE_CMD_NOP_ADDR(sc->nop_cmds, cur));
    sc->ie_bus_write16(sc, IE_CMD_XMIT_CMD(sc->xmit_cmds, cur),
                       IE_CMD_XMIT | IE_CMD_INTR);

    /*
     * Loopback at following NOP
     */
    sc->ie_bus_write16(sc, IE_CMD_NOP_STATUS(sc->nop_cmds, cur), 0);
    sc->ie_bus_write16(sc, IE_CMD_NOP_LINK(sc->nop_cmds, cur),
                       IE_CMD_NOP_ADDR(sc->nop_cmds, cur));

    /*
     * Gate preceding NOP to this XMIT command
     */
    prev = (cur + NTXBUF - 1) % NTXBUF;
    sc->ie_bus_write16(sc, IE_CMD_NOP_STATUS(sc->nop_cmds, prev), 0);
    sc->ie_bus_write16(sc, IE_CMD_NOP_LINK(sc->nop_cmds, prev),
                       IE_CMD_XMIT_ADDR(sc->xmit_cmds, cur));

    off = IE_SCB_STATUS(sc->scb);
    IE_BUS_BARRIER(sc, off, 2, BUS_SPACE_BARRIER_READ);
    if ((sc->ie_bus_read16(sc, off) & IE_CUS_ACTIVE) == 0) {
      printf("iexmit: CU not active\n");
      i82586_start_transceiver(sc);
    }
  } else {
    sc->ie_bus_write16(sc, IE_CMD_XMIT_LINK(sc->xmit_cmds,cur),
                       0xffff);

    sc->ie_bus_write16(sc, IE_CMD_XMIT_CMD(sc->xmit_cmds, cur),
                       IE_CMD_XMIT | IE_CMD_INTR | IE_CMD_LAST);

    off = IE_SCB_CMDLST(sc->scb);
    sc->ie_bus_write16(sc, off, IE_CMD_XMIT_ADDR(sc->xmit_cmds, cur));
    IE_BUS_BARRIER(sc, off, 2, BUS_SPACE_BARRIER_READ);

    if (i82586_start_cmd(sc, IE_CUC_START, 0, 0, ASYNC_OPTION))
      printf("%s: iexmit: start xmit command timed out\n",
             sc->arpcom.ac_if.if_name);
  }

  sc->arpcom.ac_if.if_timer = 5;
}


/*
 * Device timeout/watchdog routine.
 * Entered if the device neglects to generate an interrupt after a
 * transmit has been started on it.
 */
void
i82586_watchdog(struct ifnet *ifp)
{
  struct ie_softc *sc = ifp->if_softc;
  printf("%s: device timeout\n", ifp->if_name);
  ++ifp->if_oerrors;
  i82586_reset(sc, 1);
}

static int
i82586_cmd_wait(struct ie_softc *sc)
{
  /* spin on i82586 command acknowledge; wait at most 0.9 (!) seconds */
  int i, off;
  u_int16_t cmd;

  for (i = 0; i < 900000; i++) {
    /* Read the command word */
    off = IE_SCB_CMD(sc->scb);

    IE_BUS_BARRIER(sc, off, 2, BUS_SPACE_BARRIER_READ);
    if ((cmd = sc->ie_bus_read16(sc, off)) == 0)
      return (0);
    delay(1);
  }

  off = IE_SCB_STATUS(sc->scb);
  printf("i82586_cmd_wait: timo(%ssync): scb status: 0x%x, cmd: 0x%x\n",
         sc->async_cmd_inprogress?"a":"", 
         sc->ie_bus_read16(sc, off), cmd);

  return (1);  /* Timeout */
}

/*
 * Send a command to the controller and wait for it to either complete
 * or be accepted, depending on the command.  If the command pointer
 * is null, then pretend that the command is not an action command.
 * If the command pointer is not null, and the command is an action
 * command, wait for one of the MASK bits to turn on in the command's
 * status field.
 * If ASYNC is set, we just call the chip's attention and return.
 * We may have to wait for the command's acceptance later though.
 */
static int
i82586_start_cmd(struct ie_softc *sc, int cmd, int iecmdbuf, int mask, int async)
{
  int i;
  int off;

  if (sc->async_cmd_inprogress != 0) {
    /*
     * If previous command was issued asynchronously, wait
     * for it now.
     */
    if (i82586_cmd_wait(sc) != 0)
      return (1);
    sc->async_cmd_inprogress = 0;
  }

  off = IE_SCB_CMD(sc->scb);
  sc->ie_bus_write16(sc, off, cmd);
  IE_BUS_BARRIER(sc, off, 2, BUS_SPACE_BARRIER_WRITE);
  (sc->chan_attn)(sc, CARD_RESET);

  if (async != 0) {
    sc->async_cmd_inprogress = 1;
    return (0);
  }

  if (IE_ACTION_COMMAND(cmd) && iecmdbuf) {
    int status;
    /*
     * Now spin-lock waiting for status.  This is not a very nice
     * thing to do, and can kill performance pretty well...
     * According to the packet driver, the minimum timeout
     * should be .369 seconds.
     */
    for (i = 0; i < 369000; i++) {
      /* Read the command status */
      off = IE_CMD_COMMON_STATUS(iecmdbuf);
      IE_BUS_BARRIER(sc, off, 2, BUS_SPACE_BARRIER_READ);
      status = sc->ie_bus_read16(sc, off);
      if (status & mask)
        return (0);
      delay(1);
    }

  } else {
    /*
     * Otherwise, just wait for the command to be accepted.
     */
    return (i82586_cmd_wait(sc));
  }

  /* Timeout */
  return (1);
}


/*
 * Transfer accumulated chip error counters to IF.
 */
static __inline void
i82586_count_errors(struct ie_softc *sc)
{
  int scb = sc->scb;

  sc->arpcom.ac_if.if_ierrors +=
    sc->ie_bus_read16(sc, IE_SCB_ERRCRC(scb)) +
    sc->ie_bus_read16(sc, IE_SCB_ERRALN(scb)) +
    sc->ie_bus_read16(sc, IE_SCB_ERRRES(scb)) +
    sc->ie_bus_read16(sc, IE_SCB_ERROVR(scb));

  /* Clear error counters */
  sc->ie_bus_write16(sc, IE_SCB_ERRCRC(scb), 0);
  sc->ie_bus_write16(sc, IE_SCB_ERRALN(scb), 0);
  sc->ie_bus_write16(sc, IE_SCB_ERRRES(scb), 0);
  sc->ie_bus_write16(sc, IE_SCB_ERROVR(scb), 0);
}


static void
i82586_rx_errors(struct ie_softc *sc, int fn, int status)
{
  char bits[128];

  printf("%s: rx error (frame# %d): %s\n", sc->arpcom.ac_if.if_name, fn,
         bitmask_snprintf(status, IE_FD_STATUSBITS, bits, sizeof(bits)));
}

/*
 * i82586 interrupt entry point.
 */
rtems_isr
i82586_intr(rtems_vector_number vec, void *arg)
{
  struct ie_softc *sc = arg;

#if I82586_DEBUG
  static unsigned long icnt = 0;
  I82586_TRACE(sc, I82586_INTS_REQ, icnt++);
#endif
    
  /*
   * Implementation dependent interrupt handling. It must at least
   * disabled interrupts from the i82586. It is hoped this can
   * happen somewhere outside the i82586.
   */
  if (sc->intrhook)
    (sc->intrhook)(sc, INTR_ENTER);

  /*
   * Wake the task to handle the interrupt. It will
   * enabled the interrupts when it has finished.
   */
  rtems_event_send (sc->intr_task, i82586_WAKE_EVENT);
}

/*
 * i82586 interrupt task. The task is actually an extension of the interrupt
 * with a context switch in the middle. The RTEMS TCP/IP stack requires a task
 * be used to talk to the stack as a network semaphore is claimed. This
 * cannot happen during an interrupt.
 */
static void
i82586_intr_task(void *arg)
{
  struct ie_softc *sc = arg;
  rtems_event_set events;
  u_int           status;
  int             off;
  int             reset;
  
  /*
   * Not sure this is a good idea but as a out path exists and
   * roads lead to it, it seems ok.
   */
  for (;;) {
    rtems_bsdnet_event_receive (i82586_WAKE_EVENT,
                                RTEMS_WAIT | RTEMS_EVENT_ANY,
                                0, &events);

    off = IE_SCB_STATUS(sc->scb);
    IE_BUS_BARRIER(sc, off, 2, BUS_SPACE_BARRIER_READ);
    status = sc->ie_bus_read16(sc, off) & IE_ST_WHENCE;

#if I82586_DEBUG
    I82586_TRACE(sc, I82586_INTS_IN, status);
#endif

    reset = 0;
    
    while ((status & IE_ST_WHENCE) != 0) {
#if I82586_DEBUG
      if (sc->sc_debug)
        printf ("%s: -------\n%s: scbstatus=0x%x\n",
                sc->arpcom.ac_if.if_name, sc->arpcom.ac_if.if_name, status);
#endif
    
#if 1
      /* Ack interrupts FIRST in case we receive more during the ISR. */
      ie_ack(sc, status & IE_ST_WHENCE);
#endif

      i82586_start_cmd(sc, status & IE_ST_WHENCE, 0, 0, ASYNC_OPTION);

      if (status & (IE_ST_FR | IE_ST_RNR))
        if (i82586_rint(sc, status) != 0) {
          reset = 1;
          break;
        }

      if (status & IE_ST_CX)
        if (i82586_tint(sc, status) != 0) {
          reset = 1;
          break;
        }

#if I82586_DEBUG
      if ((status & IE_ST_CNA) && (sc->sc_debug & IED_CNA))
        printf("%s: cna; status=0x%x\n", sc->arpcom.ac_if.if_name, status);
#endif

#if 0
      if (sc->intrhook)
        (sc->intrhook)(sc, INTR_LOOP);
#endif

#if 1
      /*
       * Interrupt ACK was posted asynchronously; wait for
       * completion here before reading SCB status again.
       * 
       * If ACK fails, try to reset the chip, in hopes that
       * it helps.
       */
      if (i82586_cmd_wait(sc) != 0) {
          reset = 1;
          break;
        }
#endif
    
      IE_BUS_BARRIER(sc, off, 2, BUS_SPACE_BARRIER_READ);
      status = sc->ie_bus_read16(sc, off);
#if I82586_DEBUG
      I82586_TRACE(sc, I82586_INTS_LOOPS, status);
#endif
    }
    
    if (reset) {
#if I82586_DEBUG
      printf("%s: intr reset; status=0x%x\n", sc->arpcom.ac_if.if_name, status);
#endif
      i82586_cmd_wait(sc);
      i82586_reset(sc, 1);
    }
    
#if I82586_DEBUG
    I82586_TRACE(sc, I82586_INTS_OUT, status);
#endif
    
    if (sc->intrhook)
      (sc->intrhook)(sc, INTR_EXIT);
  }
}

/*
 * Process a received-frame interrupt.
 */
int
i82586_rint(struct ie_softc *sc, int scbstatus)
{
  static int timesthru = 1024;
  int i, status, off;

#if I82586_DEBUG
  I82586_TRACE(sc, I82586_RX_INT, scbstatus);
  
  if (sc->sc_debug & IED_RINT)
    printf("%s: rint: status 0x%x\n",
           sc->arpcom.ac_if.if_name, scbstatus);
#endif

  for (;;) {
    int drop = 0;

    i = sc->rfhead;
    off = IE_RFRAME_STATUS(sc->rframes, i);
    IE_BUS_BARRIER(sc, off, 2, BUS_SPACE_BARRIER_READ);
    status = sc->ie_bus_read16(sc, off);

#if I82586_DEBUG
    if (sc->sc_debug & IED_RINT)
      printf("%s: rint: frame(%d) status 0x%x\n",
             sc->arpcom.ac_if.if_name, i, status);
#endif
    if ((status & IE_FD_COMPLETE) == 0) {
      if ((status & IE_FD_OK) != 0) {
        printf("%s: rint: weird: ",
               sc->arpcom.ac_if.if_name);
        i82586_rx_errors(sc, i, status);
#if I82586_DEBUG
        I82586_TRACE(sc, I82586_RX_ERR, status);
#endif
        break;
      }
      if (--timesthru == 0) {
        /* Account the accumulated errors */
        i82586_count_errors(sc);
        timesthru = 1024;
      }
      break;
    } else if ((status & IE_FD_OK) == 0) {
      /*
       * If the chip is configured to automatically
       * discard bad frames, the only reason we can
       * get here is an "out-of-resource" condition.
       */
      i82586_rx_errors(sc, i, status);
      drop = 1;
      
#if I82586_DEBUG
      I82586_TRACE(sc, I82586_RX_DROP, status);
#endif
    }

#if I82586_DEBUG
    if ((status & IE_FD_BUSY) != 0)
      printf("%s: rint: frame(%d) busy; status=0x%x\n",
             sc->arpcom.ac_if.if_name, i, status);
#endif

    /*
     * Advance the RFD list, since we're done with
     * this descriptor.
     */

    /* Clear frame status */
    sc->ie_bus_write16(sc, off, 0);

    /* Put fence at this frame (the head) */
    off = IE_RFRAME_LAST(sc->rframes, i);
    sc->ie_bus_write16(sc, off, IE_FD_EOL|IE_FD_SUSP);

    /* and clear RBD field */
    off = IE_RFRAME_BUFDESC(sc->rframes, i);
    sc->ie_bus_write16(sc, off, 0xffff);

    /* Remove fence from current tail */
    off = IE_RFRAME_LAST(sc->rframes, sc->rftail);
    sc->ie_bus_write16(sc, off, 0);

    if (++sc->rftail == sc->nframes)
      sc->rftail = 0;
    if (++sc->rfhead == sc->nframes)
      sc->rfhead = 0;

    /* Pull the frame off the board */
    if (drop) {
      i82586_drop_frames(sc);
      if ((status & IE_FD_RNR) != 0)
        sc->rnr_expect = 1;
      sc->arpcom.ac_if.if_ierrors++;
    } else if (ie_readframe(sc, i) != 0)
      return (1);
  }

  if ((scbstatus & IE_ST_RNR) != 0) {

    /*
     * Receiver went "Not Ready". We try to figure out
     * whether this was an expected event based on past
     * frame status values.
     */

    if ((scbstatus & IE_RUS_SUSPEND) != 0) {
      /*
       * We use the "suspend on last frame" flag.
       * Send a RU RESUME command in response, since
       * we should have dealt with all completed frames
       * by now.
       */
      printf("RINT: SUSPENDED; scbstatus=0x%x\n",
             scbstatus);
      if (i82586_start_cmd(sc, IE_RUC_RESUME, 0, 0, 0) == 0)
        return (0);
      printf("%s: RU RESUME command timed out\n",
             sc->arpcom.ac_if.if_name);
      return (1);  /* Ask for a reset */
    }

    if (sc->rnr_expect != 0) {
      /*
       * The RNR condition was announced in the previously
       * completed frame.  Assume the receive ring is Ok,
       * so restart the receiver without further delay.
       */
      i82586_start_transceiver(sc);
      sc->rnr_expect = 0;
      return (0);

    } else if ((scbstatus & IE_RUS_NOSPACE) != 0) {
      /*
       * We saw no previous IF_FD_RNR flag.
       * We check our ring invariants and, if ok,
       * just restart the receiver at the current
       * point in the ring.
       */
      if (i82586_chk_rx_ring(sc) != 0)
        return (1);

      i82586_start_transceiver(sc);
      sc->arpcom.ac_if.if_ierrors++;
      return (0);
    } else
      printf("%s: receiver not ready; scbstatus=0x%x\n",
             sc->arpcom.ac_if.if_name, scbstatus);

    sc->arpcom.ac_if.if_ierrors++;
    return (1);  /* Ask for a reset */
  }

  return (0);
}

/*
 * Process a command-complete interrupt.  These are only generated by the
 * transmission of frames.  This routine is deceptively simple, since most
 * of the real work is done by i82586_start().
 */
int
i82586_tint(struct ie_softc *sc, int scbstatus)
{
  struct ifnet *ifp = &sc->arpcom.ac_if;
  int status;

#if I82586_DEBUG
  I82586_TRACE(sc, I82586_TX_INT, sc->xmit_busy);
#endif

#if I82586_DEBUG
  if (sc->xmit_busy <= 0) {
    printf("i82586_tint: (%d), WEIRD: xmit_busy=%d, xctail=%d, xchead=%d\n",
           sc->trace_flow_in / 2, sc->xmit_busy, sc->xctail, sc->xchead);
    I82586_TRACE(sc, I82586_TX_BAD, sc->xctail);
    return (0);
  }
#endif

  ifp->if_timer = 0;
  ifp->if_flags &= ~IFF_OACTIVE;

  status = sc->ie_bus_read16(sc, IE_CMD_XMIT_STATUS(sc->xmit_cmds,
                                                    sc->xctail));

#if I82586_DEBUG
  if (sc->sc_debug & IED_TINT)
    printf("%s: tint: SCB status 0x%x; xmit status 0x%x\n",
           sc->arpcom.ac_if.if_name, scbstatus, status);
#endif

  if ((status & IE_STAT_COMPL) == 0 || (status & IE_STAT_BUSY)) {
    printf("i82586_tint: (%d) command still busy; status=0x%x; tail=%d\n",
           sc->trace_flow_in / 2, status, sc->xctail);
    printf("iestatus = 0x%x\n", scbstatus);
/*    sc->sc_debug = IED_ALL; */
  }

  if (status & IE_STAT_OK) {
    ifp->if_opackets++;
    ifp->if_collisions += (status & IE_XS_MAXCOLL);
  } else {
    ifp->if_oerrors++;
    /*
     * Check SQE and DEFERRED?
     * What if more than one bit is set?
     */
    if (status & IE_STAT_ABORT)
      printf("%s: send aborted\n", sc->arpcom.ac_if.if_name);
    else if (status & IE_XS_NOCARRIER)
      printf("%s: no carrier\n", sc->arpcom.ac_if.if_name);
    else if (status & IE_XS_LOSTCTS)
      printf("%s: lost CTS\n", sc->arpcom.ac_if.if_name);
    else if (status & IE_XS_UNDERRUN)
      printf("%s: DMA underrun\n", sc->arpcom.ac_if.if_name);
    else if (status & IE_XS_EXCMAX) {
      printf("%s: too many collisions\n",
             sc->arpcom.ac_if.if_name);
      sc->arpcom.ac_if.if_collisions += 16;
    }
  }

  /*
   * If multicast addresses were added or deleted while transmitting,
   * ie_mc_reset() set the want_mcsetup flag indicating that we
   * should do it.
   */
  if (sc->want_mcsetup) {
    ie_mc_setup(sc, IE_XBUF_ADDR(sc, sc->xctail));
    sc->want_mcsetup = 0;
  }

  /* Done with the buffer. */
  sc->xmit_busy--;
  sc->xctail = (sc->xctail + 1) % NTXBUF;

  /* Start the next packet, if any, transmitting. */
  if (sc->xmit_busy > 0)
    iexmit(sc);

  i82586_start_tx(sc);
  return (0);
}

/*
 * Get a range of receive buffer descriptors that represent one packet.
 */
static int
i82586_get_rbd_list(struct ie_softc *sc, u_int16_t *start, u_int16_t *end, int *pktlen)
{
  int  off, rbbase = sc->rbds;
  int  rbindex, count = 0;
  int  plen = 0;
  int  rbdstatus;

  *start = rbindex = sc->rbhead;

  do {
    off = IE_RBD_STATUS(rbbase, rbindex);
    IE_BUS_BARRIER(sc, off, 2, BUS_SPACE_BARRIER_READ);
    rbdstatus = sc->ie_bus_read16(sc, off);
    if ((rbdstatus & IE_RBD_USED) == 0) {
      /*
       * This means we are somehow out of sync.  So, we
       * reset the adapter.
       */
#if I82586_DEBUG
      print_rbd(sc, rbindex);
#endif
      printf("%s: receive descriptors out of sync at %d\n",
             sc->arpcom.ac_if.if_name, rbindex);
      return (0);
    }
    plen += (rbdstatus & IE_RBD_CNTMASK);

    if (++rbindex == sc->nrxbuf)
      rbindex = 0;

    ++count;
  } while ((rbdstatus & IE_RBD_LAST) == 0);
  *end = rbindex;
  *pktlen = plen;
  return (count);
}


/*
 * Release a range of receive buffer descriptors after we've copied the packet.
 */
static void
i82586_release_rbd_list(struct ie_softc *sc, u_int16_t start, u_int16_t end)
{
  int  off, rbbase = sc->rbds;
  int  rbindex = start;

  do {
    /* Clear buffer status */
    off = IE_RBD_STATUS(rbbase, rbindex);
    sc->ie_bus_write16(sc, off, 0);
    if (++rbindex == sc->nrxbuf)
      rbindex = 0;
  } while (rbindex != end);

  /* Mark EOL at new tail */
  rbindex = ((rbindex == 0) ? sc->nrxbuf : rbindex) - 1;
  off = IE_RBD_BUFLEN(rbbase, rbindex);
  sc->ie_bus_write16(sc, off, IE_RBUF_SIZE|IE_RBD_EOL);

  /* Remove EOL from current tail */
  off = IE_RBD_BUFLEN(rbbase, sc->rbtail);
  sc->ie_bus_write16(sc, off, IE_RBUF_SIZE);

  /* New head & tail pointer */
/* hmm, why have both? head is always (tail + 1) % NRXBUF */
  sc->rbhead = end;
  sc->rbtail = rbindex;
}

/*
 * Drop the packet at the head of the RX buffer ring.
 * Called if the frame descriptor reports an error on this packet.
 * Returns 1 if the buffer descriptor ring appears to be corrupt;
 * and 0 otherwise.
 */
static int
i82586_drop_frames(struct ie_softc *sc)
{
  u_int16_t bstart, bend;
  int pktlen;

  if (i82586_get_rbd_list(sc, &bstart, &bend, &pktlen) == 0)
    return (1);
  i82586_release_rbd_list(sc, bstart, bend);
  return (0);
}

/*
 * Check the RX frame & buffer descriptor lists for our invariants,
 * i.e.: EOL bit set iff. it is pointed at by the r*tail pointer.
 *
 * Called when the receive unit has stopped unexpectedly.
 * Returns 1 if an inconsistency is detected; 0 otherwise.
 *
 * The Receive Unit is expected to be NOT RUNNING.
 */
static int
i82586_chk_rx_ring(struct ie_softc *sc)
{
  int n, off, val;

  for (n = 0; n < sc->nrxbuf; n++) {
    off = IE_RBD_BUFLEN(sc->rbds, n);
    val = sc->ie_bus_read16(sc, off);
    if ((n == sc->rbtail) ^ ((val & IE_RBD_EOL) != 0)) {
      /* `rbtail' and EOL flag out of sync */
      printf("%s: rx buffer descriptors out of sync at %d\n",
             sc->arpcom.ac_if.if_name, n);
      return (1);
    }

    /* Take the opportunity to clear the status fields here ? */
  }

  for (n = 0; n < sc->nframes; n++) {
    off = IE_RFRAME_LAST(sc->rframes, n);
    val = sc->ie_bus_read16(sc, off);
    if ((n == sc->rftail) ^ ((val & (IE_FD_EOL|IE_FD_SUSP)) != 0)) {
      /* `rftail' and EOL flag out of sync */
      printf("%s: rx frame list out of sync at %d\n",
             sc->arpcom.ac_if.if_name, n);
      return (1);
    }
  }

  return (0);
}


/*
 * Read frame NUM from unit UNIT (pre-cached as IE).
 *
 * This routine reads the RFD at NUM, and copies in the buffers from the list
 * of RBD, then rotates the RBD list so that the receiver doesn't start
 * complaining.  Trailers are DROPPED---there's no point in wasting time
 * on confusing code to deal with them.  Hopefully, this machine will
 * never ARP for trailers anyway.
 */
static int
ie_readframe(struct ie_softc *sc, int num) /* frame number to read */
{
  struct ether_header *eh;
  struct mbuf         *m;
  u_int16_t           bstart, bend;
  int                 pktlen;

  if (i82586_get_rbd_list(sc, &bstart, &bend, &pktlen) == 0) {
    sc->arpcom.ac_if.if_ierrors++;
    return (1);
  }

  m = ieget(sc, bstart, pktlen);
  i82586_release_rbd_list(sc, bstart, bend);

  if (m == 0) {
    sc->arpcom.ac_if.if_ierrors++;
    return (0);
  }

  /*
   * Remove the mac header. This is different from the NetBSD
   * stack.
   */
  eh = mtod(m, struct ether_header *);
  m->m_data += sizeof (struct ether_header);
  m->m_len -= sizeof (struct ether_header);
  m->m_pkthdr.len -= sizeof (struct ether_header);

#if I82586_DEBUG
  if (sc->sc_debug & IED_READFRAME) {

    printf("%s: frame from ether %s type 0x%x len %d\n",
           sc->arpcom.ac_if.if_name,
           ether_sprintf(eh->ether_shost),
           (u_int)ntohs(eh->ether_type),
           pktlen);
  }
#endif

#if NBPFILTER > 0
  /* Check for a BPF filter; if so, hand it up. */
  if (sc->arpcom.ac_if.if_bpf != 0)
    /* Pass it up. */
    bpf_mtap(sc->arpcom.ac_if.if_bpf, m);
#endif /* NBPFILTER > 0 */

  /*
   * Finally pass this packet up to higher layers.
   */
  ether_input (&sc->arpcom.ac_if, eh, m);
  sc->arpcom.ac_if.if_ipackets++;
  
#if I82586_DEBUG
  I82586_TRACE(sc, I82586_RX_OK, sc->arpcom.ac_if.if_ipackets);
#endif

  return (0);
}

/*
 * Start transmission on an interface.
 */
void
i82586_start(struct ifnet *ifp)
{
  struct ie_softc *sc = ifp->if_softc;
  
  if ((ifp->if_flags & (IFF_RUNNING | IFF_OACTIVE)) != IFF_RUNNING)
    return;
  
#if I82586_DEBUG
  I82586_TRACE(sc, I82586_TX_REQ, sc->xmit_busy);
#endif
  
  rtems_event_send (sc->tx_task, i82586_TX_EVENT);
}

static void
i82586_tx_task(void *arg)
{
  struct ie_softc *sc = arg;
  rtems_event_set events;

  for (;;) {
    rtems_bsdnet_event_receive (i82586_TX_EVENT,
                                RTEMS_WAIT | RTEMS_EVENT_ANY,
                                0, &events);
    
#if I82586_DEBUG
    I82586_TRACE(sc, I82586_TX_EVT, sc->xmit_busy);
    
    if (sc->sc_debug)
      printf ("%s: =======\n", sc->arpcom.ac_if.if_name);
#endif

    i82586_start_tx(sc);
  }
}

static void
i82586_start_tx(struct ie_softc *sc)
{
  struct ifnet *ifp = &sc->arpcom.ac_if;
  struct mbuf *m0, *m;
  int  buffer, head, xbase;
  u_short  len;
  int  s;
  
#if I82586_DEBUG
  I82586_TRACE(sc, I82586_START_TX, sc->xmit_busy);
#endif
  
  if ((ifp->if_flags & (IFF_RUNNING | IFF_OACTIVE)) != IFF_RUNNING)
  {
#if I82586_DEBUG
    I82586_TRACE(sc, I82586_TX_ACTIVE, ifp->if_snd.ifq_len);
#endif
    return;
  }
  
  for (;;) {
    if (sc->xmit_busy == NTXBUF) {
      ifp->if_flags |= IFF_OACTIVE;
      break;
    }

    head = sc->xchead;
    xbase = sc->xbds;

    IF_DEQUEUE(&ifp->if_snd, m0);
    if (m0 == 0)
      break;

    /* We need to use m->m_pkthdr.len, so require the header */
    if ((m0->m_flags & M_PKTHDR) == 0)
      panic("i82586_start: no header mbuf");

#if NBPFILTER > 0
    /* Tap off here if there is a BPF listener. */
    if (ifp->if_bpf)
      bpf_mtap(ifp->if_bpf, m0);
#endif

#if I82586_DEBUG
    if (sc->sc_debug & IED_ENQ)
      printf("%s: fill buffer %d\n", sc->arpcom.ac_if.if_name,
             sc->xchead);
#endif

    if (m0->m_pkthdr.len > IE_TBUF_SIZE)
      printf("%s: tbuf overflow\n", sc->arpcom.ac_if.if_name);

    buffer = IE_XBUF_ADDR(sc, head);
    for (m = m0; m != 0; m = m->m_next) {
      (sc->memcopyout)(sc, mtod(m,caddr_t), buffer, m->m_len);
      buffer += m->m_len;
    }

    len = max(m0->m_pkthdr.len, ETHER_MIN_LEN);
    m_freem(m0);

    /*
     * Setup the transmit buffer descriptor here, while we
     * know the packet's length.
     */
    sc->ie_bus_write16(sc, IE_XBD_FLAGS(xbase, head),
                       len | IE_TBD_EOL);
    sc->ie_bus_write16(sc, IE_XBD_NEXT(xbase, head), 0xffff);
    sc->ie_bus_write24(sc, IE_XBD_BUF(xbase, head),
                       IE_XBUF_ADDR(sc, head));

    if (++head == NTXBUF)
      head = 0;
    sc->xchead = head;

#if I82586_DEBUG
    I82586_TRACE(sc, I82586_TX_START, sc->xmit_busy);
#endif
      
    s = splnet();
    /* Start the first packet transmitting. */
    if (sc->xmit_busy == 0)
      iexmit(sc);

    sc->xmit_busy++;
      
    splx(s);
  }
}

/*
 * Probe IE's ram setup   [ Move all this into MD front-end!? ]
 * Use only if SCP and ISCP represent offsets into shared ram space.
 */
int
i82586_proberam(struct ie_softc *sc)
{
  int result, off;

  /* Put in 16-bit mode */
  off = IE_SCP_BUS_USE(sc->scp);
  sc->ie_bus_write16(sc, off, 0);
  IE_BUS_BARRIER(sc, off, 2, BUS_SPACE_BARRIER_WRITE);

  /* Set the ISCP `busy' bit */
  off = IE_ISCP_BUSY(sc->iscp);
  sc->ie_bus_write16(sc, off, 1);
  IE_BUS_BARRIER(sc, off, 2, BUS_SPACE_BARRIER_WRITE);

  if (sc->hwreset)
    (sc->hwreset)(sc, CHIP_PROBE);

  (sc->chan_attn) (sc, CHIP_PROBE);

  delay(100);    /* wait a while... */

  /* Read back the ISCP `busy' bit; it should be clear by now */
  off = IE_ISCP_BUSY(sc->iscp);
  IE_BUS_BARRIER(sc, off, 2, BUS_SPACE_BARRIER_READ);
  result = sc->ie_bus_read16(sc, off) == 0;

  /* Acknowledge any interrupts we may have caused. */
  ie_ack(sc, IE_ST_WHENCE);

  return (result);
}

void
i82586_reset(struct ie_softc *sc, int hard)
{
  int s = splnet();

  if (hard)
    printf("%s: reset\n", sc->arpcom.ac_if.if_name);

  /* Clear OACTIVE in case we're called from watchdog (frozen xmit). */
  sc->arpcom.ac_if.if_timer = 0;
  sc->arpcom.ac_if.if_flags &= ~IFF_OACTIVE;

  /*
   * Stop i82586 dead in its tracks.
   */
  if (i82586_start_cmd(sc, IE_RUC_ABORT | IE_CUC_ABORT, 0, 0, 0))
    printf("%s: abort commands timed out\n", sc->arpcom.ac_if.if_name);

  /*
   * This can really slow down the i82586_reset() on some cards, but it's
   * necessary to unwedge other ones (eg, the Sun VME ones) from certain
   * lockups.
   */
  if (hard && sc->hwreset)
    (sc->hwreset)(sc, CARD_RESET);

  delay(100);
  ie_ack(sc, IE_ST_WHENCE);

  if ((sc->arpcom.ac_if.if_flags & IFF_UP) != 0) {
    i82586_init(&sc->arpcom.ac_if);
  }

  splx(s);
}

static void
setup_simple_command(struct ie_softc *sc, int cmd, int cmdbuf)
{
  /* Setup a simple command */
  sc->ie_bus_write16(sc, IE_CMD_COMMON_STATUS(cmdbuf), 0);
  sc->ie_bus_write16(sc, IE_CMD_COMMON_CMD(cmdbuf), cmd | IE_CMD_LAST);
  sc->ie_bus_write16(sc, IE_CMD_COMMON_LINK(cmdbuf), 0xffff);

  /* Assign the command buffer to the SCB command list */
  sc->ie_bus_write16(sc, IE_SCB_CMDLST(sc->scb), cmdbuf);
}

/*
 * Run the time-domain reflectometer.
 */
static void
ie_run_tdr(struct ie_softc *sc, int cmd)
{
  int result;

  setup_simple_command(sc, IE_CMD_TDR, cmd);
  sc->ie_bus_write16(sc, IE_CMD_TDR_TIME(cmd), 0);

  if (i82586_start_cmd(sc, IE_CUC_START, cmd, IE_STAT_COMPL, 0) ||
      (sc->ie_bus_read16(sc, IE_CMD_COMMON_STATUS(cmd)) & IE_STAT_OK) == 0)
    result = 0x10000; /* XXX */
  else
    result = sc->ie_bus_read16(sc, IE_CMD_TDR_TIME(cmd));

  /* Squash any pending interrupts */
  ie_ack(sc, IE_ST_WHENCE);

  if (result & IE_TDR_SUCCESS)
    return;

  if (result & 0x10000)
    printf("%s: TDR command failed\n", sc->arpcom.ac_if.if_name);
  else if (result & IE_TDR_XCVR)
    printf("%s: transceiver problem\n", sc->arpcom.ac_if.if_name);
  else if (result & IE_TDR_OPEN)
    printf("%s: TDR detected incorrect termination %d clocks away\n",
           sc->arpcom.ac_if.if_name, result & IE_TDR_TIME);
  else if (result & IE_TDR_SHORT)
    printf("%s: TDR detected a short circuit %d clocks away\n",
           sc->arpcom.ac_if.if_name, result & IE_TDR_TIME);
  else
    printf("%s: TDR returned unknown status 0x%x\n",
           sc->arpcom.ac_if.if_name, result);
}


/*
 * i82586_setup_bufs: set up the buffers
 *
 * We have a block of KVA at sc->buf_area which is of size sc->buf_area_sz.
 * this is to be used for the buffers.  The chip indexs its control data
 * structures with 16 bit offsets, and it indexes actual buffers with
 * 24 bit addresses.   So we should allocate control buffers first so that
 * we don't overflow the 16 bit offset field.   The number of transmit
 * buffers is fixed at compile time.
 *
 */
static void
i82586_setup_bufs(struct ie_softc *sc)
{
  int  ptr = sc->buf_area;  /* memory pool */
  int     n, r;

  /*
   * step 0: zero memory and figure out how many recv buffers and
   * frames we can have.
   */
  ptr = (ptr + 3) & ~3;  /* set alignment and stick with it */


  /*
   *  step 1: lay out data structures in the shared-memory area
   */

  /* The no-op commands; used if "nop-chaining" is in effect */
  sc->nop_cmds = ptr;
  ptr += NTXBUF * IE_CMD_NOP_SZ;

  /* The transmit commands */
  sc->xmit_cmds = ptr;
  ptr += NTXBUF * IE_CMD_XMIT_SZ;

  /* The transmit buffers descriptors */
  sc->xbds = ptr;
  ptr += NTXBUF * IE_XBD_SZ;

  /* The transmit buffers */
  sc->xbufs = ptr;
  ptr += NTXBUF * IE_TBUF_SIZE;

  ptr = (ptr + 3) & ~3;    /* re-align.. just in case */

  /* Compute free space for RECV stuff */
  n = sc->buf_area_sz - (ptr - sc->buf_area);

  /* Compute size of one RECV frame */
  r = IE_RFRAME_SZ + ((IE_RBD_SZ + IE_RBUF_SIZE) * B_PER_F);

  sc->nframes = n / r;

  if (sc->nframes <= 0)
    panic("ie: bogus buffer calc\n");

  sc->nrxbuf = sc->nframes * B_PER_F;

  /* The receice frame descriptors */
  sc->rframes = ptr;
  ptr += sc->nframes * IE_RFRAME_SZ;

  /* The receive buffer descriptors */
  sc->rbds = ptr;
  ptr += sc->nrxbuf * IE_RBD_SZ;

  /* The receive buffers */
  sc->rbufs = ptr;
  ptr += sc->nrxbuf * IE_RBUF_SIZE;

#if I82586_DEBUG
  printf("%s: %d frames %d bufs\n", sc->arpcom.ac_if.if_name, sc->nframes,
         sc->nrxbuf);
#endif

  /*
   * step 2: link together the recv frames and set EOL on last one
   */
  for (n = 0; n < sc->nframes; n++) {
    int m = (n == sc->nframes - 1) ? 0 : n + 1;

    /* Clear status */
    sc->ie_bus_write16(sc, IE_RFRAME_STATUS(sc->rframes,n), 0);

    /* RBD link = NULL */
    sc->ie_bus_write16(sc, IE_RFRAME_BUFDESC(sc->rframes,n),
                       0xffff);

    /* Make a circular list */
    sc->ie_bus_write16(sc, IE_RFRAME_NEXT(sc->rframes,n),
                       IE_RFRAME_ADDR(sc->rframes,m));

    /* Mark last as EOL */
    sc->ie_bus_write16(sc, IE_RFRAME_LAST(sc->rframes,n),
                       ((m==0)? (IE_FD_EOL|IE_FD_SUSP) : 0));
  }

  /*
   * step 3: link the RBDs and set EOL on last one
   */
  for (n = 0; n < sc->nrxbuf; n++) {
    int m = (n == sc->nrxbuf - 1) ? 0 : n + 1;

    /* Clear status */
    sc->ie_bus_write16(sc, IE_RBD_STATUS(sc->rbds,n), 0);

    /* Make a circular list */
    sc->ie_bus_write16(sc, IE_RBD_NEXT(sc->rbds,n),
                       IE_RBD_ADDR(sc->rbds,m));

    /* Link to data buffers */
    sc->ie_bus_write24(sc, IE_RBD_BUFADDR(sc->rbds, n),
                       IE_RBUF_ADDR(sc, n));
    sc->ie_bus_write16(sc, IE_RBD_BUFLEN(sc->rbds,n),
                       IE_RBUF_SIZE | ((m==0)?IE_RBD_EOL:0));
  }

  /*
   * step 4: all xmit no-op commands loopback onto themselves
   */
  for (n = 0; n < NTXBUF; n++) {
    sc->ie_bus_write16(sc, IE_CMD_NOP_STATUS(sc->nop_cmds, n), 0);

    sc->ie_bus_write16(sc, IE_CMD_NOP_CMD(sc->nop_cmds, n),
                       IE_CMD_NOP);

    sc->ie_bus_write16(sc, IE_CMD_NOP_LINK(sc->nop_cmds, n),
                       IE_CMD_NOP_ADDR(sc->nop_cmds, n));
  }


  /*
   * step 6: set the head and tail pointers on receive to keep track of
   * the order in which RFDs and RBDs are used.
   */

  /* Pointers to last packet sent and next available transmit buffer. */
  sc->xchead = sc->xctail = 0;

  /* Clear transmit-busy flag and set number of free transmit buffers. */
  sc->xmit_busy = 0;

  /*
   * Pointers to first and last receive frame.
   * The RFD pointed to by rftail is the only one that has EOL set.
   */
  sc->rfhead = 0;
  sc->rftail = sc->nframes - 1;

  /*
   * Pointers to first and last receive descriptor buffer.
   * The RBD pointed to by rbtail is the only one that has EOL set.
   */
  sc->rbhead = 0;
  sc->rbtail = sc->nrxbuf - 1;

/* link in recv frames * and buffer into the scb. */
#if I82586_DEBUG
  printf("%s: reserved %d bytes\n",
         sc->arpcom.ac_if.if_name, ptr - sc->buf_area);
#endif
}

static int
ie_cfg_setup(struct ie_softc *sc, int cmd, int promiscuous, int manchester)
{
  int cmdresult, status;
  u_int8_t buf[IE_CMD_CFG_SZ]; /* XXX malloc? */

  *IE_CMD_CFG_CNT(buf)       = 0x0c;
  *IE_CMD_CFG_FIFO(buf)      = 8;
  *IE_CMD_CFG_SAVEBAD(buf)   = 0x40;
  *IE_CMD_CFG_ADDRLEN(buf)   = 0x2e;
  *IE_CMD_CFG_PRIORITY(buf)  = 0;
  *IE_CMD_CFG_IFS(buf)       = 0x60;
  *IE_CMD_CFG_SLOT_LOW(buf)  = 0;
  *IE_CMD_CFG_SLOT_HIGH(buf) = 0xf2;
  *IE_CMD_CFG_PROMISC(buf)   = !!promiscuous | manchester << 2;
  *IE_CMD_CFG_CRSCDT(buf)    = 0;
  *IE_CMD_CFG_MINLEN(buf)    = 64;
  *IE_CMD_CFG_JUNK(buf)      = 0xff;
  sc->memcopyout(sc, buf, cmd, IE_CMD_CFG_SZ);
  setup_simple_command(sc, IE_CMD_CONFIG, cmd);
  IE_BUS_BARRIER(sc, cmd, IE_CMD_CFG_SZ, BUS_SPACE_BARRIER_WRITE);

  cmdresult = i82586_start_cmd(sc, IE_CUC_START, cmd, IE_STAT_COMPL, 0);
  status = sc->ie_bus_read16(sc, IE_CMD_COMMON_STATUS(cmd));
  if (cmdresult != 0) {
    printf("%s: configure command timed out; status %x\n",
           sc->arpcom.ac_if.if_name, status);
    return (0);
  }
  if ((status & IE_STAT_OK) == 0) {
    printf("%s: configure command failed; status %x\n",
           sc->arpcom.ac_if.if_name, status);
    return (0);
  }

  /* Squash any pending interrupts */
  ie_ack(sc, IE_ST_WHENCE);
  return (1);
}

static int
ie_ia_setup(struct ie_softc *sc, int cmdbuf)
{
  int cmdresult, status;

  setup_simple_command(sc, IE_CMD_IASETUP, cmdbuf);

  (sc->memcopyout)(sc, sc->arpcom.ac_enaddr,
                   IE_CMD_IAS_EADDR(cmdbuf), ETHER_ADDR_LEN);
  
  cmdresult = i82586_start_cmd(sc, IE_CUC_START, cmdbuf, IE_STAT_COMPL, 0);
  status = sc->ie_bus_read16(sc, IE_CMD_COMMON_STATUS(cmdbuf));
  if (cmdresult != 0) {
    printf("%s: individual address command timed out; status %x\n",
           sc->arpcom.ac_if.if_name, status);
    return (0);
  }
  if ((status & IE_STAT_OK) == 0) {
    printf("%s: individual address command failed; status %x\n",
           sc->arpcom.ac_if.if_name, status);
    return (0);
  }

  /* Squash any pending interrupts */
  ie_ack(sc, IE_ST_WHENCE);
  return (1);
}

/*
 * Run the multicast setup command.
 * Called at splnet().
 */
static int
ie_mc_setup(struct ie_softc *sc, int cmdbuf)
{
  int cmdresult, status;

  if (sc->mcast_count == 0)
    return (1);

  setup_simple_command(sc, IE_CMD_MCAST, cmdbuf);

  (sc->memcopyout)(sc, (caddr_t)sc->mcast_addrs,
                   IE_CMD_MCAST_MADDR(cmdbuf),
                   sc->mcast_count * ETHER_ADDR_LEN);

  sc->ie_bus_write16(sc, IE_CMD_MCAST_BYTES(cmdbuf),
                     sc->mcast_count * ETHER_ADDR_LEN);

  /* Start the command */
  cmdresult = i82586_start_cmd(sc, IE_CUC_START, cmdbuf, IE_STAT_COMPL, 0);
  status = sc->ie_bus_read16(sc, IE_CMD_COMMON_STATUS(cmdbuf));
  if (cmdresult != 0) {
    printf("%s: multicast setup command timed out; status %x\n",
           sc->arpcom.ac_if.if_name, status);
    return (0);
  }
  if ((status & IE_STAT_OK) == 0) {
    printf("%s: multicast setup command failed; status %x\n",
           sc->arpcom.ac_if.if_name, status);
    return (0);
  }

  /* Squash any pending interrupts */
  ie_ack(sc, IE_ST_WHENCE);
  return (1);
}

/*
 * This routine takes the environment generated by check_ie_present() and adds
 * to it all the other structures we need to operate the adapter.  This
 * includes executing the CONFIGURE, IA-SETUP, and MC-SETUP commands, starting
 * the receiver unit, and clearing interrupts.
 *
 * THIS ROUTINE MUST BE CALLED AT splnet() OR HIGHER.
 */
void
i82586_init(void *arg)
{
  struct ie_softc *sc  = arg;
  struct ifnet    *ifp = &sc->arpcom.ac_if;
  int             cmd;

  sc->async_cmd_inprogress = 0;
  sc->xmit_busy = 0;

#if I82586_DEBUG
  memset(sc->trace_flow, 0, sizeof(sc->trace_flow));
  sc->trace_flow_wrap = 0;
#endif
  sc->trace_flow_in = 0;
  
  cmd = sc->buf_area;
  
#if I82586_DEBUG
  printf ("%s: sc_debug at 0x%08x\n", sc->arpcom.ac_if.if_name, (unsigned int) &sc->sc_debug);
#endif
  
  /*
   * Send the configure command first.
   */
  if (ie_cfg_setup(sc, cmd, sc->promisc, 0) == 0)
    return;

  /*
   * Send the Individual Address Setup command.
   */
  if (ie_ia_setup(sc, cmd) == 0)
    return;

  /*
   * Run the time-domain reflectometer.
   */
  ie_run_tdr(sc, cmd);

  /*
   * Set the multi-cast filter, if any
   */
  if (ie_mc_setup(sc, cmd) == 0)
    return;

  /*
   * If no tasks exist, create them. Need to add something to allow
   * different names for the different devices.
   */
  if (sc->intr_task == 0)
    sc->intr_task = rtems_bsdnet_newproc ("IEi0", 2048, i82586_intr_task, sc);
  if (sc->tx_task == 0)
    sc->tx_task = rtems_bsdnet_newproc ("IEt0", 2048, i82586_tx_task, sc);


  /*
   * Acknowledge any interrupts we have generated thus far.
   */
  ie_ack(sc, IE_ST_WHENCE);

  /*
   * Set up the transmit and recv buffers.
   */
  i82586_setup_bufs(sc);

  if (sc->hwinit)
    (sc->hwinit)(sc);

  ifp->if_flags |= IFF_RUNNING;
  ifp->if_flags &= ~IFF_OACTIVE;

  if (NTXBUF < 2)
    sc->do_xmitnopchain = 0;

  i82586_start_transceiver(sc);
}

/*
 * Start the RU and possibly the CU unit
 */
static void
i82586_start_transceiver(struct ie_softc *sc)
{
#if I82586_DEBUG
      I82586_TRACE(sc, I82586_RX_START, 0);
#endif
      
  /*
   * Start RU at current position in frame & RBD lists.
   */
  sc->ie_bus_write16(sc, IE_RFRAME_BUFDESC(sc->rframes,sc->rfhead),
                     IE_RBD_ADDR(sc->rbds, sc->rbhead));

  sc->ie_bus_write16(sc, IE_SCB_RCVLST(sc->scb),
                     IE_RFRAME_ADDR(sc->rframes,sc->rfhead));

  if (sc->do_xmitnopchain) {
    /* Stop transmit command chain */
    if (i82586_start_cmd(sc, IE_CUC_SUSPEND|IE_RUC_SUSPEND, 0, 0, 0))
      printf("%s: CU/RU stop command timed out\n",
             sc->arpcom.ac_if.if_name);

    /* Start the receiver & transmitter chain */
    /* sc->scb->ie_command_list =
       IEADDR(sc->nop_cmds[(sc->xctail+NTXBUF-1) % NTXBUF]);*/
    sc->ie_bus_write16(sc, IE_SCB_CMDLST(sc->scb),
                       IE_CMD_NOP_ADDR(
                         sc->nop_cmds,
                         (sc->xctail + NTXBUF - 1) % NTXBUF));

    if (i82586_start_cmd(sc, IE_CUC_START|IE_RUC_START, 0, 0, 0))
      printf("%s: CU/RU command timed out\n",
             sc->arpcom.ac_if.if_name);
  } else {
    if (i82586_start_cmd(sc, IE_RUC_START, 0, 0, 0))
      printf("%s: RU command timed out\n",
             sc->arpcom.ac_if.if_name);
  }
}

void
i82586_stop(struct ifnet *ifp, int disable)
{
  struct ie_softc *sc = ifp->if_softc;

  if (i82586_start_cmd(sc, IE_RUC_SUSPEND | IE_CUC_SUSPEND, 0, 0, 0))
    printf("%s: iestop: disable commands timed out\n",
           sc->arpcom.ac_if.if_name);
}

int
i82586_ioctl(struct ifnet *ifp, int cmd, caddr_t data)
{
  struct ie_softc *sc  = ifp->if_softc;
/*  struct ifreq    *ifr = (struct ifreq *)data; */
  int             s;
  int             error = 0;

  s = splnet();
  switch(cmd) {
    case SIOCGIFMEDIA:
    case SIOCSIFMEDIA:
      break;
    case SIO_RTEMS_SHOW_STATS:
#if I82586_DEBUG
      print_softie(sc);
#endif
      break;
    default:
      error = ether_ioctl(ifp, cmd, data);
      if (error == ENETRESET) {
        /*
         * Multicast list has changed; set the hardware filter
         * accordingly.
         */
        ie_mc_reset(sc);
        error = 0;
      }
      break;
  }
#if I82586_DEBUG
  if (cmd == SIOCSIFFLAGS)
    sc->sc_debug = (ifp->if_flags & IFF_DEBUG) ? IED_ALL : 0;
#endif
  splx(s);
  return (error);
}

static void
ie_mc_reset(struct ie_softc *sc)
{
  struct ether_multi *enm;
  struct ether_multistep step;
  int size;

  /*
   * Step through the list of addresses.
   */
  again:
  size = 0;
  sc->mcast_count = 0;
  ETHER_FIRST_MULTI(step, &sc->arpcom, enm);
  while (enm) {
    size += 6;
    if (sc->mcast_count >= IE_MAXMCAST ||
        memcmp(enm->enm_addrlo, enm->enm_addrhi, 6) != 0) {
      sc->arpcom.ac_if.if_flags |= IFF_ALLMULTI;
      i82586_ioctl(&sc->arpcom.ac_if,
                   SIOCSIFFLAGS, (void *)0);
      return;
    }
    ETHER_NEXT_MULTI(step, enm);
  }

  if (size > sc->mcast_addrs_size) {
    /* Need to allocate more space */
    if (sc->mcast_addrs_size)
      free(sc->mcast_addrs, M_IPMADDR);
    sc->mcast_addrs = (char *)
      malloc(size, M_IPMADDR, M_WAITOK);
    sc->mcast_addrs_size = size;
  }

  /*
   * We've got the space; now copy the addresses
   */
  ETHER_FIRST_MULTI(step, &sc->arpcom, enm);
  while (enm) {
    if (sc->mcast_count >= IE_MAXMCAST)
      goto again; /* Just in case */

    memcpy(&sc->mcast_addrs[sc->mcast_count], enm->enm_addrlo, 6);
    sc->mcast_count++;
    ETHER_NEXT_MULTI(step, enm);
  }
  sc->want_mcsetup = 1;
}

/*
 * Media change callback.
 */
int
i82586_mediachange(struct ifnet *ifp)
{
  struct ie_softc *sc = ifp->if_softc;

  if (sc->sc_mediachange)
    return ((*sc->sc_mediachange)(sc));
  return (0);
}

/*
 * Media status callback.
 */
void
i82586_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
  struct ie_softc *sc = ifp->if_softc;
  
  if (sc->sc_mediastatus)
    (*sc->sc_mediastatus)(sc, ifmr);
}

#if I82586_DEBUG
static void
print_softie(struct ie_softc *sc)
{
  static char *trace_labels[] = {
    "INTS_REQ",
    "INTS_IN",
    "INTS_LOOPS",
    "INTS_OUT",
    "RX_INT",
    "RX_DROP",
    "RX_ERR",
    "RX_OK",
    "RX_START",
    "START_TX",
    "TX_START",
    "TX_INT",
    "TX_REQ",
    "TX_EVT",
    "TX_EMIT",
    "TX_BAD",
    "TX_ACTIVE",
    "TRACE_CNT"
  };
  
  int i;
  
  printf("i82586 %s:\n", sc->arpcom.ac_if.if_name);

  printf(" iobase=%p\n", sc->sc_iobase);
  
  printf(" scp=0x%08x\t\tiscp=0x%08x\t\tscb=0x%08x\n",
         sc->scp, sc->iscp, sc->scb);
  printf(" buf_area=0x%08x\tbuf_area_sz=0x%08x\n",
         sc->buf_area, sc->buf_area_sz);
  printf(" rframes=0x%08x\trbds=0x%08x\t\trbufs=0x%08x\n",
         sc->rframes, sc->rbds, sc->rbufs);
  printf(" nop_cmds=0x%08x\txmit_cmds=0x%08x\n",
         sc->nop_cmds, sc->xmit_cmds);
  printf(" xbds=0x%08x\txbufs=0x%08x\n\n",
         sc->xbds, sc->xbufs);
  printf(" rfhead=%d\trftail=%d\n",
         sc->rfhead, sc->rftail);
  printf(" rbhead=%d\trbtail=%d\n",
         sc->rbhead, sc->rbtail);
  printf(" nframes=%d\tnrxbuf=%d\trnr_expect=%d\n",
         sc->nframes, sc->nrxbuf, sc->rnr_expect);
  printf(" xchead=%d\txctail=%d\n",
         sc->xchead, sc->xctail);
  printf(" xmit_busy=%d\txmit_req=%d\tdo_xmitnopchain=%d\n",
         sc->xmit_busy, sc->xmit_req, sc->do_xmitnopchain);
  printf(" promisc=%d\tasync_cmd_inprogress=%d\n\n",
         sc->promisc, sc->async_cmd_inprogress);

  {
    int cnt;
    int in;
    int lfdone = 0;
    char *tabs;
    
    if (!sc->trace_flow_wrap) {
      cnt = sc->trace_flow_in;
      in = 0;
    }
    else {
      cnt = I82586_TRACE_FLOW;
      in = sc->trace_flow_in;
    }

    sc->trace_flow_in = sc->trace_flow_wrap = 0;

    cnt /= 2;
    
    for (i = 0; i < cnt; i++) {
      if (!lfdone) {
        switch (sc->trace_flow[in]) {
          case I82586_INTS_REQ:
          case I82586_INTS_IN:
            printf("\n");
        }
      }

      lfdone = 0;

      if (strlen(trace_labels[sc->trace_flow[in]]) < 8)
        tabs = "\t\t";
      else
        tabs = "\t";
      
      printf(" %d\t%s%s0x%08x (%d)\n",
             i, trace_labels[sc->trace_flow[in]], tabs,
             sc->trace_flow[in + 1], sc->trace_flow[in + 1]);

      switch (sc->trace_flow[in]) {
        case I82586_INTS_REQ:
        case I82586_INTS_OUT:
          lfdone = 1;
          printf("\n");
      }
      
      in += 2;
      
      if (in >= I82586_TRACE_FLOW)
        in = 0;
    }
  }
}

static void
print_rbd(struct ie_softc *sc, int n)
{
  printf("RBD at %08x:\n  status %04x, next %04x, buffer %lx\n"
         "length/EOL %04x\n", IE_RBD_ADDR(sc->rbds,n),
         sc->ie_bus_read16(sc, IE_RBD_STATUS(sc->rbds,n)),
         sc->ie_bus_read16(sc, IE_RBD_NEXT(sc->rbds,n)),
         (u_long)0,/*bus_space_read_4(sc->bt, sc->bh, IE_RBD_BUFADDR(sc->rbds,n)),-* XXX */
         sc->ie_bus_read16(sc, IE_RBD_BUFLEN(sc->rbds,n)));
}

#endif
