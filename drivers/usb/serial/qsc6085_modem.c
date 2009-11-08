/*
 * Copyright (C) 2009 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/irq.h>
#include <mach/irqs.h>
#include <mach/gpio.h>
#include <mach/usb.h>
#include <linux/gpio.h>
#include <linux/wakelock.h>

#define WDR_TIMEOUT       (HZ * 5)
#define MODEM_NO_TRAFFIC_TIME (HZ/4)
#define MODEM_WAKELOCK_TIME (HZ/2)
#define MODEM_INTERFACE_NUM 0

#define BP_MODEM_STATUS 0x20a1
#define BP_RSP_AVAIL 0x01a1
#define BP_SPEED_CHANGE 0x2aa1

#define BP_CAR 0x01
#define BP_DSR 0x02
#define BP_BREAK 0x04
#define BP_RNG 0x08

#define BULKOUT_SIZE 1280

#define AP_NW  16
#define AP_NR  16

struct ap_wb {
	unsigned char *buf;
	dma_addr_t dmah;
	int len;
	int use;
	struct urb *urb;
	struct modem_port *instance;
};

struct ap_rb {
	struct list_head list;
	int size;
	unsigned char *base;
	dma_addr_t dma;
};

struct ap_ru {
	struct list_head list;
	struct ap_rb *buffer;
	struct urb *urb;
	struct modem_port *instance;
};

struct modem_port {
	__u16 modem_status;	/* only used for data modem port */
	__u8  wakeup_gpio;
	struct ap_ru ru[AP_NR];
	struct ap_rb rb[AP_NR];
	struct ap_wb wb[AP_NW];
	struct ap_wb *delayed_wb;
	int rx_buflimit;
	int rx_endpoint;
	unsigned int susp_count;
	unsigned int resuming;
	struct tasklet_struct urb_task;
	struct usb_serial_port *port;
	spinlock_t read_lock;
	spinlock_t write_lock;
	atomic_t wakeup_flag;
	spinlock_t last_traffic_lock;
	unsigned long last_traffic;
	unsigned int readsize;
	unsigned int writesize;
	struct list_head spare_read_urbs;
	struct list_head spare_read_bufs;
	struct list_head filled_read_bufs;
	int processing;
	int sending;
	unsigned int port_closing;
	struct work_struct wake_and_write;
	struct work_struct usb_wkup_work;
};

static struct usb_device_id id_table[] = {
	{USB_DEVICE(0x22b8, 0x2a6e)}, 	/* Sholes CDMA BP modem */
	{USB_DEVICE(0x22b8, 0x2a6f)}, 	/* Sholes CDMA BP modem */
	{},
};

MODULE_DEVICE_TABLE(usb, id_table);

static uint32_t cdma_modem_debug;
module_param_named(cdma_mdm_debug, cdma_modem_debug, uint, 0664);

static struct wake_lock modem_wakelock;

static int modem_wb_alloc(struct modem_port *modem_ptr)
{
	int i;
	struct ap_wb *wb;

	for (i = 0; i < AP_NW; i++) {
		wb = &modem_ptr->wb[i];
		if (!wb->use) {
			wb->use = 1;
			return i;
		}
	}
	return -1;
}

static void modem_write_buffers_free(
		struct modem_port *modem_ptr,
		struct usb_serial *serial)
{
	int i;
	struct ap_wb *wb;
	struct usb_device *usb_dev = serial->dev;

	for (wb = &modem_ptr->wb[0], i = 0; i < AP_NW; i++, wb++)
		usb_buffer_free(usb_dev, modem_ptr->writesize,
				wb->buf, wb->dmah);
}

static int modem_write_buffers_alloc(
		struct modem_port *modem_ptr,
		struct usb_serial *serial)
{
	int i;
	struct ap_wb *wb;

	for (wb = &modem_ptr->wb[0], i = 0; i < AP_NW; i++, wb++) {
		wb->buf = usb_buffer_alloc(serial->dev, modem_ptr->writesize,
					GFP_KERNEL, &wb->dmah);
		if (!wb->buf) {
			while (i != 0) {
				--i;
				--wb;
				usb_buffer_free(serial->dev,
					modem_ptr->writesize,
					wb->buf, wb->dmah);
			}
			return -ENOMEM;
		}
	}
	return 0;
}

static void mark_latest_traffic_time(struct modem_port *modem_port_ptr)
{
	unsigned long flags;
	spin_lock_irqsave(&modem_port_ptr->last_traffic_lock, flags);
	modem_port_ptr->last_traffic = jiffies;
	spin_unlock_irqrestore(&modem_port_ptr->last_traffic_lock, flags);
}

static void stop_data_traffic(struct modem_port *modem_port_ptr)
{
	int i;
	struct usb_serial_port *port =  modem_port_ptr->port;

	if (port == NULL)
		return;
	if (cdma_modem_debug)
		dev_info(&port->dev, "%s() port %d\n",
			__func__, port->number);

	tasklet_disable(&modem_port_ptr->urb_task);

	for (i = 0; i < AP_NW; i++)
		usb_kill_urb(modem_port_ptr->wb[i].urb);
	for (i = 0; i < modem_port_ptr->rx_buflimit; i++)
		usb_kill_urb(modem_port_ptr->ru[i].urb);

	usb_kill_urb(modem_port_ptr->port->interrupt_in_urb);

	tasklet_enable(&modem_port_ptr->urb_task);

	cancel_work_sync(&port->work);
	cancel_work_sync(&modem_port_ptr->usb_wkup_work);
}

static void modem_read_buffers_free(
		struct modem_port *modem_ptr,
		struct usb_serial *serial)
{
	struct usb_device *usb_dev = serial->dev;
	int i;
	int n = modem_ptr->rx_buflimit;

