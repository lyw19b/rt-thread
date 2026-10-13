#ifndef __LS2K_UART_H__
#define __LS2K_UART_H__

#include <rthw.h>

/* UART registers */
#define UART_DAT(base)        HWREG8(base + 0x00)
#define UART_IER(base)        HWREG8(base + 0x01)
#define UART_IIR(base)        HWREG8(base + 0x02)
#define UART_FCR(base)        HWREG8(base + 0x02)
#define UART_LCR(base)        HWREG8(base + 0x03)
#define UART_MCR(base)        HWREG8(base + 0x04)
#define UART_LSR(base)        HWREG8(base + 0x05)
#define UART_MSR(base)        HWREG8(base + 0x06)

#define UART_LSB(base)        HWREG8(base + 0x00)
#define UART_MSB(base)        HWREG8(base + 0x01)

/* interrupt enable register */
#define    IER_IRxE         0x1
#define    IER_ITxE         0x2
#define    IER_ILE          0x4
#define    IER_IME          0x8

/* interrupt identification register */
#define    IIR_IMASK        0xf    /* mask */
#define    IIR_RXTOUT       0xc    /* receive timeout */
#define    IIR_RLS          0x6    /* receive line status */
#define    IIR_RXRDY        0x4    /* receive ready */
#define    IIR_TXRDY        0x2    /* transmit ready */
#define    IIR_NOPEND       0x1    /* nothing */
#define    IIR_MLSC         0x0    /* modem status */
#define    IIR_FIFO_MASK    0xc0    /* set if FIFOs are enabled */

/* fifo control register */
#define    FIFO_ENABLE      0x01    /* enable fifo */
#define    FIFO_RCV_RST     0x02    /* reset receive fifo */
#define    FIFO_XMT_RST     0x04    /* reset transmit fifo */
#define    FIFO_DMA_MODE    0x08    /* enable dma mode */
#define    FIFO_TRIGGER_1   0x00    /* trigger at 1 char */
#define    FIFO_TRIGGER_4   0x40    /* trigger at 4 chars */
#define    FIFO_TRIGGER_8   0x80    /* trigger at 8 chars */
#define    FIFO_TRIGGER_14  0xc0    /* trigger at 14 chars */

/* character format control register */
#define    CFCR_DLAB        0x80    /* divisor latch */
#define    CFCR_SBREAK      0x40    /* send break */
#define    CFCR_PZERO       0x30    /* zero parity */
#define    CFCR_PONE        0x20    /* one parity */
#define    CFCR_PEVEN       0x10    /* even parity */
#define    CFCR_PODD        0x00    /* odd parity */
#define    CFCR_PENAB       0x08    /* parity enable */
#define    CFCR_STOPB       0x04    /* 2 stop bits */
#define    CFCR_8BITS       0x03    /* 8 data bits */
#define    CFCR_7BITS       0x02    /* 7 data bits */
#define    CFCR_6BITS       0x01    /* 6 data bits */
#define    CFCR_5BITS       0x00    /* 5 data bits */

/* modem control register */
#define    MCR_LOOPBACK     0x10    /* loopback */
#define    MCR_IENABLE      0x08    /* output 2 = int enable */
#define    MCR_DRS          0x04    /* output 1 = xxx */
#define    MCR_RTS          0x02    /* enable RTS */
#define    MCR_DTR          0x01    /* enable DTR */

/* line status register */
#define    LSR_RCV_FIFO     0x80    /* error in receive fifo */
#define    LSR_TSRE         0x40    /* transmitter empty */
#define    LSR_TXRDY        0x20    /* transmitter ready */
#define    LSR_BI           0x10    /* break detected */
#define    LSR_FE           0x08    /* framing error */
#define    LSR_PE           0x04    /* parity error */
#define    LSR_OE           0x02    /* overrun error */
#define    LSR_RXRDY        0x01    /* receiver ready */
#define    LSR_RCV_MASK     0x1f



// Divisor Latch
// #define UART_LSB 0
// #define UART_MSB 1

// DAT
// #define UART_RHR 0
// #define UART_THR 0

// Interrupt Enable Register
// #define UART_IER 1
#define UART_IER_IRxE         0x01 /* Enable receiver data interrupt */
#define UART_IER_ITxE         0x02 /* Enable Transmitter holding register int. */
#define UART_IER_ILE          0x04 /* Enable receiver line status interrupt */
#define UART_IER_IME          0x08 /* Enable Modem status interrupt */