	for (i = 0; i < n; i++)
		usb_buffer_free(usb_dev, modem_ptr->readsize,
				modem_ptr->rb[i].base,
				modem_ptr->rb[i].dma);
}

static int modem_dtr_control(struct usb_serial *serial, int ctrl)
{
	struct modem_port *modem_port_ptr =
		usb_get_serial_data(serial);
	uint8_t bRequesttype =
		(USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_OUT);
	uint16_t wLength = 0;
	uint8_t bRequest = 0x22;
	uint16_t wValue = ctrl;
	uint16_t wIndex = 0;
	unsigned int pipe;
	int status;

	status = usb_autopm_get_interface(serial->interface);
	if (status < 0) {
		dev_err(&serial->dev->dev, "%s %s autopm failed %d",
			dev_driver_string
			(&serial->interface->dev),
			dev_name(&serial->interface->dev), status);
		return status;
	}

	pipe = usb_sndctrlpipe(serial->dev, 0);
	status = usb_control_msg(serial->dev, pipe,
				 bRequest, bRequesttype,
				 wValue, wIndex, NULL, wLength,
				 WDR_TIMEOUT);
	usb_autopm_put_interface(serial->interface);
	if (modem_port_ptr)
		mark_latest_traffic_time(modem_port_ptr);

	return status;
}

static int modem_tiocmget(struct tty_struct *tty, struct file *file)
{
	struct usb_serial_port *port = tty->driver_data;
	struct modem_port *modem_port_ptr = usb_get_serial_data(port->serial);

	if (modem_port_ptr == NULL)
		return 0;

	return (int)modem_port_ptr->modem_status;
}

static int modem_tiocmset(struct tty_struct *tty, struct file *file,
				    unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	int status = 0;

	if (cdma_modem_debug)
		dev_info(&port->dev, "%s: Enter. clear is %d, set is %d \n",
			 __func__, clear, set);

	if (port->number == MODEM_INTERFACE_NUM) {

		if (clear & TIOCM_DTR)
			status = modem_dtr_control(port->serial, 0);

		if (set & TIOCM_DTR)
			status = modem_dtr_control(port->serial, 1);
	}

	if (cdma_modem_debug)
		dev_info(&port->dev, "%s: Exit. Status %d \n",
			__func__, status);

	return status;
}

static void modem_read_bulk_callback(struct urb *urb)
{
	struct ap_rb *buf;
	struct ap_ru *rcv = urb->context;
	struct modem_port *modem_port_ptr;
	int status = urb->status;

	modem_port_ptr = rcv->instance;
	if (modem_port_ptr == NULL)
		return;

	if (modem_port_ptr->port == NULL)
		return;

	mark_latest_traffic_time(modem_port_ptr);
	buf = rcv->buffer;
	buf->size = urb->actual_length;

	spin_lock(&modem_port_ptr->read_lock);
	list_add_tail(&rcv->list, &modem_port_ptr->spare_read_urbs);

	if (likely(status == 0)) {
		modem_port_ptr->processing++;
		list_add_tail(&buf->list, &modem_port_ptr->filled_read_bufs);
	} else {
		if (cdma_modem_debug)
			dev_info(&modem_port_ptr->port->dev,
				 "%s: bulk rx err %d\n", __func__, status);
		/* we drop the buffer due to an error */
		list_add(&buf->list, &modem_port_ptr->spare_read_bufs);
		/* nevertheless the tasklet must be kicked unconditionally
		so the queue cannot dry up */
	}

	if (likely(modem_port_ptr->susp_count == 0) &&
	   (modem_port_ptr->port_closing != 1)
	  )
		tasklet_schedule(&modem_port_ptr->urb_task);
	spin_unlock(&modem_port_ptr->read_lock);

}

static void modem_update_modem_status(struct usb_serial_port *port,
						__u8 modem_status)
{
	struct modem_port *modem_port_ptr;

	if (port->number == MODEM_INTERFACE_NUM) {
		modem_port_ptr = usb_get_serial_data(port->serial);
		if (modem_port_ptr == NULL) {
			dev_err(&port->dev,
				"%s: null modem port pointer. \n",
				 __func__);
			return;
		}

		if (modem_status & BP_CAR)
			modem_port_ptr->modem_status |= TIOCM_CAR;
		else
			modem_port_ptr->modem_status &= ~TIOCM_CAR;

		if (modem_status & BP_DSR)
			modem_port_ptr->modem_status |= TIOCM_DSR;
		else
			modem_port_ptr->modem_status &= ~TIOCM_DSR;

		if (modem_status & BP_RNG)
			modem_port_ptr->modem_status |= TIOCM_RNG;
		else
			modem_port_ptr->modem_status &= ~TIOCM_RNG;

		if (cdma_modem_debug)
			dev_info(&port->dev, "%s: modem status is now %d  \n",
				__func__,
				modem_port_ptr->modem_status);
	}
}

static void modem_interrupt_callback(struct urb *urb)
{
	int status = urb->status;
	uint16_t request_and_type;
	uint8_t modem_status;
	uint8_t *data;
	int length;
	int retval;
	unsigned long flags;

	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct modem_port *modem_port_ptr =
	    usb_get_serial_data(port->serial);

	if (modem_port_ptr->port == NULL)
		return;

	if (port->number != MODEM_INTERFACE_NUM) {
		if (cdma_modem_debug)
			dev_info(&port->dev,
			 "%s: Not Modem port. \n", __func__);
		goto exit;
	}

	switch (status) {
	case 0:
		if (cdma_modem_debug)
			dev_info(&port->dev, "%s: usb -inter_cbk \n", __func__);
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		if (cdma_modem_debug)
			dev_info(&port->dev, "%s: urb shutting down\n", __func__);
		return;
	default:
		dev_err(&port->dev, "%s: nonzero urb status, %d. \n",
				 __func__, status);
		goto exit;
	}

	spin_lock_irqsave(&modem_port_ptr->read_lock, flags);
	modem_port_ptr->processing++;
	spin_unlock_irqrestore(&modem_port_ptr->read_lock, flags);

	length = urb->actual_length;
	data = (__u8 *) urb->transfer_buffer;

	request_and_type = *((__u16 *) data);

	if (cdma_modem_debug)
		dev_info(&port->dev, "%s: request and type is %d \n",
			 __func__, request_and_type);

	switch (request_and_type) {
	case BP_MODEM_STATUS:
		modem_status = data[8];
		if (cdma_modem_debug)
			dev_info(&port->dev, "%s: MODEM status %d \n",
				__func__, modem_status);
		modem_update_modem_status(port, modem_status);
		break;

	case BP_RSP_AVAIL:
		if (cdma_modem_debug)
			dev_info(&port->dev, "%s: BP_RSP_AVAIL \n",
			__func__);
		break;

	case BP_SPEED_CHANGE:
		if (cdma_modem_debug)
			dev_info(&port->dev, "%s: BP_SPEED_CHANGE \n",
			__func__);
		break;

	default:
		if (cdma_modem_debug)
			dev_info(&port->dev,
				 "%s: undefined BP request type %d \n",
				__func__, request_and_type);
		break;
	}

exit:
	spin_lock_irqsave(&modem_port_ptr->read_lock, flags);
	if (modem_port_ptr->susp_count == 0) {
		retval = usb_submit_urb(urb, GFP_ATOMIC);
		if (retval) {
			dev_err(&port->dev,
				 "%s:  submit int usb failed. ret = %d \n",
				__func__, retval);
		}
	}
	modem_port_ptr->processing--;
	spin_unlock_irqrestore(&modem_port_ptr->read_lock, flags);
	mark_latest_traffic_time(modem_port_ptr);
	return;
}

static irqreturn_t gpio_wkup_interrupt_handler (int irq, void *data_ptr)
{
	struct modem_port *modem_port_ptr =
		(struct modem_port *)data_ptr;
	if (modem_port_ptr->port != 0)
		schedule_work(&modem_port_ptr->usb_wkup_work);
	return IRQ_HANDLED;
}

static int modem_open(struct tty_struct *tty,
		struct usb_serial_port *port,
		struct file *filp)
{
	struct modem_port *modem_port_ptr =
	    usb_get_serial_data(port->serial);
	int retval = 0;
	int ret = 0;
	int i;
	unsigned long flags;

	if (cdma_modem_debug)
		dev_info(&port->dev, "%s: Enter. Open Port %d\n",
				 __func__, port->number);

	/*
	 * Force low_latency on
	 */
	if (tty)
		tty->low_latency = 1;

	/* clear the throttle flags */
	port->throttled = 0;
	port->throttle_req = 0;

	if (modem_port_ptr == NULL) {
		dev_err(&port->dev,
			 "%s: null modem port pointer. \n",
			 __func__);
		return -ENODEV;
	}

	port->serial->interface->needs_remote_wakeup = 1;

	modem_port_ptr->port_closing = 0;
	modem_port_ptr->port = port;

	INIT_LIST_HEAD(&modem_port_ptr->spare_read_urbs);
	INIT_LIST_HEAD(&modem_port_ptr->spare_read_bufs);
	INIT_LIST_HEAD(&modem_port_ptr->filled_read_bufs);

	for (i = 0; i < modem_port_ptr->rx_buflimit; i++) {
		list_add(&(modem_port_ptr->ru[i].list),
			 &modem_port_ptr->spare_read_urbs);
	}

	for (i = 0; i < modem_port_ptr->rx_buflimit; i++) {
		list_add(&(modem_port_ptr->rb[i].list),
			 &modem_port_ptr->spare_read_bufs);
	}

	spin_lock_irqsave(&modem_port_ptr->read_lock, flags);
	if (modem_port_ptr->susp_count == 0)
		tasklet_schedule(&modem_port_ptr->urb_task);

	spin_unlock_irqrestore(&modem_port_ptr->read_lock, flags);

	if (port->number == MODEM_INTERFACE_NUM) {
		spin_lock_irqsave(&modem_port_ptr->read_lock, flags);
		if (modem_port_ptr->susp_count == 0) {
			if (port->interrupt_in_urb) {
				/* start to read INT EP data */
				port->interrupt_in_urb->dev = port->serial->dev;
				retval =
				    usb_submit_urb(port->interrupt_in_urb,
						   GFP_KERNEL);
				if (retval) {
					usb_kill_urb(port->interrupt_in_urb);
					dev_err(&port->dev,
						 "%s: retval is %d \n",
						 __func__, retval);
				}
			} else {
				dev_err(&port->dev,
					 "%s: no interrupt endpoint \n",
					 __func__);
			}
		}
		spin_unlock_irqrestore(&modem_port_ptr->read_lock,
					flags);

		/* clean up the modem status data */
		modem_port_ptr->modem_status = 0;

		wake_lock_init(&modem_wakelock, WAKE_LOCK_SUSPEND,
				"omap_usb_modem");

		/* install the BP GPIO wakeup irq and disable it first */
		if (modem_port_ptr->wakeup_gpio) {
			ret = request_irq(
				gpio_to_irq(modem_port_ptr->wakeup_gpio),
				gpio_wkup_interrupt_handler,
				IRQ_DISABLED | IRQ_TYPE_EDGE_RISING,
				"gpio_wakeup", modem_port_ptr);

			if (ret)
				dev_err(&port->dev, "%s request_irq failed \n",
				__func__);
			else
				disable_irq(
				   gpio_to_irq(modem_port_ptr->wakeup_gpio));
		}
	}