// FIFO Control Register
// #define UART_FCR 2
#define UART_FCR_FIFO_EN      0x01 /* Fifo enable */
#define UART_FCR_CLEAR_RCVR   0x02 /* Clear the RCVR FIFO */
#define UART_FCR_CLEAR_XMIT   0x04 /* Clear the XMIT FIFO */
#define UART_FCR_DMA_SELECT   0x08 /* For DMA applications */
#define UART_FCR_TRIGGER_MASK 0xC0 /* Mask for the FIFO trigger range */
#define UART_FCR_TRIGGER_1    0x00 /* Mask for trigger set at 1 */
#define UART_FCR_TRIGGER_4    0x40 /* Mask for trigger set at 4 */
#define UART_FCR_TRIGGER_8    0x80 /* Mask for trigger set at 8 */
#define UART_FCR_TRIGGER_14   0xC0 /* Mask for trigger set at 14 */
#define UART_FCR_FIFO_CLEAR   (UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT)

// Interrupt Identification Register
// #define UART_IIR 2
#define UART_IIR_NO_INT 0x01 /* No interrupts pending */
#define UART_IIR_ID     0x06 /* Mask for the interrupt ID */
#define UART_IIR_MSI    0x00 /* Modem status interrupt */
#define UART_IIR_THRI   0x02 /* Transmitter holding register empty */
#define UART_IIR_RDI    0x04 /* Receiver data interrupt */
#define UART_IIR_RLSI   0x06 /* Receiver line status interrupt */

// Line Control Register
// #define UART_LCR 3
#define UART_LCR_WLS_MSK 0x03 /* character length select mask */
#define UART_LCR_WLS_5   0x00 /* 5 bit character length */
#define UART_LCR_WLS_6   0x01 /* 6 bit character length */
#define UART_LCR_WLS_7   0x02 /* 7 bit character length */
#define UART_LCR_WLS_8   0x03 /* 8 bit character length */
#define UART_LCR_STB     0x04 /* stop Bits, off=1, on=1.5 or 2 */
#define UART_LCR_PEN     0x08 /* Parity eneble */
#define UART_LCR_EPS     0x10 /* Even Parity Select */
#define UART_LCR_STKP    0x20 /* Stick Parity */
#define UART_LCR_SBRK    0x40 /* Set Break */
#define UART_LCR_BKSE    0x80 /* Bank select enable */
#define UART_LCR_DLAB    0x80 /* Divisor latch access bit */

// Modem Control Register
// #define UART_MCR 4
#define UART_MCR_LOOP   0x10 /* Enable loopback test mode */
#define UART_MCR_OUT2   0x08 /* Out2 complement */
#define UART_MCR_OUT1   0x04 /* Out1 complement */
#define UART_MCR_RTS    0x02 /* RTS complement */
#define UART_MCR_DTR    0x01 /* DTR complement */

// Line Status Register
// #define UART_LSR 5
#define UART_LSR_DR  0x01 /* Data ready */
#define UART_LSR_OE  0x02 /* Overrun */
#define UART_LSR_PE  0x04 /* Parity error */
#define UART_LSR_FE  0x08 /* Framing error */
#define UART_LSR_BI  0x10 /* Break */
#define UART_LSR_TFE 0x20 /* Xmit holding register empty */
#define UART_LSR_TE  0x40 /* Xmitter empty */
#define UART_LSR_ERR 0x80 /* Error */

// Modern Status Register
// #define UART_MSR 6
#define UART_MSR_DCD  0x80 /* Data Carrier Detect */
#define UART_MSR_RI   0x40 /* Ring Indicator */
#define UART_MSR_DSR  0x20 /* Data Set Ready */
#define UART_MSR_CTS  0x10 /* Clear to Send */
#define UART_MSR_DDCD 0x08 /* Delta DCD */
#define UART_MSR_TERI 0x04 /* Trailing edge ring indicator */
#define UART_MSR_DDSR 0x02 /* Delta DSR */
#define UART_MSR_DCTS 0x01 /* Delta CTS */

void rt_hw_uart_init(void);

#endif // __LS2K_UART_H__