	/*  pm interface is taken at
	 *  serial_open() at usb-serial.c.
	 *  For data modem port: the pm count needs to be put back here
	 *  to support the auto-suspend/auto-resume.
	 *  For other test command port: the pm count will be put back at
	 *  the time when port is closed.
	 */
	if (port->number == MODEM_INTERFACE_NUM)
		usb_autopm_put_interface(port->serial->interface);

	if (cdma_modem_debug)
		dev_info(&port->dev, "%s: Exit. retval = %d\n",
			 __func__, retval);

	return retval;
}

#ifdef CONFIG_PM
static void modem_usb_disable_wakeup_irq(struct usb_interface *intf)
{
	struct usb_serial *serial = usb_get_intfdata(intf);
	struct modem_port *modem_port_ptr =
		usb_get_serial_data(serial);
	if (modem_port_ptr == NULL)
		return;

	if (modem_port_ptr->wakeup_gpio) {
		disable_irq_wake(gpio_to_irq(modem_port_ptr->wakeup_gpio));
		disable_irq(gpio_to_irq(modem_port_ptr->wakeup_gpio));
	}
}
#endif

static void modem_rx_tasklet(unsigned long _modem_port)
{
	struct modem_port *modem_port_ptr = (void *)_modem_port;
	struct ap_rb *buf;
	struct tty_struct *tty;
	struct usb_serial_port *port;
	struct ap_ru *rcv;
	unsigned long flags;
	unsigned char throttled;

	if (!modem_port_ptr)
		return;

	port = modem_port_ptr->port;
	if (!port)
		return;

	tty = port->port.tty;

	spin_lock_irqsave(&modem_port_ptr->port->lock, flags);
	throttled = modem_port_ptr->port->throttle_req;
	spin_unlock_irqrestore(&modem_port_ptr->port->lock, flags);
	if (throttled) {
		dev_err(&port->dev, "%s: throttled.\n", __func__);
		return;
	}

next_buffer:
	spin_lock_irqsave(&modem_port_ptr->read_lock, flags);
	if (list_empty(&modem_port_ptr->filled_read_bufs)) {
		spin_unlock_irqrestore(&modem_port_ptr->read_lock, flags);
		goto urbs;
	}
	buf = list_entry(modem_port_ptr->filled_read_bufs.next,
			 struct ap_rb, list);
	list_del(&buf->list);
	spin_unlock_irqrestore(&modem_port_ptr->read_lock, flags);

	tty_buffer_request_room(tty, buf->size);
	spin_lock_irqsave(&modem_port_ptr->port->lock, flags);
	throttled = modem_port_ptr->port->throttle_req;
	spin_unlock_irqrestore(&modem_port_ptr->port->lock, flags);
	if (!throttled)
		tty_insert_flip_string(tty, buf->base, buf->size);
	tty_flip_buffer_push(tty);

	if (throttled) {
		dev_err(&port->dev, "%s: Throttling noticed.\n", __func__);
		spin_lock_irqsave(&modem_port_ptr->read_lock, flags);
		list_add(&buf->list, &modem_port_ptr->filled_read_bufs);
		spin_unlock_irqrestore(&modem_port_ptr->read_lock, flags);
		return;
	}

	spin_lock_irqsave(&modem_port_ptr->read_lock, flags);
	list_add(&buf->list, &modem_port_ptr->spare_read_bufs);
	spin_unlock_irqrestore(&modem_port_ptr->read_lock, flags);
	goto next_buffer;

urbs:
	while (!list_empty(&modem_port_ptr->spare_read_bufs)) {
		spin_lock_irqsave(&modem_port_ptr->read_lock, flags);
		if (list_empty(&modem_port_ptr->spare_read_urbs)) {
			modem_port_ptr->processing = 0;
			spin_unlock_irqrestore(&modem_port_ptr->read_lock,
					       flags);
			if (cdma_modem_debug)
				dev_info(&port->dev,
					 "%s: no urb to create. \n", __func__);
			return;
		}
		rcv = list_entry(modem_port_ptr->spare_read_urbs.next,
				 struct ap_ru, list);
		list_del(&rcv->list);

		buf = list_entry(modem_port_ptr->spare_read_bufs.next,
				 struct ap_rb, list);
		list_del(&buf->list);

		spin_unlock_irqrestore(&modem_port_ptr->read_lock, flags);

		rcv->buffer = buf;

		usb_fill_bulk_urb(rcv->urb, modem_port_ptr->port->serial->dev,
				  modem_port_ptr->rx_endpoint,
				  buf->base,
				  modem_port_ptr->readsize,
				  modem_read_bulk_callback, rcv);
		rcv->urb->transfer_dma = buf->dma;
		rcv->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

		spin_lock_irqsave(&modem_port_ptr->read_lock, flags);
		if ((modem_port_ptr->susp_count > 0) ||
		    usb_submit_urb(rcv->urb, GFP_ATOMIC) < 0) {
			list_add(&buf->list, &modem_port_ptr->spare_read_bufs);
			list_add(&rcv->list, &modem_port_ptr->spare_read_urbs);
			modem_port_ptr->processing = 0;
			dev_err(&port->dev, "%s: submit bulk in  urb failed.\n",
				__func__);
			spin_unlock_irqrestore(&modem_port_ptr->read_lock,
						flags);
			return;
		} else {
			spin_unlock_irqrestore(&modem_port_ptr->read_lock,
						flags);
		}
	}
	spin_lock_irqsave(&modem_port_ptr->read_lock, flags);
	modem_port_ptr->processing = 0;
	spin_unlock_irqrestore(&modem_port_ptr->read_lock, flags);

}

static void modem_close(struct tty_struct *tty,
				  struct usb_serial_port *port,
				  struct file *filp)
{
	struct modem_port *modem_port_ptr;

	if (cdma_modem_debug)
		dev_info(&port->dev, "%s: Enter. Close Port %d  \n",
			 __func__, port->number);

	modem_port_ptr = usb_get_serial_data(port->serial);
	if (!modem_port_ptr) {
		dev_err(&port->dev,
			 "%s: null modem port pointer. \n",
			 __func__);
		return;
	}

	modem_port_ptr->port_closing = 1;

	/*  For the data modem port, the pm interface needs to be get here
	 *  and will be put back at serial_close() of usb-serial.c
	 */

	if (port->number == MODEM_INTERFACE_NUM) {
		usb_autopm_get_interface(port->serial->interface);
		/* free BP GPIO wakeup irq */
		if (modem_port_ptr->wakeup_gpio) {
			free_irq(gpio_to_irq(modem_port_ptr->wakeup_gpio),
					modem_port_ptr);
		}
	}

	stop_data_traffic(modem_port_ptr);
	cancel_work_sync(&modem_port_ptr->wake_and_write);
	modem_port_ptr->port = 0;
	modem_port_ptr->modem_status = 0;
	if (modem_port_ptr->delayed_wb)
		modem_port_ptr->delayed_wb->use = 0;

	if (port->number == MODEM_INTERFACE_NUM)
		wake_lock_destroy(&modem_wakelock);

	if (cdma_modem_debug)
		dev_info(&port->dev, "%s: Exit. \n", __func__);
}

/* caller hold modem_port_ptr->write_lock */
static void modem_write_done(struct modem_port *modem_port_ptr,
				struct ap_wb *wb)
{
	wb->use = 0;
	modem_port_ptr->sending--;
}

static int modem_start_wb(struct modem_port *modem_port_ptr,
				struct ap_wb *wb)
{
	int result = 0;
	struct usb_serial_port *port =  modem_port_ptr->port;
	unsigned long flags;

	if (port == NULL)
		return -ENODEV;

	spin_lock_irqsave(&modem_port_ptr->write_lock, flags);
	modem_port_ptr->sending++;

	wb->urb->transfer_buffer = wb->buf;
	wb->urb->transfer_dma = wb->dmah;
	wb->urb->transfer_buffer_length = wb->len;
	wb->urb->dev = modem_port_ptr->port->serial->dev;

	result = usb_submit_urb(wb->urb, GFP_ATOMIC);
	if (result < 0) {
		dev_err(&port->dev,
			"%s: Submit bulk out URB failed. ret = %d \n",
			__func__, result);
		modem_write_done(modem_port_ptr, wb);
	}

	spin_unlock_irqrestore(&modem_port_ptr->write_lock, flags);
	mark_latest_traffic_time(modem_port_ptr);

	return result;
}

static void modem_wake_and_write(struct work_struct *work)
{
	struct modem_port *modem_port_ptr =
		container_of(work, struct modem_port, wake_and_write);
	struct usb_serial *serial;
	struct usb_serial_port *port =  modem_port_ptr->port;
	int result;

	if (modem_port_ptr->port == NULL)
		return;

	serial = modem_port_ptr->port->serial;

	result = usb_autopm_get_interface(serial->interface);
	if (result < 0) {
		dev_err(&port->dev, "%s: autopm failed. result = %d \n",
			__func__, result);
		return;
	}
	if (modem_port_ptr->delayed_wb) {
		modem_start_wb(modem_port_ptr, modem_port_ptr->delayed_wb);
		modem_port_ptr->delayed_wb = NULL;
	}

	usb_autopm_put_interface(serial->interface);
}

static void modem_write_bulk_callback(struct urb *urb)
{
	struct ap_wb *wb = urb->context;
	int status = urb->status;
	struct modem_port *modem_port_ptr = wb->instance;
	struct usb_serial_port *port = modem_port_ptr->port;
	unsigned long flags;

	if (port == NULL)
		return;

	spin_lock_irqsave(&modem_port_ptr->write_lock, flags);
	modem_write_done(modem_port_ptr, wb);
	spin_unlock_irqrestore(&modem_port_ptr->write_lock, flags);
	if (status) {
		dev_err(&port->dev, "%s: status non-zero. status = %d \n",
			 __func__, status);
		return;
	}
	usb_serial_port_softint(port);
}

static int modem_write(struct tty_struct *tty,
				 struct usb_serial_port *port,
				 const unsigned char *buf, int count)
{
	struct usb_serial *serial = port->serial;
	int result, wbn;
	struct ap_wb *wb;
	struct modem_port *modem_port_ptr =
	    usb_get_serial_data(port->serial);


	if (count == 0) {
		if (cdma_modem_debug)
			dev_info(&port->dev, "%s: Exit1: %s count = 0  \n",
				 __func__, dev_name(&port->dev));
		return 0;
	}

	if (serial->num_bulk_out) {
		unsigned long flags;
		spin_lock_irqsave(&modem_port_ptr->write_lock, flags);

		if ((modem_port_ptr->susp_count > 0) &&
			(modem_port_ptr->resuming != 0)) {
			spin_unlock_irqrestore(&modem_port_ptr->write_lock,
						flags);
			return 0;
		}

		wbn = modem_wb_alloc(modem_port_ptr);
		if (wbn < 0) {
			spin_unlock_irqrestore(&modem_port_ptr->write_lock,
						flags);
			if (cdma_modem_debug)
				dev_info(&port->dev,
					"%s: all buffers busy!\n", __func__);
			return 0;
		}
		wb = &modem_port_ptr->wb[wbn];

		count = min((int)(modem_port_ptr->writesize), count);

		if (cdma_modem_debug)
			dev_info(&port->dev, "%s: Get %d bytes.\n",
				__func__, count);
		memcpy(wb->buf, buf, count);
		wb->len = count;

		/* start sending */
		if (modem_port_ptr->susp_count > 0) {
			modem_port_ptr->resuming = 1;
			modem_port_ptr->delayed_wb = wb;
			spin_unlock_irqrestore(&modem_port_ptr->write_lock,
						flags);

			/* for the data modem, add wakelock to bypass the issue
			 * caused by skip_sys_resume for FS USB
			 */
			if (modem_port_ptr->port->number
					 == MODEM_INTERFACE_NUM) {
				wake_lock_timeout(&modem_wakelock,
						  MODEM_WAKELOCK_TIME);
				if (cdma_modem_debug)
					dev_info(&modem_port_ptr->port->dev,
					      "%s: add wakelock\n", __func__);
			}
			schedule_work(&modem_port_ptr->wake_and_write);
			return count;
		}
		spin_unlock_irqrestore(&modem_port_ptr->write_lock, flags);
		result = modem_start_wb(modem_port_ptr, wb);
		if (result >= 0)
			result = count;
		return result;
	}

	/* no bulk out, so return 0 bytes written */
	return 0;
}

#ifdef CONFIG_PM
static void modem_usb_wkup_work(struct work_struct *work)
{
	struct modem_port *modem_port_ptr =
	container_of(work, struct modem_port, usb_wkup_work);
	struct usb_serial *serial;
	int result;

	if (modem_port_ptr->port == 0)
		return;

	serial = modem_port_ptr->port->serial;
	if ((modem_port_ptr->port != 0) &&
	    !(atomic_cmpxchg(&modem_port_ptr->wakeup_flag, 0, 1))) {
		/* for the data modem, add wakelock to bypass the issue
		 * caused by skip_sys_resume for FS USB
		*/
		if (modem_port_ptr->port->number == MODEM_INTERFACE_NUM) {
			if (cdma_modem_debug)
				dev_info(&modem_port_ptr->port->dev,
					 "%s: add wakelock\n", __func__);
			wake_lock_timeout(&modem_wakelock, MODEM_WAKELOCK_TIME);
		}
		result = usb_autopm_get_interface(serial->interface);
		if (result < 0) {
			atomic_set(&modem_port_ptr->wakeup_flag, 0);
			dev_err(&modem_port_ptr->port->dev,
				 "%s: autopm failed. result = %d \n",
				__func__, result);
			return;
		}

		if (cdma_modem_debug)
			dev_info(&modem_port_ptr->port->dev,
				 "%s: woke up interface\n", __func__);
		usb_autopm_put_interface(serial->interface);
	}
}

static int modem_usb_enable_wakeup_irq(struct usb_interface *intf)
{
	struct usb_serial *serial = usb_get_intfdata(intf);
	struct modem_port *modem_port_ptr =
		usb_get_serial_data(serial);
	int ret = 0;

	if (modem_port_ptr == NULL)
		return  -ENODEV;

	if (modem_port_ptr->wakeup_gpio) {
		enable_irq(gpio_to_irq(modem_port_ptr->wakeup_gpio));
		enable_irq_wake(
				gpio_to_irq(modem_port_ptr->wakeup_gpio));
	}

	return ret;
}

static int modem_suspend(struct usb_interface *intf,
				   pm_message_t message)
{
	struct usb_serial *serial = usb_get_intfdata(intf);
	struct modem_port *modem_port_ptr =
	    usb_get_serial_data(serial);
	struct usb_serial_port *port;
	unsigned long flags;
	unsigned long threshold_time;
	int tmp;

	if (modem_port_ptr == NULL) {
		dev_err(&intf->dev, " NULL modem_port ptr \n");
		return 0;
	}

	port = modem_port_ptr->port;

	if (port == NULL) {
		if (cdma_modem_debug)
			dev_info(&intf->dev,
				 "%s: port not open yet \n",
				 __func__);
		modem_port_ptr->susp_count++;
		return 0;
	}

	if (cdma_modem_debug)
		dev_info(&intf->dev, "%s: Suspend Port  num %d.\n",
			 __func__, port->number);

	spin_lock_irqsave(&modem_port_ptr->read_lock, flags);
	spin_lock(&modem_port_ptr->write_lock);
	tmp = modem_port_ptr->processing + modem_port_ptr->sending;
	spin_unlock(&modem_port_ptr->write_lock);
	spin_unlock_irqrestore(&modem_port_ptr->read_lock, flags);

	if (tmp) {
		if (cdma_modem_debug)
			dev_info(&intf->dev,
				 "%s:  sending = %d, receiving = %d.\n",
				 __func__, modem_port_ptr->sending,
				 modem_port_ptr->processing);
		return -EBUSY;
	}

	threshold_time = modem_port_ptr->last_traffic + MODEM_NO_TRAFFIC_TIME;

	if (time_before(jiffies, threshold_time)) {
		if (cdma_modem_debug)
			dev_info(&intf->dev,
				 "%s: busy. suspend failed.\n", __func__);
		return -EBUSY;
	}

	spin_lock_irqsave(&modem_port_ptr->read_lock, flags);
	spin_lock(&modem_port_ptr->write_lock);
	modem_port_ptr->susp_count++;
	spin_unlock(&modem_port_ptr->write_lock);
	spin_unlock_irqrestore(&modem_port_ptr->read_lock, flags);

	stop_data_traffic(modem_port_ptr);

	if (port->number == MODEM_INTERFACE_NUM) {
		modem_usb_enable_wakeup_irq(intf);
		atomic_set(&modem_port_ptr->wakeup_flag, 0);
	}

	return 0;
}

static int modem_resume(struct usb_interface *intf)
{
	struct usb_serial *serial = usb_get_intfdata(intf);
	struct modem_port *modem_port_ptr =
	    usb_get_serial_data(serial);
	struct usb_serial_port *port;
	unsigned long flags;
	int retval;

	if (modem_port_ptr == NULL) {
		dev_err(&intf->dev, "%s: null modem port pointer. \n",
			 __func__);
		return 0;
	}

	port = modem_port_ptr->port;

	if (port == NULL) {
		if (cdma_modem_debug)
			dev_info(&intf->dev,
				 "%s: port not open yet \n",
				 __func__);
		modem_port_ptr->susp_count--;
		return 0;

	}

	spin_lock_irqsave(&modem_port_ptr->read_lock, flags);
	spin_lock(&modem_port_ptr->write_lock);
	if (modem_port_ptr->susp_count > 0) {
		modem_port_ptr->susp_count--;
		spin_unlock(&modem_port_ptr->write_lock);
		spin_unlock_irqrestore(&modem_port_ptr->read_lock, flags);

		modem_port_ptr->resuming = 0;

		if (cdma_modem_debug)
			dev_info(&intf->dev, "%s: port %d is resumed here \n",
				 __func__, port->number);

		if (port->number == MODEM_INTERFACE_NUM) {
			spin_lock_irqsave(&modem_port_ptr->read_lock, flags);
			if (port->interrupt_in_urb) {
				port->interrupt_in_urb->dev = port->serial->dev;
				retval =
					usb_submit_urb(port->interrupt_in_urb,
						       GFP_KERNEL);
				if (retval) {
					usb_kill_urb(port->interrupt_in_urb);
					dev_err(&port->dev,
						"%s: retval is %d \n",
						__func__, retval);
				}
			} else {
				dev_err(&port->dev,
					"%s: no interrupt endpoint \n",
					__func__);
			}
			spin_unlock_irqrestore(&modem_port_ptr->read_lock,
					       flags);

			modem_usb_disable_wakeup_irq(intf);

		}

		tasklet_schedule(&modem_port_ptr->urb_task);
	} else {
		spin_unlock(&modem_port_ptr->write_lock);
		spin_unlock_irqrestore(&modem_port_ptr->read_lock, flags);
	}

	return 0;
}

static int modem_reset_resume(struct usb_interface *intf)
{
	int ret = 0;

	if (cdma_modem_debug)
		dev_info(&intf->dev,
		"%s: Enter \n", __func__);

	ret = modem_resume(intf);

	if (cdma_modem_debug)
		dev_info(&intf->dev,
		"%s: Exit ret is %d \n", __func__, ret);

	return ret;
}

static int modem_pre_reset(struct usb_interface *intf)
{
	return 0;
}

static int modem_post_reset(struct usb_interface *intf)
{
	return 0;
}

#endif /* CONFIG_PM */

static int modem_startup(struct usb_serial *serial)
{
	struct usb_serial_port *port = serial->port[0];
	struct modem_port *modem_port_ptr = NULL;
	struct usb_interface *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_endpoint_descriptor *epread = NULL;
	struct usb_endpoint_descriptor *epwrite = NULL;
	struct usb_host_interface *iface_desc;
	int readsize;
	int num_rx_buf;
	int i;
	#if defined(CONFIG_ARCH_OMAP34XX)
	struct omap_usb_config *config =
			 serial->dev->bus->controller->platform_data;
	#endif


	interface = serial->interface;
	iface_desc = interface->cur_altsetting;

	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;
		if (usb_endpoint_is_bulk_in(endpoint))
			epread = endpoint;
		if (usb_endpoint_is_bulk_out(endpoint))
			epwrite = endpoint;
	}

	if (epread == NULL) {
		dev_err(&serial->dev->dev,
			 "%s: No Bulk In Endpoint for this Interface \n",
			 __func__);
		return -EPERM;
	}
	if (epwrite == NULL) {
		dev_err(&serial->dev->dev,
			 "%s: No Bulk Out Endpoint for this Interface \n",
			 __func__);
		return -EPERM;
	}

	num_rx_buf = AP_NR;
	readsize = le16_to_cpu(epread->wMaxPacketSize) * 2;

	/* setup a buffer to store interface data */
	modem_port_ptr =
	    kzalloc(sizeof(struct modem_port), GFP_KERNEL);
	if (modem_port_ptr == NULL) {
		dev_err(&serial->dev->dev,
			 "%s: error -- no memory on start up. \n",
			 __func__);
		return -ENOMEM;
	}

#if defined(CONFIG_ARCH_OMAP34XX)
	modem_port_ptr->wakeup_gpio =
			 config->usb_remote_wake_gpio;
#endif

	/* init tasklet for rx processing */
	tasklet_init(&modem_port_ptr->urb_task, modem_rx_tasklet,
		     (unsigned long)modem_port_ptr);
	modem_port_ptr->rx_buflimit = num_rx_buf;
	modem_port_ptr->rx_endpoint =
		usb_rcvbulkpipe(serial->dev, port->bulk_in_endpointAddress);
	spin_lock_init(&modem_port_ptr->read_lock);
	spin_lock_init(&modem_port_ptr->write_lock);
	spin_lock_init(&modem_port_ptr->last_traffic_lock);

	atomic_set(&modem_port_ptr->wakeup_flag, 0);
	modem_port_ptr->susp_count = 0;
	modem_port_ptr->resuming = 0;
	modem_port_ptr->port = 0;
	modem_port_ptr->last_traffic = 0;
	modem_port_ptr->readsize = readsize;
	modem_port_ptr->writesize = le16_to_cpu(epwrite->wMaxPacketSize) * 20;

	INIT_WORK(&modem_port_ptr->wake_and_write, modem_wake_and_write);
	INIT_WORK(&modem_port_ptr->usb_wkup_work, modem_usb_wkup_work);

	if (modem_write_buffers_alloc(modem_port_ptr, serial) < 0) {
		dev_err(&serial->dev->dev,
			"%s: out of memory \n", __func__);
		goto alloc_write_buf_fail;
	}

	/* allocate multiple receive urb pool */
	for (i = 0; i < num_rx_buf; i++) {
		struct ap_ru *rcv = &(modem_port_ptr->ru[i]);

		rcv->urb = usb_alloc_urb(0, GFP_KERNEL);
		if (rcv->urb == NULL) {
			dev_err(&serial->dev->dev,
				"%s: out of memory \n", __func__);
			goto alloc_rb_urb_fail;
		}

		rcv->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
		rcv->instance = modem_port_ptr;
	}

	/* allocate multiple receive buffer */
	for (i = 0; i < num_rx_buf; i++) {
		struct ap_rb *rb = &(modem_port_ptr->rb[i]);

		rb->base = usb_buffer_alloc(serial->dev, readsize,
					    GFP_KERNEL, &rb->dma);
		if (!rb->base) {
			dev_err(&serial->dev->dev,
				 "%s : out of memory \n",
				__func__);
			goto alloc_rb_buffer_fail;
		}
	}
	for (i = 0; i < AP_NW; i++) {
		struct ap_wb *snd = &(modem_port_ptr->wb[i]);

		snd->urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!snd->urb) {
			dev_err(&serial->dev->dev, "%s : out of memory "
				"(write urbs usb_alloc_urb)\n", __func__);
			goto alloc_wb_urb_fail;
		}
		usb_fill_bulk_urb(snd->urb, serial->dev,
				usb_sndbulkpipe(serial->dev,
					epwrite->bEndpointAddress),
				NULL, modem_port_ptr->writesize,
				modem_write_bulk_callback, snd);
		snd->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
		snd->instance = modem_port_ptr;
	}

	modem_port_ptr->modem_status = 0;

	/* install serial private data */
	usb_set_serial_data(serial, modem_port_ptr);

	return 0;

alloc_wb_urb_fail:
	for (i = 0; i < AP_NW; i++)
		usb_free_urb(modem_port_ptr->wb[i].urb);
alloc_rb_buffer_fail:
	modem_read_buffers_free(modem_port_ptr, serial);
alloc_rb_urb_fail:
	for (i = 0; i < num_rx_buf; i++)
		usb_free_urb(modem_port_ptr->ru[i].urb);
alloc_write_buf_fail:
	modem_write_buffers_free(modem_port_ptr, serial);
	if (modem_port_ptr != NULL) {
		kfree(modem_port_ptr);
		usb_set_serial_data(serial, NULL);
	}
	return -ENOMEM;
}

static void modem_shutdown(struct usb_serial *serial)
{
	int i;
	struct modem_port *modem_port_ptr =
	    usb_get_serial_data(serial);

	uint8_t interface_num =
		serial->interface->cur_altsetting->desc.bInterfaceNumber;

	if (cdma_modem_debug)
		dev_info(&serial->dev->dev,
			 "%s: Shutdown Interface %d  \n", __func__,
			interface_num);

	stop_data_traffic(modem_port_ptr);
	cancel_work_sync(&modem_port_ptr->wake_and_write);
	modem_write_buffers_free(modem_port_ptr, serial);
	modem_read_buffers_free(modem_port_ptr, serial);

	for (i = 0; i < AP_NW; i++)
		usb_free_urb(modem_port_ptr->wb[i].urb);

	for (i = 0; i < modem_port_ptr->rx_buflimit; i++)
		usb_free_urb(modem_port_ptr->ru[i].urb);

	if (modem_port_ptr) {
		/* free private structure allocated for serial device */
		kfree(modem_port_ptr);
		usb_set_serial_data(serial, NULL);
	}
}

static struct usb_driver modem_driver = {
	.name = "cdma-modem",
	.probe = usb_serial_probe,
	.disconnect = usb_serial_disconnect,
	.id_table = id_table,
	.no_dynamic_id = 1,
#ifdef CONFIG_PM
	.supports_autosuspend = 1,
	.suspend = modem_suspend,
	.resume = modem_resume,
	.reset_resume = modem_reset_resume,
	.pre_reset = modem_pre_reset,
	.post_reset = modem_post_reset,
#endif
};

static struct usb_serial_driver modem_device = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "qsc6085-modem",
		   },
	.description = "QSC 6085 Modem Driver",
	.usb_driver = &modem_driver,
	.id_table = id_table,
	.num_ports = 1,
	.write = modem_write,
	.write_bulk_callback = modem_write_bulk_callback,
	.read_int_callback = modem_interrupt_callback,
	.tiocmset = modem_tiocmset,
	.tiocmget = modem_tiocmget,
	.open = modem_open,
	.close = modem_close,
	.attach = modem_startup,
	.shutdown = modem_shutdown,
};

static void __exit modem_exit(void)
{
	usb_deregister(&modem_driver);
	usb_serial_deregister(&modem_device);
}

static int __init modem_init(void)
{
	int retval;

	retval = usb_serial_register(&modem_device);
	if (retval)
		return retval;
	retval = usb_register(&modem_driver);
	if (retval)
		usb_serial_deregister(&modem_device);
	return retval;
}

module_init(modem_init);
module_exit(modem_exit);

MODULE_DESCRIPTION("USB IPC Driver for QSC 6085");
MODULE_AUTHOR("Motorola");
MODULE_LICENSE("GPL");
